/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws035-p107: starts the splash spinner (src/drivers/platform/pcat/graphics/
 * splash.c) on a screen read from a PPM (the splash as the loader drew it,
 * logo-host's output), turns it some steps, and writes the screen as a PPM.
 *
 *   spinner-host SCREEN.ppm OUT.ppm STEPS
 */

#include "src/drivers/platform/pcat/graphics/splash.h"

#include <stdio.h>
#include <stdlib.h>

int
main(
	int argc,
	char **argv)
{
	uint32_t *pixels;
	unsigned width;
	unsigned height;
	unsigned index;
	unsigned steps;
	int red;
	int green;
	int blue;
	FILE *file;

	/* The arguments. */
	if (argc != 4) {
		fprintf(stderr, "usage: spinner-host SCREEN.ppm OUT.ppm STEPS\n");
		return 2;
	}

	/* The screen, as BGRX. */
	file = fopen(argv[1], "rb");
	if (file == NULL || fscanf(file, "P6 %u %u 255", &width, &height) != 2)
		return 1;
	fgetc(file);
	pixels = malloc((size_t)width * height * 4U);
	if (pixels == NULL)
		return 1;
	for (index = 0; index < width * height; index++) {
		red = fgetc(file);
		green = fgetc(file);
		blue = fgetc(file);
		pixels[index] = (uint32_t)blue | ((uint32_t)green << 8) | ((uint32_t)red << 16);
	}
	fclose(file);

	/* The spinner, turned. */
	drv_pcat_splash_start(pixels, width, height, width, 0);
	steps = (unsigned)atoi(argv[3]);
	for (index = 0; index < steps; index++)
		drv_pcat_splash_step();

	/* The screen as a PPM. */
	file = fopen(argv[2], "wb");
	if (file == NULL)
		return 1;
	fprintf(file, "P6\n%u %u\n255\n", width, height);
	for (index = 0; index < width * height; index++) {
		fputc((int)((pixels[index] >> 16) & 255U), file);
		fputc((int)((pixels[index] >> 8) & 255U), file);
		fputc((int)(pixels[index] & 255U), file);
	}
	fclose(file);

	/* Succeeded. */
	return 0;
}
