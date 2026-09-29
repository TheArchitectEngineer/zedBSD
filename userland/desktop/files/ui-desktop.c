/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The desktop's icons (files --desktop, ws094-p003, plan/ws094/design.md
 * §4): the items of ~/Desktop drawn on zdesktop's desktop surface, over
 * the wallpaper, in cells of a grid.
 *
 * The cells are filled from the top-right corner down, a column at a time,
 * and the next column is to the left (the top-left corner is App Home's
 * drag).  Each cell has the item's icon (a picture's thumbnail) and its
 * name under it, dark with a white halo so that it reads on the light
 * wallpaper.  The rest of the surface is clear.
 */

#include "files.h"

#include <stdio.h>
#include <string.h>

/* A cell of the grid, the icon in it, and the margin from the surface's edges. */
#define DESKTOP_CELL_WIDTH	96
#define DESKTOP_CELL_HEIGHT	104
#define DESKTOP_ICON		64
#define DESKTOP_MARGIN		16

/* The name: its size, how far under the cell's top its baseline is, and its colours. */
#define DESKTOP_TEXT		13U
#define DESKTOP_TEXT_BASELINE	(DESKTOP_ICON + 24)
#define DESKTOP_TEXT_COLOR	FM_RGB(0x1e293b)
#define DESKTOP_HALO_COLOR	FM_RGBA(0xffffff, 150)

static void desktop_item(struct fm_app *app, struct fm_canvas *canvas, const struct fm_entry *entry, const struct fm_rect *cell);
static void desktop_name(struct fm_app *app, struct fm_canvas *canvas, const char *name, const struct fm_rect *cell);

/*
 * Draws the desktop: clear, with each item of the tab's folder in its cell.
 * The layout is logged when the number of items changes.
 */
void
fm_desktop_draw(
	struct fm_app *app,
	struct fm_canvas *canvas)
{
	struct fm_rect cell;
	struct fm_tab *tab;
	size_t index;
	int column;
	int row;
	int placed;
	int cells;
	int logging;

	/* Clear, so that the wallpaper shows. */
	fm_canvas_clear(canvas);
	tab = fm_ui_tab(app);

	/* The layout is logged when the number of items changed (the tests read it). */
	logging = 0;
	if (app->desktop_logged != (int)tab->listing.count + 1)
		logging = 1;

	/* Each item in its cell, as long as there are cells. */
	cells = 0;
	for (index = 0; index < tab->listing.count; index++) {
		/* The cell of the index; the grid may be full. */
		placed = fm_desktop_cell((int)index, canvas->width, canvas->height, &column, &row, &cell);
		if (!placed)
			break;

		/* The item. */
		desktop_item(app, canvas, &tab->listing.entries[index], &cell);
		cells++;

		/* Where it is, for the tests. */
		if (logging)
			fm_log("DESKTOP place name=%s column=%d row=%d x=%d y=%d", tab->listing.entries[index].name, column, row, cell.x, cell.y);
	}

	/* The summary line, once for the layout. */
	if (logging) {
		fm_log("DESKTOP ready items=%lu cells=%d width=%d height=%d", (unsigned long)tab->listing.count, cells, canvas->width, canvas->height);
		app->desktop_logged = (int)tab->listing.count + 1;
	}

	/* The frame is drawn. */
	app->dirty = 0;
}

/*
 * Finds the cell of the index-th item on a desktop of a size: the column,
 * counted from the right (0 the rightmost), the row from the top, and its
 * rectangle.  Returns 1, or 0 when the desktop has no cell for it.
 */
int
fm_desktop_cell(
	int index,
	int width,
	int height,
	int *column,
	int *row,
	struct fm_rect *rect)
{
	int rows;
	int columns;

	/* How many rows fit a column, and how many columns fit the desktop (at least one of each). */
	rows = (height - 2 * DESKTOP_MARGIN) / DESKTOP_CELL_HEIGHT;
	if (rows < 1)
		rows = 1;
	columns = (width - 2 * DESKTOP_MARGIN) / DESKTOP_CELL_WIDTH;
	if (columns < 1)
		columns = 1;

	/* A column at a time, from the top. */
	*column = index / rows;
	*row = index % rows;
	if (*column >= columns)
		return 0;

	/* The cell, from the right edge leftwards. */
	rect->x = width - DESKTOP_MARGIN - (*column + 1) * DESKTOP_CELL_WIDTH;
	rect->y = DESKTOP_MARGIN + *row * DESKTOP_CELL_HEIGHT;
	rect->width = DESKTOP_CELL_WIDTH;
	rect->height = DESKTOP_CELL_HEIGHT;

	/* Succeeded: the cell. */
	return 1;
}

/* Draws one item in its cell: its icon, centred at the top, and its name under it. */
static void
desktop_item(
	struct fm_app *app,
	struct fm_canvas *canvas,
	const struct fm_entry *entry,
	const struct fm_rect *cell)
{
	float left;
	float top;

	/* The icon, centred across the cell, a little under its top. */
	left = (float)cell->x + (float)(DESKTOP_CELL_WIDTH - DESKTOP_ICON) / 2.0f;
	top = (float)cell->y + 4.0f;
	fm_grid_entry_icon(app, canvas, entry, left, top, (float)DESKTOP_ICON);

	/* The name under it. */
	desktop_name(app, canvas, entry->name, cell);
}

/*
 * Draws an item's name centred under its icon, cut to the cell with an
 * ellipsis: a soft white halo (the text drawn around it a pixel each way)
 * under the dark text.
 */
static void
desktop_name(
	struct fm_app *app,
	struct fm_canvas *canvas,
	const char *name,
	const struct fm_rect *cell)
{
	int available;
	int width;
	int left;
	int baseline;
	int dx;
	int dy;

	/* The width the name takes, no more than the cell's. */
	available = DESKTOP_CELL_WIDTH - 8;
	width = fm_text_width(app->text, name, strlen(name), DESKTOP_TEXT, 0);
	if (width > available)
		width = available;

	/* Centred across the cell, under the icon. */
	left = cell->x + (DESKTOP_CELL_WIDTH - width) / 2;
	baseline = cell->y + DESKTOP_TEXT_BASELINE;

	/* The halo: the text a pixel off in each direction. */
	for (dy = -1; dy <= 1; dy++) {
		for (dx = -1; dx <= 1; dx++) {
			/* The centre is the text itself, drawn last. */
			if (dx == 0 && dy == 0)
				continue;
			(void)fm_text_draw_fit(app->text, canvas, left + dx, baseline + dy, name, DESKTOP_TEXT, 0, available, DESKTOP_HALO_COLOR);
		}
	}

	/* The text over it. */
	(void)fm_text_draw_fit(app->text, canvas, left, baseline, name, DESKTOP_TEXT, 0, available, DESKTOP_TEXT_COLOR);
}
