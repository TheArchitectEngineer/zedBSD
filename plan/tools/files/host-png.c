/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws071-p010: drives libz-compat and libpng-compat on the host
 * (host-png.sh compares the results with Python's zlib and PIL).
 *
 *   files-png inflate IN OUT          decompresses a zlib stream (uncompress; in pieces with "pieces")
 *   files-png pieces IN OUT           the same through inflate, a few bytes of input at a time
 *   files-png png IN OUT FORMAT       reads a PNG into OUT as raw pixels of a format
 *                                     (gray, ga, rgb, rgba, bgra, argb), with its width and height
 *                                     printed; a failure prints its message
 */

#include <compat/png/png.h>
#include <compat/zlib/zlib.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned char *host_read(const char *path, size_t *size);
static int host_write(const char *path, const unsigned char *data, size_t size);
static int host_inflate(const char *in, const char *out, int pieces);
static int host_png(const char *in, const char *out, const char *format);

/*
 * Runs one check.
 */
int
main(
	int argc,
	char **argv)
{
	int command;
	int error;

	/* The command and its files. */
	if (argc < 4) {
		fprintf(stderr, "usage: files-png inflate|pieces IN OUT | png IN OUT FORMAT\n");
		return 2;
	}

	/* Decompression, at once or in pieces. */
	command = strcmp(argv[1], "inflate");
	if (command == 0) {
		error = host_inflate(argv[2], argv[3], 0);
		return error;
	}

	/* The same in pieces. */
	command = strcmp(argv[1], "pieces");
	if (command == 0) {
		error = host_inflate(argv[2], argv[3], 1);
		return error;
	}

	/* A PNG. */
	command = strcmp(argv[1], "png");
	if (command == 0 && argc == 5) {
		error = host_png(argv[2], argv[3], argv[4]);
		return error;
	}

	/* Anything else. */
	fprintf(stderr, "files-png: unknown command %s\n", argv[1]);
	return 2;
}

/* Reads a whole file. */
static unsigned char *
host_read(
	const char *path,
	size_t *size)
{
	unsigned char *data;
	FILE *file;
	long length;

	/* The file and its size. */
	file = fopen(path, "rb");
	if (file == NULL)
		return NULL;
	fseek(file, 0L, SEEK_END);
	length = ftell(file);
	rewind(file);

	/* Its bytes. */
	data = malloc((size_t)length + 1U);
	if (data == NULL) {
		fclose(file);
		return NULL;
	}

	/* Read. */
	*size = fread(data, 1U, (size_t)length, file);
	fclose(file);
	return data;
}

/* Writes a whole file. */
static int
host_write(
	const char *path,
	const unsigned char *data,
	size_t size)
{
	FILE *file;
	size_t written;

	/* The file, its bytes. */
	file = fopen(path, "wb");
	if (file == NULL)
		return 1;
	written = fwrite(data, 1U, size, file);
	fclose(file);
	if (written != size)
		return 1;
	return 0;
}

/* Decompresses a zlib stream, at once or through inflate a few bytes at a time. */
static int
host_inflate(
	const char *in,
	const char *out,
	int pieces)
{
	unsigned char *data;
	unsigned char *result;
	unsigned char chunk[97];
	z_stream stream;
	size_t size;
	size_t given;
	size_t length;
	uLongf result_length;
	int status;
	int flush;

	/* The stream, and room for a large result. */
	data = host_read(in, &size);
	if (data == NULL)
		return 1;
	result_length = 64UL * 1024UL * 1024UL;
	result = malloc(result_length);
	if (result == NULL)
		return 1;

	/* At once. */
	if (!pieces) {
		status = uncompress(result, &result_length, data, (uLong)size);
		printf("uncompress status=%d length=%lu\n", status, (unsigned long)result_length);
		if (status != Z_OK)
			return 1;
		return host_write(out, result, result_length);
	}

	/* In pieces: 7 bytes of input at a time, 97 bytes of output room at a time. */
	memset(&stream, 0, sizeof(stream));
	status = inflateInit(&stream);
	if (status != Z_OK)
		return 1;
	given = 0;
	length = 0;
	do {
		stream.next_in = data + given;
		stream.avail_in = 0;
		if (given < size) {
			stream.avail_in = 7U;
			if (size - given < 7U)
				stream.avail_in = (uInt)(size - given);
			given += stream.avail_in;
		}

		/* The room, and the flush: the end once all is given. */
		stream.next_out = chunk;
		stream.avail_out = sizeof(chunk);
		flush = Z_NO_FLUSH;
		if (given == size)
			flush = Z_FINISH;
		status = inflate(&stream, flush);
		memcpy(result + length, chunk, sizeof(chunk) - stream.avail_out);
		length += sizeof(chunk) - stream.avail_out;
	} while (status == Z_OK || (status == Z_BUF_ERROR && given < size));
	(void)inflateEnd(&stream);
	printf("inflate status=%d length=%lu\n", status, (unsigned long)length);
	if (status != Z_STREAM_END)
		return 1;
	return host_write(out, result, length);
}

/* Reads a PNG into a format and writes the pixels. */
static int
host_png(
	const char *in,
	const char *out,
	const char *format)
{
	png_image image;
	unsigned char *pixels;
	static const struct {
		const char *name;
		png_uint_32 format;
	} formats[] = {
		{ "gray", PNG_FORMAT_GRAY },
		{ "ga", PNG_FORMAT_GA },
		{ "rgb", PNG_FORMAT_RGB },
		{ "bgra", PNG_FORMAT_BGRA },
		{ "argb", PNG_FORMAT_ARGB },
	};
	png_uint_32 wanted;
	size_t i;
	int same;
	int ok;

	/* The format by its name. */
	wanted = PNG_FORMAT_RGBA;
	for (i = 0; i < sizeof(formats) / sizeof(formats[0]); i++) {
		same = strcmp(format, formats[i].name);
		if (same == 0)
			wanted = formats[i].format;
	}

	/* The header. */
	memset(&image, 0, sizeof(image));
	image.version = PNG_IMAGE_VERSION;
	ok = png_image_begin_read_from_file(&image, in);
	if (!ok) {
		printf("png failed begin message=%s\n", image.message);
		return 1;
	}

	/* Its size. */
	printf("png width=%u height=%u format=%u\n", (unsigned)image.width, (unsigned)image.height, (unsigned)image.format);

	/* The pixels. */
	image.format = wanted;
	pixels = malloc(PNG_IMAGE_SIZE(image));
	if (pixels == NULL)
		return 1;
	ok = png_image_finish_read(&image, NULL, pixels, 0, NULL);
	if (!ok) {
		printf("png failed finish message=%s\n", image.message);
		return 1;
	}

	/* The pixels, written. */
	return host_write(out, pixels, PNG_IMAGE_SIZE(image));
}
