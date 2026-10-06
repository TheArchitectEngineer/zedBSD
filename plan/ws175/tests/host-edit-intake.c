/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws175-p006: the host test of the images the editor takes and writes
 * compressed, on make-edit-samples.py's edit-images.pdf and a small JPEG:
 *
 *   host-edit-intake IN JPEG OUT
 *
 * PNGs made here (rows of every filter): an RGB and a Gray one taken as
 * they are and drawn with their own pixels (preview and saved file); the
 * refusals (tRNS, palette, alpha, 16 bits, interlace: ENOTSUP; a broken
 * CRC, rows cut short or too many, an unknown row filter: EINVAL; a side
 * past 16384 or more than 64 M pixels: E2BIG; an orientation of 9:
 * EINVAL); the JPEG turned by its EXIF orientation 6 in the form's place
 * (fitted as shown, its first pixel at the top right); an RGBA image with
 * alpha; a caller of ws175-p003's size.  An update of pages 1 and 2 with
 * the same id's PNG on both is saved to OUT: one image object with the id,
 * the edited content and the RGBA image compressed; read back, the JPEG's
 * bytes, the PNG's rows and the RGBA are those given.  Another update with
 * two images of one id is refused.  Prints each check and exits 0 when
 * all passed.
 */

#include <errno.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <compat/zlib/zlib.h>
#include <pdf.h>

/* A PNG made here: its rows' form, and what is wrong with it. */
#define FAULT_NONE	0
#define FAULT_CRC	1
#define FAULT_SHORT	2
#define FAULT_LONG	3
#define FAULT_FILTER	4

struct png_made {
	unsigned char *file;
	size_t size;
	unsigned char *rows;
	size_t rows_size;
	unsigned char *pixels;
};

static int test_passed;
static int test_failed;

static void check(int ok, const char *what);
static int make_png(size_t width, size_t height, int colour, int depth, int interlace, int transparency, int fault, struct png_made *made);
static void put_chunk(unsigned char *out, size_t *at, const char *type, const unsigned char *data, size_t length);
static void put_u32(unsigned char *out, unsigned long value);
static unsigned char paeth(unsigned char left, unsigned char above, unsigned char corner);
static const struct pdf_display_item *find_image(const struct pdf_display_list *list, size_t width, size_t height);
static int same_pixels(const struct pdf_display_item *item, const unsigned char *pixels, int components);
static size_t count_text(const unsigned char *data, size_t size, const char *text);
static unsigned char *read_file(const char *path, size_t *size);
static void png_source(struct pdf_image_source *source, const struct png_made *made);
static int try_png(struct pdf_page_editor *editor, int colour, int depth, int interlace, int transparency, int fault);

int
main(
	int argc,
	char **argv)
{
	static const double inserted[6] = { 35.0, 0.0, 0.0, -25.0, 10.0, 30.0 };
	static const double other[6] = { 30.0, 0.0, 0.0, -30.0, 20.0, 40.0 };
	struct pdf_document *document;
	struct pdf_document *saved;
	struct pdf_page_editor *editor;
	struct pdf_page_editor *second;
	struct pdf_display_list *list;
	const struct pdf_display_item *item;
	struct pdf_edit_object object;
	struct pdf_image_source source;
	struct pdf_image_source back;
	struct pdf_writer *writer;
	struct png_made rgb;
	struct png_made gray;
	unsigned char *rgba;
	unsigned char *jpeg;
	unsigned char *file;
	void *owned;
	size_t jpeg_size;
	size_t file_size;
	size_t index;
	size_t pages;
	size_t at;
	int error;

	/* The sample, the JPEG and the two PNGs taken as they are. */
	if (argc != 4)
		return 2;
	jpeg = read_file(argv[2], &jpeg_size);
	error = pdf_document_open(argv[1], &document);
	check(error == 0 && jpeg != NULL, "open edit-images.pdf and the JPEG");
	if (error != 0 || jpeg == NULL)
		return 1;
	error = make_png(7, 5, 2, 8, 0, 0, FAULT_NONE, &rgb);
	if (error == 0)
		error = make_png(6, 3, 0, 8, 0, 0, FAULT_NONE, &gray);
	rgba = malloc(64U * 64U * 4U);
	if (error != 0 || rgba == NULL)
		return 1;

	/* An RGBA image of 64 by 64, its alpha a gradient. */
	for (at = 0; at < 64U * 64U; at++) {
		rgba[at * 4U] = (unsigned char)(at % 64U * 4U);
		rgba[at * 4U + 1U] = (unsigned char)(at / 64U * 4U);
		rgba[at * 4U + 2U] = 128U;
		rgba[at * 4U + 3U] = (unsigned char)(at % 64U * 4U + 3U);
	}

	/* Page 1's editor. */
	error = pdf_page_editor_open(document, 0, &editor);
	check(error == 0, "page 1: the editor opens");
	if (error != 0)
		return 1;

	/* The refusals of PNGs of other kinds, and of broken ones. */
	check(try_png(editor, 2, 8, 0, 1, FAULT_NONE) == ENOTSUP, "a PNG with tRNS: ENOTSUP");
	check(try_png(editor, 3, 8, 0, 0, FAULT_NONE) == ENOTSUP, "a palette PNG: ENOTSUP");
	check(try_png(editor, 6, 8, 0, 0, FAULT_NONE) == ENOTSUP, "an RGBA PNG: ENOTSUP");
	check(try_png(editor, 2, 16, 0, 0, FAULT_NONE) == ENOTSUP, "a 16-bit PNG: ENOTSUP");
	check(try_png(editor, 2, 8, 1, 0, FAULT_NONE) == ENOTSUP, "an interlaced PNG: ENOTSUP");
	check(try_png(editor, 2, 8, 0, 0, FAULT_CRC) == EINVAL, "a PNG with a broken CRC: EINVAL");
	check(try_png(editor, 2, 8, 0, 0, FAULT_SHORT) == EINVAL, "a PNG with a row too few: EINVAL");
	check(try_png(editor, 2, 8, 0, 0, FAULT_LONG) == EINVAL, "a PNG with bytes after its rows: EINVAL");
	check(try_png(editor, 2, 8, 0, 0, FAULT_FILTER) == EINVAL, "a PNG with row filter 5: EINVAL");

	/* The refusals of sizes and orientations. */
	memset(&source, 0, sizeof(source));
	source.size = sizeof(source);
	source.kind = PDF_IMAGE_SOURCE_RGBA;
	source.data = rgba;
	source.bytes = 4;
	source.width = 16385;
	source.height = 1;
	check(pdf_page_editor_set_image(editor, 1, &source) == E2BIG, "a side of 16385: E2BIG");
	source.width = 8193;
	source.height = 8193;
	check(pdf_page_editor_set_image(editor, 1, &source) == E2BIG, "8193 by 8193 pixels: E2BIG");
	source.kind = PDF_IMAGE_SOURCE_JPEG;
	source.data = jpeg;
	source.bytes = jpeg_size;
	source.width = 8;
	source.height = 4;
	source.components = 3;
	source.orientation = 9;
	check(pdf_page_editor_set_image(editor, 1, &source) == EINVAL, "orientation 9: EINVAL");

	/* The JPEG turned by orientation 6 in the form's place: shown 4 by 8, fitted 15 by 30 into its 30 by 30, centred. */
	source.orientation = 6;
	source.id = 8;
	error = pdf_page_editor_set_image(editor, 1, &source);
	memset(&object, 0, sizeof(object));
	object.size = sizeof(object);
	if (error == 0)
		error = pdf_page_editor_object(editor, 1, &object);
	check(error == 0 && fabs(object.quad[0] - 107.5) < 0.01 && fabs(object.quad[1] - 70.0) < 0.01 && fabs(object.quad[4] - 122.5) < 0.01 && fabs(object.quad[5] - 100.0) < 0.01, "orientation 6: the JPEG at 107.5,70 to 122.5,100");

	/* The RGB PNG and the RGBA image inserted (id 7 and 9), the Gray PNG from a caller of ws175-p003's size. */
	png_source(&source, &rgb);
	source.id = 7;
	error = pdf_page_editor_insert_image(editor, &source, inserted, &index);
	check(error == 0 && index == 3, "the RGB PNG inserted (rows as they are)");
	memset(&object, 0, sizeof(object));
	object.size = sizeof(object);
	error = pdf_page_editor_object(editor, 3, &object);
	check(error == 0 && object.image_width == 7 && object.image_height == 5, "its size is the file's, 7 by 5");
	memset(&source, 0, sizeof(source));
	source.size = sizeof(source);
	source.kind = PDF_IMAGE_SOURCE_RGBA;
	source.data = rgba;
	source.bytes = 64U * 64U * 4U;
	source.width = 64;
	source.height = 64;
	source.id = 9;
	error = pdf_page_editor_insert_image(editor, &source, other, &index);
	check(error == 0 && index == 4, "the RGBA image inserted");
	png_source(&source, &gray);
	source.size = offsetof(struct pdf_image_source, orientation);
	error = pdf_page_editor_insert_image(editor, &source, other, &index);
	check(error == 0 && index == 5, "the Gray PNG inserted by a caller of ws175-p003's size");

	/* The preview: the PNGs with their own pixels, the turned JPEG's first pixel at the top right. */
	error = pdf_page_editor_render(editor, (size_t)-1, &list);
	check(error == 0, "the preview drawn");
	if (error == 0) {
		item = find_image(list, 7, 5);
		check(item != NULL && same_pixels(item, rgb.pixels, 3), "preview: the RGB PNG's pixels");
		item = find_image(list, 6, 3);
		check(item != NULL && same_pixels(item, gray.pixels, 1), "preview: the Gray PNG's pixels");
		item = find_image(list, 8, 4);
		check(item != NULL && fabs(item->matrix[4] - 122.5) < 0.01 && fabs(item->matrix[5] - 70.0) < 0.01 && fabs(item->matrix[1] - 30.0) < 0.01 && fabs(item->matrix[2] + 15.0) < 0.01, "preview: the turned JPEG's first pixel at 122.5,70, its rows down the page");
		pdf_display_list_destroy(list);
	}

	/* Page 2's editor, the same PNG of id 7 inserted. */
	error = pdf_page_editor_open(document, 1, &second);
	check(error == 0, "page 2: the editor opens");
	if (error != 0)
		return 1;
	png_source(&source, &rgb);
	source.id = 7;
	error = pdf_page_editor_insert_image(second, &source, other, &index);
	check(error == 0, "page 2: the same PNG of id 7 inserted");

	/* The update of pages 1 and 2, the other pages kept. */
	error = pdf_writer_create_update(document, &writer);
	if (error == 0)
		error = pdf_writer_begin_page_edited(writer, editor);
	if (error == 0)
		error = pdf_writer_end_page(writer);
	if (error == 0)
		error = pdf_writer_begin_page_edited(writer, second);
	if (error == 0)
		error = pdf_writer_end_page(writer);
	pages = pdf_document_page_count(document);
	for (index = 2; error == 0 && index < pages; index++)
		error = pdf_writer_keep_page(writer, index);
	if (error == 0)
		error = pdf_writer_save(writer, argv[3]);
	check(error == 0, "the update of pages 1 and 2 saved");
	pdf_writer_destroy(writer);

	/* The file: one image of id 7, the edited contents of both pages and the RGBA image and its mask compressed. */
	file = read_file(argv[3], &file_size);
	check(file != NULL && count_text(file, file_size, "/KeiNotesImage 7") == 1, "one image object of id 7 for both pages");
	check(file != NULL && count_text(file, file_size, "0 obj\n<< /Filter /FlateDecode /Length") == 2, "both pages' edited content compressed");
	check(file != NULL && count_text(file, file_size, "/Filter /FlateDecode /SMask") == 1, "the RGBA image's samples compressed");
	check(file != NULL && count_text(file, file_size, "/DeviceGray /BitsPerComponent 8 /Filter /FlateDecode /Length") == 1, "its mask compressed");
	check(file != NULL && count_text(file, file_size, "/DecodeParms << /Predictor 15 /Colors 3 /BitsPerComponent 8 /Columns 7 >>") == 1, "the RGB PNG's rows with the PNG predictor");
	free(file);

	/* Another update: a second image of id 7 is refused. */
	memset(&source, 0, sizeof(source));
	source.size = sizeof(source);
	source.kind = PDF_IMAGE_SOURCE_RGBA;
	source.data = rgba;
	source.bytes = 64U * 64U * 4U;
	source.width = 64;
	source.height = 64;
	source.id = 7;
	pdf_page_editor_close(second);
	error = pdf_page_editor_open(document, 1, &second);
	if (error == 0)
		error = pdf_page_editor_insert_image(second, &source, other, &index);
	if (error == 0)
		error = pdf_writer_create_update(document, &writer);
	if (error == 0)
		error = pdf_writer_begin_page_edited(writer, editor);
	if (error == 0)
		error = pdf_writer_end_page(writer);
	if (error == 0)
		check(pdf_writer_begin_page_edited(writer, second) == EINVAL, "another image of id 7 in the same update: EINVAL");
	pdf_writer_destroy(writer);
	pdf_page_editor_close(second);
	pdf_page_editor_close(editor);

	/* Opened again: drawn with the PNG's pixels; read back, each image as it was given. */
	error = pdf_document_open(argv[3], &saved);
	check(error == 0, "open the saved file");
	if (error == 0) {
		error = pdf_page_render(saved, 0, &list);
		if (error == 0) {
			item = find_image(list, 7, 5);
			check(item != NULL && same_pixels(item, rgb.pixels, 3), "saved page 1: the RGB PNG's pixels");
			item = find_image(list, 6, 3);
			check(item != NULL && same_pixels(item, gray.pixels, 1), "saved page 1: the Gray PNG's pixels");
			item = find_image(list, 64, 64);
			check(item != NULL && memcmp(item->pixels, rgba, 64U * 64U * 4U) == 0, "saved page 1: the RGBA image's pixels");
			pdf_display_list_destroy(list);
		}

		/* The images read back. */
		error = pdf_page_editor_open(saved, 0, &editor);
		check(error == 0, "saved page 1: the editor opens");
		memset(&back, 0, sizeof(back));
		back.size = sizeof(back);
		error = pdf_page_editor_read_image(editor, 8, &back, &owned);
		check(error == 0 && back.kind == PDF_IMAGE_SOURCE_JPEG && back.bytes == jpeg_size && memcmp(back.data, jpeg, jpeg_size) == 0 && back.components == 3 && back.id == 8, "read back: the JPEG's bytes");
		free(owned);
		error = pdf_page_editor_read_image(editor, 7, &back, &owned);
		check(error == 0 && back.kind == PDF_IMAGE_SOURCE_IDAT && back.bytes == rgb.rows_size && memcmp(back.data, rgb.rows, rgb.rows_size) == 0 && back.width == 7 && back.height == 5 && back.components == 3, "read back: the PNG's rows");

		/* The rows taken again. */
		error = pdf_page_editor_set_image(editor, 0, &back);
		check(error == 0, "the PNG's rows read back are taken again");
		free(owned);
		error = pdf_page_editor_read_image(editor, 9, &back, &owned);
		check(error == 0 && back.kind == PDF_IMAGE_SOURCE_RGBA && back.bytes == 64U * 64U * 4U && memcmp(back.data, rgba, back.bytes) == 0, "read back: the RGBA image");
		free(owned);
		check(pdf_page_editor_read_image(editor, 10, &back, &owned) == ENOENT, "read back an id the page does not have: ENOENT");
		pdf_page_editor_close(editor);
		pdf_document_close(saved);
	}

	/* What was made goes. */
	pdf_document_close(document);
	free(rgb.file);
	free(rgb.rows);
	free(rgb.pixels);
	free(gray.file);
	free(gray.rows);
	free(gray.pixels);
	free(rgba);
	free(jpeg);

	/* The summary. */
	printf("host-edit-intake: %d passed, %d failed\n", test_passed, test_failed);
	if (test_failed != 0)
		return 1;
	return 0;
}

/* Gives a source of a PNG made here. */
static void
png_source(
	struct pdf_image_source *source,
	const struct png_made *made)
{
	/* The file as it is. */
	memset(source, 0, sizeof(*source));
	source->size = sizeof(*source);
	source->kind = PDF_IMAGE_SOURCE_PNG;
	source->data = made->file;
	source->bytes = made->size;
}

/* Makes a PNG of 7 by 5 of a kind and a fault and sets it in object 1's place; returns what the editor said. */
static int
try_png(
	struct pdf_page_editor *editor,
	int colour,
	int depth,
	int interlace,
	int transparency,
	int fault)
{
	struct pdf_image_source source;
	struct png_made made;
	int error;

	/* The PNG. */
	error = make_png(7, 5, colour, depth, interlace, transparency, fault, &made);
	if (error != 0)
		return -1;

	/* Given to the editor. */
	png_source(&source, &made);
	error = pdf_page_editor_set_image(editor, 1, &source);
	free(made.file);
	free(made.rows);
	free(made.pixels);
	return error;
}

/*
 * Makes a PNG: colour type colour (0 Gray, 2 RGB, 3 palette, 6 RGBA),
 * depth bits, interlaced or not, with a tRNS chunk or not, and a fault;
 * its pixels (8-bit samples of a known pattern), each row of the filter
 * its index modulo 5 (row filter 5 for FAULT_FILTER), its rows
 * compressed, and the file.  Returns 0 or ENOMEM.
 */
static int
make_png(
	size_t width,
	size_t height,
	int colour,
	int depth,
	int interlace,
	int transparency,
	int fault,
	struct png_made *made)
{
	unsigned char header[13];
	unsigned char palette[6];
	unsigned char *filtered;
	unsigned char *rows;
	uLongf rows_size;
	size_t samples;
	size_t row_length;
	size_t count;
	size_t at;
	size_t x;
	size_t y;
	unsigned char left;
	unsigned char above;
	unsigned char corner;
	unsigned char value;
	int filter;
	int status;

	/* The samples of a pixel and of a row (a 16-bit sample is two bytes). */
	samples = 3;
	if (colour == 0 || colour == 3)
		samples = 1;
	if (colour == 6)
		samples = 4;
	row_length = width * samples * (size_t)(depth / 8);
	count = height;
	if (fault == FAULT_SHORT)
		count = height - 1;
	if (fault == FAULT_LONG)
		count = height + 1;

	/* The pixels and the filtered rows. */
	memset(made, 0, sizeof(*made));
	made->pixels = malloc(width * height * samples * 2U + 1U);
	filtered = malloc((row_length + 1U) * count);
	if (made->pixels == NULL || filtered == NULL)
		return ENOMEM;
	for (y = 0; y < count; y++) {
		filter = (int)(y % 5U);
		if (fault == FAULT_FILTER && y == 1)
			filter = 5;
		filtered[y * (row_length + 1U)] = (unsigned char)filter;
		for (x = 0; x < row_length; x++) {
			/* The byte, then its neighbours of the same sample. */
			value = (unsigned char)((x * 37U + (y % height) * 91U + 11U) & 0xffU);
			if (colour == 3)
				value = (unsigned char)(value & 1U);
			if (y < height)
				made->pixels[y * row_length + x] = value;
			left = 0;
			above = 0;
			corner = 0;
			if (x >= samples)
				left = (unsigned char)(((x - samples) * 37U + (y % height) * 91U + 11U) & 0xffU);
			if (y > 0)
				above = (unsigned char)((x * 37U + ((y - 1U) % height) * 91U + 11U) & 0xffU);
			if (x >= samples && y > 0)
				corner = (unsigned char)(((x - samples) * 37U + ((y - 1U) % height) * 91U + 11U) & 0xffU);
			if (colour == 3) {
				left = (unsigned char)(left & 1U);
				above = (unsigned char)(above & 1U);
				corner = (unsigned char)(corner & 1U);
			}

			/* The filtered byte. */
			if (filter == 1)
				value = (unsigned char)(value - left);
			if (filter == 2)
				value = (unsigned char)(value - above);
			if (filter == 3)
				value = (unsigned char)(value - (unsigned char)(((unsigned)left + above) / 2U));
			if (filter == 4)
				value = (unsigned char)(value - paeth(left, above, corner));
			filtered[y * (row_length + 1U) + 1U + x] = value;
		}
	}

	/* The rows compressed. */
	rows_size = compressBound((uLong)((row_length + 1U) * count));
	rows = malloc(rows_size);
	if (rows == NULL)
		return ENOMEM;
	status = compress2(rows, &rows_size, filtered, (uLong)((row_length + 1U) * count), 9);
	free(filtered);
	if (status != Z_OK)
		return ENOMEM;
	made->rows = rows;
	made->rows_size = rows_size;

	/* The file: the signature, IHDR, PLTE, tRNS, IDAT and IEND. */
	made->file = malloc(rows_size + 128U);
	if (made->file == NULL)
		return ENOMEM;
	memcpy(made->file, "\211PNG\r\n\032\n", 8);
	at = 8;
	put_u32(header, (unsigned long)width);
	put_u32(header + 4, (unsigned long)height);
	header[8] = (unsigned char)depth;
	header[9] = (unsigned char)colour;
	header[10] = 0;
	header[11] = 0;
	header[12] = (unsigned char)interlace;
	put_chunk(made->file, &at, "IHDR", header, sizeof(header));
	palette[0] = 0;
	palette[1] = 0;
	palette[2] = 0;
	palette[3] = 255;
	palette[4] = 255;
	palette[5] = 255;
	if (colour == 3)
		put_chunk(made->file, &at, "PLTE", palette, sizeof(palette));
	if (transparency)
		put_chunk(made->file, &at, "tRNS", palette, 6);
	put_chunk(made->file, &at, "IDAT", rows, rows_size);
	if (fault == FAULT_CRC)
		made->file[at - 1] ^= 1U;
	put_chunk(made->file, &at, "IEND", NULL, 0);
	made->size = at;
	return 0;
}

/* Appends a chunk: its length, type, data and CRC. */
static void
put_chunk(
	unsigned char *out,
	size_t *at,
	const char *type,
	const unsigned char *data,
	size_t length)
{
	unsigned long crc;

	/* The length and type, the data, the CRC over the type and data. */
	put_u32(out + *at, (unsigned long)length);
	memcpy(out + *at + 4U, type, 4);
	if (length > 0)
		memcpy(out + *at + 8U, data, length);
	crc = crc32(0UL, out + *at + 4U, (uInt)(length + 4U));
	put_u32(out + *at + 8U + length, crc);
	*at += length + 12U;
}

/* Writes a big-endian 32-bit number. */
static void
put_u32(
	unsigned char *out,
	unsigned long value)
{
	/* The highest byte first. */
	out[0] = (unsigned char)(value >> 24);
	out[1] = (unsigned char)(value >> 16);
	out[2] = (unsigned char)(value >> 8);
	out[3] = (unsigned char)value;
}

/* The Paeth predictor of PNG. */
static unsigned char
paeth(
	unsigned char left,
	unsigned char above,
	unsigned char corner)
{
	int estimate;
	int to_left;
	int to_above;
	int to_corner;

	/* The neighbour nearest the estimate, left first, then above. */
	estimate = (int)left + (int)above - (int)corner;
	to_left = abs(estimate - (int)left);
	to_above = abs(estimate - (int)above);
	to_corner = abs(estimate - (int)corner);
	if (to_left <= to_above && to_left <= to_corner)
		return left;
	if (to_above <= to_corner)
		return above;
	return corner;
}

/* Finds the image item of a size. */
static const struct pdf_display_item *
find_image(
	const struct pdf_display_list *list,
	size_t width,
	size_t height)
{
	size_t i;

	/* Each image. */
	for (i = 0; i < list->count; i++) {
		if (list->items[i].type == PDF_ITEM_IMAGE && list->items[i].image_width == width && list->items[i].image_height == height)
			return &list->items[i];
	}

	/* None of that size. */
	return NULL;
}

/* Tells whether an image item's pixels are the samples given (Gray or RGB), opaque. */
static int
same_pixels(
	const struct pdf_display_item *item,
	const unsigned char *pixels,
	int components)
{
	size_t count;
	size_t at;
	int channel;
	unsigned char expected;

	/* Each pixel's channels. */
	count = item->image_width * item->image_height;
	for (at = 0; at < count; at++) {
		for (channel = 0; channel < 3; channel++) {
			expected = pixels[at * (size_t)components + (size_t)(channel % components)];
			if (item->pixels[at * 4U + (size_t)channel] != expected)
				return 0;
		}

		/* Opaque. */
		if (item->pixels[at * 4U + 3U] != 255U)
			return 0;
	}

	/* All the same. */
	return 1;
}

/* Counts the places a text is in bytes. */
static size_t
count_text(
	const unsigned char *data,
	size_t size,
	const char *text)
{
	size_t length;
	size_t count;
	size_t at;
	int differs;

	/* Each place. */
	length = strlen(text);
	count = 0;
	for (at = 0; at + length <= size; at++) {
		differs = memcmp(data + at, text, length);
		if (differs == 0)
			count++;
	}

	/* The places. */
	return count;
}

/* Reads a whole file; NULL when it cannot. */
static unsigned char *
read_file(
	const char *path,
	size_t *size)
{
	unsigned char *data;
	FILE *file;
	long length;
	size_t got;

	/* The file and its length. */
	file = fopen(path, "rb");
	if (file == NULL)
		return NULL;
	(void)fseek(file, 0L, SEEK_END);
	length = ftell(file);
	(void)fseek(file, 0L, SEEK_SET);
	if (length <= 0) {
		fclose(file);
		return NULL;
	}

	/* Its bytes. */
	data = malloc((size_t)length);
	got = 0;
	if (data != NULL)
		got = fread(data, 1, (size_t)length, file);
	fclose(file);
	if (data == NULL || got != (size_t)length) {
		free(data);
		return NULL;
	}

	/* Succeeded: the bytes. */
	*size = got;
	return data;
}

/* Counts and prints one check. */
static void
check(
	int ok,
	const char *what)
{
	/* A check that holds. */
	if (ok) {
		test_passed++;
		printf("ok   %s\n", what);
		return;
	}

	/* One that does not. */
	test_failed++;
	printf("FAIL %s\n", what);
}
