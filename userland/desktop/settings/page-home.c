/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The Home page: every page as a tile, by group, so that a page can be
 * found by its picture.  The titlebar's Home control and the breadcrumb's
 * first part open it.  (The tiles' live state, "Connected" or "Volume
 * 60%", comes with ws089-p008.)
 */

#include "settings.h"

#include <string.h>

/* A tile's narrowest width (tiles widen to fill a row), its height and the space between two, and the texts' sizes. */
#define HOME_TILE_WIDTH		200
#define HOME_TILE_HEIGHT	92
#define HOME_TILE_GAP		14
#define HOME_TEXT_GROUP		13U
#define HOME_TEXT_NAME		15U
#define HOME_TEXT_SUMMARY	12U

/* The space above a group's title, and between the title and its tiles. */
#define HOME_GROUP_GAP		22
#define HOME_GROUP_TITLE	26

static int home_group(struct se_app *app, struct fm_canvas *canvas, unsigned group, const char *title, int x, int top, int width);
static void home_tile(struct se_app *app, struct fm_canvas *canvas, const struct se_page *page, int x, int y, int width);

/*
 * Draws the Home page's groups of tiles from a top edge; returns the edge
 * below them.
 */
int
se_home_draw(
	struct se_app *app,
	struct fm_canvas *canvas,
	int x,
	int top,
	int width)
{
	int y;

	/* Each group in the list's order. */
	y = home_group(app, canvas, SE_GROUP_CONNECTIVITY, "Connectivity", x, top, width);
	y = home_group(app, canvas, SE_GROUP_PERSONALIZATION, "Personalization", x, y + HOME_GROUP_GAP, width);
	y = home_group(app, canvas, SE_GROUP_DEVICES, "Devices", x, y + HOME_GROUP_GAP, width);
	y = home_group(app, canvas, SE_GROUP_SYSTEM, "System", x, y + HOME_GROUP_GAP, width);

	/* The edge below the last group. */
	return y;
}

/* Draws a group's title and its tiles in rows as wide as the column allows; returns the edge below them. */
static int
home_group(
	struct se_app *app,
	struct fm_canvas *canvas,
	unsigned group,
	const char *title,
	int x,
	int top,
	int width)
{
	struct fm_text_line line;
	unsigned id;
	int columns;
	int column;
	int tile;
	int y;

	/* The title, quiet and bold. */
	fm_text_metrics(app->text, HOME_TEXT_GROUP, &line);
	(void)fm_text_draw(app->text, canvas, x + 2, top + line.ascent, title, strlen(title), HOME_TEXT_GROUP, 1, SE_COLOR_TEXT_SECONDARY);

	/* How many tiles a row holds (at least one), and their width to fill the row. */
	columns = (width + HOME_TILE_GAP) / (HOME_TILE_WIDTH + HOME_TILE_GAP);
	if (columns < 1)
		columns = 1;
	tile = (width - (columns - 1) * HOME_TILE_GAP) / columns;

	/* Each page of the group, a new row when one fills. */
	column = 0;
	y = top + HOME_GROUP_TITLE;
	for (id = SE_PAGE_HOME + 1; id < SE_PAGES; id++) {
		if (se_pages[id].group != group)
			continue;

		/* A full row moves down one. */
		if (column == columns) {
			column = 0;
			y += HOME_TILE_HEIGHT + HOME_TILE_GAP;
		}

		/* The tile in the next column. */
		home_tile(app, canvas, &se_pages[id], x + column * (tile + HOME_TILE_GAP), y, tile);
		column++;
	}

	/* The edge below the last row. */
	return y + HOME_TILE_HEIGHT;
}

/* Draws one page's tile: its picture, name and summary, lit under the pointer. */
static void
home_tile(
	struct se_app *app,
	struct fm_canvas *canvas,
	const struct se_page *page,
	int x,
	int y,
	int width)
{
	struct fm_rect tile;
	fm_color ground;
	fm_color glyph;
	fm_color summary;
	int lit;

	/* The tile, a little darker under the pointer. */
	tile.x = x;
	tile.y = y;
	tile.width = width;
	tile.height = HOME_TILE_HEIGHT;
	lit = se_ui_lit(app, SE_HIT_TILE, (int)page->id);
	ground = SE_COLOR_TILE;
	if (lit != 0)
		ground = fm_color_mix(SE_COLOR_TILE, FM_RGBA(0xdfe7f3, 230), 0.8f);
	fm_canvas_round(canvas, (float)x, (float)y, (float)tile.width, (float)tile.height, 14.0f, ground);
	fm_canvas_round_border(canvas, (float)x, (float)y, (float)tile.width, (float)tile.height, 14.0f, 1.0f, SE_COLOR_CARD_EDGE);

	/* The picture in the accent for a page that works, faint for one that is coming. */
	glyph = SE_COLOR_ACCENT;
	summary = SE_COLOR_TEXT_SECONDARY;
	if (page->ready == 0) {
		glyph = SE_COLOR_ICON;
		summary = SE_COLOR_TEXT_FAINT;
	}

	/* The picture at the tile's upper left. */
	se_glyph_draw(canvas, page->glyph, (float)x + 16.0f, (float)y + 14.0f, 26.0f, glyph);

	/* The name and the summary. */
	(void)fm_text_draw_fit(app->text, canvas, x + 16, y + 62, page->name, HOME_TEXT_NAME, 1, tile.width - 28, SE_COLOR_TEXT);
	(void)fm_text_draw_fit(app->text, canvas, x + 16, y + 80, page->summary, HOME_TEXT_SUMMARY, 0, tile.width - 28, summary);

	/* A click opens the page. */
	se_ui_hit(app, &tile, SE_HIT_TILE, (int)page->id);
}
