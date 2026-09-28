/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws035-p108: renders the layers of the Kei mark (userland/desktop/artwork/mark.c)
 * at a size and writes them side by side as a grey PGM, for looking at on the host.
 *
 *   mark-host OUT.pgm PIXELS
 */

#include "userland/desktop/artwork/mark.h"

#include <stdio.h>
#include <stdlib.h>

int
main(
	int argc,
	char **argv)
{
	uint8_t *layers;
	unsigned pixels;
	unsigned layer;
	unsigned y;
	FILE *file;

	/* The arguments. */
	if (argc != 3) {
		fprintf(stderr, "usage: mark-host OUT.pgm PIXELS\n");
		return 2;
	}
	pixels = (unsigned)atoi(argv[2]);
	layers = malloc((size_t)pixels * pixels * KEILAND_MARK_LAYERS);
	if (layers == NULL)
		return 1;

	/* Each layer, side by side (the stride is the whole row). */
	for (layer = 0; layer < KEILAND_MARK_LAYERS; layer++)
		keiland_mark_raster(layer, pixels, layers + layer * pixels, (size_t)pixels * KEILAND_MARK_LAYERS);

	/* The layers as one grey picture. */
	file = fopen(argv[1], "wb");
	if (file == NULL)
		return 1;
	fprintf(file, "P5\n%u %u\n255\n", pixels * KEILAND_MARK_LAYERS, pixels);
	for (y = 0; y < pixels; y++)
		fwrite(layers + (size_t)y * pixels * KEILAND_MARK_LAYERS, 1, (size_t)pixels * KEILAND_MARK_LAYERS, file);
	fclose(file);

	/* Succeeded. */
	return 0;
}
