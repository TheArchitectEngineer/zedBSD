/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host dump of libtruetype's glyph drawing (ws079-p009): every glyph of
 * a font at some sizes, drawn with truetype_render_glyph(), as one line of
 * its outcome, its box and advance, and a hash of its coverage bitmap.
 * truetype-render-compare.sh compares the dumps of two builds of the
 * library.
 *
 *   truetype-render-dump FONT SIZE...
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <truetype.h>

/* The largest bitmap one glyph may take in the dump. */
#define DUMP_BITMAP_MAX ((size_t)4096 * 4096)

/* Reads a whole file into a new buffer; NULL when it cannot be read. */
static unsigned char *
read_font(
	const char *path,
	size_t *size)
{
	unsigned char *data;
	FILE *file;
	long length;

	file = fopen(path, "rb");
	if (file == NULL)
		return NULL;
	fseek(file, 0, SEEK_END);
	length = ftell(file);
	fseek(file, 0, SEEK_SET);
	data = malloc((size_t)length);
	if (data == NULL) {
		fclose(file);
		return NULL;
	}
	*size = fread(data, 1, (size_t)length, file);
	fclose(file);
	return data;
}

/* Dumps every glyph of a font at the sizes named on the command line. */
int
main(
	int argc,
	char **argv)
{
	struct truetype_face *face;
	struct truetype_glyph metrics;
	unsigned char *data;
	unsigned char *bitmap;
	unsigned long hash;
	size_t size;
	size_t index;
	unsigned glyph;
	unsigned empty;
	int argument;
	int error;

	if (argc < 3) {
		fprintf(stderr, "usage: truetype-render-dump FONT SIZE...\n");
		return 2;
	}
	data = read_font(argv[1], &size);
	bitmap = malloc(DUMP_BITMAP_MAX);
	if (data == NULL || bitmap == NULL) {
		fprintf(stderr, "truetype-render-dump: cannot read %s\n", argv[1]);
		return 1;
	}
	error = truetype_open(data, size, 0, &face);
	if (error != 0) {
		fprintf(stderr, "truetype-render-dump: open error %d\n", error);
		return 1;
	}
	for (argument = 2; argument < argc; argument++) {
		error = truetype_set_pixel_size(face, (unsigned)atoi(argv[argument]));
		if (error != 0) {
			printf("size %s error %d\n", argv[argument], error);
			continue;
		}
		empty = 0;
		for (glyph = 0; glyph < 65536U; glyph++) {
			memset(&metrics, 0, sizeof(metrics));
			error = truetype_render_glyph(face, glyph, &metrics, bitmap, DUMP_BITMAP_MAX / 4096U, DUMP_BITMAP_MAX);
			if (error == EINVAL && metrics.width == 0 && metrics.height == 0) {
				/* Past the font's glyphs every glyph is refused; stop after a run of them. */
				empty++;
				if (empty > 64)
					break;
			} else {
				empty = 0;
			}
			hash = 2166136261UL;
			if (error == 0) {
				for (index = 0; index < (size_t)metrics.height * (DUMP_BITMAP_MAX / 4096U); index++) {
					hash ^= bitmap[index];
					hash = (hash * 16777619UL) & 0xffffffffUL;
				}
			}
			printf("%s %u error=%d box=%ux%u+%d+%d advance=%d hash=%08lx\n", argv[argument], glyph, error, metrics.width,
			    metrics.height, metrics.left, metrics.top, metrics.advance, hash);
		}
	}
	truetype_close(face);
	free(bitmap);
	free(data);
	return 0;
}
