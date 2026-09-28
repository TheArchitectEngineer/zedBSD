/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The PDF writer of libpdf.
 *
 * Pages are drawn into content streams kept in memory.  Saving numbers
 * every object, writes them in order with a classic cross-reference table,
 * and stores the result in one write of the whole file.  The layout is the
 * one plan/ws079/design-pdf.md section 1 describes.
 */

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pdf.h>

/* The largest coordinate the writer accepts, well inside what any reader handles as a real. */
#define PDF_WRITER_COORDINATE_LIMIT 1000000.0

/* The most distinct fill alphas one document may use, each an ExtGState object. */
#define PDF_WRITER_ALPHA_MAX 64

/* The objects the writer places before the pages: the catalog, the page tree and the information dictionary. */
#define PDF_WRITER_OBJECT_CATALOG 1
#define PDF_WRITER_OBJECT_PAGES 2
#define PDF_WRITER_OBJECT_INFO 3
#define PDF_WRITER_OBJECT_FIRST_PAGE 4

/*
 * A growable run of bytes.
 *
 * It is used for each page's content stream and for the whole file while it
 * is being saved; the owner frees its data.
 */
struct pdf_buffer {
	unsigned char *data;
	size_t length;
	size_t capacity;
};

/*
 * One finished or open page.
 *
 * The page owns its content stream and lives until the writer is destroyed.
 */
struct pdf_writer_page {
	double width;
	double height;
	struct pdf_buffer content;
};

/*
 * The file attached to the document, which is Notes' edit data.
 *
 * The writer holds its own copy, so the caller's buffer may go away.
 */
struct pdf_writer_attachment {
	char *name;
	char *mime_type;
	unsigned char *data;
	size_t size;
};

/*
 * A document being written.
 *
 * The page array only grows.  open_page is the index of the page drawing
 * commands go to, or pages_count when no page is open.
 */
struct pdf_writer {
	struct pdf_writer_page **pages;
	size_t pages_count;
	size_t pages_capacity;
	int page_is_open;
	double current_alpha;
	double alphas[PDF_WRITER_ALPHA_MAX];
	size_t alphas_count;
	struct pdf_writer_attachment attachment;
	int has_attachment;
};

static int buffer_reserve(struct pdf_buffer *buffer, size_t extra);
static int buffer_append(struct pdf_buffer *buffer, const void *data, size_t length);
static int buffer_printf(struct pdf_buffer *buffer, const char *format, ...) __attribute__((format(printf, 2, 3)));
static int buffer_append_number(struct pdf_buffer *buffer, double number);
static int buffer_append_literal_string(struct pdf_buffer *buffer, const char *text);
static int check_coordinate(double value);
static int append_point(struct pdf_writer *writer, double x, double y, const char *operator_name);
static int find_or_add_alpha(struct pdf_writer *writer, double alpha, size_t *index);
static int copy_string(const char *source, char **copy);
static int mime_type_is_valid(const char *mime_type);
static int write_document(struct pdf_writer *writer, struct pdf_buffer *file);
static int write_page_objects(struct pdf_writer *writer, struct pdf_buffer *file, size_t *offsets, size_t page_index);
static int write_attachment_objects(struct pdf_writer *writer, struct pdf_buffer *file, size_t *offsets, size_t file_object);
static int write_resources(struct pdf_writer *writer, struct pdf_buffer *file);
static int write_cross_reference(struct pdf_buffer *file, const size_t *offsets, size_t object_count);

/*
 * Creates an empty document with no pages.
 */
int
pdf_writer_create(
	struct pdf_writer **writer)
{
	struct pdf_writer *created;

	/* Allocates the document with every list empty. */
	created = calloc(1, sizeof(*created));
	if (created == NULL)
		return ENOMEM;

	/* A new page starts opaque, so the first translucent fill must select its alpha. */
	created->current_alpha = 1.0;

	/* Succeeded: the caller owns the new document. */
	*writer = created;
	return 0;
}

/*
 * Destroys a document and every page and attachment it holds.
 */
void
pdf_writer_destroy(
	struct pdf_writer *writer)
{
	size_t index;

	/* Nothing was created. */
	if (writer == NULL)
		return;

	/* Frees each page's content stream and the page itself. */
	for (index = 0; index < writer->pages_count; index++) {
		free(writer->pages[index]->content.data);
		free(writer->pages[index]);
	}

	/* Frees the page array, the attachment and the document. */
	free(writer->pages);
	free(writer->attachment.name);
	free(writer->attachment.mime_type);
	free(writer->attachment.data);
	free(writer);
}

/*
 * Opens a new page of the given size in points.
 *
 * Drawing uses the page's top-left corner as the origin with y growing
 * downward, as Notes' model does.
 */
int
pdf_writer_begin_page(
	struct pdf_writer *writer,
	double width,
	double height)
{
	struct pdf_writer_page *page;
	struct pdf_writer_page **grown;
	size_t capacity;
	int error;

	/* Refuses a second open page. */
	if (writer->page_is_open)
		return EINVAL;

	/* Refuses a page without area or larger than any reader accepts. */
	if (!(width > 0.0) || !(height > 0.0))
		return EINVAL;
	if (width > PDF_WRITER_COORDINATE_LIMIT || height > PDF_WRITER_COORDINATE_LIMIT)
		return EINVAL;

	/* Grows the page array when it is full. */
	if (writer->pages_count == writer->pages_capacity) {
		capacity = writer->pages_capacity * 2;
		if (capacity == 0)
			capacity = 8;
		grown = realloc(writer->pages, capacity * sizeof(*grown));
		if (grown == NULL)
			return ENOMEM;
		writer->pages = grown;
		writer->pages_capacity = capacity;
	}

	/* Allocates the page with an empty content stream. */
	page = calloc(1, sizeof(*page));
	if (page == NULL)
		return ENOMEM;
	page->width = width;
	page->height = height;

	/* Flips the y axis so the content uses the model's top-left origin. */
	error = buffer_printf(&page->content, "1 0 0 -1 0 ");
	if (error == 0)
		error = buffer_append_number(&page->content, height);
	if (error == 0)
		error = buffer_printf(&page->content, " cm\n");
	if (error != 0) {
		free(page->content.data);
		free(page);
		return error;
	}

	/* Publishes the page as the one drawing goes to; its graphics state starts opaque. */
	writer->pages[writer->pages_count] = page;
	writer->pages_count++;
	writer->page_is_open = 1;
	writer->current_alpha = 1.0;

	/* Succeeded: drawing now goes to the new page. */
	return 0;
}

/*
 * Finishes the open page.
 */
int
pdf_writer_end_page(
	struct pdf_writer *writer)
{
	/* Refuses to end a page that is not open. */
	if (!writer->page_is_open)
		return EINVAL;

	/* Later drawing is refused until another page opens. */
	writer->page_is_open = 0;

	/* Succeeded: the page is final. */
	return 0;
}

/*
 * Selects the color and opacity of the following fills.
 *
 * The components run from 0 to 1.  An opacity other than the current one is
 * selected through an ExtGState that every page's resources list.
 */
int
pdf_writer_set_fill_color(
	struct pdf_writer *writer,
	double red,
	double green,
	double blue,
	double alpha)
{
	struct pdf_buffer *content;
	size_t alpha_index;
	int error;

	/* Refuses drawing outside a page. */
	if (!writer->page_is_open)
		return EINVAL;

	/* Refuses a component outside 0 to 1, including a NaN. */
	if (!(red >= 0.0 && red <= 1.0) || !(green >= 0.0 && green <= 1.0))
		return EINVAL;
	if (!(blue >= 0.0 && blue <= 1.0) || !(alpha >= 0.0 && alpha <= 1.0))
		return EINVAL;

	/* Writes the DeviceRGB fill color. */
	content = &writer->pages[writer->pages_count - 1]->content;
	error = buffer_append_number(content, red);
	if (error == 0)
		error = buffer_printf(content, " ");
	if (error == 0)
		error = buffer_append_number(content, green);
	if (error == 0)
		error = buffer_printf(content, " ");
	if (error == 0)
		error = buffer_append_number(content, blue);
	if (error == 0)
		error = buffer_printf(content, " rg\n");
	if (error != 0)
		return error;

	/* An unchanged opacity needs no graphics state. */
	if (alpha == writer->current_alpha)
		return 0;

	/* Finds the ExtGState that holds this opacity. */
	error = find_or_add_alpha(writer, alpha, &alpha_index);
	if (error != 0)
		return error;

	/* Selects that ExtGState. */
	error = buffer_printf(content, "/GS%lu gs\n", (unsigned long)alpha_index);
	if (error != 0)
		return error;

	/* The page's fills now carry this opacity. */
	writer->current_alpha = alpha;

	/* Succeeded: the next fills use the new color and opacity. */
	return 0;
}

/*
 * Starts a new subpath at a point.
 */
int
pdf_writer_move_to(
	struct pdf_writer *writer,
	double x,
	double y)
{
	int error;

	/* Writes the point with the move operator. */
	error = append_point(writer, x, y, " m\n");
	if (error != 0)
		return error;

	/* Succeeded: a subpath starts at the point. */
	return 0;
}

/*
 * Adds a straight segment to the current subpath.
 */
int
pdf_writer_line_to(
	struct pdf_writer *writer,
	double x,
	double y)
{
	int error;

	/* Writes the point with the line operator. */
	error = append_point(writer, x, y, " l\n");
	if (error != 0)
		return error;

	/* Succeeded: the subpath ends at the point. */
	return 0;
}

/*
 * Adds a cubic Bezier segment to the current subpath.
 */
int
pdf_writer_curve_to(
	struct pdf_writer *writer,
	double x1,
	double y1,
	double x2,
	double y2,
	double x3,
	double y3)
{
	int error;

	/* Writes the two control points, then the end point with the curve operator. */
	error = append_point(writer, x1, y1, " ");
	if (error == 0)
		error = append_point(writer, x2, y2, " ");
	if (error == 0)
		error = append_point(writer, x3, y3, " c\n");
	if (error != 0)
		return error;

	/* Succeeded: the subpath ends at the curve's end point. */
	return 0;
}

/*
 * Closes the current subpath back to its start.
 */
int
pdf_writer_close_path(
	struct pdf_writer *writer)
{
	int error;

	/* Refuses drawing outside a page. */
	if (!writer->page_is_open)
		return EINVAL;

	/* Writes the close operator. */
	error = buffer_printf(&writer->pages[writer->pages_count - 1]->content, "h\n");
	if (error != 0)
		return error;

	/* Succeeded: the subpath is closed. */
	return 0;
}

/*
 * Fills the current path with the current color and ends it.
 */
int
pdf_writer_fill(
	struct pdf_writer *writer,
	enum pdf_fill_rule rule)
{
	const char *operator_text;
	int error;

	/* Refuses drawing outside a page. */
	if (!writer->page_is_open)
		return EINVAL;

	/* Chooses the fill operator of the rule. */
	if (rule == PDF_FILL_NONZERO) {
		operator_text = "f\n";
	} else if (rule == PDF_FILL_EVEN_ODD) {
		operator_text = "f*\n";
	} else {
		return EINVAL;
	}

	/* Writes the fill operator. */
	error = buffer_printf(&writer->pages[writer->pages_count - 1]->content, "%s", operator_text);
	if (error != 0)
		return error;

	/* Succeeded: the path is painted and gone. */
	return 0;
}

/*
 * Attaches a file to the document, as Notes does with its edit data.
 *
 * The file is an embedded file stream that the catalog lists both in the
 * EmbeddedFiles name tree and in its associated files.  One document holds
 * one attachment.
 */
int
pdf_writer_attach_file(
	struct pdf_writer *writer,
	const char *name,
	const char *mime_type,
	const void *data,
	size_t size)
{
	struct pdf_writer_attachment attachment;
	int valid;
	int error;

	/* Refuses a second attachment. */
	if (writer->has_attachment)
		return EEXIST;

	/* Refuses an empty name, and data that is missing although it has a size. */
	if (name == NULL || name[0] == '\0')
		return EINVAL;
	if (data == NULL && size != 0)
		return EINVAL;

	/* Refuses a media type that cannot be written as a name. */
	valid = mime_type_is_valid(mime_type);
	if (!valid)
		return EINVAL;

	/* Copies the name and the media type. */
	memset(&attachment, 0, sizeof(attachment));
	error = copy_string(name, &attachment.name);
	if (error != 0)
		return error;
	error = copy_string(mime_type, &attachment.mime_type);
	if (error != 0) {
		free(attachment.name);
		return error;
	}

	/* Copies the data; one extra byte keeps an empty file's allocation valid. */
	attachment.data = malloc(size + 1);
	if (attachment.data == NULL) {
		free(attachment.name);
		free(attachment.mime_type);
		return ENOMEM;
	}
	if (size != 0)
		memcpy(attachment.data, data, size);
	attachment.size = size;

	/* Publishes the attachment in the document. */
	writer->attachment = attachment;
	writer->has_attachment = 1;

	/* Succeeded: the document carries the file. */
	return 0;
}

/*
 * Saves the document to a file, replacing the file.
 *
 * The open page, if any, is saved as it stands.  The document stays usable,
 * so more pages may be added and the document saved again.
 */
int
pdf_writer_save(
	struct pdf_writer *writer,
	const char *path)
{
	struct pdf_buffer file;
	FILE *stream;
	size_t written;
	int closed;
	int error;

	/* Refuses a document without pages, which is not a valid PDF. */
	if (writer->pages_count == 0)
		return EINVAL;

	/* Lays out the whole file in memory. */
	memset(&file, 0, sizeof(file));
	error = write_document(writer, &file);
	if (error != 0) {
		free(file.data);
		return error;
	}

	/* Opens the destination. */
	stream = fopen(path, "wb");
	if (stream == NULL) {
		error = errno;
		free(file.data);
		return error;
	}

	/* Writes the file in one pass and closes it, which reports a late write failure. */
	written = fwrite(file.data, 1, file.length, stream);
	closed = fclose(stream);
	free(file.data);

	/* Reports a short write or a failed close. */
	if (written != file.length)
		return EIO;
	if (closed != 0)
		return EIO;

	/* Succeeded: the file holds the document. */
	return 0;
}

/* Makes room for extra more bytes in a buffer. */
static int
buffer_reserve(
	struct pdf_buffer *buffer,
	size_t extra)
{
	unsigned char *grown;
	size_t capacity;

	/* The buffer already has room. */
	if (buffer->capacity - buffer->length >= extra)
		return 0;

	/* Refuses a size that would overflow. */
	if (extra > (size_t)-1 / 2 - buffer->length)
		return ENOMEM;

	/* Doubles the capacity until the bytes fit. */
	capacity = buffer->capacity;
	if (capacity < 256)
		capacity = 256;
	while (capacity - buffer->length < extra)
		capacity *= 2;

	/* Moves the bytes into the larger allocation. */
	grown = realloc(buffer->data, capacity);
	if (grown == NULL)
		return ENOMEM;
	buffer->data = grown;
	buffer->capacity = capacity;

	/* Succeeded: extra bytes fit. */
	return 0;
}

/* Appends bytes to a buffer. */
static int
buffer_append(
	struct pdf_buffer *buffer,
	const void *data,
	size_t length)
{
	int error;

	/* Makes room for the bytes. */
	error = buffer_reserve(buffer, length);
	if (error != 0)
		return error;

	/* Copies the bytes to the end. */
	if (length != 0)
		memcpy(buffer->data + buffer->length, data, length);
	buffer->length += length;

	/* Succeeded: the bytes are appended. */
	return 0;
}

/* Appends formatted text, which is always short, to a buffer. */
static int
buffer_printf(
	struct pdf_buffer *buffer,
	const char *format,
	...)
{
	char text[256];
	va_list arguments;
	int length;
	int error;

	/* Formats the text. */
	va_start(arguments, format);
	length = vsnprintf(text, sizeof(text), format, arguments);
	va_end(arguments);

	/* Refuses text that failed or did not fit; the writer only formats short tokens. */
	if (length < 0 || (size_t)length >= sizeof(text))
		return EINVAL;

	/* Appends the text. */
	error = buffer_append(buffer, text, (size_t)length);
	if (error != 0)
		return error;

	/* Succeeded: the text is appended. */
	return 0;
}

/*
 * Appends a real number as PDF writes one.
 *
 * PDF has no exponent notation, so the number is written with four decimals
 * and the trailing zeros removed.
 */
static int
buffer_append_number(
	struct pdf_buffer *buffer,
	double number)
{
	char text[64];
	int length;
	int error;

	/* Formats the number with a fixed number of decimals. */
	length = snprintf(text, sizeof(text), "%.4f", number);
	if (length < 0 || (size_t)length >= sizeof(text))
		return EINVAL;

	/* Removes the trailing zeros, then a trailing point. */
	while (length > 1 && text[length - 1] == '0')
		length--;
	if (length > 1 && text[length - 1] == '.')
		length--;

	/* A negative number that rounded to zero is written as zero. */
	if (length == 2 && text[0] == '-' && text[1] == '0') {
		text[0] = '0';
		length = 1;
	}

	/* Appends the number. */
	error = buffer_append(buffer, text, (size_t)length);
	if (error != 0)
		return error;

	/* Succeeded: the number is appended. */
	return 0;
}

/* Appends text as a PDF literal string, escaping the characters that end or escape one. */
static int
buffer_append_literal_string(
	struct pdf_buffer *buffer,
	const char *text)
{
	const char *character;
	char escaped[2];
	int error;

	/* Opens the string. */
	error = buffer_append(buffer, "(", 1);
	if (error != 0)
		return error;

	/* Copies each character, escaping parentheses and backslashes. */
	for (character = text; *character != '\0'; character++) {
		if (*character == '(' || *character == ')' || *character == '\\') {
			escaped[0] = '\\';
			escaped[1] = *character;
			error = buffer_append(buffer, escaped, 2);
		} else {
			error = buffer_append(buffer, character, 1);
		}
		if (error != 0)
			return error;
	}

	/* Closes the string. */
	error = buffer_append(buffer, ")", 1);
	if (error != 0)
		return error;

	/* Succeeded: the string is appended. */
	return 0;
}

/* Reports whether a coordinate is finite and inside the writer's limit. */
static int
check_coordinate(
	double value)
{
	/* Refuses a NaN and anything past the limit on either side. */
	if (!(value >= -PDF_WRITER_COORDINATE_LIMIT && value <= PDF_WRITER_COORDINATE_LIMIT))
		return EINVAL;

	/* Succeeded: the coordinate can be written. */
	return 0;
}

/* Appends one point followed by an operator or a separator to the open page. */
static int
append_point(
	struct pdf_writer *writer,
	double x,
	double y,
	const char *operator_name)
{
	struct pdf_buffer *content;
	int error;

	/* Refuses drawing outside a page. */
	if (!writer->page_is_open)
		return EINVAL;

	/* Refuses a coordinate that cannot be written. */
	error = check_coordinate(x);
	if (error != 0)
		return error;
	error = check_coordinate(y);
	if (error != 0)
		return error;

	/* Writes the two coordinates and the operator. */
	content = &writer->pages[writer->pages_count - 1]->content;
	error = buffer_append_number(content, x);
	if (error == 0)
		error = buffer_append(content, " ", 1);
	if (error == 0)
		error = buffer_append_number(content, y);
	if (error == 0)
		error = buffer_printf(content, "%s", operator_name);
	if (error != 0)
		return error;

	/* Succeeded: the point is written. */
	return 0;
}

/* Finds the ExtGState index of an opacity, adding one when the document has none yet. */
static int
find_or_add_alpha(
	struct pdf_writer *writer,
	double alpha,
	size_t *index)
{
	size_t candidate;

	/* Reuses the ExtGState that already holds this opacity. */
	for (candidate = 0; candidate < writer->alphas_count; candidate++) {
		if (writer->alphas[candidate] == alpha) {
			*index = candidate;
			return 0;
		}
	}

	/* Refuses an opacity past the document's limit. */
	if (writer->alphas_count == PDF_WRITER_ALPHA_MAX)
		return ENOSPC;

	/* Adds the opacity as a new ExtGState. */
	writer->alphas[writer->alphas_count] = alpha;
	*index = writer->alphas_count;
	writer->alphas_count++;

	/* Succeeded: index names the ExtGState. */
	return 0;
}

/* Copies a string into a new allocation. */
static int
copy_string(
	const char *source,
	char **copy)
{
	size_t length;
	char *created;

	/* Allocates the copy with its terminator. */
	length = strlen(source);
	created = malloc(length + 1);
	if (created == NULL)
		return ENOMEM;

	/* Copies the characters and the terminator. */
	memcpy(created, source, length + 1);

	/* Succeeded: the caller owns the copy. */
	*copy = created;
	return 0;
}

/*
 * Reports whether a media type can be written as a PDF name.
 *
 * Only the characters of ordinary media types are accepted; the slash is
 * written as #2F.
 */
static int
mime_type_is_valid(
	const char *mime_type)
{
	const char *character;

	/* A missing or empty type is not a media type. */
	if (mime_type == NULL || mime_type[0] == '\0')
		return 0;

	/* Accepts letters, digits and the punctuation of media types. */
	for (character = mime_type; *character != '\0'; character++) {
		if (*character >= 'a' && *character <= 'z')
			continue;
		if (*character >= 'A' && *character <= 'Z')
			continue;
		if (*character >= '0' && *character <= '9')
			continue;
		if (*character == '.' || *character == '+' || *character == '-' || *character == '/')
			continue;
		return 0;
	}

	/* Every character can be written in a name. */
	return 1;
}

/*
 * Lays out the whole document.
 *
 * The objects are numbered as follows: 1 the catalog, 2 the page tree, 3 the
 * information dictionary, then a page object and its content stream for each
 * page, then the ExtGStates, then the embedded file stream and its file
 * specification.
 */
static int
write_document(
	struct pdf_writer *writer,
	struct pdf_buffer *file)
{
	static const char header[] = "%PDF-1.7\n%\xE2\xE3\xCF\xD3\n";
	size_t *offsets;
	size_t object_count;
	size_t first_alpha_object;
	size_t file_object;
	size_t index;
	int error;

	/* Numbers the objects after the pages. */
	first_alpha_object = PDF_WRITER_OBJECT_FIRST_PAGE + writer->pages_count * 2;
	file_object = first_alpha_object + writer->alphas_count;
	object_count = file_object;
	if (writer->has_attachment)
		object_count += 2;

	/* Allocates the offset of each object; entry 0 is the free list head. */
	offsets = calloc(object_count, sizeof(*offsets));
	if (offsets == NULL)
		return ENOMEM;

	/* Writes the header, whose binary comment marks the file as binary. */
	error = buffer_append(file, header, sizeof(header) - 1);
	if (error != 0)
		goto out;

	/* Writes the catalog, which lists the attachment when there is one. */
	offsets[PDF_WRITER_OBJECT_CATALOG] = file->length;
	error = buffer_printf(file, "1 0 obj\n<< /Type /Catalog /Pages 2 0 R");
	if (error == 0 && writer->has_attachment) {
		error = buffer_printf(file, " /Names << /EmbeddedFiles << /Names [");
		if (error == 0)
			error = buffer_append_literal_string(file, writer->attachment.name);
		if (error == 0)
			error = buffer_printf(file, " %lu 0 R] >> >> /AF [%lu 0 R]", (unsigned long)(file_object + 1), (unsigned long)(file_object + 1));
	}
	if (error == 0)
		error = buffer_printf(file, " >>\nendobj\n");
	if (error != 0)
		goto out;

	/* Writes the page tree, which lists every page object. */
	offsets[PDF_WRITER_OBJECT_PAGES] = file->length;
	error = buffer_printf(file, "2 0 obj\n<< /Type /Pages /Kids [");
	for (index = 0; error == 0 && index < writer->pages_count; index++)
		error = buffer_printf(file, "%s%lu 0 R", index == 0 ? "" : " ", (unsigned long)(PDF_WRITER_OBJECT_FIRST_PAGE + index * 2));
	if (error == 0)
		error = buffer_printf(file, "] /Count %lu >>\nendobj\n", (unsigned long)writer->pages_count);
	if (error != 0)
		goto out;

	/* Writes the information dictionary. */
	offsets[PDF_WRITER_OBJECT_INFO] = file->length;
	error = buffer_printf(file, "3 0 obj\n<< /Producer (zedBSD Notes) >>\nendobj\n");
	if (error != 0)
		goto out;

	/* Writes each page and its content stream. */
	for (index = 0; index < writer->pages_count; index++) {
		error = write_page_objects(writer, file, offsets, index);
		if (error != 0)
			goto out;
	}

	/* Writes each opacity's ExtGState. */
	for (index = 0; index < writer->alphas_count; index++) {
		offsets[first_alpha_object + index] = file->length;
		error = buffer_printf(file, "%lu 0 obj\n<< /Type /ExtGState /ca ", (unsigned long)(first_alpha_object + index));
		if (error == 0)
			error = buffer_append_number(file, writer->alphas[index]);
		if (error == 0)
			error = buffer_printf(file, " >>\nendobj\n");
		if (error != 0)
			goto out;
	}

	/* Writes the attachment. */
	if (writer->has_attachment) {
		error = write_attachment_objects(writer, file, offsets, file_object);
		if (error != 0)
			goto out;
	}

	/* Writes the cross-reference table and the trailer. */
	error = write_cross_reference(file, offsets, object_count);

out:
	free(offsets);

	/* Reports why the document could not be laid out. */
	if (error != 0)
		return error;

	/* Succeeded: file holds the whole document. */
	return 0;
}

/* Writes one page object and its content stream. */
static int
write_page_objects(
	struct pdf_writer *writer,
	struct pdf_buffer *file,
	size_t *offsets,
	size_t page_index)
{
	struct pdf_writer_page *page;
	size_t page_object;
	int error;

	/* Finds the page and its object number; the content stream follows it. */
	page = writer->pages[page_index];
	page_object = PDF_WRITER_OBJECT_FIRST_PAGE + page_index * 2;

	/* Writes the page with its media box and resources. */
	offsets[page_object] = file->length;
	error = buffer_printf(file, "%lu 0 obj\n<< /Type /Page /Parent 2 0 R /MediaBox [0 0 ", (unsigned long)page_object);
	if (error == 0)
		error = buffer_append_number(file, page->width);
	if (error == 0)
		error = buffer_append(file, " ", 1);
	if (error == 0)
		error = buffer_append_number(file, page->height);
	if (error == 0)
		error = buffer_printf(file, "] /Resources ");
	if (error == 0)
		error = write_resources(writer, file);
	if (error == 0)
		error = buffer_printf(file, " /Contents %lu 0 R >>\nendobj\n", (unsigned long)(page_object + 1));
	if (error != 0)
		return error;

	/* Writes the content stream with its exact length. */
	offsets[page_object + 1] = file->length;
	error = buffer_printf(file, "%lu 0 obj\n<< /Length %lu >>\nstream\n", (unsigned long)(page_object + 1), (unsigned long)page->content.length);
	if (error == 0)
		error = buffer_append(file, page->content.data, page->content.length);
	if (error == 0)
		error = buffer_printf(file, "\nendstream\nendobj\n");
	if (error != 0)
		return error;

	/* Succeeded: the page and its content are written. */
	return 0;
}

/* Writes the embedded file stream and the file specification that names it. */
static int
write_attachment_objects(
	struct pdf_writer *writer,
	struct pdf_buffer *file,
	size_t *offsets,
	size_t file_object)
{
	const char *character;
	int error;

	/* Writes the embedded file stream's dictionary with its media type as a name. */
	offsets[file_object] = file->length;
	error = buffer_printf(file, "%lu 0 obj\n<< /Type /EmbeddedFile /Subtype /", (unsigned long)file_object);
	for (character = writer->attachment.mime_type; error == 0 && *character != '\0'; character++) {
		if (*character == '/') {
			error = buffer_append(file, "#2F", 3);
		} else {
			error = buffer_append(file, character, 1);
		}
	}
	if (error == 0) {
		error = buffer_printf(file,
				      " /Params << /Size %lu >> /Length %lu >>\nstream\n",
				      (unsigned long)writer->attachment.size,
				      (unsigned long)writer->attachment.size);
	}
	if (error != 0)
		return error;

	/* Writes the file's bytes. */
	error = buffer_append(file, writer->attachment.data, writer->attachment.size);
	if (error == 0)
		error = buffer_printf(file, "\nendstream\nendobj\n");
	if (error != 0)
		return error;

	/* Writes the file specification, which marks the file as the source of the drawing. */
	offsets[file_object + 1] = file->length;
	error = buffer_printf(file, "%lu 0 obj\n<< /Type /Filespec /F ", (unsigned long)(file_object + 1));
	if (error == 0)
		error = buffer_append_literal_string(file, writer->attachment.name);
	if (error == 0)
		error = buffer_printf(file, " /UF ");
	if (error == 0)
		error = buffer_append_literal_string(file, writer->attachment.name);
	if (error == 0) {
		error = buffer_printf(file,
				      " /Desc (zedBSD Notes edit data) /AFRelationship /Source /EF << /F %lu 0 R >> >>\nendobj\n",
				      (unsigned long)file_object);
	}
	if (error != 0)
		return error;

	/* Succeeded: the attachment is written. */
	return 0;
}

/* Writes the resource dictionary every page shares, which lists the opacities. */
static int
write_resources(
	struct pdf_writer *writer,
	struct pdf_buffer *file)
{
	size_t first_alpha_object;
	size_t index;
	int error;

	/* A document without translucent fills needs no resources. */
	if (writer->alphas_count == 0) {
		error = buffer_printf(file, "<< >>");
		if (error != 0)
			return error;
		return 0;
	}

	/* Lists each ExtGState under the name the content streams use. */
	first_alpha_object = PDF_WRITER_OBJECT_FIRST_PAGE + writer->pages_count * 2;
	error = buffer_printf(file, "<< /ExtGState <<");
	for (index = 0; error == 0 && index < writer->alphas_count; index++)
		error = buffer_printf(file, " /GS%lu %lu 0 R", (unsigned long)index, (unsigned long)(first_alpha_object + index));
	if (error == 0)
		error = buffer_printf(file, " >> >>");
	if (error != 0)
		return error;

	/* Succeeded: the resources are written. */
	return 0;
}

/* Writes the classic cross-reference table, the trailer and the end marker. */
static int
write_cross_reference(
	struct pdf_buffer *file,
	const size_t *offsets,
	size_t object_count)
{
	size_t table_offset;
	size_t index;
	int error;

	/* Writes the table's header and the free list head. */
	table_offset = file->length;
	error = buffer_printf(file, "xref\n0 %lu\n0000000000 65535 f \n", (unsigned long)object_count);
	if (error != 0)
		return error;

	/* Writes each object's offset as a twenty-byte entry. */
	for (index = 1; index < object_count; index++) {
		error = buffer_printf(file, "%010lu 00000 n \n", (unsigned long)offsets[index]);
		if (error != 0)
			return error;
	}

	/* Writes the trailer and the offset of the table. */
	error = buffer_printf(file,
			      "trailer\n<< /Size %lu /Root 1 0 R /Info 3 0 R >>\nstartxref\n%lu\n%%%%EOF\n",
			      (unsigned long)object_count,
			      (unsigned long)table_offset);
	if (error != 0)
		return error;

	/* Succeeded: the file is complete. */
	return 0;
}
