/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws094-p003, p004: the desktop's grid and layout on the host
 * (userland/desktop/files/desktop-layout.c): cells from the top-right
 * corner down a column, then leftwards; saved places kept when they are in
 * the grid and free, the others filling the free cells; the layout file
 * read and written (a place set, Clean Up removing the file).
 *
 *   host-desktop TEMPORARY-FOLDER
 */

#include "files.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* How many checks failed. */
static int failures;

static void check(int condition, const char *text);

/* Runs the checks. */
int
main(
	int argc,
	char **argv)
{
	static const char *const names[] = { "a", "b", "c", "d" };
	struct fm_desktop_place places[4];
	struct fm_desktop_saved saved[3];
	struct fm_desktop_saved *read;
	struct fm_desktop desk;
	struct fm_rect rect;
	char config[1024];
	char path[1200];
	size_t count;
	int placed;
	int columns;
	int rows;
	int error;

	/* The temporary folder is the configuration folder. */
	if (argc != 2) {
		fprintf(stderr, "usage: host-desktop TEMPORARY-FOLDER\n");
		return 2;
	}

	/* The configuration folder under it. */
	snprintf(config, sizeof(config), "%s/config", argv[1]);
	setenv("XDG_CONFIG_HOME", config, 1);

	/* The grid of a 1280x766 desktop: 13 columns of 7 rows. */
	fm_desktop_grid(1280, 766, &columns, &rows);
	check(columns == 13 && rows == 7, "grid 13 x 7");

	/* The top-right cell, the one under it, the first of the next column, none past the grid. */
	placed = fm_desktop_cell_rect(0, 0, 1280, 766, &rect);
	check(placed == 1 && rect.x == 1168 && rect.y == 16, "cell 0,0 at the top right (1168,16)");
	placed = fm_desktop_cell_rect(0, 1, 1280, 766, &rect);
	check(placed == 1 && rect.x == 1168 && rect.y == 120, "cell 0,1 under it (1168,120)");
	placed = fm_desktop_cell_rect(1, 0, 1280, 766, &rect);
	check(placed == 1 && rect.x == 1072 && rect.y == 16, "cell 1,0 to the left (1072,16)");
	placed = fm_desktop_cell_rect(13, 0, 1280, 766, &rect);
	check(placed == 0, "no cell past the grid");

	/* No saved places: in order down the first column. */
	fm_desktop_arrange(names, 4U, NULL, 0U, 1280, 766, places);
	check(places[0].column == 0 && places[0].row == 0 && places[3].column == 0 && places[3].row == 3, "in order down the first column");

	/* Saved places: c at 2,5; d at c's cell (taken, so filled in); x not an item; a outside the grid. */
	snprintf(saved[0].name, sizeof(saved[0].name), "c");
	saved[0].column = 2;
	saved[0].row = 5;
	snprintf(saved[1].name, sizeof(saved[1].name), "d");
	saved[1].column = 2;
	saved[1].row = 5;
	snprintf(saved[2].name, sizeof(saved[2].name), "a");
	saved[2].column = 40;
	saved[2].row = 0;
	fm_desktop_arrange(names, 4U, saved, 3U, 1280, 766, places);
	check(places[2].column == 2 && places[2].row == 5, "c where it was put (2,5)");
	check(places[0].column == 0 && places[0].row == 0, "a (saved outside the grid) fills the first free cell");
	check(places[1].column == 0 && places[1].row == 1, "b fills the next");
	check(places[3].column == 0 && places[3].row == 2, "d (its cell taken by c) fills the next");

	/* A tiny desktop has one cell: the second item has none. */
	fm_desktop_arrange(names, 2U, NULL, 0U, 50, 50, places);
	check(places[0].column == 0 && places[1].column == -1, "a tiny desktop has one cell");

	/* The file: a place set is written and read back; Clean Up removes it. */
	memset(&desk, 0, sizeof(desk));
	error = fm_desktop_layout_set(&desk, "photo.png", 3, 2);
	check(error == 0, "a place set");
	error = fm_desktop_layout_path(path, sizeof(path));
	check(error == 0, "the layout file's path");
	error = fm_desktop_layout_read(path, &read, &count);
	check(error == 0 && count == 1U && read[0].column == 3 && read[0].row == 2, "the place read back");
	free(read);
	error = fm_desktop_layout_set(&desk, "photo.png", 4, 1);
	error |= fm_desktop_layout_read(path, &read, &count);
	check(error == 0 && count == 1U && read[0].column == 4 && read[0].row == 1, "the same item placed again: one line");
	free(read);
	error = fm_desktop_clean_up(&desk);
	error |= fm_desktop_layout_read(path, &read, &count);
	check(error == 0 && count == 0U && desk.saved_count == 0U, "Clean Up forgets every place");
	free(read);
	fm_desktop_release(&desk);

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
