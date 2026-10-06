/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws175-p005: the host test of the TrueType subset (subset.c):
 *
 *   host-truetype-subset FONT OUT GLYPH...
 *
 * Writes the subset of FONT that draws the glyphs named (numbers) to OUT,
 * which run-host-edit-scan.sh reads with fontTools; prints the font's
 * embedding permission.  Exits 0 when the subset was made.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pdf.h>

#include "internal.h"

static unsigned char *read_file(const char *path, size_t *size);

int
main(
	int argc,
	char **argv)
{
	unsigned char used[65536];
	unsigned char *font;
	unsigned char *subset;
	size_t size;
	size_t subset_size;
	long glyph;
	int allowed;
	int whole;
	int at;
	int error;
	FILE *file;

	/* The font and the glyphs. */
	if (argc < 4)
		return 2;
	font = read_file(argv[1], &size);
	if (font == NULL)
		return 1;
	memset(used, 0, sizeof(used));
	for (at = 3; at < argc; at++) {
		glyph = strtol(argv[at], NULL, 10);
		if (glyph >= 0 && glyph < 65536L)
			used[glyph] = 1U;
	}

	/* The permission, then the subset. */
	error = pdf_truetype_permission(font, size, &allowed, &whole);
	printf("permission error=%d allowed=%d whole=%d\n", error, allowed, whole);
	error = pdf_truetype_subset(font, size, used, sizeof(used), &subset, &subset_size);
	printf("subset error=%d bytes=%lu of %lu\n", error, (unsigned long)subset_size, (unsigned long)size);
	free(font);
	if (error != 0)
		return 1;

	/* Written. */
	file = fopen(argv[2], "wb");
	if (file == NULL) {
		free(subset);
		return 1;
	}

	/* The bytes. */
	(void)fwrite(subset, 1, subset_size, file);
	fclose(file);
	free(subset);
	return 0;
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
