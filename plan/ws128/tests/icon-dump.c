/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws128-p012: draws every icon of userland/desktop/wayland/icons.c (compiled
 * unchanged) at one size with the compositor's own rasterizer, and writes
 * each one's coverage as a PGM file (DIR/NN.pgm, NN the icon's number), so
 * that the mock and the montage (icon-montage.py) show what the compositor
 * draws.  It also checks that each application's picture covers something
 * and stays inside its square.
 *
 *   icon-dump PIXELS DIR
 *
 * Prints "icon-dump: PASS first_app=N count=M" or "FAIL".
 */

#include "icons.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The largest size drawn, in pixels. */
#define DUMP_MOST	512U

static int dump_one(unsigned icon, unsigned pixels, const char *directory, uint8_t *coverage);

/* Draws every icon and writes each one. */
int
main(
	int argc,
	char **argv)
{
	static uint8_t coverage[DUMP_MOST * DUMP_MOST];
	unsigned pixels;
	unsigned icon;
	int failures;
	int failed;

	/* Both arguments are given. */
	if (argc != 3) {
		fprintf(stderr, "usage: icon-dump PIXELS DIR\n");
		return 2;
	}

	/* The size, no larger than the buffer. */
	pixels = (unsigned)strtoul(argv[1], NULL, 10);
	if (pixels == 0U || pixels > DUMP_MOST) {
		fprintf(stderr, "icon-dump: PIXELS must be 1 to %u\n", DUMP_MOST);
		return 2;
	}

	/* Each icon. */
	failures = 0;
	for (icon = 0; icon < GLASS_ICON_COUNT; icon++) {
		failed = dump_one(icon, pixels, argv[2], coverage);
		if (failed)
			failures++;
	}

	/* The result. */
	if (failures != 0) {
		printf("icon-dump: FAIL (%d icons)\n", failures);
		return 1;
	}

	/* Succeeded. */
	printf("icon-dump: PASS first_app=%u count=%u\n", (unsigned)GLASS_ICON_FIRST_APP, (unsigned)GLASS_ICON_COUNT);
	return 0;
}

/* Draws one icon and writes it; returns 1 when it failed its check or could not be written. */
static int
dump_one(
	unsigned icon,
	unsigned pixels,
	const char *directory,
	uint8_t *coverage)
{
	char path[1024];
	unsigned covered;
	unsigned edge;
	unsigned x;
	unsigned y;
	uint8_t value;
	FILE *file;

	/* The icon, drawn by the compositor's rasterizer. */
	kwl_icon_raster(icon, pixels, coverage, pixels);

	/* How much it covers, and whether it reaches its square's edge. */
	covered = 0;
	edge = 0;
	for (y = 0; y < pixels; y++) {
		for (x = 0; x < pixels; x++) {
			value = coverage[y * pixels + x];
			if (value > 0U)
				covered++;
			if (value > 128U && (x == 0U || y == 0U || x + 1U == pixels || y + 1U == pixels))
				edge++;
		}
	}

	/* The file. */
	(void)snprintf(path, sizeof(path), "%s/%02u.pgm", directory, icon);
	file = fopen(path, "wb");
	if (file == NULL) {
		printf("icon %u: cannot write %s\n", icon, path);
		return 1;
	}

	/* The header and the coverage, a byte a pixel. */
	fprintf(file, "P5\n%u %u\n255\n", pixels, pixels);
	(void)fwrite(coverage, 1, (size_t)pixels * pixels, file);
	(void)fclose(file);

	/* An empty icon, or one that leaves its square, fails. */
	if (covered == 0U || edge != 0U) {
		printf("icon %u: covered=%u edge=%u FAIL\n", icon, covered, edge);
		return 1;
	}

	/* Succeeded. */
	return 0;
}
