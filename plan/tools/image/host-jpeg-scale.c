/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The test driver of libjpeg-compat's scaled decoding (ws157-p017),
 * built for the host with the library's sources:
 *
 *   host-jpeg-scale DENOM IN.jpg OUT
 *
 * decodes IN.jpg with scale_num 1 and scale_denom DENOM (1, 2, 4 or 8;
 * any other value is passed as it is) to RGB, or gray for a gray file,
 * and writes OUT as a PPM or a PGM.  It prints "size WIDTH HEIGHT
 * SCALED" (SCALED is min_DCT_scaled_size) on standard output; an error
 * prints "error: MESSAGE" and exits with 2.
 */

#include <compat/jpeglib.h>

#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * The error manager with the place error_exit jumps back to.
 */
struct scale_error {
	struct jpeg_error_mgr pub;
	jmp_buf back;
};

static void scale_exit(j_common_ptr cinfo);
static int scale_decode(unsigned denom, FILE *in, FILE *out);

/*
 * Decodes the file named on the command line at the scale asked.
 */
int
main(
	int argc,
	char **argv)
{
	FILE *in;
	FILE *out;
	unsigned long denom;
	int status;

	/* The arguments. */
	if (argc != 4) {
		fprintf(stderr, "usage: host-jpeg-scale DENOM IN.jpg OUT\n");
		return 64;
	}

	/* The denominator of the scale. */
	denom = strtoul(argv[1], NULL, 10);

	/* The input. */
	in = fopen(argv[2], "rb");
	if (in == NULL) {
		perror(argv[2]);
		return 1;
	}

	/* The output. */
	out = fopen(argv[3], "wb");
	if (out == NULL) {
		perror(argv[3]);
		fclose(in);
		return 1;
	}

	/* The decoding. */
	status = scale_decode((unsigned)denom, in, out);
	fclose(in);
	fclose(out);
	if (status != 0)
		return status;

	/* Succeeded: the scaled picture is written. */
	return 0;
}

/* Jumps back to scale_decode with the message printed. */
static void
scale_exit(
	j_common_ptr cinfo)
{
	struct scale_error *failure;
	char message[JMSG_LENGTH_MAX];

	/* The message, then back. */
	failure = (struct scale_error *)(void *)cinfo->err;
	cinfo->err->format_message(cinfo, message);
	printf("error: %s\n", message);
	longjmp(failure->back, 1);
}

/* Decodes one file at 1/denom into a PPM or PGM; 0, or 2 on an error. */
static int
scale_decode(
	unsigned denom,
	FILE *in,
	FILE *out)
{
	struct jpeg_decompress_struct info;
	struct scale_error failure;
	unsigned char *volatile row;
	JSAMPROW rows[1];
	size_t bytes;

	/* A decompressor whose errors come back here. */
	memset(&info, 0, sizeof(info));
	info.err = jpeg_std_error(&failure.pub);
	failure.pub.error_exit = scale_exit;
	row = NULL;

	/*
	 * An error comes back here (setjmp must stand in the condition
	 * itself); row is volatile so that it survives the jump.
	 */
	if (setjmp(failure.back) != 0) {
		jpeg_destroy_decompress(&info);
		free(row);
		return 2;
	}

	/* The header, then the scale and RGB (gray stays gray). */
	jpeg_create_decompress(&info);
	jpeg_stdio_src(&info, in);
	(void)jpeg_read_header(&info, TRUE);
	info.scale_num = 1;
	info.scale_denom = denom;
	if (info.out_color_space != JCS_GRAYSCALE)
		info.out_color_space = JCS_RGB;
	jpeg_calc_output_dimensions(&info);
	printf("size %u %u %d\n", (unsigned)info.output_width, (unsigned)info.output_height, info.min_DCT_scaled_size);

	/* The decoding, and the output's header. */
	(void)jpeg_start_decompress(&info);
	if (info.output_components == 1) {
		fprintf(out, "P5\n%u %u\n255\n", (unsigned)info.output_width, (unsigned)info.output_height);
	} else {
		fprintf(out, "P6\n%u %u\n255\n", (unsigned)info.output_width, (unsigned)info.output_height);
	}

	/* Room for a row. */
	bytes = (size_t)info.output_width * (size_t)info.output_components;
	row = malloc(bytes);
	if (row == NULL) {
		jpeg_destroy_decompress(&info);
		return 2;
	}

	/* Each row. */
	while (info.output_scanline < info.output_height) {
		rows[0] = row;
		(void)jpeg_read_scanlines(&info, rows, 1U);
		fwrite(row, 1, bytes, out);
	}

	/* The end. */
	(void)jpeg_finish_decompress(&info);
	jpeg_destroy_decompress(&info);
	free(row);

	/* Succeeded: the file is decoded. */
	return 0;
}
