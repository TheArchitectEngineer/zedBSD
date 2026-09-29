/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The desktop's grid and its layout file (files --desktop, ws094-p004,
 * plan/ws094/design.md §4).
 *
 * The desktop is a grid of cells, counted from the top-right corner: a
 * column is a cell's distance from the right edge, a row from the top.
 * Items are placed where the user put them (the layout file), and the
 * others fill the free cells from the top-right corner down a column,
 * then the next column to the left.  The layout file is
 * $XDG_CONFIG_HOME/keiland/desktop-layout (or ~/.config/...), one line an
 * item the user placed: NAME<TAB>COLUMN<TAB>ROW.  It is written to a new
 * file beside it and renamed over it.
 *
 * The places the items are shown at are also kept by name in memory
 * (ws094-p005), and a new layout keeps them after the saved ones, so that
 * an item made or pasted takes a free cell and the others stay where they
 * were; a renamed item keeps its cell under its new name.
 */

#include "files.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* A cell of the grid, and the margin from the desktop's edges. */
#define LAYOUT_CELL_WIDTH	96
#define LAYOUT_CELL_HEIGHT	104
#define LAYOUT_MARGIN		16

/* The layout file under the configuration folder, and the suffix of the new one written before the rename. */
#define LAYOUT_FILE		"keiland/desktop-layout"
#define LAYOUT_NEW_SUFFIX	".new"

/* The longest line of the layout file. */
#define LAYOUT_LINE		(FM_NAME_MAX + 32)

static int layout_saved_index(const struct fm_desktop_saved *saved, size_t count, const char *name);

/*
 * Works out how many columns and rows of cells a desktop of a size has (at
 * least one of each).
 */
void
fm_desktop_grid(
	int width,
	int height,
	int *columns,
	int *rows)
{
	/* The columns across. */
	*columns = (width - 2 * LAYOUT_MARGIN) / LAYOUT_CELL_WIDTH;
	if (*columns < 1)
		*columns = 1;

	/* The rows down. */
	*rows = (height - 2 * LAYOUT_MARGIN) / LAYOUT_CELL_HEIGHT;
	if (*rows < 1)
		*rows = 1;
}

/*
 * Finds the rectangle of a cell on a desktop of a size.  Returns 1, or 0
 * when the desktop has no such cell.
 */
int
fm_desktop_cell_rect(
	int column,
	int row,
	int width,
	int height,
	struct fm_rect *rect)
{
	int columns;
	int rows;

	/* The cell must be in the grid. */
	fm_desktop_grid(width, height, &columns, &rows);
	if (column < 0 || row < 0)
		return 0;
	if (column >= columns || row >= rows)
		return 0;

	/* From the right edge leftwards, from the top down. */
	rect->x = width - LAYOUT_MARGIN - (column + 1) * LAYOUT_CELL_WIDTH;
	rect->y = LAYOUT_MARGIN + row * LAYOUT_CELL_HEIGHT;
	rect->width = LAYOUT_CELL_WIDTH;
	rect->height = LAYOUT_CELL_HEIGHT;

	/* Succeeded: the cell. */
	return 1;
}

/*
 * Places the items of a list of names on a desktop of a size: each saved
 * place that is in the grid and not taken first, then the others in the
 * free cells from the top-right corner down.  An item without a cell gets
 * column -1.
 */
void
fm_desktop_arrange(
	const char *const *names,
	size_t count,
	const struct fm_desktop_saved *saved,
	size_t saved_count,
	int width,
	int height,
	struct fm_desktop_place *places)
{
	unsigned char *taken;
	size_t index;
	int columns;
	int rows;
	int found;
	int cell;
	int next;

	/* The grid, and which of its cells are taken (none without memory for the marks: every item then fills in order). */
	fm_desktop_grid(width, height, &columns, &rows);
	taken = calloc((size_t)columns * (size_t)rows, 1U);

	/* The items the user placed, where they were put, when the cell is in the grid and free. */
	for (index = 0; index < count; index++) {
		/* No place yet. */
		places[index].column = -1;
		places[index].row = -1;
		found = layout_saved_index(saved, saved_count, names[index]);
		if (found < 0 || taken == NULL)
			continue;

		/* A place outside the grid, or one another item has, is not kept. */
		if (saved[found].column < 0 || saved[found].column >= columns)
			continue;
		if (saved[found].row < 0 || saved[found].row >= rows)
			continue;
		cell = saved[found].column * rows + saved[found].row;
		if (taken[cell] != 0U)
			continue;

		/* The item takes its cell. */
		places[index].column = saved[found].column;
		places[index].row = saved[found].row;
		taken[cell] = 1U;
	}

	/* The other items in the free cells, a column at a time from the right. */
	next = 0;
	for (index = 0; index < count; index++) {
		/* An item placed already. */
		if (places[index].column >= 0)
			continue;

		/* The next free cell. */
		while (next < columns * rows && taken != NULL && taken[next] != 0U)
			next++;
		if (next >= columns * rows)
			continue;

		/* The item takes it. */
		places[index].column = next / rows;
		places[index].row = next % rows;
		if (taken != NULL)
			taken[next] = 1U;
		next++;
	}

	/* The marks are done with. */
	free(taken);
}

/*
 * Finds the layout file's path: under $XDG_CONFIG_HOME, or ~/.config.
 * Returns 0, ENOENT without either, or ENAMETOOLONG.
 */
int
fm_desktop_layout_path(
	char *path,
	size_t size)
{
	const char *config;
	const char *home;
	int written;

	/* $XDG_CONFIG_HOME, when it is set. */
	config = getenv("XDG_CONFIG_HOME");
	if (config != NULL && config[0] != '\0') {
		written = snprintf(path, size, "%s/%s", config, LAYOUT_FILE);
		if (written < 0 || (size_t)written >= size)
			return ENAMETOOLONG;

		/* Succeeded: the file under the configuration folder. */
		return 0;
	}

	/* Otherwise ~/.config. */
	home = getenv("HOME");
	if (home == NULL || home[0] == '\0')
		return ENOENT;
	written = snprintf(path, size, "%s/.config/%s", home, LAYOUT_FILE);
	if (written < 0 || (size_t)written >= size)
		return ENAMETOOLONG;

	/* Succeeded: the file under ~/.config. */
	return 0;
}

/*
 * Reads the layout file into a new array (the caller frees it); a missing
 * file is an empty layout.  Malformed lines are left out.  Returns 0 or
 * ENOMEM.
 */
int
fm_desktop_layout_read(
	const char *path,
	struct fm_desktop_saved **saved,
	size_t *count)
{
	struct fm_desktop_saved *grown;
	char line[LAYOUT_LINE];
	char *first_tab;
	char *second_tab;
	char *end;
	char *read;
	FILE *file;
	size_t capacity;
	long column;
	long row;

	/* Nothing yet. */
	*saved = NULL;
	*count = 0;
	capacity = 0;

	/* A missing file is an empty layout. */
	file = fopen(path, "r");
	if (file == NULL)
		return 0;

	/* Each line: NAME<TAB>COLUMN<TAB>ROW. */
	for (;;) {
		/* The next line, until the file ends. */
		read = fgets(line, sizeof(line), file);
		if (read == NULL)
			break;

		/* The name ends at the first tab, the column at the second. */
		first_tab = strchr(line, '\t');
		if (first_tab == NULL || first_tab == line)
			continue;
		second_tab = strchr(first_tab + 1, '\t');
		if (second_tab == NULL)
			continue;
		*first_tab = '\0';
		*second_tab = '\0';

		/* The column and the row, whole numbers. */
		column = strtol(first_tab + 1, &end, 10);
		if (end == first_tab + 1 || *end != '\0')
			continue;
		row = strtol(second_tab + 1, &end, 10);
		if (end == second_tab + 1)
			continue;

		/* Room for one more. */
		if (*count == capacity) {
			capacity += 16U;
			grown = realloc(*saved, capacity * sizeof(**saved));
			if (grown == NULL) {
				fclose(file);
				free(*saved);
				*saved = NULL;
				*count = 0;
				return ENOMEM;
			}

			/* The grown array. */
			*saved = grown;
		}

		/* A name longer than an item's is not one. */
		if ((size_t)(first_tab - line) >= sizeof((*saved)[*count].name))
			continue;

		/* The saved place. */
		memcpy((*saved)[*count].name, line, (size_t)(first_tab - line) + 1U);
		(*saved)[*count].column = (int)column;
		(*saved)[*count].row = (int)row;
		(*count)++;
	}

	/* The file is done with. */
	fclose(file);

	/* Succeeded: the saved places. */
	return 0;
}

/*
 * Writes the layout file: a new file beside it, renamed over it, the
 * folder made when it is not there (no places: the file is removed).
 * Returns 0 or an errno value with the old file as it was.
 */
int
fm_desktop_layout_write(
	const char *path,
	const struct fm_desktop_saved *saved,
	size_t count)
{
	char fresh[FM_PATH_MAX + sizeof(LAYOUT_NEW_SUFFIX)];
	char folder[FM_PATH_MAX];
	char *slash;
	FILE *file;
	size_t index;
	int written;
	int error;

	/* No places: no file. */
	if (count == 0U) {
		error = unlink(path);
		if (error != 0 && errno != ENOENT)
			return errno;
		return 0;
	}

	/* The folder, and the configuration folder above it, made when they are not there. */
	snprintf(folder, sizeof(folder), "%s", path);
	slash = strrchr(folder, '/');
	if (slash != NULL) {
		*slash = '\0';
		slash = strrchr(folder, '/');
		if (slash != NULL && slash != folder) {
			*slash = '\0';
			(void)mkdir(folder, 0755);
			*slash = '/';
		}

		/* The keiland folder in it. */
		(void)mkdir(folder, 0755);
	}

	/* The new file. */
	snprintf(fresh, sizeof(fresh), "%s%s", path, LAYOUT_NEW_SUFFIX);
	file = fopen(fresh, "w");
	if (file == NULL)
		return errno;

	/* Each place, a line. */
	for (index = 0; index < count; index++) {
		written = fprintf(file, "%s\t%d\t%d\n", saved[index].name, saved[index].column, saved[index].row);
		if (written < 0) {
			fclose(file);
			(void)unlink(fresh);
			return EIO;
		}
	}

	/* The new file, complete. */
	error = fclose(file);
	if (error != 0) {
		(void)unlink(fresh);
		return EIO;
	}

	/* It takes the old one's place at once. */
	error = rename(fresh, path);
	if (error != 0) {
		error = errno;
		(void)unlink(fresh);
		return error;
	}

	/* Succeeded: the layout is written. */
	return 0;
}

/*
 * Keeps the place the user gave an item (a move), in the desktop's saved
 * places and its layout file; the next layout uses it.  Returns 0 or an
 * errno value.
 */
int
fm_desktop_layout_set(
	struct fm_desktop *desk,
	const char *name,
	int column,
	int row)
{
	struct fm_desktop_saved *grown;
	char path[FM_PATH_MAX];
	int found;
	int error;

	/* The item's saved place, or a new one. */
	found = layout_saved_index(desk->saved, desk->saved_count, name);
	if (found < 0) {
		grown = realloc(desk->saved, (desk->saved_count + 1U) * sizeof(desk->saved[0]));
		if (grown == NULL)
			return ENOMEM;
		desk->saved = grown;
		found = (int)desk->saved_count;
		desk->saved_count++;
		snprintf(desk->saved[found].name, sizeof(desk->saved[found].name), "%s", name);
	}

	/* The place. */
	desk->saved[found].column = column;
	desk->saved[found].row = row;
	desk->laid_count = (size_t)-1;

	/* The file. */
	error = fm_desktop_layout_path(path, sizeof(path));
	if (error != 0)
		return error;
	error = fm_desktop_layout_write(path, desk->saved, desk->saved_count);
	fm_log("DESKTOP saved name=%s column=%d row=%d error=%d", name, column, row, error);
	if (error != 0)
		return error;

	/* Succeeded: the place is kept. */
	return 0;
}

/*
 * Forgets every place the user gave (Clean Up): the items are laid out in
 * order again.  Returns 0 or an errno value.
 */
int
fm_desktop_clean_up(
	struct fm_desktop *desk)
{
	char path[FM_PATH_MAX];
	int error;

	/* No saved places, nor the places shown, and a new layout. */
	free(desk->saved);
	desk->saved = NULL;
	desk->saved_count = 0;
	free(desk->shown);
	desk->shown = NULL;
	desk->shown_count = 0;
	desk->laid_count = (size_t)-1;

	/* No file. */
	error = fm_desktop_layout_path(path, sizeof(path));
	if (error != 0)
		return error;
	error = fm_desktop_layout_write(path, NULL, 0U);
	fm_log("DESKTOP clean-up error=%d", error);
	if (error != 0)
		return error;

	/* Succeeded: the desktop is in order. */
	return 0;
}

/*
 * Gives an item's place a new name (the item was renamed), in the places
 * shown and in the saved places, whose file is written again when the
 * item had one.  Returns 0 or an errno value.
 */
int
fm_desktop_layout_rename(
	struct fm_desktop *desk,
	const char *old_name,
	const char *new_name)
{
	char path[FM_PATH_MAX];
	int found;
	int error;

	/* The place it is shown at keeps it under its new name. */
	found = layout_saved_index(desk->shown, desk->shown_count, old_name);
	if (found >= 0)
		snprintf(desk->shown[found].name, sizeof(desk->shown[found].name), "%s", new_name);

	/* An item the user did not place has nothing saved. */
	found = layout_saved_index(desk->saved, desk->saved_count, old_name);
	if (found < 0)
		return 0;

	/* The saved place under the new name, and the file. */
	snprintf(desk->saved[found].name, sizeof(desk->saved[found].name), "%s", new_name);
	error = fm_desktop_layout_path(path, sizeof(path));
	if (error != 0)
		return error;
	error = fm_desktop_layout_write(path, desk->saved, desk->saved_count);
	fm_log("DESKTOP saved-rename from=%s to=%s error=%d", old_name, new_name, error);
	if (error != 0)
		return error;

	/* Succeeded: the saved place follows the item. */
	return 0;
}

/*
 * Remembers where the items of a layout are shown (names and the
 * desktop's places, in the same order), for the next layout.  Returns 0 or
 * ENOMEM (the places shown before are then forgotten).
 */
int
fm_desktop_remember(
	struct fm_desktop *desk,
	const char *const *names,
	size_t count)
{
	struct fm_desktop_saved *shown;
	size_t index;

	/* The places shown before go. */
	free(desk->shown);
	desk->shown = NULL;
	desk->shown_count = 0;

	/* Room for every placed item. */
	shown = calloc(count + 1U, sizeof(shown[0]));
	if (shown == NULL)
		return ENOMEM;
	desk->shown = shown;

	/* Each item that has a cell, by its name. */
	for (index = 0; index < count && index < desk->place_count; index++) {
		/* An item without a cell is placed afresh next time. */
		if (desk->places[index].column < 0)
			continue;

		/* Its name and cell. */
		snprintf(shown[desk->shown_count].name, sizeof(shown[desk->shown_count].name), "%s", names[index]);
		shown[desk->shown_count].column = desk->places[index].column;
		shown[desk->shown_count].row = desk->places[index].row;
		desk->shown_count++;
	}

	/* Succeeded: the next layout keeps these places. */
	return 0;
}

/*
 * Frees the desktop's places, saved places and places shown.
 */
void
fm_desktop_release(
	struct fm_desktop *desk)
{
	/* The arrays, and nothing is left. */
	free(desk->places);
	free(desk->saved);
	free(desk->shown);
	memset(desk, 0, sizeof(*desk));
}

/* Finds a name among the saved places; -1 when it is not there. */
static int
layout_saved_index(
	const struct fm_desktop_saved *saved,
	size_t count,
	const char *name)
{
	size_t index;
	int differs;

	/* Each saved place. */
	for (index = 0; index < count; index++) {
		/* The same name. */
		differs = strcmp(saved[index].name, name);
		if (differs == 0)
			return (int)index;
	}

	/* Not saved. */
	return -1;
}
