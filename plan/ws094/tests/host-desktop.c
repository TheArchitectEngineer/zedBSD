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
 * ws094-p005: the places shown kept for the next layout (a new item takes
 * a free cell, the others stay), a rename carrying the place (shown and
 * saved) to the new name, Clean Up forgetting the places shown, and the
 * desktop's context menus (ui-context.c) on an item and on the empty
 * desktop, with the file manager's model over a folder of the temporary
 * folder.
 *
 * ws094-p006: the cell at a point; a drop of the desktop's own items on a
 * cell moving them there (a taken cell keeping them), and a drop of
 * another window's items placing them from the drop's cell.
 *
 *   host-desktop TEMPORARY-FOLDER
 */

#include "files.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* How many checks failed. */
static int failures;

static void check(int condition, const char *text);
static void check_shown(void);
static void check_menus(const char *temporary);
static int context_has(const struct fm_context *context, const char *label);
static void check_drag(const char *temporary);
static int saved_at(const struct fm_desktop *desk, const char *name, int column, int row);

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

	/* ws094-p005: the places shown, the rename, and the context menus. */
	check_shown();
	check_menus(argv[1]);
	check_drag(argv[1]);

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

/* Checks the places shown: kept for the next layout, carried by a rename, forgotten by Clean Up (ws094-p005). */
static void
check_shown(void)
{
	static const char *const before[] = { "b", "c", "d" };
	static const char *const after[] = { "a", "b", "c", "d" };
	struct fm_desktop_place places[4];
	struct fm_desktop_saved *read;
	struct fm_desktop desk;
	char path[1200];
	size_t count;
	int error;

	/* b, c and d laid out down the first column, and remembered. */
	memset(&desk, 0, sizeof(desk));
	desk.places = places;
	desk.place_count = 3U;
	fm_desktop_arrange(before, 3U, NULL, 0U, 1280, 766, places);
	error = fm_desktop_remember(&desk, before, 3U);
	check(error == 0 && desk.shown_count == 3U, "the places shown remembered");

	/* A new item a (before them by name) takes the first free cell; the others stay. */
	fm_desktop_arrange(after, 4U, desk.shown, desk.shown_count, 1280, 766, places);
	check(places[1].column == 0 && places[1].row == 0, "b stays at 0,0");
	check(places[3].column == 0 && places[3].row == 2, "d stays at 0,2");
	check(places[0].column == 0 && places[0].row == 3, "the new a takes the free cell 0,3");

	/* A renamed item keeps its place shown under the new name; c had a saved place, which follows too. */
	desk.place_count = 4U;
	error = fm_desktop_remember(&desk, after, 4U);
	error |= fm_desktop_layout_set(&desk, "c", 5, 4);
	error |= fm_desktop_layout_rename(&desk, "c", "e");
	check(error == 0 && strcmp(desk.shown[2].name, "e") == 0, "the rename carries the place shown");
	error = fm_desktop_layout_path(path, sizeof(path));
	error |= fm_desktop_layout_read(path, &read, &count);
	check(error == 0 && count == 1U && strcmp(read[0].name, "e") == 0 && read[0].column == 5, "the rename carries the saved place in the file");
	free(read);

	/* Clean Up forgets the places shown too. */
	error = fm_desktop_clean_up(&desk);
	check(error == 0 && desk.shown_count == 0U && desk.shown == NULL, "Clean Up forgets the places shown");

	/* The places were the test's; the rest is freed. */
	desk.places = NULL;
	fm_desktop_release(&desk);
}

/* Checks the desktop's context menus and its rename over a folder of the temporary folder (ws094-p005). */
static void
check_menus(
	const char *temporary)
{
	static struct fm_context context;
	static struct fm_app app;
	struct fm_tab *tab;
	struct stat status;
	char folder[1024];
	char file[1200];
	char renamed[1200];
	FILE *made;
	int differs;
	int index;
	int error;

	/* A Desktop folder with a file and a folder, and the model over it in the desktop mode. */
	snprintf(folder, sizeof(folder), "%s/Desktop", temporary);
	(void)mkdir(folder, 0755);
	snprintf(file, sizeof(file), "%s/notes.txt", folder);
	made = fopen(file, "w");
	if (made != NULL)
		fclose(made);
	snprintf(renamed, sizeof(renamed), "%s/Projects", folder);
	(void)mkdir(renamed, 0755);
	setenv("HOME", temporary, 1);
	error = fm_app_init(&app, NULL, folder);
	check(error == 0, "the model over the desktop's folder");
	if (error != 0)
		return;
	app.desktop = 1;
	tab = fm_ui_tab(&app);
	check(tab->listing.count == 2U, "two items on the desktop");

	/* The empty desktop's menu: New Folder, Paste, Clean Up, Show Desktop in Files; no view or sort. */
	app.context_where = FM_CONTEXT_EMPTY;
	fm_select_none(tab);
	fm_ui_context(&app, &context);
	check(context_has(&context, "New Folder") && context_has(&context, "Paste") && context_has(&context, "Clean Up"), "the empty desktop's menu: New Folder, Paste, Clean Up");
	check(context_has(&context, "Show Desktop in Files") && !context_has(&context, "View") && !context_has(&context, "Sort By"), "... Show Desktop in Files, and no View or Sort By");

	/* An item's menu: the file manager's, with Show in Files instead of Get Info and no new tab. */
	index = 0;
	differs = strcmp(tab->listing.entries[0].name, "Projects");
	if (differs != 0)
		index = 1;
	fm_select_only(tab, index);
	app.context_where = FM_CONTEXT_ITEMS;
	fm_ui_context(&app, &context);
	check(context_has(&context, "Open") && context_has(&context, "Rename") && context_has(&context, "Move to Trash") && context_has(&context, "Copy"), "an item's menu: Open, Copy, Rename, Move to Trash");
	check(context_has(&context, "Show in Files") && !context_has(&context, "Get Info") && !context_has(&context, "Open in New Tab"), "... Show in Files, no Get Info or Open in New Tab");

	/* A rename on the desktop: F2's field, a new name typed, Enter; the file renamed. */
	fm_select_only(tab, 1 - index);
	tab->cursor = 1 - index;
	fm_desktop_action(&app, FM_ACTION_RENAME);
	check(app.focus == FM_FOCUS_RENAME, "Rename opens the field");
	fm_field_set(&app.rename, "todo.txt");
	fm_desktop_rename_end(&app, 1);
	snprintf(renamed, sizeof(renamed), "%s/todo.txt", folder);
	error = lstat(renamed, &status);
	check(error == 0 && app.focus != FM_FOCUS_RENAME, "the file renamed to todo.txt");

	/* The model is done with. */
	fm_desktop_release(&app.desk);
	fm_app_release(&app);
}

/* Tells whether a context menu has a row with a label. */
static int
context_has(
	const struct fm_context *context,
	const char *label)
{
	unsigned index;
	int differs;

	/* Each row's label. */
	for (index = 0; index < context->count; index++) {
		differs = strcmp(context->rows[index].label, label);
		if (differs == 0)
			return 1;
	}

	/* No such row. */
	return 0;
}

/* Checks the cell at a point, and the drops on the desktop (ws094-p006), over the Desktop folder check_menus made. */
static void
check_drag(
	const char *temporary)
{
	static struct fm_app app;
	static const char *names[4];
	static char *dropped[2] = { "/elsewhere/a.txt", "/elsewhere/b.txt" };
	struct fm_desktop_place places[4];
	struct fm_tab *tab;
	char folder[1024];
	size_t index;
	int column;
	int row;
	int found;
	int error;
	int todo;

	/* The cells at points: the top-right one, the one left of it, the margin. */
	found = fm_desktop_cell_at(1200, 50, 1280, 766, &column, &row);
	check(found == 1 && column == 0 && row == 0, "the cell at (1200,50) is 0,0");
	found = fm_desktop_cell_at(1100, 150, 1280, 766, &column, &row);
	check(found == 1 && column == 1 && row == 1, "the cell at (1100,150) is 1,1");
	found = fm_desktop_cell_at(1270, 50, 1280, 766, &column, &row);
	check(found == 0, "no cell in the right margin");

	/* The model over the Desktop folder (Projects and todo.txt), laid out down the first column. */
	snprintf(folder, sizeof(folder), "%s/Desktop", temporary);
	error = fm_app_init(&app, NULL, folder);
	check(error == 0, "the model for the drag");
	if (error != 0)
		return;
	app.desktop = 1;
	tab = fm_ui_tab(&app);
	for (index = 0; index < tab->listing.count && index < 4U; index++)
		names[index] = tab->listing.entries[index].name;
	fm_desktop_arrange(names, tab->listing.count, NULL, 0U, 1280, 766, places);
	app.desk.places = places;
	app.desk.place_count = tab->listing.count;
	app.desk.width = 1280;
	app.desk.height = 766;
	todo = 0;
	error = strcmp(tab->listing.entries[0].name, "todo.txt");
	if (error != 0)
		todo = 1;

	/* todo.txt dragged to the cell 3,2: it moves there, and the place is saved. */
	fm_select_only(tab, todo);
	app.desk.press_index = todo;
	app.desk.drop_place = 1;
	app.desk.drop_column = 3;
	app.desk.drop_row = 2;
	found = fm_desktop_drop_place(&app);
	check(found == 1 && saved_at(&app.desk, "todo.txt", 3, 2), "todo.txt moved to 3,2 and saved");

	/* Dragged onto Projects' cell 0,0: it stays. */
	app.desk.drop_place = 1;
	app.desk.drop_column = 0;
	app.desk.drop_row = 0;
	found = fm_desktop_drop_place(&app);
	check(found == 1 && saved_at(&app.desk, "todo.txt", 3, 2), "a drop on another item's cell keeps todo.txt where it was saved");

	/* Two items of another window dropped at 0,0: placed in the free cells from there (0,0 and 0,1 are the items'). */
	snprintf(app.drop_folder, sizeof(app.drop_folder), "%s", folder);
	app.desk.drop_item = -1;
	app.desk.drop_column = 0;
	app.desk.drop_row = 0;
	fm_desktop_dropped(&app, dropped, 2U);
	check(saved_at(&app.desk, "a.txt", 0, 2) && saved_at(&app.desk, "b.txt", 0, 3), "the dropped a.txt and b.txt placed at 0,2 and 0,3");

	/* The places were the test's; the rest is freed. */
	app.desk.places = NULL;
	fm_desktop_release(&app.desk);
	fm_app_release(&app);
}

/* Tells whether a name has a saved place at a cell. */
static int
saved_at(
	const struct fm_desktop *desk,
	const char *name,
	int column,
	int row)
{
	size_t index;
	int differs;

	/* Each saved place. */
	for (index = 0; index < desk->saved_count; index++) {
		differs = strcmp(desk->saved[index].name, name);
		if (differs != 0)
			continue;

		/* The name's place. */
		if (desk->saved[index].column == column && desk->saved[index].row == row)
			return 1;
		return 0;
	}

	/* No place for the name. */
	return 0;
}
