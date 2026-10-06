/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws128-p012: draws every application's tile with the compositor's own
 * kwl_icon_tile (userland/desktop/wayland/icons.c, compiled unchanged) at
 * one size and writes each one as raw premultiplied RGBA
 * (DIR/NN-PIXELS.rgba, NN the icon's number), for the host pictures of
 * App Home and the bar (tile-screens.py).  It also checks each tile: its
 * corners are transparent, its middle band is opaque somewhere, and its
 * picture is cut out (transparent pixels inside the rounded square).
 *
 *   tile-dump PIXELS DIR
 *
 * Prints "tile-dump: PASS pixels=N tiles=M" or "FAIL".
 */

#include "icons.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int dump_tile(unsigned icon, unsigned pixels, const char *directory, uint32_t *argb);

/* Draws every tile and writes each one. */
int
main(
	int argc,
	char **argv)
{
	static uint32_t argb[GLASS_ICON_TILE_MOST * GLASS_ICON_TILE_MOST];
	unsigned pixels;
	unsigned icon;
	int failures;
	int failed;

	/* Both arguments are given. */
	if (argc != 3) {
		fprintf(stderr, "usage: tile-dump PIXELS DIR\n");
		return 2;
	}

	/* The size, no larger than a tile can be. */
	pixels = (unsigned)strtoul(argv[1], NULL, 10);
	if (pixels < 8U || pixels > GLASS_ICON_TILE_MOST) {
		fprintf(stderr, "tile-dump: PIXELS must be 8 to %u\n", GLASS_ICON_TILE_MOST);
		return 2;
	}

	/* Each application's tile. */
	failures = 0;
	for (icon = GLASS_ICON_FIRST_APP; icon < GLASS_ICON_COUNT; icon++) {
		failed = dump_tile(icon, pixels, argv[2], argb);
		if (failed)
			failures++;
	}

	/* A titlebar icon has no tile: its square stays transparent. */
	argb[0] = 1U;
	kwl_icon_tile(0U, pixels, argb, pixels);
	if (argb[0] != 0U || argb[(pixels / 2U) * pixels + pixels / 2U] != 0U) {
		printf("titlebar icon 0 has a tile FAIL\n");
		failures++;
	}

	/* The result. */
	if (failures != 0) {
		printf("tile-dump: FAIL (%d)\n", failures);
		return 1;
	}

	/* Succeeded. */
	printf("tile-dump: PASS pixels=%u tiles=%u\n", pixels, (unsigned)GLASS_ICON_APPS);
	return 0;
}

/* Draws one tile, checks it and writes it; returns 1 when it failed its check or could not be written. */
static int
dump_tile(
	unsigned icon,
	unsigned pixels,
	const char *directory,
	uint32_t *argb)
{
	unsigned char bytes[4];
	char path[512];
	uint32_t value;
	uint32_t alpha;
	unsigned opaque;
	unsigned holes;
	unsigned index;
	unsigned x;
	unsigned y;
	FILE *file;

	/* The tile, rows packed. */
	kwl_icon_tile(icon, pixels, argb, pixels);

	/* Its four corners are outside the rounded square. */
	if (argb[0] != 0U ||
	    argb[pixels - 1U] != 0U ||
	    argb[(pixels - 1U) * pixels] != 0U ||
	    argb[pixels * pixels - 1U] != 0U) {
		printf("icon %u: a corner is not transparent FAIL\n", icon);
		return 1;
	}

	/* Opaque pixels (the bands), and pixels mostly cut out well inside the square (the picture). */
	opaque = 0;
	holes = 0;
	for (y = 0; y < pixels; y++) {
		for (x = 0; x < pixels; x++) {
			alpha = argb[y * pixels + x] >> 24;
			if (alpha == 255U)
				opaque++;
			if (alpha < 128U && x >= pixels / 4U && x < pixels - pixels / 4U && y >= pixels / 4U && y < pixels - pixels / 4U)
				holes++;
		}
	}

	/* A tile without bands or without its picture cut out fails. */
	if (opaque == 0U || holes == 0U) {
		printf("icon %u: opaque=%u holes=%u FAIL\n", icon, opaque, holes);
		return 1;
	}

	/* No channel above the alpha (premultiplied). */
	for (index = 0; index < pixels * pixels; index++) {
		value = argb[index];
		alpha = value >> 24;
		if (((value >> 16) & 0xffU) > alpha || ((value >> 8) & 0xffU) > alpha || (value & 0xffU) > alpha) {
			printf("icon %u: pixel %u not premultiplied FAIL\n", icon, index);
			return 1;
		}
	}

	/* The file, R G B A a pixel. */
	snprintf(path, sizeof(path), "%s/%02u-%u.rgba", directory, icon, pixels);
	file = fopen(path, "wb");
	if (file == NULL) {
		printf("icon %u: cannot write %s FAIL\n", icon, path);
		return 1;
	}

	/* Each pixel, its bytes in that order. */
	for (index = 0; index < pixels * pixels; index++) {
		value = argb[index];
		bytes[0] = (unsigned char)(value >> 16);
		bytes[1] = (unsigned char)(value >> 8);
		bytes[2] = (unsigned char)value;
		bytes[3] = (unsigned char)(value >> 24);
		fwrite(bytes, 1, sizeof(bytes), file);
	}

	/* The file is complete. */
	fclose(file);

	/* Succeeded: the tile is written. */
	return 0;
}
