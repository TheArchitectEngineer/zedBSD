/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The libjpeg-compat test driver (ws074-p019), built for the host with
 * the library's sources and for zedBSD against libjpeg-compat.so:
 *
 *   host-jpeg [--space=NAME] [--stdio] [--no-fancy] IN.jpg OUT
 *
 * decodes IN.jpg as a program does (jpeg_std_error with its own
 * error_exit, jpeg_create_decompress, a memory or stdio source,
 * jpeg_read_header, jpeg_start_decompress, jpeg_read_scanlines,
 * jpeg_finish_decompress) and writes OUT: a PGM for gray, a PPM for RGB,
 * or for the other orders a "RAW WIDTH HEIGHT COMPONENTS" line and the
 * bytes.  NAME is gray, rgb, bgr, rgbx, bgrx, xbgr, xrgb, rgba, bgra,
 * abgr or argb (default: the library's default).  It prints the header's
 * facts and the warnings on standard output; an error prints "error:
 * MESSAGE" and exits with 2.
 */

#include <compat/jpeglib.h>

#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * The error manager with the place error_exit jumps back to.
 */
struct test_error {
	struct jpeg_error_mgr pub;
	jmp_buf back;
};

/*
 * A colour space the command line names.
 */
struct test_space {
	const char *name;
	J_COLOR_SPACE space;
};

/* The names --space= takes, ending with a NULL name.  The table is constant for the life of the program. */
static const struct test_space test_spaces[] = {
	{ "gray", JCS_GRAYSCALE },
	{ "rgb", JCS_RGB },
	{ "bgr", JCS_EXT_BGR },
	{ "rgbx", JCS_EXT_RGBX },
	{ "bgrx", JCS_EXT_BGRX },
	{ "xbgr", JCS_EXT_XBGR },
	{ "xrgb", JCS_EXT_XRGB },
	{ "rgba", JCS_EXT_RGBA },
	{ "bgra", JCS_EXT_BGRA },
	{ "abgr", JCS_EXT_ABGR },
	{ "argb", JCS_EXT_ARGB },
	{ NULL, JCS_UNKNOWN }
};

static void test_error_exit(j_common_ptr cinfo);
static int test_read_file(const char *path, unsigned char **bytes, size_t *size);
static int test_decode(const char *in, const char *out, J_COLOR_SPACE space, int stdio, int fancy);
static int test_write(const char *path, struct jpeg_decompress_struct *cinfo, const unsigned char *pixels);

/* Reads the command line and decodes the file. */
int
main(
	int argc,
	char **argv)
{
	J_COLOR_SPACE space;
	const char *in;
	const char *out;
	int index;
	int entry;
	int stdio;
	int fancy;
	int differs;
	int status;

	/* The options, then the two files. */
	space = JCS_UNKNOWN;
	stdio = 0;
	fancy = 1;
	in = NULL;
	out = NULL;
	for (index = 1; index < argc; index++) {
		differs = strncmp(argv[index], "--space=", 8);
		if (differs == 0) {
			for (entry = 0; test_spaces[entry].name != NULL; entry++) {
				differs = strcmp(argv[index] + 8, test_spaces[entry].name);
				if (differs == 0)
					space = test_spaces[entry].space;
			}

			/* A name that is not known leaves the default. */
			continue;
		}

		/* The source and the upsampling. */
		differs = strcmp(argv[index], "--stdio");
		if (differs == 0) {
			stdio = 1;
			continue;
		}

		/* Plain replication instead of libjpeg's triangle filter. */
		differs = strcmp(argv[index], "--no-fancy");
		if (differs == 0) {
			fancy = 0;
			continue;
		}

		/* The files. */
		if (in == NULL) {
			in = argv[index];
		} else {
			out = argv[index];
		}
	}

	/* Both files are needed. */
	if (in == NULL || out == NULL) {
		fprintf(stderr, "usage: host-jpeg [--space=NAME] [--stdio] [--no-fancy] IN.jpg OUT\n");
		return 2;
	}

	/* The decoding. */
	status = test_decode(in, out, space, stdio, fancy);
	return status;
}

/* The program's error exit: the message, then back to test_decode. */
static void
test_error_exit(
	j_common_ptr cinfo)
{
	struct test_error *error;
	char message[JMSG_LENGTH_MAX];

	/* The text on standard output, then the jump. */
	error = (struct test_error *)cinfo->err;
	cinfo->err->format_message(cinfo, message);
	printf("error: %s\n", message);
	longjmp(error->back, 1);
}

/* Reads a whole file. */
static int
test_read_file(
	const char *path,
	unsigned char **bytes,
	size_t *size)
{
	FILE *file;
	long length;
	size_t got;

	/* The file's size. */
	file = fopen(path, "rb");
	if (file == NULL)
		return 1;
	fseek(file, 0, SEEK_END);
	length = ftell(file);
	fseek(file, 0, SEEK_SET);
	if (length <= 0) {
		fclose(file);
		return 1;
	}

	/* Its bytes. */
	*bytes = malloc((size_t)length);
	if (*bytes == NULL) {
		fclose(file);
		return 1;
	}

	/* The bytes read (a short read gives a cut file, which the tests may want). */
	got = fread(*bytes, 1, (size_t)length, file);
	fclose(file);
	*size = got;
	return 0;
}

/* Decodes a file as a program would and writes the pixels; returns the exit status. */
static int
test_decode(
	const char *in,
	const char *out,
	J_COLOR_SPACE space,
	int stdio,
	int fancy)
{
	struct jpeg_decompress_struct cinfo;
	struct test_error error;
	unsigned char *volatile bytes;
	unsigned char *volatile pixels;
	FILE *volatile file;
	unsigned char *read_bytes;
	JSAMPROW row;
	size_t size;
	size_t stride;
	int jumped;
	int status;

	/* The error manager, whose exit comes back here. */
	bytes = NULL;
	pixels = NULL;
	file = NULL;
	cinfo.err = jpeg_std_error(&error.pub);
	error.pub.error_exit = test_error_exit;
	jumped = setjmp(error.back);
	if (jumped != 0) {
		jpeg_destroy_decompress(&cinfo);
		free(bytes);
		free(pixels);
		if (file != NULL)
			fclose(file);
		return 2;
	}

	/* The object and its source. */
	jpeg_create_decompress(&cinfo);
	if (stdio) {
		file = fopen(in, "rb");
		if (file == NULL) {
			printf("error: cannot open %s\n", in);
			jpeg_destroy_decompress(&cinfo);
			return 2;
		}

		/* The stream is read a buffer at a time. */
		jpeg_stdio_src(&cinfo, file);
	} else {
		status = test_read_file(in, &read_bytes, &size);
		if (status != 0) {
			printf("error: cannot read %s\n", in);
			jpeg_destroy_decompress(&cinfo);
			return 2;
		}

		/* The whole file in memory. */
		bytes = read_bytes;
		jpeg_mem_src(&cinfo, bytes, (unsigned long)size);
	}

	/* The header, and the output asked for. */
	jpeg_read_header(&cinfo, TRUE);
	printf("header: %ux%u components=%d space=%d jfif=%d adobe=%d transform=%d restart=%u\n", cinfo.image_width, cinfo.image_height, cinfo.num_components, (int)cinfo.jpeg_color_space, cinfo.saw_JFIF_marker, cinfo.saw_Adobe_marker, cinfo.Adobe_transform, cinfo.restart_interval);
	if (space != JCS_UNKNOWN)
		cinfo.out_color_space = space;
	cinfo.do_fancy_upsampling = (boolean)fancy;

	/* The rows, one call at a time as most programs read them. */
	jpeg_start_decompress(&cinfo);
	stride = (size_t)cinfo.output_width * (size_t)cinfo.output_components;
	pixels = malloc(stride * cinfo.output_height);
	if (pixels == NULL) {
		printf("error: out of memory\n");
		jpeg_destroy_decompress(&cinfo);
		free(bytes);
		return 2;
	}
	while (cinfo.output_scanline < cinfo.output_height) {
		row = pixels + stride * cinfo.output_scanline;
		jpeg_read_scanlines(&cinfo, &row, 1);
	}

	/* Every row is read. */
	jpeg_finish_decompress(&cinfo);

	/* The picture, then everything freed. */
	printf("output: %ux%u components=%d warnings=%ld\n", cinfo.output_width, cinfo.output_height, cinfo.output_components, error.pub.num_warnings);
	status = test_write(out, &cinfo, pixels);
	jpeg_destroy_decompress(&cinfo);
	free(bytes);
	free(pixels);
	if (file != NULL)
		fclose(file);
	if (status != 0)
		return 2;

	/* Succeeded: the picture is written. */
	return 0;
}

/* Writes the pixels: PGM, PPM, or the RAW form for four components. */
static int
test_write(
	const char *path,
	struct jpeg_decompress_struct *cinfo,
	const unsigned char *pixels)
{
	FILE *file;
	size_t size;
	size_t written;

	/* The header of the form. */
	file = fopen(path, "wb");
	if (file == NULL)
		return 1;
	if (cinfo->output_components == 1) {
		fprintf(file, "P5\n%u %u\n255\n", cinfo->output_width, cinfo->output_height);
	} else if (cinfo->output_components == 3 && cinfo->out_color_space == JCS_RGB) {
		fprintf(file, "P6\n%u %u\n255\n", cinfo->output_width, cinfo->output_height);
	} else {
		fprintf(file, "RAW %u %u %d\n", cinfo->output_width, cinfo->output_height, cinfo->output_components);
	}

	/* The pixels. */
	size = (size_t)cinfo->output_width * (size_t)cinfo->output_components * cinfo->output_height;
	written = fwrite(pixels, 1, size, file);
	fclose(file);
	if (written != size)
		return 1;

	/* Succeeded: the file is written. */
	return 0;
}
