/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws068-p039: decodes reference.py's ETC2 and EAC blocks with libGLESv2's
 * decoder (etc2.c) on the host and compares every texel with the
 * reference's.
 *
 *   etc2-test DIRECTORY
 */

#include "../../../../userland/desktop/libglesv2/gles.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The blocks of each format reference.py writes, and the image they make: 4 texels wide, a block a row of blocks. */
#define TEST_BLOCKS		64

static unsigned char *test_read(const char *path, size_t *size);
static int test_format(const char *directory, const char *name, GLenum internal);

/*
 * Checks each format reference.py wrote.
 */
int
main(
	int argc,
	char **argv)
{
	int failures;

	/* The directory. */
	if (argc != 2) {
		fprintf(stderr, "usage: etc2-test DIRECTORY\n");
		return 2;
	}

	/* The seven files' formats (the sRGB ones decode as the others). */
	failures = 0;
	failures += test_format(argv[1], "rgb8", GL_COMPRESSED_RGB8_ETC2);
	failures += test_format(argv[1], "punch", GL_COMPRESSED_RGB8_PUNCHTHROUGH_ALPHA1_ETC2);
	failures += test_format(argv[1], "rgba8", GL_COMPRESSED_RGBA8_ETC2_EAC);
	failures += test_format(argv[1], "r11", GL_COMPRESSED_R11_EAC);
	failures += test_format(argv[1], "sr11", GL_COMPRESSED_SIGNED_R11_EAC);
	failures += test_format(argv[1], "rg11", GL_COMPRESSED_RG11_EAC);
	failures += test_format(argv[1], "srg11", GL_COMPRESSED_SIGNED_RG11_EAC);
	if (failures != 0) {
		printf("etc2-host: FAIL (%d texels differ)\n", failures);
		return 1;
	}

	/* Every texel the reference's. */
	printf("etc2-host: PASS\n");
	return 0;
}

/* Reads a whole file; NULL when it cannot be read. */
static unsigned char *
test_read(
	const char *path,
	size_t *size)
{
	FILE *file;
	unsigned char *bytes;
	long length;

	/* The file and its length. */
	file = fopen(path, "rb");
	if (file == NULL)
		return NULL;
	(void)fseek(file, 0L, SEEK_END);
	length = ftell(file);
	(void)fseek(file, 0L, SEEK_SET);

	/* Its bytes. */
	bytes = malloc((size_t)length + 1U);
	if (bytes == NULL) {
		fclose(file);
		return NULL;
	}
	*size = fread(bytes, 1U, (size_t)length, file);
	fclose(file);
	bytes[*size] = '\0';

	/* Succeeded: the bytes. */
	return bytes;
}

/* Decodes one format's blocks and compares the texels with the expected file; returns how many differ. */
static int
test_format(
	const char *directory,
	const char *name,
	GLenum internal)
{
	const struct gles_format *format;
	unsigned char *blocks;
	unsigned char *texels;
	char path[1024];
	char line[128];
	FILE *expected;
	size_t size;
	size_t want;
	uint16_t halves[2];
	int values[4];
	int differ;
	int texel;
	int count;
	int channel;
	int got;

	/* The format and its blocks, of the size the format's image has. */
	format = gles_compressed_format(internal);
	want = gles_compressed_size(internal, 4, 4 * TEST_BLOCKS, 1);
	(void)snprintf(path, sizeof(path), "%s/%s.blocks", directory, name);
	blocks = test_read(path, &size);
	if (format == NULL || blocks == NULL || size != want) {
		printf("%s: FAIL (no format, or %zu bytes of blocks where %zu are wanted)\n", name, blocks == NULL ? 0U : size, want);
		free(blocks);
		return 1;
	}

	/* The image decoded. */
	texels = calloc((size_t)4 * 4 * TEST_BLOCKS, format->bytes);
	gles_compressed_decode(format, blocks, 4, 4 * TEST_BLOCKS, texels);

	/* Each texel against its line. */
	(void)snprintf(path, sizeof(path), "%s/%s.expected", directory, name);
	expected = fopen(path, "r");
	differ = 0;
	for (texel = 0; expected != NULL && texel < 16 * TEST_BLOCKS; texel++) {
		if (fgets(line, sizeof(line), expected) == NULL)
			break;
		count = sscanf(line, "%d %d %d %d", &values[0], &values[1], &values[2], &values[3]);

		/* An RGBA8 texel: its four bytes. */
		if (format->bytes == 4U && format->components == 4U) {
			for (channel = 0; channel < 4 && channel < count; channel++) {
				got = texels[texel * 4 + channel];
				if (got != values[channel]) {
					if (differ < 8)
						printf("%s: texel %d (block %d) channel %d got %d expected %d\n", name, texel, texel / 16, channel, got, values[channel]);
					differ++;
				}
			}
			continue;
		}

		/* An 11-bit texel: its half floats, as the reference's values make them. */
		memcpy(halves, texels + (size_t)texel * format->bytes, format->bytes);
		for (channel = 0; channel < (int)format->components && channel < count; channel++) {
			want = gles_half_from_float((float)values[channel] / 2047.0f);
			if (format->internal == GL_COMPRESSED_SIGNED_R11_EAC || format->internal == GL_COMPRESSED_SIGNED_RG11_EAC)
				want = gles_half_from_float((float)values[channel] / 1023.0f);
			if (halves[channel] != want) {
				if (differ < 8)
					printf("%s: texel %d (block %d) channel %d got 0x%04x expected 0x%04zx (%d)\n", name, texel, texel / 16, channel,
					       halves[channel], want, values[channel]);
				differ++;
			}
		}
	}

	/* The file read to its end. */
	if (expected == NULL || texel != 16 * TEST_BLOCKS) {
		printf("%s: FAIL (the expected file is short)\n", name);
		differ++;
	}
	if (expected != NULL)
		fclose(expected);
	free(blocks);
	free(texels);

	/* The count of texels that differ. */
	printf("%s: %s\n", name, differ == 0 ? "ok" : "DIFFERS");
	return differ;
}
