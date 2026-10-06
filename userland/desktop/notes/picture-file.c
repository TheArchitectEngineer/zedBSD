/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The image files Notes puts on a page (ws175-p008, plan/ws175/phase001/
 * design.md section 5.1): a JPEG or a PNG read into an image of the
 * notebook (edit.c), kept as compressed as it came.
 *
 * A JPEG of one or three components keeps its bytes; its size and
 * components come from its frame header and its EXIF orientation from its
 * APP1 block (picture.c, the orientation turns the placement, not the
 * pixels).  A CMYK (or YCCK) JPEG is refused [M15].  A PNG of 8-bit Gray
 * or RGB without alpha, palette, transparency or interlace keeps its bytes
 * (libpdf takes its rows as they are); any other PNG is decoded by
 * libpng-compat into RGBA, which the image keeps compressed.  An image
 * with a side past 16384 or more than 64 M pixels is refused.
 */

#include "notes.h"

#include "../picture/picture.h"

#include <compat/png/png.h>
#include <compat/zlib/zlib.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The largest file read, the largest side and the most pixels (libpdf's). */
#define PICTURE_FILE_MAX	((size_t)256 * 1024 * 1024)
#define PICTURE_SIDE_MAX	16384U
#define PICTURE_PIXELS_MAX	((size_t)64 * 1024 * 1024)

static int picture_read(const char *path, unsigned char **data, size_t *size);
static int picture_jpeg(struct notes_document *document, const unsigned char *data, size_t size, struct notes_image **image);
static int picture_png(struct notes_document *document, const unsigned char *data, size_t size, struct notes_image **image);
static uint32_t picture_u32(const unsigned char *bytes);

/*
 * Reads an image file into a new image of the document (held once by the
 * caller; its number the document's next).  Returns 0, ENOTSUP for a file
 * that is neither a JPEG nor a PNG or a CMYK JPEG, E2BIG for one too
 * large, EINVAL for a damaged one, ENOMEM, or the errno value of reading
 * it.
 */
int
notes_picture_load(
	struct notes_document *document,
	const char *path,
	struct notes_image **image)
{
	static const unsigned char png_signature[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
	unsigned char *data;
	size_t size;
	int differs;
	int error;

	/* The file's bytes. */
	data = NULL;
	size = 0;
	error = picture_read(path, &data, &size);
	if (error != 0)
		return error;

	/* A JPEG by its start of image, a PNG by its signature. */
	error = ENOTSUP;
	if (size >= 4U && data[0] == 0xffU && data[1] == 0xd8U)
		error = picture_jpeg(document, data, size, image);
	differs = 1;
	if (size >= sizeof(png_signature))
		differs = memcmp(data, png_signature, sizeof(png_signature));
	if (differs == 0)
		error = picture_png(document, data, size, image);

	/* The bytes go (the image has its own). */
	free(data);
	return error;
}

/*
 * Reads a JPEG: its frame header's size and components, its EXIF
 * orientation, and its bytes as they are.
 */
static int
picture_jpeg(
	struct notes_document *document,
	const unsigned char *data,
	size_t size,
	struct notes_image **image)
{
	struct notes_image *made;
	size_t position;
	size_t length;
	size_t width;
	size_t height;
	unsigned marker;
	int components;
	int orientation;
	int matched;

	/* The markers after the start of image, up to the frame header. */
	width = 0;
	height = 0;
	components = 0;
	orientation = 1;
	position = 2;
	while (position + 4U <= size && width == 0) {
		/* A marker and its segment's length (fill bytes skipped). */
		if (data[position] != 0xffU)
			return EINVAL;
		marker = data[position + 1U];
		if (marker == 0xffU) {
			position++;
			continue;
		}

		/* The segment's length, within the file. */
		length = ((size_t)data[position + 2U] << 8) | data[position + 3U];
		if (length < 2U || length > size - position - 2U)
			return EINVAL;

		/* The EXIF block's orientation. */
		if (marker == 0xe1U && length >= 8U) {
			matched = memcmp(data + position + 4U, "Exif", 4U);
			if (matched == 0)
				orientation = kl_picture_exif_orientation(data + position + 4U, length - 2U);
		}

		/* A frame header (SOF0 to SOF15 but DHT, JPG and DAC): the size and the components. */
		if (marker >= 0xc0U && marker <= 0xcfU && marker != 0xc4U && marker != 0xc8U && marker != 0xccU) {
			if (length < 8U)
				return EINVAL;
			height = ((size_t)data[position + 5U] << 8) | data[position + 6U];
			width = ((size_t)data[position + 7U] << 8) | data[position + 8U];
			components = data[position + 9U];
			if (width == 0 || height == 0)
				return EINVAL;
		}

		/* The next marker; the scan's data ends the header. */
		if (marker == 0xdaU)
			break;
		position += 2U + length;
	}

	/* A frame of one or three components (CMYK and YCCK are refused) within the sizes. */
	if (width == 0)
		return EINVAL;
	if (components != 1 && components != 3)
		return ENOTSUP;
	if (width > PICTURE_SIDE_MAX || height > PICTURE_SIDE_MAX || width * height > PICTURE_PIXELS_MAX)
		return E2BIG;

	/* The image of the bytes as they are. */
	made = notes_image_create(document, NOTES_IMAGE_JPEG, data, size, width, height, components, orientation);
	if (made == NULL)
		return ENOMEM;
	*image = made;
	return 0;
}

/*
 * Reads a PNG: one whose rows a PDF takes as they are keeps its bytes; any
 * other is decoded into RGBA, kept compressed.
 */
static int
picture_png(
	struct notes_document *document,
	const unsigned char *data,
	size_t size,
	struct notes_image **image)
{
	struct notes_image *made;
	png_image png;
	unsigned char *pixels;
	unsigned char *packed;
	uLongf packed_size;
	size_t position;
	size_t width;
	size_t height;
	uint32_t length;
	int as_they_are;
	int transparency;
	int components;
	int differs;
	int status;
	int ok;

	/* The header: the size, the depth, the colour type and the interlace. */
	if (size < 33U)
		return EINVAL;
	differs = memcmp(data + 12U, "IHDR", 4U);
	if (differs != 0)
		return EINVAL;
	width = picture_u32(data + 16U);
	height = picture_u32(data + 20U);
	if (width == 0 || height == 0)
		return EINVAL;
	if (width > PICTURE_SIDE_MAX || height > PICTURE_SIDE_MAX || width * height > PICTURE_PIXELS_MAX)
		return E2BIG;

	/* A tRNS chunk anywhere asks for a mask. */
	transparency = 0;
	position = 8;
	while (position + 12U <= size) {
		length = picture_u32(data + position);
		differs = memcmp(data + position + 4U, "tRNS", 4U);
		if (differs == 0)
			transparency = 1;
		if (length > size - position - 12U)
			break;
		position += 12U + length;
	}

	/* 8-bit Gray or RGB without the rest: the bytes as they are. */
	as_they_are = data[24] == 8U && (data[25] == 0U || data[25] == 2U) && data[28] == 0U && !transparency;
	if (as_they_are) {
		components = 1;
		if (data[25] == 2U)
			components = 3;
		made = notes_image_create(document, NOTES_IMAGE_PNG, data, size, width, height, components, 0);
		if (made == NULL)
			return ENOMEM;
		*image = made;
		return 0;
	}

	/* Any other: decoded into RGBA. */
	memset(&png, 0, sizeof(png));
	png.version = PNG_IMAGE_VERSION;
	ok = png_image_begin_read_from_memory(&png, data, size);
	if (!ok)
		return EINVAL;
	png.format = PNG_FORMAT_RGBA;
	pixels = malloc(PNG_IMAGE_SIZE(png));
	if (pixels == NULL) {
		png_image_free(&png);
		return ENOMEM;
	}

	/* Decoded. */
	ok = png_image_finish_read(&png, NULL, pixels, 0, NULL);
	if (!ok || png.width != width || png.height != height) {
		free(pixels);
		png_image_free(&png);
		return EINVAL;
	}

	/* The pixels compressed. */
	packed_size = compressBound((uLong)(width * height * 4U));
	packed = malloc((size_t)packed_size);
	status = Z_MEM_ERROR;
	if (packed != NULL)
		status = compress2(packed, &packed_size, pixels, (uLong)(width * height * 4U), Z_DEFAULT_COMPRESSION);
	free(pixels);
	if (status != Z_OK) {
		free(packed);
		return ENOMEM;
	}

	/* The image of the compressed pixels. */
	made = notes_image_create(document, NOTES_IMAGE_RGBA, packed, (size_t)packed_size, width, height, 4, 0);
	free(packed);
	if (made == NULL)
		return ENOMEM;
	*image = made;
	return 0;
}

/* Reads a whole file of at most PICTURE_FILE_MAX bytes. */
static int
picture_read(
	const char *path,
	unsigned char **data,
	size_t *size)
{
	unsigned char *bytes;
	FILE *file;
	long length;
	size_t got;

	/* The file and its length. */
	file = fopen(path, "rb");
	if (file == NULL)
		return errno;
	(void)fseek(file, 0L, SEEK_END);
	length = ftell(file);
	(void)fseek(file, 0L, SEEK_SET);
	if (length <= 0 || (unsigned long)length > PICTURE_FILE_MAX) {
		fclose(file);
		return E2BIG;
	}

	/* Its bytes. */
	bytes = malloc((size_t)length);
	got = 0;
	if (bytes != NULL)
		got = fread(bytes, 1, (size_t)length, file);
	fclose(file);
	if (bytes == NULL)
		return ENOMEM;
	if (got != (size_t)length) {
		free(bytes);
		return EIO;
	}

	/* Succeeded: the caller frees the bytes. */
	*data = bytes;
	*size = got;
	return 0;
}

/* Reads a big-endian 32-bit number. */
static uint32_t
picture_u32(
	const unsigned char *bytes)
{
	/* The first byte the highest. */
	return ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) | ((uint32_t)bytes[2] << 8) | (uint32_t)bytes[3];
}
