/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The content panel of zdesktop-files: the place's title and its items as
 * a grid of icons (spec §11).
 *
 * Only the rows in sight are drawn.  A folder's item count, shown under
 * its icon, is counted the first time the folder is drawn and kept with
 * the entry.
 */

#include "files.h"

#include <stdio.h>
#include <string.h>

/* The panel's padding, and the height of its title row. */
#define GRID_PADDING		16
#define GRID_TITLE_HEIGHT	58

/* One cell of the grid, and the icon in it. */
#define GRID_CELL_WIDTH		112
#define GRID_CELL_HEIGHT	132
#define GRID_ICON		64

/* The text sizes of the panel. */
#define GRID_TEXT_TITLE		20U
#define GRID_TEXT_COUNT		13U
#define GRID_TEXT_NAME		13U
#define GRID_TEXT_DETAIL	11U

/* The panel's corner radius. */
#define GRID_RADIUS		16.0f

static void grid_panel(struct fm_app *app, struct fm_canvas *canvas, const struct fm_rect *area);
static void grid_title(struct fm_app *app, struct fm_canvas *canvas, const struct fm_rect *area);
static void grid_items(struct fm_app *app, struct fm_canvas *canvas, const struct fm_rect *inner);
static void grid_cell(struct fm_app *app, struct fm_canvas *canvas, struct fm_entry *entry, int index, int x, int y);
static int grid_name(struct fm_app *app, struct fm_canvas *canvas, const struct fm_entry *entry, int x, int y, int selected);
static void grid_message(struct fm_app *app, struct fm_canvas *canvas, const struct fm_rect *inner, const char *message);
static void grid_band(struct fm_app *app, struct fm_canvas *canvas, const struct fm_rect *inner);
static void grid_status(struct fm_app *app, struct fm_canvas *canvas, const struct fm_rect *area);

/*
 * Draws the content panel for the place the tab shows.
 */
void
fm_grid_draw(
	struct fm_app *app,
	struct fm_canvas *canvas,
	const struct fm_rect *area)
{
	struct fm_rect inner;
	struct fm_tab *tab;
	char message[128];

	/* The panel and its title. */
	grid_panel(app, canvas, area);
	grid_title(app, canvas, area);

	/* The items' part of the panel, under the title. */
	inner.x = area->x + GRID_PADDING;
	inner.y = area->y + GRID_TITLE_HEIGHT;
	inner.width = area->width - 2 * GRID_PADDING;
	inner.height = area->height - GRID_TITLE_HEIGHT - 4;
	app->layout.content_height = 0;
	app->layout.items = inner;

	/* The whole panel's background can be clicked (it clears the selection later). */
	fm_ui_hit(app, area, FM_HIT_CONTENT, 0);

	/* A folder that could not be read says so. */
	tab = fm_ui_tab(app);
	if (tab->listing.error != 0) {
		snprintf(message, sizeof(message), "This folder can't be opened.");
		grid_message(app, canvas, &inner, message);
		return;
	}

	/* An empty place says so too. */
	if (tab->listing.count == 0) {
		grid_message(app, canvas, &inner, "Nothing here");
		return;
	}

	/* The items, as a list or a grid, and the rubber band over them. */
	fm_canvas_clip_push(canvas, &inner);
	if (app->view == FM_VIEW_LIST) {
		fm_list_draw(app, canvas, &inner);
	} else {
		grid_items(app, canvas, &inner);
	}

	/* The rubber band over the items. */
	grid_band(app, canvas, &inner);
	fm_canvas_clip_pop(canvas);

	/* The status pill, when there is something to say. */
	grid_status(app, canvas, area);
}

/*
 * Reports where an item is drawn in the last frame's layout (scroll
 * included), for the keyboard and the rubber band.
 */
void
fm_view_item_rect(
	struct fm_app *app,
	int index,
	struct fm_rect *rect)
{
	struct fm_tab *tab;
	int columns;

	/* The list: one row an item under the header. */
	tab = fm_ui_tab(app);
	if (app->view == FM_VIEW_LIST) {
		rect->x = app->layout.items.x;
		rect->y = app->layout.items.y + FM_LIST_HEADER + index * FM_LIST_ROW - tab->scroll;
		rect->width = app->layout.items.width;
		rect->height = FM_LIST_ROW;
		return;
	}

	/* The grid: the cell of its row and column. */
	columns = app->layout.columns;
	if (columns < 1)
		columns = 1;
	rect->x = app->layout.grid_left + (index % columns) * GRID_CELL_WIDTH + 4;
	rect->y = app->layout.items.y + (index / columns) * GRID_CELL_HEIGHT - tab->scroll + 2;
	rect->width = GRID_CELL_WIDTH - 8;
	rect->height = GRID_CELL_HEIGHT - 4;
}

/*
 * Draws an entry's icon in a square box: a folder, or a page of its kind's
 * color.
 */
void
fm_grid_entry_icon(
	struct fm_app *app,
	struct fm_canvas *canvas,
	const struct fm_entry *entry,
	float x,
	float y,
	float size)
{
	char label[8];

	/* A folder is blue. */
	if (entry->folder != 0) {
		fm_icon_folder(canvas, x, y, size, FM_COLOR_FOLDER);
		return;
	}

	/* A file is a page with its kind's band and its extension. */
	fm_mime_label(entry->name, label, sizeof(label));
	fm_icon_file(canvas, app->text, x, y, size, fm_mime_color(entry->mime->category), label);
}

/* Draws the white panel with its shadow and edge. */
static void
grid_panel(
	struct fm_app *app,
	struct fm_canvas *canvas,
	const struct fm_rect *area)
{
	/* The shadow, the white panel and its thin edge. */
	(void)app;
	fm_canvas_shadow(canvas, (float)area->x, (float)area->y + 4.0f, (float)area->width, (float)area->height, GRID_RADIUS, 14.0f, FM_COLOR_SHADOW);
	fm_canvas_round(canvas, (float)area->x, (float)area->y, (float)area->width, (float)area->height, GRID_RADIUS, FM_COLOR_PANEL);
	fm_canvas_round_border(canvas, (float)area->x, (float)area->y, (float)area->width, (float)area->height, GRID_RADIUS, 1.0f, FM_COLOR_PANEL_EDGE);
}

/* Draws the place's name and how many items it holds. */
static void
grid_title(
	struct fm_app *app,
	struct fm_canvas *canvas,
	const struct fm_rect *area)
{
	struct fm_tab *tab;
	const struct fm_location *location;
	const char *name;
	char count[64];
	int baseline;
	int width;

	/* The place's name, large. */
	tab = fm_ui_tab(app);
	location = &tab->history[tab->history_index].location;
	name = fm_location_name(location, app->home);
	baseline = area->y + 38;
	width = fm_text_draw_fit(app->text, canvas, area->x + 24, baseline, name, GRID_TEXT_TITLE, 1, area->width / 2, FM_COLOR_TEXT);

	/* How many items, after it, quietly. */
	if (tab->listing.error != 0)
		return;
	fm_dir_items_text((long)tab->listing.count, count, sizeof(count));
	(void)fm_text_draw(app->text, canvas, area->x + 24 + width + 12, baseline, count, strlen(count), GRID_TEXT_COUNT, 0, FM_COLOR_TEXT_SECONDARY);
}

/* Draws the rows of cells that are in sight, and records each cell. */
static void
grid_items(
	struct fm_app *app,
	struct fm_canvas *canvas,
	const struct fm_rect *inner)
{
	struct fm_tab *tab;
	size_t index;
	int columns;
	int rows;
	int left;
	int row;
	int column;
	int x;
	int y;

	/* As many columns as fit, the grid centred. */
	tab = fm_ui_tab(app);
	columns = inner->width / GRID_CELL_WIDTH;
	if (columns < 1)
		columns = 1;
	left = inner->x + (inner->width - columns * GRID_CELL_WIDTH) / 2;
	rows = (int)((tab->listing.count + (size_t)columns - 1U) / (size_t)columns);

	/* The grid's measure, which scrolling and the keyboard use. */
	app->layout.columns = columns;
	app->layout.grid_left = left;
	app->layout.cell_width = GRID_CELL_WIDTH;
	app->layout.cell_height = GRID_CELL_HEIGHT;
	app->layout.content_height = rows * GRID_CELL_HEIGHT + GRID_TITLE_HEIGHT + 8;

	/* Each cell whose row is in sight. */
	for (index = 0; index < tab->listing.count; index++) {
		row = (int)(index / (size_t)columns);
		column = (int)(index % (size_t)columns);
		y = inner->y + row * GRID_CELL_HEIGHT - tab->scroll;
		if (y + GRID_CELL_HEIGHT < inner->y || y > inner->y + inner->height)
			continue;
		x = left + column * GRID_CELL_WIDTH;
		grid_cell(app, canvas, &tab->listing.entries[index], (int)index, x, y);
	}
}

/* Draws one cell: the lit ground, the icon, the name and the detail line. */
static void
grid_cell(
	struct fm_app *app,
	struct fm_canvas *canvas,
	struct fm_entry *entry,
	int index,
	int x,
	int y)
{
	struct fm_rect cell;
	fm_color selection;
	char detail[64];
	int baseline;
	int width;

	/* The cell's rectangle, recorded as the item. */
	cell.x = x + 4;
	cell.y = y + 2;
	cell.width = GRID_CELL_WIDTH - 8;
	cell.height = GRID_CELL_HEIGHT - 4;

	/* The ground: the selection's color, or a faint one under the pointer. */
	selection = FM_COLOR_SELECTION;
	if (app->focused == 0)
		selection = FM_COLOR_SELECTION_INACTIVE;
	if (entry->selected != 0) {
		fm_canvas_round(canvas, (float)x + (GRID_CELL_WIDTH - GRID_ICON) * 0.5f - 6.0f, (float)y + 4.0f, GRID_ICON + 12.0f, GRID_ICON + 12.0f, 12.0f, selection);
	} else if (app->hover_kind == FM_HIT_ITEM && app->hover_index == index) {
		fm_canvas_round(canvas, (float)cell.x, (float)cell.y, (float)cell.width, (float)cell.height, 12.0f, FM_COLOR_HOVER);
	}

	/* The icon, faded when the item is cut. */
	fm_grid_entry_icon(app, canvas, entry, (float)x + (GRID_CELL_WIDTH - GRID_ICON) * 0.5f, (float)y + 10.0f, (float)GRID_ICON);

	/* The name, on the accent when selected. */
	baseline = grid_name(app, canvas, entry, x, y + GRID_ICON + 30, entry->selected);

	/* The detail: a folder's item count, a file's size. */
	if (entry->folder != 0) {
		if (entry->child_count < 0)
			entry->child_count = fm_dir_count(entry->path, app->show_hidden);
		fm_dir_items_text(entry->child_count, detail, sizeof(detail));
	} else {
		fm_dir_size_text(entry->size, detail, sizeof(detail));
	}

	/* The detail, centred under the name. */
	width = fm_text_width(app->text, detail, strlen(detail), GRID_TEXT_DETAIL, 0);
	(void)fm_text_draw(app->text, canvas, x + (GRID_CELL_WIDTH - width) / 2, baseline + 16, detail, strlen(detail), GRID_TEXT_DETAIL, 0, FM_COLOR_TEXT_SECONDARY);

	/* The cell can be clicked. */
	fm_ui_hit(app, &cell, FM_HIT_ITEM, index);
}

/* Draws an item's name in up to two centred lines, on an accent pill when selected, and returns the last line's baseline. */
static int
grid_name(
	struct fm_app *app,
	struct fm_canvas *canvas,
	const struct fm_entry *entry,
	int x,
	int baseline,
	int selected)
{
	struct fm_text_line line;
	char second[FM_NAME_MAX];
	fm_color ink;
	size_t first_length;
	int limit;
	int first_width;
	int second_width;
	int widest;
	int lines;

	/* The first line: what fits the cell. */
	limit = GRID_CELL_WIDTH - 12;
	first_length = fm_text_break(app->text, entry->name, GRID_TEXT_NAME, 0, limit);
	first_width = fm_text_width(app->text, entry->name, first_length, GRID_TEXT_NAME, 0);

	/* The second line: the rest, cut with an ellipsis. */
	lines = 1;
	second[0] = '\0';
	second_width = 0;
	if (entry->name[first_length] != '\0') {
		(void)fm_text_fit(app->text, entry->name + first_length, GRID_TEXT_NAME, 0, limit, second, sizeof(second));
		second_width = fm_text_width(app->text, second, strlen(second), GRID_TEXT_NAME, 0);
		lines = 2;
	}

	/* A selected name sits on an accent pill as wide as its widest line. */
	fm_text_metrics(app->text, GRID_TEXT_NAME, &line);
	ink = FM_COLOR_TEXT;
	if (selected != 0) {
		widest = first_width;
		if (second_width > widest)
			widest = second_width;
		fm_canvas_round(canvas, (float)(x + (GRID_CELL_WIDTH - widest) / 2 - 6), (float)(baseline - line.ascent - 3), (float)(widest + 12), (float)(lines * line.height + 5), 7.0f, FM_COLOR_ACCENT);
		ink = FM_RGB(0xffffff);
	}

	/* The first line, centred. */
	(void)fm_text_draw(app->text, canvas, x + (GRID_CELL_WIDTH - first_width) / 2, baseline, entry->name, first_length, GRID_TEXT_NAME, 0, ink);

	/* A name of one line ends there. */
	if (lines == 1)
		return baseline;

	/* The second line under it. */
	(void)fm_text_draw(app->text, canvas, x + (GRID_CELL_WIDTH - second_width) / 2, baseline + line.height, second, strlen(second), GRID_TEXT_NAME, 0, ink);

	/* Reports the second line's baseline. */
	return baseline + line.height;
}

/* Draws a quiet message in the middle of the panel. */
static void
grid_message(
	struct fm_app *app,
	struct fm_canvas *canvas,
	const struct fm_rect *inner,
	const char *message)
{
	int width;

	/* Centred, faint. */
	width = fm_text_width(app->text, message, strlen(message), 15U, 0);
	(void)fm_text_draw(app->text, canvas, inner->x + (inner->width - width) / 2, inner->y + inner->height / 2 - 20, message, strlen(message), 15U, 0, FM_COLOR_TEXT_FAINT);
}

/* Draws the rubber band being dragged: a faint accent box with an edge. */
static void
grid_band(
	struct fm_app *app,
	struct fm_canvas *canvas,
	const struct fm_rect *inner)
{
	struct fm_tab *tab;
	int left;
	int top;
	int right;
	int bottom;

	/* No band is being dragged. */
	if (app->band == 0)
		return;

	/* Its corners in order, on the screen. */
	tab = fm_ui_tab(app);
	left = app->band_x0;
	right = app->band_x1;
	if (left > right) {
		left = app->band_x1;
		right = app->band_x0;
	}

	/* And its top and bottom. */
	top = app->band_y0;
	bottom = app->band_y1;
	if (top > bottom) {
		top = app->band_y1;
		bottom = app->band_y0;
	}

	/* On the screen, the scroll taken off. */
	top -= tab->scroll;
	bottom -= tab->scroll;

	/* The box and its edge. */
	(void)inner;
	fm_canvas_round(canvas, (float)left, (float)top, (float)(right - left), (float)(bottom - top), 3.0f, FM_RGBA(0x2f7cf6, 30));
	fm_canvas_round_border(canvas, (float)left, (float)top, (float)(right - left), (float)(bottom - top), 3.0f, 1.0f, FM_RGBA(0x2f7cf6, 150));
}

/* Draws the status pill at the bottom of the panel: a message, or the selection's count and size. */
static void
grid_status(
	struct fm_app *app,
	struct fm_canvas *canvas,
	const struct fm_rect *area)
{
	struct fm_tab *tab;
	uint64_t bytes;
	size_t count;
	char text[256];
	char size[32];
	int width;
	int x;
	int y;

	/* A message while it lasts. */
	tab = fm_ui_tab(app);
	text[0] = '\0';
	if (app->message[0] != '\0' && app->now < app->message_until)
		snprintf(text, sizeof(text), "%s", app->message);

	/* Otherwise two or more selected items: how many and how large. */
	if (text[0] == '\0') {
		count = fm_select_count(tab, &bytes);
		if (count < 2U)
			return;
		snprintf(text, sizeof(text), "%lu items selected", (unsigned long)count);

		/* The size, when files (not only folders) are among them. */
		if (bytes != 0U) {
			fm_dir_size_text(bytes, size, sizeof(size));
			snprintf(text, sizeof(text), "%lu items selected \xe2\x80\x94 %s", (unsigned long)count, size);
		}
	}

	/* A soft pill, centred at the bottom. */
	width = fm_text_width(app->text, text, strlen(text), 12U, 0) + 32;
	x = area->x + (area->width - width) / 2;
	y = area->y + area->height - 44;
	fm_canvas_shadow(canvas, (float)x, (float)y + 2.0f, (float)width, 28.0f, 14.0f, 8.0f, FM_COLOR_SHADOW);
	fm_canvas_round(canvas, (float)x, (float)y, (float)width, 28.0f, 14.0f, FM_RGBA(0x2a3345, 225));
	(void)fm_text_draw(app->text, canvas, x + 16, fm_text_center(12U, y, 28), text, strlen(text), 12U, 0, FM_RGB(0xffffff));
}
