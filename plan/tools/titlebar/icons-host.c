/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws070-p009: draws every titlebar icon (userland/base/zdesktop/icons.c) at
 * 16, 20, 32 and 64 pixels into one PPM sheet, dark on white, a row a size,
 * and checks that each icon covers something and stays inside its square.
 *
 *   icons-host OUT.ppm
 *
 * Prints "icons-host: PASS" or "FAIL".
 */

#include "icons.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const unsigned sizes[] = { 16U, 20U, 32U, 64U };

int
main(
	int argc,
	char **argv)
{
	static uint8_t coverage[64 * 64];
	unsigned char *sheet;
	unsigned width;
	unsigned height;
	unsigned row;
	unsigned icon;
	unsigned x;
	unsigned y;
	unsigned left;
	unsigned top;
	unsigned covered;
	unsigned edge;
	int failures;
	FILE *file;

	if (argc != 2) {
		fprintf(stderr, "usage: icons-host OUT.ppm\n");
		return 2;
	}

	/* One cell of 72 pixels an icon, one row a size. */
	width = 72U * GLASS_ICON_COUNT;
	height = 72U * (unsigned)(sizeof(sizes) / sizeof(sizes[0]));
	sheet = malloc((size_t)width * height * 3U);
	if (sheet == NULL)
		return 1;
	memset(sheet, 255, (size_t)width * height * 3U);

	failures = 0;
	for (row = 0; row < sizeof(sizes) / sizeof(sizes[0]); row++) {
		for (icon = 0; icon < GLASS_ICON_COUNT; icon++) {
			zwl_icon_raster(icon, sizes[row], coverage, sizes[row]);
			left = icon * 72U + (72U - sizes[row]) / 2U;
			top = row * 72U + (72U - sizes[row]) / 2U;
			covered = 0;
			edge = 0;
			for (y = 0; y < sizes[row]; y++) {
				for (x = 0; x < sizes[row]; x++) {
					unsigned char value = coverage[y * sizes[row] + x];
					unsigned char *pixel = sheet + ((size_t)(top + y) * width + left + x) * 3U;
					if (value > 0U)
						covered++;
					if (value > 128U && (x == 0U || y == 0U || x + 1U == sizes[row] || y + 1U == sizes[row]))
						edge++;
					pixel[0] = (unsigned char)(255 - value * (255 - 0x1e) / 255);
					pixel[1] = (unsigned char)(255 - value * (255 - 0x26) / 255);
					pixel[2] = (unsigned char)(255 - value * (255 - 0x32) / 255);
				}
			}
			if (covered == 0U || edge != 0U) {
				printf("icon %u at %u: covered=%u edge=%u FAIL\n", icon, sizes[row], covered, edge);
				failures++;
			}
		}
	}

	file = fopen(argv[1], "wb");
	if (file == NULL)
		return 1;
	fprintf(file, "P6\n%u %u\n255\n", width, height);
	fwrite(sheet, 1, (size_t)width * height * 3U, file);
	fclose(file);
	free(sheet);

	printf("icons-host: %s\n", failures == 0 ? "PASS" : "FAIL");
	return failures != 0;
}
