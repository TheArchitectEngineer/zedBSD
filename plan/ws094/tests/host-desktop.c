/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws094-p003: the desktop's cells on the host (fm_desktop_cell,
 * userland/desktop/files/ui-desktop.c): filled from the top-right corner
 * down a column, then the next column to the left; no cell past the last
 * column; a tiny desktop still has one cell.
 *
 *   host-desktop
 */

#include "files.h"

#include <stdio.h>

/* How many checks failed. */
static int failures;

static void check(int condition, const char *text);

/* Runs the checks. */
int
main(void)
{
	struct fm_rect rect;
	int column;
	int row;
	int placed;

	/* The first item: the top-right cell of a 1280x766 desktop (under the system bar). */
	placed = fm_desktop_cell(0, 1280, 766, &column, &row, &rect);
	check(placed == 1 && column == 0 && row == 0 && rect.x == 1168 && rect.y == 16, "first item at the top right (1168,16)");

	/* The second: under it. */
	placed = fm_desktop_cell(1, 1280, 766, &column, &row, &rect);
	check(placed == 1 && column == 0 && row == 1 && rect.x == 1168 && rect.y == 120, "second item under it (1168,120)");

	/* Seven rows fit (766 - 32) / 104: the eighth item starts the next column to the left. */
	placed = fm_desktop_cell(7, 1280, 766, &column, &row, &rect);
	check(placed == 1 && column == 1 && row == 0 && rect.x == 1072 && rect.y == 16, "eighth item starts the next column (1072,16)");

	/* Thirteen columns fit (1280 - 32) / 96: the 92nd item has no cell. */
	placed = fm_desktop_cell(13 * 7 - 1, 1280, 766, &column, &row, &rect);
	check(placed == 1 && column == 12 && row == 6, "the last cell (column 12, row 6)");
	placed = fm_desktop_cell(13 * 7, 1280, 766, &column, &row, &rect);
	check(placed == 0, "no cell past the last column");

	/* A desktop smaller than a cell still has one. */
	placed = fm_desktop_cell(0, 50, 50, &column, &row, &rect);
	check(placed == 1 && column == 0 && row == 0, "a tiny desktop has one cell");

	/* The outcome. */
	if (failures != 0) {
		printf("host-desktop: FAIL (%d)\n", failures);
		return 1;
	}

	/* Succeeded: every check passed. */
	printf("host-desktop: PASS\n");
	return 0;
}

/* Prints a check's outcome and counts a failure. */
static void
check(
	int condition,
	const char *text)
{
	/* A failed check is counted. */
	if (!condition) {
		printf("FAIL: %s\n", text);
		failures++;
		return;
	}

	/* A passed check is printed. */
	printf("ok: %s\n", text);
}
