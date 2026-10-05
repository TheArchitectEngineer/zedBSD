/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws099-p034: writes each layer of the Kei mark (userland/desktop/artwork/
 * mark.c, compiled unchanged) at one size as a PGM file (DIR/mark-N.pgm),
 * for the system bar's mock (p034-bar-mock.py), which colours them as the
 * bar does (glass.c's bar colours).
 *
 *   p034-mark-dump PIXELS DIR
 */

#include "userland/desktop/artwork/mark.h"

#include <stdio.h>
#include <stdlib.h>

/* The largest size drawn, in pixels. */
#define MARK_MOST	512U

/* Draws every layer and writes each one. */
int
main(
	int argc,
	char **argv)
{
	static uint8_t coverage[MARK_MOST * MARK_MOST];
	char path[1024];
	unsigned pixels;
	unsigned layer;
	FILE *file;

	/* Both arguments are given. */
	if (argc != 3) {
		fprintf(stderr, "usage: p034-mark-dump PIXELS DIR\n");
		return 2;
	}

	/* The size, no larger than the buffer. */
	pixels = (unsigned)strtoul(argv[1], NULL, 10);
	if (pixels == 0U || pixels > MARK_MOST) {
		fprintf(stderr, "p034-mark-dump: PIXELS must be 1 to %u\n", MARK_MOST);
		return 2;
	}

	/* Each layer, drawn by the mark's own rasterizer, into its file. */
	for (layer = 0; layer < KEILAND_MARK_LAYERS; layer++) {
		kl_mark_raster(layer, pixels, coverage, pixels);
		(void)snprintf(path, sizeof(path), "%s/mark-%u.pgm", argv[2], layer);
		file = fopen(path, "wb");
		if (file == NULL) {
			fprintf(stderr, "p034-mark-dump: cannot write %s\n", path);
			return 1;
		}

		/* The header and the coverage, a byte a pixel. */
		fprintf(file, "P5\n%u %u\n255\n", pixels, pixels);
		(void)fwrite(coverage, 1, (size_t)pixels * pixels, file);
		(void)fclose(file);
	}

	/* Succeeded. */
	printf("p034-mark-dump: %u layers at %u pixels\n", (unsigned)KEILAND_MARK_LAYERS, pixels);
	return 0;
}
