/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The desktop's icons (files --desktop, ws094-p003, p004,
 * plan/ws094/design.md §4): the items of ~/Desktop drawn on zdesktop's
 * desktop surface, over the wallpaper, in the cells desktop-layout.c gives
 * them, and what the pointer and the keys do to them.
 *
 * Each cell has the item's icon (a picture's thumbnail) and its name under
 * it, dark with a white halo so that it reads on the light wallpaper; a
 * selected item has a light ground behind its icon and its name on a blue
 * pill.  A click selects (Ctrl adds or takes away, Shift selects from the
 * anchor), a drag from where no item is draws a rubber band that selects
 * what it touches, a double click or Enter opens (a file with its default
 * way, WS093; a folder in a new Files window), the arrows move the
 * selection to the nearest item that way, Ctrl+A selects all and Esc
 * nothing.  The rest of the surface is clear.
 */

#include "files.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* A cell's icon, how far it is under the cell's top, and the name's size and baseline. */
#define DESKTOP_ICON		64
#define DESKTOP_ICON_TOP	4
#define DESKTOP_TEXT		13U
#define DESKTOP_TEXT_BASELINE	(DESKTOP_ICON + 24)

/* The name's colours: dark text, a white halo; a selected name's pill and text. */
#define DESKTOP_TEXT_COLOR	FM_RGB(0x1e293b)
#define DESKTOP_HALO_COLOR	FM_RGBA(0xffffff, 150)
#define DESKTOP_PILL_COLOR	FM_RGB(0x2f7cf6)
#define DESKTOP_PILL_TEXT	FM_RGB(0xffffff)

/* A selected icon's ground, and the rubber band's fill and edge. */
#define DESKTOP_GROUND_COLOR	FM_RGBA(0xffffff, 110)
#define DESKTOP_BAND_FILL	FM_RGBA(0x2f7cf6, 40)
#define DESKTOP_BAND_EDGE	FM_RGBA(0x2f7cf6, 160)

/* A second click this soon after the first on the same item is a double click, in milliseconds. */
#define DESKTOP_DOUBLE_CLICK_MS	400U

/* The most items opened by one Enter. */
#define DESKTOP_OPEN_MAX	8

/* The keys the desktop answers (evdev codes). */
#define DESKTOP_KEY_ESC		1U
#define DESKTOP_KEY_ENTER	28U
#define DESKTOP_KEY_A		30U
#define DESKTOP_KEY_KPENTER	96U
#define DESKTOP_KEY_UP		103U
#define DESKTOP_KEY_LEFT	105U
#define DESKTOP_KEY_RIGHT	106U
#define DESKTOP_KEY_DOWN	108U

/* The program a folder opens in. */
#define DESKTOP_FILES		"/bin/files"

static void desktop_layout(struct fm_app *app, int width, int height);
static void desktop_item(struct fm_app *app, struct fm_canvas *canvas, const struct fm_entry *entry, const struct fm_rect *cell);
static void desktop_name(struct fm_app *app, struct fm_canvas *canvas, const char *name, const struct fm_rect *cell, int selected);
static void desktop_band_rect(const struct fm_desktop *desk, struct fm_rect *rect);
static void desktop_band_select(struct fm_app *app);
static void desktop_press(struct fm_app *app, const struct fm_event *event);
static void desktop_key(struct fm_app *app, const struct fm_event *event);
static void desktop_arrow(struct fm_app *app, int dx, int dy);
static void desktop_open(struct fm_app *app, int index);
static int desktop_rects_meet(const struct fm_rect *a, const struct fm_rect *b);

/*
 * Draws the desktop: clear, with each item of the tab's folder in its cell
 * and the rubber band over them.  The layout is logged when the number of
 * items changes.
 */
void
fm_desktop_draw(
	struct fm_app *app,
	struct fm_canvas *canvas)
{
	struct fm_desktop *desk;
	struct fm_rect cell;
	struct fm_rect band;
	struct fm_tab *tab;
	size_t index;
	int placed;
	int cells;
	int logging;

	/* Clear, so that the wallpaper shows; the places for this size and listing. */
	fm_canvas_clear(canvas);
	tab = fm_ui_tab(app);
	desk = &app->desk;
	desktop_layout(app, canvas->width, canvas->height);

	/* The layout is logged when the number of items changed (the tests read it). */
	logging = 0;
	if (desk->logged != (int)tab->listing.count + 1)
		logging = 1;

	/* Each item in its cell; an item without a cell is not shown. */
	cells = 0;
	for (index = 0; index < tab->listing.count && index < desk->place_count; index++) {
		/* The item's cell. */
		placed = fm_desktop_cell_rect(desk->places[index].column, desk->places[index].row, canvas->width, canvas->height, &cell);
		if (!placed)
			continue;

		/* The item. */
		desktop_item(app, canvas, &tab->listing.entries[index], &cell);
		cells++;

		/* Where it is, for the tests. */
		if (logging)
			fm_log("DESKTOP place name=%s column=%d row=%d x=%d y=%d", tab->listing.entries[index].name, desk->places[index].column, desk->places[index].row, cell.x, cell.y);
	}

	/* The rubber band over the items. */
	if (desk->band) {
		desktop_band_rect(desk, &band);
		fm_canvas_fill(canvas, &band, DESKTOP_BAND_FILL);
		fm_canvas_round_border(canvas, (float)band.x, (float)band.y, (float)band.width, (float)band.height, 0.0f, 1.0f, DESKTOP_BAND_EDGE);
	}

	/* The summary line, once for the layout. */
	if (logging) {
		fm_log("DESKTOP ready items=%lu cells=%d width=%d height=%d", (unsigned long)tab->listing.count, cells, canvas->width, canvas->height);
		desk->logged = (int)tab->listing.count + 1;
	}

	/* The frame is drawn. */
	app->dirty = 0;
}

/*
 * Takes an input on the desktop: a button, a motion (the rubber band) or
 * a key.
 */
void
fm_desktop_event(
	struct fm_app *app,
	const struct fm_event *event)
{
	struct fm_desktop *desk;

	/* The pointer's place, for the rubber band. */
	desk = &app->desk;
	desk->pointer_x = event->x;
	desk->pointer_y = event->y;

	/* Each kind of input. */
	switch (event->type) {
	case FM_EVENT_BUTTON:
		/* The left button: a press selects, opens or starts a band; its release ends the band. */
		if (event->button != FM_BUTTON_LEFT)
			break;

		/* A press, or the release of a band. */
		if (event->pressed) {
			desktop_press(app, event);
		} else if (desk->band) {
			desk->band = 0;
			app->dirty = 1;
			fm_log("DESKTOP band end first=%d", fm_select_first(fm_ui_tab(app)));
		}

		/* Nothing more for a button. */
		break;
	case FM_EVENT_MOTION:
		/* A band follows the pointer and selects what it touches. */
		if (desk->band) {
			desktop_band_select(app);
			app->dirty = 1;
		}

		/* Nothing more for a motion. */
		break;
	case FM_EVENT_KEY:
		/* A key press. */
		if (event->pressed)
			desktop_key(app, event);
		break;
	default:
		break;
	}
}

/*
 * Opens the selected items (no more than DESKTOP_OPEN_MAX): each file
 * with its default way, each folder in a new Files window.
 */
void
fm_desktop_open_selected(
	struct fm_app *app)
{
	struct fm_tab *tab;
	size_t index;
	int opened;

	/* Each selected item. */
	tab = fm_ui_tab(app);
	opened = 0;
	for (index = 0; index < tab->listing.count; index++) {
		/* An item not selected, or one too many. */
		if (tab->listing.entries[index].selected == 0)
			continue;
		if (opened == DESKTOP_OPEN_MAX)
			break;

		/* It opens. */
		desktop_open(app, (int)index);
		opened++;
	}
}

/*
 * Finds the item whose cell is at a point of the desktop; -1 for none.
 */
int
fm_desktop_item_at(
	struct fm_app *app,
	int x,
	int y)
{
	struct fm_desktop *desk;
	struct fm_rect cell;
	size_t index;
	int placed;

	/* Each placed item's cell. */
	desk = &app->desk;
	for (index = 0; index < desk->place_count; index++) {
		/* An item without a cell. */
		placed = fm_desktop_cell_rect(desk->places[index].column, desk->places[index].row, desk->width, desk->height, &cell);
		if (!placed)
			continue;

		/* The point in its cell. */
		if (x >= cell.x && x < cell.x + cell.width && y >= cell.y && y < cell.y + cell.height)
			return (int)index;
	}

	/* No item there. */
	return -1;
}

/* Works out the items' places for the desktop's size and listing when either changed (the saved places read once). */
static void
desktop_layout(
	struct fm_app *app,
	int width,
	int height)
{
	struct fm_desktop_place *places;
	struct fm_desktop *desk;
	struct fm_tab *tab;
	const char **names;
	char path[FM_PATH_MAX];
	size_t index;
	int error;

	/* The saved places, once. */
	desk = &app->desk;
	tab = fm_ui_tab(app);
	if (!desk->loaded) {
		desk->loaded = 1;
		error = fm_desktop_layout_path(path, sizeof(path));
		if (error == 0)
			error = fm_desktop_layout_read(path, &desk->saved, &desk->saved_count);
		fm_log("DESKTOP layout saved=%lu error=%d", (unsigned long)desk->saved_count, error);
	}

	/* An unchanged size and listing keep their places. */
	if (desk->width == width &&
	    desk->height == height &&
	    desk->laid_count == tab->listing.count &&
	    desk->laid_modified == tab->listing.modified &&
	    desk->place_count == tab->listing.count)
		return;

	/* Room for the places and the names. */
	places = realloc(desk->places, (tab->listing.count + 1U) * sizeof(places[0]));
	if (places == NULL)
		return;
	desk->places = places;
	names = malloc((tab->listing.count + 1U) * sizeof(names[0]));
	if (names == NULL)
		return;

	/* The names in the listing's order, placed. */
	for (index = 0; index < tab->listing.count; index++)
		names[index] = tab->listing.entries[index].name;
	fm_desktop_arrange(names, tab->listing.count, desk->saved, desk->saved_count, width, height, desk->places);
	free(names);

	/* The places are for this size and listing. */
	desk->place_count = tab->listing.count;
	desk->width = width;
	desk->height = height;
	desk->laid_count = tab->listing.count;
	desk->laid_modified = tab->listing.modified;
	desk->logged = 0;
}

/* Draws one item in its cell: the ground of a selected one, its icon, centred at the top, and its name under it. */
static void
desktop_item(
	struct fm_app *app,
	struct fm_canvas *canvas,
	const struct fm_entry *entry,
	const struct fm_rect *cell)
{
	float left;
	float top;

	/* The icon's place, centred across the cell, a little under its top. */
	left = (float)cell->x + (float)(cell->width - DESKTOP_ICON) / 2.0f;
	top = (float)cell->y + (float)DESKTOP_ICON_TOP;

	/* A selected item's light ground behind its icon. */
	if (entry->selected)
		fm_canvas_round(canvas, left - 6.0f, top - 4.0f, (float)DESKTOP_ICON + 12.0f, (float)DESKTOP_ICON + 8.0f, 10.0f, DESKTOP_GROUND_COLOR);

	/* The icon, and the name under it. */
	fm_grid_entry_icon(app, canvas, entry, left, top, (float)DESKTOP_ICON);
	desktop_name(app, canvas, entry->name, cell, entry->selected);
}

/*
 * Draws an item's name centred under its icon, cut to the cell with an
 * ellipsis: white on a blue pill when selected, otherwise dark over a soft
 * white halo (the text drawn around it a pixel each way).
 */
static void
desktop_name(
	struct fm_app *app,
	struct fm_canvas *canvas,
	const char *name,
	const struct fm_rect *cell,
	int selected)
{
	int available;
	int width;
	int left;
	int baseline;
	int dx;
	int dy;

	/* The width the name takes, no more than the cell's. */
	available = cell->width - 8;
	width = fm_text_width(app->text, name, strlen(name), DESKTOP_TEXT, 0);
	if (width > available)
		width = available;

	/* Centred across the cell, under the icon. */
	left = cell->x + (cell->width - width) / 2;
	baseline = cell->y + DESKTOP_TEXT_BASELINE;

	/* A selected name: white on its pill. */
	if (selected) {
		fm_canvas_round(canvas, (float)left - 5.0f, (float)baseline - 13.0f, (float)width + 10.0f, 18.0f, 9.0f, DESKTOP_PILL_COLOR);
		(void)fm_text_draw_fit(app->text, canvas, left, baseline, name, DESKTOP_TEXT, 0, available, DESKTOP_PILL_TEXT);
		return;
	}

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

/* Works out the rubber band's rectangle, from where it started to the pointer. */
static void
desktop_band_rect(
	const struct fm_desktop *desk,
	struct fm_rect *rect)
{
	/* The left edge and the width, whichever way the pointer went. */
	rect->x = desk->band_x;
	rect->width = desk->pointer_x - desk->band_x;
	if (rect->width < 0) {
		rect->x = desk->pointer_x;
		rect->width = -rect->width;
	}

	/* The top edge and the height. */
	rect->y = desk->band_y;
	rect->height = desk->pointer_y - desk->band_y;
	if (rect->height < 0) {
		rect->y = desk->pointer_y;
		rect->height = -rect->height;
	}
}

/* Selects the items whose cells the rubber band touches, and only those. */
static void
desktop_band_select(
	struct fm_app *app)
{
	struct fm_desktop *desk;
	struct fm_rect band;
	struct fm_rect cell;
	struct fm_tab *tab;
	size_t index;
	int placed;
	int meets;

	/* The band. */
	desk = &app->desk;
	tab = fm_ui_tab(app);
	desktop_band_rect(desk, &band);

	/* Each item's mark: whether its cell meets the band. */
	for (index = 0; index < tab->listing.count && index < desk->place_count; index++) {
		/* An item without a cell is not selected. */
		tab->listing.entries[index].selected = 0;
		placed = fm_desktop_cell_rect(desk->places[index].column, desk->places[index].row, desk->width, desk->height, &cell);
		if (!placed)
			continue;

		/* Its cell and the band. */
		meets = desktop_rects_meet(&band, &cell);
		if (meets)
			tab->listing.entries[index].selected = 1;
	}
}

/* Handles a left press: a double click opens, a click selects, a press where no item is starts a rubber band. */
static void
desktop_press(
	struct fm_app *app,
	const struct fm_event *event)
{
	struct fm_desktop *desk;
	struct fm_tab *tab;
	int index;

	/* The item under the press. */
	desk = &app->desk;
	tab = fm_ui_tab(app);
	index = fm_desktop_item_at(app, event->x, event->y);
	app->dirty = 1;

	/* No item: the selection goes (Ctrl keeps it) and a rubber band starts. */
	if (index < 0) {
		if ((event->modifiers & FM_MOD_CTRL) == 0U)
			fm_select_none(tab);
		desk->band = 1;
		desk->band_x = event->x;
		desk->band_y = event->y;
		desk->click_index = -1;
		fm_log("DESKTOP band start x=%d y=%d", event->x, event->y);
		return;
	}

	/* A second press on the same item soon after the first opens the selection. */
	if (index == desk->click_index && event->time - desk->click_ms <= DESKTOP_DOUBLE_CLICK_MS) {
		desk->click_index = -1;
		fm_select_only(tab, index);
		fm_log("DESKTOP open name=%s via=double-click", tab->listing.entries[index].name);
		fm_desktop_open_selected(app);
		return;
	}

	/* A first click, remembered for a double click. */
	desk->click_index = index;
	desk->click_ms = event->time;

	/* Ctrl adds or takes away, Shift selects from the anchor, a plain click selects the item alone. */
	if ((event->modifiers & FM_MOD_CTRL) != 0U) {
		fm_select_toggle(tab, index);
	} else if ((event->modifiers & FM_MOD_SHIFT) != 0U) {
		fm_select_range(tab, tab->anchor, index);
	} else {
		fm_select_only(tab, index);
	}

	/* The log line the tests read. */
	fm_log("DESKTOP select name=%s selected=%d", tab->listing.entries[index].name, tab->listing.entries[index].selected);
}

/* Handles a key: Enter opens, the arrows move, Ctrl+A selects all, Esc nothing. */
static void
desktop_key(
	struct fm_app *app,
	const struct fm_event *event)
{
	struct fm_tab *tab;

	/* Each key. */
	tab = fm_ui_tab(app);
	app->dirty = 1;
	switch (event->key) {
	case DESKTOP_KEY_ENTER:
	case DESKTOP_KEY_KPENTER:
		/* The selection opens. */
		fm_log("DESKTOP open via=enter");
		fm_desktop_open_selected(app);
		break;
	case DESKTOP_KEY_A:
		/* Ctrl+A selects all. */
		if ((event->modifiers & FM_MOD_CTRL) != 0U)
			fm_select_all(tab);
		break;
	case DESKTOP_KEY_ESC:
		/* Esc selects nothing. */
		fm_select_none(tab);
		break;
	case DESKTOP_KEY_UP:
		desktop_arrow(app, 0, -1);
		break;
	case DESKTOP_KEY_DOWN:
		desktop_arrow(app, 0, 1);
		break;
	case DESKTOP_KEY_LEFT:
		desktop_arrow(app, -1, 0);
		break;
	case DESKTOP_KEY_RIGHT:
		desktop_arrow(app, 1, 0);
		break;
	default:
		break;
	}
}

/*
 * Moves the selection to the nearest item in a direction (dx, dy: -1, 0 or
 * 1 on the screen) from the cursor; without a cursor, the first item.
 */
static void
desktop_arrow(
	struct fm_app *app,
	int dx,
	int dy)
{
	struct fm_desktop *desk;
	struct fm_rect from;
	struct fm_rect cell;
	struct fm_tab *tab;
	size_t index;
	long distance;
	long best_distance;
	int best;
	int ahead_x;
	int ahead_y;
	int placed;

	/* The cursor's cell; without one the first item is selected. */
	desk = &app->desk;
	tab = fm_ui_tab(app);
	if (tab->cursor < 0 || (size_t)tab->cursor >= desk->place_count) {
		fm_select_only(tab, 0);
		return;
	}

	/* The cursor's cell must be on the desktop. */
	placed = fm_desktop_cell_rect(desk->places[tab->cursor].column, desk->places[tab->cursor].row, desk->width, desk->height, &from);
	if (!placed)
		return;

	/* The nearest item whose cell is that way. */
	best = -1;
	best_distance = 0;
	for (index = 0; index < desk->place_count; index++) {
		/* An item without a cell. */
		placed = fm_desktop_cell_rect(desk->places[index].column, desk->places[index].row, desk->width, desk->height, &cell);
		if (!placed)
			continue;

		/* How far it is along the direction; it must be ahead. */
		ahead_x = (cell.x - from.x) * dx;
		ahead_y = (cell.y - from.y) * dy;
		if (ahead_x + ahead_y <= 0)
			continue;

		/* The distance, the sideways part counting double. */
		distance = (long)(ahead_x + ahead_y) + 2L * (long)abs((cell.x - from.x) * dy + (cell.y - from.y) * dx);
		if (best < 0 || distance < best_distance) {
			best = (int)index;
			best_distance = distance;
		}
	}

	/* The item that way becomes the selection. */
	if (best >= 0) {
		fm_select_only(tab, best);
		fm_log("DESKTOP select name=%s selected=1 via=arrow", tab->listing.entries[best].name);
	}
}

/* Opens one item: a folder in a new Files window, a file with its default way (WS093). */
static void
desktop_open(
	struct fm_app *app,
	int index)
{
	char *arguments[3];
	struct fm_entry *entry;
	struct fm_tab *tab;
	int error;

	/* The item. */
	tab = fm_ui_tab(app);
	entry = &tab->listing.entries[index];

	/* A file opens with its default way. */
	if (entry->folder == 0) {
		fm_open_entry(app, index, 0);
		return;
	}

	/* A folder opens in a new Files window. */
	arguments[0] = DESKTOP_FILES;
	arguments[1] = entry->path;
	arguments[2] = NULL;
	error = fm_apps_spawn(arguments);
	fm_log("DESKTOP open-folder path=%s error=%d", entry->path, error);
}

/* Tells whether two rectangles overlap. */
static int
desktop_rects_meet(
	const struct fm_rect *a,
	const struct fm_rect *b)
{
	/* Apart across. */
	if (a->x + a->width <= b->x || b->x + b->width <= a->x)
		return 0;

	/* Apart down. */
	if (a->y + a->height <= b->y || b->y + b->height <= a->y)
		return 0;

	/* They meet. */
	return 1;
}
