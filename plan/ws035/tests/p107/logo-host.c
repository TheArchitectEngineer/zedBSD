/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws035-p107: draws a PPM the way the UEFI loader does (bootloader/uefi/logo.c)
 * into a framebuffer of a size in memory and writes the screen as a PPM, so
 * the cover mode (fit=cover) can be seen on the host.
 *
 *   logo-host IN.ppm OUT.ppm WIDTH HEIGHT
 */

#include "bootloader/uefi/logo.h"

#include <stdio.h>
#include <stdlib.h>

int
main(
	int argc,
	char **argv)
{
	struct zbl6_framebuffer framebuffer;
	uint32_t *pixels;
	uint8_t *data;
	FILE *file;
	long size;
	uint32_t x;
	uint32_t y;
	uint32_t pixel;
	int drawn;

	/* The arguments. */
	if (argc != 5) {
		fprintf(stderr, "usage: logo-host IN.ppm OUT.ppm WIDTH HEIGHT\n");
		return 2;
	}

	/* The picture file. */
	file = fopen(argv[1], "rb");
	if (file == NULL)
		return 1;
	fseek(file, 0, SEEK_END);
	size = ftell(file);
	fseek(file, 0, SEEK_SET);
	data = malloc((size_t)size);
	if (data == NULL || fread(data, 1, (size_t)size, file) != (size_t)size)
		return 1;
	fclose(file);

	/* A BGRX framebuffer of the size asked for. */
	framebuffer.width = (uint32_t)atoi(argv[3]);
	framebuffer.height = (uint32_t)atoi(argv[4]);
	framebuffer.stride = framebuffer.width;
	framebuffer.format = ZBL6_FRAMEBUFFER_BGRX8888;
	framebuffer.size = (uint64_t)framebuffer.stride * framebuffer.height * 4U;
	pixels = calloc(framebuffer.size, 1);
	if (pixels == NULL)
		return 1;
	framebuffer.physical_base = (uint64_t)(uintptr_t)pixels;

	/* The loader's drawing. */
	drawn = zbl_uefi_logo_draw(data, (size_t)size, &framebuffer);
	printf("logo-host: drawn=%d %ux%u\n", drawn, framebuffer.width, framebuffer.height);

	/* The screen as a PPM. */
	file = fopen(argv[2], "wb");
	if (file == NULL)
		return 1;
	fprintf(file, "P6\n%u %u\n255\n", framebuffer.width, framebuffer.height);
	for (y = 0; y < framebuffer.height; y++) {
		for (x = 0; x < framebuffer.width; x++) {
			pixel = pixels[y * framebuffer.stride + x];
			fputc((int)((pixel >> 16) & 255U), file);
			fputc((int)((pixel >> 8) & 255U), file);
			fputc((int)(pixel & 255U), file);
		}
	}
	fclose(file);

	/* Succeeded when the logo was drawn. */
	if (drawn != 1)
		return 1;
	return 0;
}
