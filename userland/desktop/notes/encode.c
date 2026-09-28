/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The edit data of Notes (plan/ws079/design-pdf.md section 2.1).
 *
 * A saved PDF carries the whole document as an attached file: the magic
 * "ZNOT", the version 1.0, then chunks (a four-letter tag, a 32-bit length,
 * the body).  DOC gives the page count, the pressure's range, the time base
 * and the next stroke number; TOOL lists the tools (kind, colour, width);
 * each PAGE gives a page's size, its background and its strokes.  A stroke's
 * samples are stored as differences from the sample before, as LEB128
 * numbers (zigzag for signed ones): positions in 1/64 point, the pressure,
 * the tilt in 1/100 degree when the stroke has it, and the time in
 * milliseconds.  A reader skips the chunks it does not know, which is how a
 * later minor version adds to the data.
 *
 * The journal stores one stroke at a time in the same way, with its tool
 * written out instead of an index into TOOL.
 */

#include "notes.h"

#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* The version of the edit data this file writes and reads. */
#define ENCODE_MAJOR		1U
#define ENCODE_MINOR		0U

/* The stroke flags: the samples carry the tilt, and their times. */
#define ENCODE_STROKE_TILT	0x01U
#define ENCODE_STROKE_TIME	0x02U

/* The largest counts a document may declare, which keep a damaged file from asking for all memory. */
#define ENCODE_PAGES_MAX	100000U
#define ENCODE_TOOLS_MAX	4096U
#define ENCODE_POINTS_MAX	10000000U

/* The largest page side and stroke width, in points. */
#define ENCODE_LENGTH_MAX	100000.0f

/* The smallest room a buffer grows to. */
#define ENCODE_BUFFER_MIN	256U

/*
 * One tool of the TOOL chunk: a kind, a colour and a width.
 */
struct encode_tool {
	unsigned kind;
	uint32_t color;
	float width;
};

/*
 * The tools a document uses, gathered while it is encoded.
 */
struct encode_tools {
	struct encode_tool *tools;
	size_t count;
	size_t capacity;
};

/*
 * A cursor over bytes being read.
 *
 * error stays nonzero once a read runs past the end or finds a malformed
 * number; every later read then gives zero, and the caller checks error
 * once per step.
 */
struct encode_reader {
	const unsigned char *data;
	size_t length;
	size_t offset;
	int error;
};

static void buffer_reserve(struct notes_buffer *buffer, size_t more);
static int tools_find(struct encode_tools *tools, const struct notes_stroke *stroke, size_t *index);
static void encode_samples(struct notes_buffer *buffer, const struct notes_stroke *stroke, uint64_t first_time);
static int decode_samples(struct encode_reader *reader, struct notes_stroke *stroke, unsigned flags, size_t count, uint64_t *first_time);
static int decode_doc(struct encode_reader *reader, struct notes_document *document, size_t *pages);
static int decode_tools(struct encode_reader *reader, struct encode_tools *tools);
static int decode_page(struct encode_reader *reader, struct notes_document *document, const struct encode_tools *tools, size_t index);
static unsigned read_u8(struct encode_reader *reader);
static unsigned read_u16(struct encode_reader *reader);
static uint32_t read_u32(struct encode_reader *reader);
static uint64_t read_u64(struct encode_reader *reader);
static uint64_t read_varint(struct encode_reader *reader);
static int64_t read_zigzag(struct encode_reader *reader);
static float read_length(struct encode_reader *reader);
static int64_t units(float value);

/*
 * Makes an empty buffer.
 */
void
notes_buffer_init(
	struct notes_buffer *buffer)
{
	/* No bytes and no failure. */
	memset(buffer, 0, sizeof(*buffer));
}

/*
 * Frees a buffer's bytes.
 */
void
notes_buffer_free(
	struct notes_buffer *buffer)
{
	/* The bytes, and the buffer is empty again. */
	free(buffer->data);
	memset(buffer, 0, sizeof(*buffer));
}

/*
 * Appends bytes; a buffer that failed takes nothing more.
 */
void
notes_buffer_bytes(
	struct notes_buffer *buffer,
	const void *data,
	size_t length)
{
	/* Room for the bytes, or the buffer's failure. */
	buffer_reserve(buffer, length);
	if (buffer->error != 0)
		return;

	/* Copies them to the end. */
	memcpy(buffer->data + buffer->length, data, length);
	buffer->length += length;
}

/*
 * Appends one byte.
 */
void
notes_buffer_u8(
	struct notes_buffer *buffer,
	unsigned value)
{
	unsigned char byte;

	/* The low eight bits. */
	byte = (unsigned char)(value & 0xffU);
	notes_buffer_bytes(buffer, &byte, 1U);
}

/*
 * Appends a 16-bit number, least significant byte first.
 */
void
notes_buffer_u16(
	struct notes_buffer *buffer,
	unsigned value)
{
	unsigned char bytes[2];

	/* The two bytes, low first. */
	bytes[0] = (unsigned char)(value & 0xffU);
	bytes[1] = (unsigned char)((value >> 8) & 0xffU);
	notes_buffer_bytes(buffer, bytes, sizeof(bytes));
}

/*
 * Appends a 32-bit number, least significant byte first.
 */
void
notes_buffer_u32(
	struct notes_buffer *buffer,
	uint32_t value)
{
	unsigned char bytes[4];
	unsigned index;

	/* The four bytes, low first. */
	for (index = 0; index < 4U; index++)
		bytes[index] = (unsigned char)((value >> (index * 8U)) & 0xffU);
	notes_buffer_bytes(buffer, bytes, sizeof(bytes));
}

/*
 * Appends a 64-bit number, least significant byte first.
 */
void
notes_buffer_u64(
	struct notes_buffer *buffer,
	uint64_t value)
{
	unsigned char bytes[8];
	unsigned index;

	/* The eight bytes, low first. */
	for (index = 0; index < 8U; index++)
		bytes[index] = (unsigned char)((value >> (index * 8U)) & 0xffU);
	notes_buffer_bytes(buffer, bytes, sizeof(bytes));
}

/*
 * Appends an unsigned LEB128 number: seven bits a byte, low first, the top
 * bit set on every byte but the last.
 */
void
notes_buffer_varint(
	struct notes_buffer *buffer,
	uint64_t value)
{
	unsigned char bytes[10];
	size_t count;

	/* Seven bits at a time until what is left fits in one byte. */
	count = 0;
	while (value >= 0x80U) {
		bytes[count] = (unsigned char)((value & 0x7fU) | 0x80U);
		count++;
		value >>= 7;
	}

	/* The last byte, without the continuation bit. */
	bytes[count] = (unsigned char)value;
	count++;
	notes_buffer_bytes(buffer, bytes, count);
}

/*
 * Appends a signed number as a zigzag LEB128 number (0, -1, 1, -2 ... as
 * 0, 1, 2, 3 ...), which keeps small differences of either sign short.
 */
void
notes_buffer_zigzag(
	struct notes_buffer *buffer,
	int64_t value)
{
	uint64_t folded;

	/* Positive values go to the even numbers, negative ones to the odd. */
	if (value < 0)
		folded = ((uint64_t)(-(value + 1)) << 1) | 1U;
	else
		folded = (uint64_t)value << 1;
	notes_buffer_varint(buffer, folded);
}

/*
 * Encodes a whole document as the edit data.
 *
 * Returns 0, or ENOMEM (the buffer's contents are then of no use).
 */
int
notes_encode_document(
	const struct notes_document *document,
	struct notes_buffer *buffer)
{
	struct encode_tools tools;
	struct notes_buffer chunk;
	const struct notes_page *page;
	const struct notes_stroke *stroke;
	uint64_t first_time;
	size_t page_index;
	size_t stroke_index;
	size_t tool;
	int error;

	/* The header: magic, version and no flags. */
	memset(&tools, 0, sizeof(tools));
	notes_buffer_init(&chunk);
	notes_buffer_bytes(buffer, "ZNOT", 4U);
	notes_buffer_u16(buffer, ENCODE_MAJOR);
	notes_buffer_u16(buffer, ENCODE_MINOR);
	notes_buffer_u32(buffer, 0U);

	/* DOC: the pages, the pressure's range, the time base and the next stroke number. */
	notes_buffer_varint(&chunk, document->page_count);
	notes_buffer_u16(&chunk, NOTES_PRESSURE_MAX);
	notes_buffer_u64(&chunk, document->time_base);
	notes_buffer_varint(&chunk, document->next_id);
	notes_buffer_bytes(buffer, "DOC ", 4U);
	notes_buffer_u32(buffer, (uint32_t)chunk.length);
	notes_buffer_bytes(buffer, chunk.data, chunk.length);
	chunk.length = 0;

	/* Gathers the tools every stroke uses, so that TOOL comes before the pages. */
	for (page_index = 0; page_index < document->page_count; page_index++) {
		page = document->pages[page_index];
		for (stroke_index = 0; stroke_index < page->stroke_count; stroke_index++) {
			error = tools_find(&tools, page->strokes[stroke_index], &tool);
			if (error != 0) {
				free(tools.tools);
				notes_buffer_free(&chunk);
				return error;
			}
		}
	}

	/* TOOL: each tool's kind, colour (red, green, blue, alpha), width, pressure curve and flags. */
	notes_buffer_varint(&chunk, tools.count);
	for (tool = 0; tool < tools.count; tool++) {
		notes_buffer_u8(&chunk, tools.tools[tool].kind);
		notes_buffer_u8(&chunk, (tools.tools[tool].color >> 24) & 0xffU);
		notes_buffer_u8(&chunk, (tools.tools[tool].color >> 16) & 0xffU);
		notes_buffer_u8(&chunk, (tools.tools[tool].color >> 8) & 0xffU);
		notes_buffer_u8(&chunk, tools.tools[tool].color & 0xffU);
		notes_buffer_varint(&chunk, (uint64_t)units(tools.tools[tool].width));
		notes_buffer_u8(&chunk, 0U);
		notes_buffer_u8(&chunk, 0U);
	}

	/* The chunk. */
	notes_buffer_bytes(buffer, "TOOL", 4U);
	notes_buffer_u32(buffer, (uint32_t)chunk.length);
	notes_buffer_bytes(buffer, chunk.data, chunk.length);
	chunk.length = 0;

	/* PAGE, one a page, with the digest of its content stream as last saved (zero when not known). */
	for (page_index = 0; page_index < document->page_count; page_index++) {
		/* The page's number, size, background, digest and stroke count. */
		page = document->pages[page_index];
		notes_buffer_varint(&chunk, page_index);
		notes_buffer_varint(&chunk, (uint64_t)units(page->width));
		notes_buffer_varint(&chunk, (uint64_t)units(page->height));
		notes_buffer_u8(&chunk, page->background);
		notes_buffer_bytes(&chunk, page->content_hash, sizeof(page->content_hash));
		notes_buffer_varint(&chunk, page->stroke_count);

		/* Each stroke: number, tool, flags, samples. */
		for (stroke_index = 0; stroke_index < page->stroke_count; stroke_index++) {
			stroke = page->strokes[stroke_index];
			(void)tools_find(&tools, stroke, &tool);
			notes_buffer_varint(&chunk, stroke->id);
			notes_buffer_varint(&chunk, tool);
			if (stroke->has_tilt)
				notes_buffer_u8(&chunk, ENCODE_STROKE_TILT | ENCODE_STROKE_TIME);
			else
				notes_buffer_u8(&chunk, ENCODE_STROKE_TIME);
			notes_buffer_varint(&chunk, stroke->point_count);

			/* The first sample's time counts from the document's time base. */
			first_time = 0U;
			if (stroke->start_ms > document->time_base)
				first_time = stroke->start_ms - document->time_base;
			encode_samples(&chunk, stroke, first_time);
		}

		/* The chunk. */
		notes_buffer_bytes(buffer, "PAGE", 4U);
		notes_buffer_u32(buffer, (uint32_t)chunk.length);
		notes_buffer_bytes(buffer, chunk.data, chunk.length);
		chunk.length = 0;
	}

	/* The scratch buffers go; a failure of either is the encoding's. */
	free(tools.tools);
	error = chunk.error;
	notes_buffer_free(&chunk);
	if (error != 0)
		return error;
	if (buffer->error != 0)
		return buffer->error;

	/* Succeeded: the buffer holds the edit data. */
	return 0;
}

/*
 * Decodes edit data into an empty document.
 *
 * The document is made here (notes_document_init is not called first);
 * on failure it is freed again.  Returns 0, EINVAL for data that is not
 * edit data of a version this file reads or is damaged, or ENOMEM.
 */
int
notes_decode_document(
	const void *data,
	size_t size,
	struct notes_document *document)
{
	struct encode_reader reader;
	struct encode_tools tools;
	unsigned char tag[4];
	size_t declared;
	size_t pages;
	size_t length;
	size_t end;
	unsigned major;
	int seen_doc;
	int matched;
	int is_doc;
	int is_tool;
	int is_page;
	int error;

	/* An empty document to fill, and a reader at the start. */
	memset(document, 0, sizeof(*document));
	memset(&tools, 0, sizeof(tools));
	memset(&reader, 0, sizeof(reader));
	reader.data = data;
	reader.length = size;

	/* The magic, after the header's twelve bytes are known to be there. */
	if (size < 12U)
		return EINVAL;
	matched = memcmp(data, "ZNOT", 4U);
	if (matched != 0)
		return EINVAL;
	reader.offset = 4U;

	/* A major version this file does not know is not read (design-pdf.md section 2.1). */
	major = read_u16(&reader);
	(void)read_u16(&reader);
	(void)read_u32(&reader);
	if (major != ENCODE_MAJOR)
		return EINVAL;

	/* Each chunk, until the data ends. */
	declared = 0;
	pages = 0;
	seen_doc = 0;
	error = 0;
	while (reader.offset < reader.length && error == 0) {
		/* The tag and the body's length, which must fit in the data. */
		if (reader.length - reader.offset < 8U) {
			error = EINVAL;
			break;
		}

		/* The tag, then the length of the body. */
		memcpy(tag, reader.data + reader.offset, sizeof(tag));
		reader.offset += 4U;
		length = read_u32(&reader);
		if (length > reader.length - reader.offset) {
			error = EINVAL;
			break;
		}

		/* Where the next chunk starts. */
		end = reader.offset + length;

		/* The chunks this version knows (DOC once, TOOL once, PAGE after DOC); any other is skipped. */
		is_doc = memcmp(tag, "DOC ", 4U);
		is_tool = memcmp(tag, "TOOL", 4U);
		is_page = memcmp(tag, "PAGE", 4U);
		if (is_doc == 0 && !seen_doc) {
			error = decode_doc(&reader, document, &declared);
			seen_doc = 1;
		} else if (is_tool == 0 && tools.count == 0U) {
			error = decode_tools(&reader, &tools);
		} else if (is_page == 0 && seen_doc) {
			error = decode_page(&reader, document, &tools, pages);
			pages++;
		}

		/* A body read past its end is damaged; the next chunk starts after it. */
		if (error == 0 && reader.offset > end)
			error = EINVAL;
		reader.offset = end;
	}

	/* The tools are not needed any more. */
	free(tools.tools);

	/* A document needs its DOC chunk and the pages it declared, at least one. */
	if (error == 0 &&
	    (!seen_doc ||
	     pages != declared ||
	     pages == 0U))
		error = EINVAL;
	if (error != 0) {
		notes_document_free(document);
		return error;
	}

	/* Succeeded: the document as it was saved, unchanged since. */
	document->dirty = 0;
	return 0;
}

/*
 * Encodes one stroke with its tool written out, for the journal.
 *
 * Returns 0, or ENOMEM.
 */
int
notes_encode_stroke(
	const struct notes_stroke *stroke,
	struct notes_buffer *buffer)
{
	unsigned flags;

	/* The stroke's number, its tool and colour, its width and when it started. */
	notes_buffer_varint(buffer, stroke->id);
	notes_buffer_u8(buffer, stroke->tool);
	notes_buffer_u32(buffer, stroke->color);
	notes_buffer_varint(buffer, (uint64_t)units(stroke->width));
	flags = ENCODE_STROKE_TIME;
	if (stroke->has_tilt)
		flags |= ENCODE_STROKE_TILT;
	notes_buffer_u8(buffer, flags);
	notes_buffer_u64(buffer, stroke->start_ms);

	/* Its samples; the first sample's time counts from the stroke's start. */
	notes_buffer_varint(buffer, stroke->point_count);
	encode_samples(buffer, stroke, 0U);
	if (buffer->error != 0)
		return buffer->error;

	/* Succeeded: the stroke is in the buffer. */
	return 0;
}

/*
 * Decodes one stroke that notes_encode_stroke() wrote.
 *
 * *used tells how many bytes it took.  Returns 0, EINVAL or ENOMEM.
 */
int
notes_decode_stroke(
	const unsigned char *data,
	size_t size,
	size_t *used,
	struct notes_stroke **stroke)
{
	struct encode_reader reader;
	struct notes_stroke *made;
	uint64_t first_time;
	uint64_t count;
	uint32_t id;
	uint32_t color;
	unsigned tool;
	unsigned flags;
	float width;
	uint64_t start;
	int error;

	/* A reader over the bytes. */
	memset(&reader, 0, sizeof(reader));
	reader.data = data;
	reader.length = size;

	/* The number, tool, colour, width, flags and start. */
	id = (uint32_t)read_varint(&reader);
	tool = read_u8(&reader);
	color = read_u32(&reader);
	width = read_length(&reader);
	flags = read_u8(&reader);
	start = read_u64(&reader);
	count = read_varint(&reader);
	if (reader.error != 0)
		return EINVAL;

	/* A stroke of no samples, too many, or no width is damaged. */
	if (count == 0U ||
	    count > ENCODE_POINTS_MAX ||
	    !(width > 0.0f))
		return EINVAL;

	/* The stroke. */
	made = notes_stroke_create(id, tool, color, width, start);
	if (made == NULL)
		return ENOMEM;

	/* Its samples. */
	error = decode_samples(&reader, made, flags, (size_t)count, &first_time);
	if (error != 0) {
		notes_stroke_free(made);
		return error;
	}

	/* Succeeded: the caller owns the stroke. */
	*used = reader.offset;
	*stroke = made;
	return 0;
}

/* Makes room for more bytes, or marks the buffer failed. */
static void
buffer_reserve(
	struct notes_buffer *buffer,
	size_t more)
{
	unsigned char *larger;
	size_t capacity;

	/* A failed buffer stays failed. */
	if (buffer->error != 0)
		return;

	/* Refuses a length that overflows. */
	if (more > (size_t)-1 / 2U - buffer->length) {
		buffer->error = ENOMEM;
		return;
	}

	/* Room enough already. */
	if (buffer->length + more <= buffer->capacity)
		return;

	/* Doubles the room from a small start until the bytes fit. */
	capacity = buffer->capacity;
	if (capacity < ENCODE_BUFFER_MIN)
		capacity = ENCODE_BUFFER_MIN;
	while (capacity < buffer->length + more)
		capacity *= 2U;

	/* Reallocates the bytes. */
	larger = realloc(buffer->data, capacity);
	if (larger == NULL) {
		buffer->error = ENOMEM;
		return;
	}

	/* Succeeded: the buffer has room. */
	buffer->data = larger;
	buffer->capacity = capacity;
}

/* Finds a stroke's tool in the list, adding it when it is new. */
static int
tools_find(
	struct encode_tools *tools,
	const struct notes_stroke *stroke,
	size_t *index)
{
	struct encode_tool *larger;
	size_t capacity;
	size_t tool;

	/* A tool of the same kind, colour and width is the stroke's. */
	for (tool = 0; tool < tools->count; tool++) {
		if (tools->tools[tool].kind != stroke->tool)
			continue;
		if (tools->tools[tool].color != stroke->color)
			continue;
		if (tools->tools[tool].width != stroke->width)
			continue;
		*index = tool;
		return 0;
	}

	/* Room for another tool. */
	if (tools->count == tools->capacity) {
		capacity = tools->capacity * 2U;
		if (capacity < 8U)
			capacity = 8U;
		larger = realloc(tools->tools, capacity * sizeof(*larger));
		if (larger == NULL)
			return ENOMEM;
		tools->tools = larger;
		tools->capacity = capacity;
	}

	/* Succeeded: the new tool is the last. */
	tools->tools[tools->count].kind = stroke->tool;
	tools->tools[tools->count].color = stroke->color;
	tools->tools[tools->count].width = stroke->width;
	*index = tools->count;
	tools->count++;
	return 0;
}

/* Writes a stroke's samples as differences from the sample before. */
static void
encode_samples(
	struct notes_buffer *buffer,
	const struct notes_stroke *stroke,
	uint64_t first_time)
{
	const struct notes_point *point;
	int64_t x;
	int64_t y;
	int64_t last_x;
	int64_t last_y;
	int64_t last_pressure;
	int64_t last_tilt_x;
	int64_t last_tilt_y;
	uint32_t last_time;
	size_t index;

	/* The sample before the first is all zero. */
	last_x = 0;
	last_y = 0;
	last_pressure = 0;
	last_tilt_x = 0;
	last_tilt_y = 0;
	last_time = 0U;

	/* Each sample. */
	for (index = 0; index < stroke->point_count; index++) {
		/* The position, in 1/64 point. */
		point = &stroke->points[index];
		x = units(point->x);
		y = units(point->y);
		notes_buffer_zigzag(buffer, x - last_x);
		notes_buffer_zigzag(buffer, y - last_y);
		last_x = x;
		last_y = y;

		/* The pressure. */
		notes_buffer_zigzag(buffer, (int64_t)point->pressure - last_pressure);
		last_pressure = point->pressure;

		/* The tilt, when the stroke has it. */
		if (stroke->has_tilt) {
			notes_buffer_zigzag(buffer, (int64_t)point->tilt_x - last_tilt_x);
			notes_buffer_zigzag(buffer, (int64_t)point->tilt_y - last_tilt_y);
			last_tilt_x = point->tilt_x;
			last_tilt_y = point->tilt_y;
		}

		/* The time: the first from the given base, the others from the sample before (never backwards). */
		if (index == 0U) {
			notes_buffer_varint(buffer, first_time + point->time_ms);
		} else if (point->time_ms >= last_time) {
			notes_buffer_varint(buffer, point->time_ms - last_time);
		} else {
			notes_buffer_varint(buffer, 0U);
		}

		/* A sample that went backwards in time keeps the later time. */
		if (point->time_ms > last_time)
			last_time = point->time_ms;
	}
}

/* Reads a stroke's samples; *first_time gets the first sample's time from its base. */
static int
decode_samples(
	struct encode_reader *reader,
	struct notes_stroke *stroke,
	unsigned flags,
	size_t count,
	uint64_t *first_time)
{
	struct notes_point point;
	int64_t x;
	int64_t y;
	int64_t pressure;
	int64_t tilt_x;
	int64_t tilt_y;
	uint64_t time;
	size_t index;
	int error;

	/* The sample before the first is all zero. */
	x = 0;
	y = 0;
	pressure = 0;
	tilt_x = 0;
	tilt_y = 0;
	time = 0U;
	*first_time = 0U;
	stroke->has_tilt = 0;
	if ((flags & ENCODE_STROKE_TILT) != 0U)
		stroke->has_tilt = 1;

	/* Each sample. */
	for (index = 0; index < count; index++) {
		/* The position and the pressure. */
		x += read_zigzag(reader);
		y += read_zigzag(reader);
		pressure += read_zigzag(reader);

		/* The tilt, when the stroke has it. */
		if (stroke->has_tilt) {
			tilt_x += read_zigzag(reader);
			tilt_y += read_zigzag(reader);
		}

		/* The time: the first sample's is the stroke's start, the others count from it. */
		if ((flags & ENCODE_STROKE_TIME) != 0U) {
			if (index == 0U)
				*first_time = read_varint(reader);
			else
				time += read_varint(reader);
		}

		/* A sample past the data, or out of range, is damaged. */
		if (reader->error != 0)
			return EINVAL;
		if (pressure < 0 || pressure > (int64_t)NOTES_PRESSURE_MAX)
			return EINVAL;
		if (x < -(int64_t)(ENCODE_LENGTH_MAX * NOTES_UNITS_PER_POINT) || x > (int64_t)(ENCODE_LENGTH_MAX * NOTES_UNITS_PER_POINT))
			return EINVAL;
		if (y < -(int64_t)(ENCODE_LENGTH_MAX * NOTES_UNITS_PER_POINT) || y > (int64_t)(ENCODE_LENGTH_MAX * NOTES_UNITS_PER_POINT))
			return EINVAL;
		if (tilt_x < -32768 ||
		    tilt_x > 32767 ||
		    tilt_y < -32768 ||
		    tilt_y > 32767 ||
		    time > 0xffffffffU)
			return EINVAL;

		/* The sample. */
		memset(&point, 0, sizeof(point));
		point.x = (float)x / NOTES_UNITS_PER_POINT;
		point.y = (float)y / NOTES_UNITS_PER_POINT;
		point.pressure = (uint16_t)pressure;
		point.tilt_x = (int16_t)tilt_x;
		point.tilt_y = (int16_t)tilt_y;
		point.time_ms = (uint32_t)time;
		error = notes_stroke_append(stroke, &point);
		if (error != 0)
			return error;
	}

	/* Succeeded: the stroke has its samples. */
	return 0;
}

/* Reads the DOC chunk: the page count, the pressure's range, the time base, the next number. */
static int
decode_doc(
	struct encode_reader *reader,
	struct notes_document *document,
	size_t *pages)
{
	uint64_t count;
	uint64_t next;
	unsigned pressure_max;

	/* The fields. */
	count = read_varint(reader);
	pressure_max = read_u16(reader);
	document->time_base = read_u64(reader);
	next = read_varint(reader);
	if (reader->error != 0)
		return EINVAL;

	/* A count out of range, another pressure range, or a number past 32 bits is not this version's. */
	if (count == 0U || count > ENCODE_PAGES_MAX)
		return EINVAL;
	if (pressure_max != NOTES_PRESSURE_MAX ||
	    next == 0U ||
	    next > 0xffffffffU)
		return EINVAL;

	/* Succeeded: the document knows how many pages follow. */
	document->next_id = (uint32_t)next;
	*pages = (size_t)count;
	return 0;
}

/* Reads the TOOL chunk. */
static int
decode_tools(
	struct encode_reader *reader,
	struct encode_tools *tools)
{
	uint64_t count;
	size_t index;
	uint32_t color;

	/* The count, which must be sane. */
	count = read_varint(reader);
	if (reader->error != 0 || count > ENCODE_TOOLS_MAX)
		return EINVAL;
	if (count == 0U)
		return 0;

	/* The list. */
	tools->tools = calloc((size_t)count, sizeof(tools->tools[0]));
	if (tools->tools == NULL)
		return ENOMEM;
	tools->capacity = (size_t)count;

	/* Each tool: kind, colour, width, curve and flags (the last two have one value in version 1). */
	for (index = 0; index < (size_t)count; index++) {
		tools->tools[index].kind = read_u8(reader);
		color = (uint32_t)read_u8(reader) << 24;
		color |= (uint32_t)read_u8(reader) << 16;
		color |= (uint32_t)read_u8(reader) << 8;
		color |= (uint32_t)read_u8(reader);
		tools->tools[index].color = color;
		tools->tools[index].width = read_length(reader);
		(void)read_u8(reader);
		(void)read_u8(reader);
		if (reader->error != 0)
			return EINVAL;
	}

	/* Succeeded: the strokes can name their tools. */
	tools->count = (size_t)count;
	return 0;
}

/* Reads one PAGE chunk and adds the page to the document. */
static int
decode_page(
	struct encode_reader *reader,
	struct notes_document *document,
	const struct encode_tools *tools,
	size_t index)
{
	struct notes_page *page;
	struct notes_page **larger;
	struct notes_stroke **grown;
	struct notes_stroke *stroke;
	unsigned char digest[32];
	uint64_t number;
	uint64_t strokes;
	uint64_t id;
	uint64_t tool;
	uint64_t count;
	uint64_t first_time;
	float width;
	float height;
	unsigned background;
	unsigned flags;
	size_t stroke_index;
	int error;

	/* The page's number, which must be the next, its size, background and digest. */
	number = read_varint(reader);
	width = read_length(reader);
	height = read_length(reader);
	background = read_u8(reader);
	memset(digest, 0, sizeof(digest));
	if (reader->length - reader->offset < sizeof(digest)) {
		reader->error = 1;
	} else {
		memcpy(digest, reader->data + reader->offset, sizeof(digest));
		reader->offset += sizeof(digest);
	}

	/* How many strokes follow, which must be read in full. */
	strokes = read_varint(reader);
	if (reader->error != 0)
		return EINVAL;

	/* The page must be the next one, have a size, and hold no more strokes than the data could. */
	if (number != index ||
	    !(width > 0.0f) ||
	    !(height > 0.0f))
		return EINVAL;
	if (strokes > reader->length)
		return EINVAL;

	/* The page, with the digest its content had when it was saved. */
	page = notes_page_create(width, height, background);
	if (page == NULL)
		return ENOMEM;
	memcpy(page->content_hash, digest, sizeof(digest));

	/* Room for it in the document. */
	larger = realloc(document->pages, (document->page_count + 1U) * sizeof(document->pages[0]));
	if (larger == NULL) {
		notes_page_free(page);
		return ENOMEM;
	}

	/* The page is the document's last. */
	document->pages = larger;
	document->page_capacity = document->page_count + 1U;
	document->pages[document->page_count] = page;
	document->page_count++;

	/* Each stroke; the page owns each as it is read. */
	for (stroke_index = 0; stroke_index < strokes; stroke_index++) {
		/* The number, the tool, the flags and the sample count. */
		id = read_varint(reader);
		tool = read_varint(reader);
		flags = read_u8(reader);
		count = read_varint(reader);
		if (reader->error != 0 ||
		    tool >= tools->count ||
		    count == 0U ||
		    count > ENCODE_POINTS_MAX ||
		    id > 0xffffffffU)
			return EINVAL;

		/* The stroke with its tool. */
		stroke = notes_stroke_create((uint32_t)id, tools->tools[tool].kind, tools->tools[tool].color, tools->tools[tool].width, 0U);
		if (stroke == NULL)
			return ENOMEM;

		/* The page's array grows to hold it (the count is bounded, so the size cannot overflow). */
		if (page->stroke_count == page->stroke_capacity) {
			grown = realloc(page->strokes, (page->stroke_capacity * 2U + 16U) * sizeof(page->strokes[0]));
			if (grown == NULL) {
				notes_stroke_free(stroke);
				return ENOMEM;
			}

			/* The larger array. */
			page->strokes = grown;
			page->stroke_capacity = page->stroke_capacity * 2U + 16U;
		}

		/* The page owns the stroke from now on. */
		page->strokes[page->stroke_count] = stroke;
		page->stroke_count++;

		/* Its samples, and its start from the time base. */
		error = decode_samples(reader, stroke, flags, (size_t)count, &first_time);
		if (error != 0)
			return error;
		stroke->start_ms = document->time_base + first_time;
	}

	/* Succeeded: the page is the document's last. */
	return 0;
}

/* Reads one byte. */
static unsigned
read_u8(
	struct encode_reader *reader)
{
	/* Past the end is a failure. */
	if (reader->error != 0 || reader->offset >= reader->length) {
		reader->error = 1;
		return 0U;
	}

	/* Reports the byte and moves past it. */
	reader->offset++;
	return reader->data[reader->offset - 1U];
}

/* Reads a 16-bit number, least significant byte first. */
static unsigned
read_u16(
	struct encode_reader *reader)
{
	unsigned value;

	/* The two bytes, low first. */
	value = read_u8(reader);
	value |= read_u8(reader) << 8;

	/* Reports the number. */
	return value;
}

/* Reads a 32-bit number, least significant byte first. */
static uint32_t
read_u32(
	struct encode_reader *reader)
{
	uint32_t value;
	unsigned index;

	/* The four bytes, low first. */
	value = 0U;
	for (index = 0; index < 4U; index++)
		value |= (uint32_t)read_u8(reader) << (index * 8U);

	/* Reports the number. */
	return value;
}

/* Reads a 64-bit number, least significant byte first. */
static uint64_t
read_u64(
	struct encode_reader *reader)
{
	uint64_t value;
	unsigned index;

	/* The eight bytes, low first. */
	value = 0U;
	for (index = 0; index < 8U; index++)
		value |= (uint64_t)read_u8(reader) << (index * 8U);

	/* Reports the number. */
	return value;
}

/* Reads an unsigned LEB128 number of at most 64 bits. */
static uint64_t
read_varint(
	struct encode_reader *reader)
{
	uint64_t value;
	unsigned shift;
	unsigned byte;

	/* Seven bits a byte, low first, while the continuation bit is set. */
	value = 0U;
	for (shift = 0; shift < 64U; shift += 7U) {
		byte = read_u8(reader);
		value |= (uint64_t)(byte & 0x7fU) << shift;
		if ((byte & 0x80U) == 0U)
			return value;
	}

	/* A number longer than 64 bits is malformed. */
	reader->error = 1;
	return 0U;
}

/* Reads a zigzag LEB128 number. */
static int64_t
read_zigzag(
	struct encode_reader *reader)
{
	uint64_t folded;

	/* The even numbers are the positive values, the odd ones the negative. */
	folded = read_varint(reader);
	if ((folded & 1U) != 0U)
		return -(int64_t)(folded >> 1) - 1;

	/* Reports a positive value. */
	return (int64_t)(folded >> 1);
}

/* Reads a length in 1/64 point, and gives it in points (zero when out of range). */
static float
read_length(
	struct encode_reader *reader)
{
	uint64_t value;

	/* The number, which must stay inside the largest length. */
	value = read_varint(reader);
	if ((float)value > ENCODE_LENGTH_MAX * NOTES_UNITS_PER_POINT)
		return 0.0f;

	/* Reports it in points. */
	return (float)value / NOTES_UNITS_PER_POINT;
}

/* Converts a length in points to whole 1/64 points. */
static int64_t
units(
	float value)
{
	/* The nearest whole number. */
	return (int64_t)floor((double)value * (double)NOTES_UNITS_PER_POINT + 0.5);
}
