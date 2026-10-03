/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws035-p107: feeds a PPM to the BIOS loader's decoder (bootloader/bios/logo.c)
 * 512 bytes at a time, as bootzbsd.S does, applies its writes (and the
 * background fill) to a BGRX framebuffer in memory, and writes the screen as a
 * PPM.  It also counts the writes of the fullest sector.
 *
 *   bios-logo-host IN.ppm OUT.ppm WIDTH HEIGHT
 */

#include "bootloader/bios/logo.h"

#include <stdio.h>
#include <stdlib.h>

int
main(
	int argc,
	char **argv)
{
	static struct zbl_bios_logo logo;
	uint8_t sector[512];
	uint32_t *pixels;
	uint32_t width;
	uint32_t height;
	uint32_t index;
	uint32_t fullest;
	uint32_t pixel;
	size_t got;
	FILE *file;
	int result;

	/* The arguments. */
	if (argc != 5) {
		fprintf(stderr, "usage: bios-logo-host IN.ppm OUT.ppm WIDTH HEIGHT\n");
		return 2;
	}
	width = (uint32_t)atoi(argv[3]);
	height = (uint32_t)atoi(argv[4]);
	pixels = calloc((size_t)width * height, 4U);
	if (pixels == NULL)
		return 1;

	/* The file, a sector at a time, until the decoder says it is done. */
	file = fopen(argv[1], "rb");
	if (file == NULL)
		return 1;
	zbl_bios_logo_begin(&logo, width, height, width, 2U);
	fullest = 0;
	result = 0;
	while (result == 0) {
		got = fread(sector, 1, sizeof(sector), file);
		if (got == 0)
			break;
		result = zbl_bios_logo_feed(&logo, sector, (uint32_t)got);

		/* The background fill, once. */
		if (logo.fill_now) {
			logo.fill_now = 0;
			for (index = 0; index < width * height; index++)
				pixels[index] = logo.background;
		}

		/* The writes of this sector. */
		for (index = 0; index < logo.count; index++)
			pixels[logo.offsets[index] / 4U] = logo.pixels[index];
		if (logo.count > fullest)
			fullest = logo.count;
	}
	fclose(file);
	printf("bios-logo-host: result=%d contain=%u fullest=%u of %u\n", result, logo.contain, fullest, (unsigned)ZBL_BIOS_LOGO_WRITES);

	/* The screen as a PPM. */
	file = fopen(argv[2], "wb");
	if (file == NULL)
		return 1;
	fprintf(file, "P6\n%u %u\n255\n", width, height);
	for (index = 0; index < width * height; index++) {
		pixel = pixels[index];
		fputc((int)((pixel >> 16) & 255U), file);
		fputc((int)((pixel >> 8) & 255U), file);
		fputc((int)(pixel & 255U), file);
	}
	fclose(file);

	/* Succeeded when the whole logo was decoded. */
	if (result != 1)
		return 1;
	return 0;
}
