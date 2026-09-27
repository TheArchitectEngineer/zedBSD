/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The tabs of a window (spec §30, design §3).  A new tab opens beside the
 * one shown, at the place given; closing the last tab closes the window.
 * While the window has two tabs or more, the tabs stand on the top edge of
 * the content panel, whose place they switch: the shown tab is cut from
 * the panel's own surface and flows into it, the others are quiet labels
 * beside it.  The sidebar and the preview do not move.
 */

#include "files.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The bar's height, and the room above the tabs inside it. */
#define TABS_HEIGHT		34
#define TABS_ABOVE		3

/* How far the first tab starts inside the panel's left end (past its rounded corner). */
#define TABS_INSET		20

/* A tab's widest and narrowest, its top corners' radius and the radius of the feet that join it to the panel. */
#define TABS_WIDEST		220
#define TABS_NARROWEST		96
#define TABS_RADIUS		10.0f
#define TABS_FOOT		7.0f

/* The segments of a quarter circle in a tab's outline. */
#define TABS_ARC_STEPS		6

/* The most corners a tab's outline has: four quarter circles and the two points under it. */
#define TABS_OUTLINE_POINTS	(4 * (TABS_ARC_STEPS + 1) + 2)

/* The close button's size and the padding inside a tab. */
#define TABS_CLOSE		18
#define TABS_PADDING		14

/* The text's size. */
#define TABS_TEXT		12U

/* A quarter of a turn, in radians. */
#define TABS_QUARTER		1.57079632679f

/* The colour of a quiet tab under the pointer, and of the separators between quiet tabs. */
#define TABS_COLOR_HOVER	FM_RGBA(0xffffff, 120)
#define TABS_COLOR_SEPARATOR	FM_RGBA(0x5a6b85, 60)

static void tabs_leave(struct fm_app *app);
static void tabs_shown(struct fm_app *app);
static void tabs_place(const struct fm_rect *bar, int width, int index, struct fm_rect *tab);
static int tabs_quiet(struct fm_app *app, int index);
static void tabs_draw_separators(struct fm_app *app, struct fm_canvas *canvas, const struct fm_rect *bar, int width, int count);
static int tabs_arc(float *points, int count, float cx, float cy, float radius, float from, float to);
static int tabs_outline(float *points, const struct fm_rect *tab, float grow, float foot);
static void tabs_draw_one(struct fm_app *app, struct fm_canvas *canvas, int index, const struct fm_rect *tab);

/*
 * Opens a new tab beside the one shown, at a place, and shows it.  A
 * window already holding its most tabs says so and stays as it is.
 */
void
fm_tabs_new(
	struct fm_app *app,
	const struct fm_location *location)
{
	struct fm_tab *tab;
	int index;

	/* A window holds eight tabs at most. */
	if (app->tab_count == FM_TABS) {
		fm_ui_message(app, "A window holds at most 8 tabs.");
		return;
	}

	/* The new tab, with nothing selected yet. */
	tab = calloc(1, sizeof(*tab));
	if (tab == NULL) {
		fm_ui_message(app, "There is not enough memory for a new tab.");
		return;
	}

	/* Nothing has the cursor yet. */
	tab->cursor = -1;
	tab->anchor = -1;

	/* It goes right after the one shown, which the tabs after it make room for. */
	index = app->tab_index + 1;
	memmove(&app->tabs[index + 1], &app->tabs[index], sizeof(app->tabs[0]) * (size_t)(app->tab_count - index));
	app->tabs[index] = tab;
	app->tab_count++;

	/* The window leaves what it had open on the old tab, and the new tab goes to the place. */
	tabs_leave(app);
	app->tab_index = index;
	fm_ui_go(app, location);
	fm_log("TABS new index=%d count=%d", app->tab_index, app->tab_count);
}

/*
 * Opens a new tab at the place the shown tab is at.
 */
void
fm_tabs_duplicate(
	struct fm_app *app)
{
	struct fm_location location;
	struct fm_tab *tab;

	/* The place of the tab shown, copied before the tab list moves. */
	tab = fm_ui_tab(app);
	location = tab->history[tab->history_index].location;
	fm_tabs_new(app, &location);
}

/*
 * Closes a tab.  The last tab closes the window instead; otherwise the tab
 * to its right is shown (or the one to its left when it was the last).
 */
void
fm_tabs_close(
	struct fm_app *app,
	int index)
{
	struct fm_tab *tab;

	/* A tab that is not there. */
	if (index < 0 || index >= app->tab_count)
		return;

	/* The last tab closes the window. */
	if (app->tab_count == 1) {
		app->request = FM_REQUEST_CLOSE;
		return;
	}

	/* The tab and its listing go, and the tabs after it move down. */
	tab = app->tabs[index];
	fm_dir_free(&tab->listing);
	free(tab);
	memmove(&app->tabs[index], &app->tabs[index + 1], sizeof(app->tabs[0]) * (size_t)(app->tab_count - index - 1));
	app->tab_count--;
	app->tabs[app->tab_count] = NULL;

	/* The tab shown stays shown, one place to the left when it was after the closed one. */
	if (app->tab_index > index)
		app->tab_index--;

	/* The closed tab was shown: the one that took its place is (or the last). */
	if (app->tab_index == index) {
		if (app->tab_index == app->tab_count)
			app->tab_index--;
		tabs_shown(app);
	}

	/* The frame changes. */
	app->dirty = 1;
	fm_log("TABS close index=%d count=%d shown=%d", index, app->tab_count, app->tab_index);
}

/*
 * Shows a tab.
 */
void
fm_tabs_select(
	struct fm_app *app,
	int index)
{
	/* A tab that is not there, or the one already shown. */
	if (index < 0 || index >= app->tab_count || index == app->tab_index)
		return;

	/* The tab, read again. */
	app->tab_index = index;
	tabs_shown(app);
	app->dirty = 1;
	fm_log("TABS select index=%d count=%d", app->tab_index, app->tab_count);
}

/*
 * Shows the next tab (step 1) or the previous (step -1), going around at
 * the ends.
 */
void
fm_tabs_step(
	struct fm_app *app,
	int step)
{
	int index;

	/* One tab has nothing beside it. */
	if (app->tab_count < 2)
		return;

	/* The tab beside the one shown, around the ends. */
	index = (app->tab_index + step + app->tab_count) % app->tab_count;
	fm_tabs_select(app, index);
}

/*
 * Places the tab bar on the top edge of the content panel, when the window
 * has two tabs or more, and returns how far the content panel moves down
 * under it.  The sidebar and the preview keep their places: the tabs
 * belong to the content they switch.
 */
int
fm_tabs_layout(
	struct fm_app *app,
	int top,
	int left,
	int right)
{
	struct fm_rect *bar;

	/* No bar for one tab. */
	bar = &app->layout.tabbar;
	memset(bar, 0, sizeof(*bar));
	if (app->tab_count < 2)
		return 0;

	/* The bar over the content panel, its tabs standing on the panel's top edge. */
	bar->x = left;
	bar->y = top;
	bar->width = right - left;
	bar->height = TABS_HEIGHT;
	return TABS_HEIGHT;
}

/*
 * Draws the tab bar, when there is one, after the content panel it stands
 * on.  The tabs share the bar's width up to their widest; the shown one is
 * of the panel's surface and joins it, the others are quiet labels on the
 * window's ground with thin separators between them.
 */
void
fm_tabs_draw(
	struct fm_app *app,
	struct fm_canvas *canvas)
{
	const struct fm_rect *bar;
	struct fm_rect tab;
	int width;
	int count;
	int index;

	/* No bar for one tab. */
	bar = &app->layout.tabbar;
	if (bar->width <= 0)
		return;

	/* The tabs share the bar inside the panel's rounded corners, within their widest and narrowest. */
	width = (bar->width - 2 * TABS_INSET) / app->tab_count;
	if (width > TABS_WIDEST)
		width = TABS_WIDEST;
	if (width < TABS_NARROWEST)
		width = TABS_NARROWEST;

	/* How many tabs the bar has room for. */
	count = (bar->width - 2 * TABS_INSET) / width;
	if (count > app->tab_count)
		count = app->tab_count;

	/* The quiet tabs first, left to right. */
	for (index = 0; index < count; index++) {
		if (index == app->tab_index)
			continue;
		tabs_place(bar, width, index, &tab);
		tabs_draw_one(app, canvas, index, &tab);
	}

	/* The separators between quiet tabs. */
	tabs_draw_separators(app, canvas, bar, width, count);

	/* The shown tab last, over its neighbours' ends and the panel's top edge. */
	if (app->tab_index < count) {
		tabs_place(bar, width, app->tab_index, &tab);
		tabs_draw_one(app, canvas, app->tab_index, &tab);
	}
}

/*
 * Makes a tab the one shown: whatever was open on the tab before closes
 * and the tab's place is read again, since it may have changed while the
 * tab was hidden.
 */
static void
tabs_shown(
	struct fm_app *app)
{
	/* What was open closes. */
	tabs_leave(app);

	/* The tab's place read again (a search tab's search starts again, another's stops). */
	fm_ui_reload(app, fm_ui_tab(app));
}

/*
 * Closes what was open on the tab being left: Quick Look, the information
 * and a name being changed; typing goes back to the items.
 */
static void
tabs_leave(
	struct fm_app *app)
{
	/* A name being changed keeps what was typed. */
	if (app->focus == FM_FOCUS_RENAME)
		fm_action_rename_end(app, 1);

	/* Quick Look and the information close, and typing goes to the items. */
	fm_look_close(app);
	fm_info_close(app);
	app->focus = FM_FOCUS_CONTENT;
	app->band = 0;
	app->typed_length = 0;
}

/* Places one tab of the bar, all tabs being the same width. */
static void
tabs_place(
	const struct fm_rect *bar,
	int width,
	int index,
	struct fm_rect *tab)
{
	/* The tab's slot, counted from the bar's inset left end, standing on the bar's bottom. */
	tab->x = bar->x + TABS_INSET + index * width;
	tab->y = bar->y + TABS_ABOVE;
	tab->width = width;
	tab->height = bar->height - TABS_ABOVE;
}

/* Tells whether a tab is quiet: neither the one shown nor under the pointer. */
static int
tabs_quiet(
	struct fm_app *app,
	int index)
{
	/* The shown tab stands out. */
	if (index == app->tab_index)
		return 0;

	/* A tab (or its close button) under the pointer is lit. */
	if (app->hover_kind == FM_HIT_TAB && app->hover_index == index)
		return 0;
	if (app->hover_kind == FM_HIT_TAB_CLOSE && app->hover_index == index)
		return 0;

	/* The rest are quiet. */
	return 1;
}

/* Draws the thin separators between neighbouring quiet tabs. */
static void
tabs_draw_separators(
	struct fm_app *app,
	struct fm_canvas *canvas,
	const struct fm_rect *bar,
	int width,
	int count)
{
	struct fm_rect tab;
	struct fm_rect line;
	int quiet_left;
	int quiet_right;
	int index;

	/* Each boundary between one tab and the next. */
	for (index = 0; index + 1 < count; index++) {
		/* A separator stands only between two quiet tabs; a lit or shown tab needs none. */
		quiet_left = tabs_quiet(app, index);
		quiet_right = tabs_quiet(app, index + 1);
		if (quiet_left == 0 || quiet_right == 0)
			continue;

		/* A short line at the start of the right-hand tab, down the middle of its height. */
		tabs_place(bar, width, index + 1, &tab);
		line.x = tab.x;
		line.y = tab.y + 8;
		line.width = 1;
		line.height = tab.height - 14;
		fm_canvas_fill(canvas, &line, TABS_COLOR_SEPARATOR);
	}
}

/*
 * Adds a quarter circle to an outline, from one angle to another (radians,
 * y growing downwards), and returns the outline's new count of points.
 */
static int
tabs_arc(
	float *points,
	int count,
	float cx,
	float cy,
	float radius,
	float from,
	float to)
{
	float angle;
	int step;

	/* The points along the arc, both ends included. */
	for (step = 0; step <= TABS_ARC_STEPS; step++) {
		angle = from + (to - from) * (float)step / (float)TABS_ARC_STEPS;
		points[2 * count] = cx + radius * cosf(angle);
		points[2 * count + 1] = cy + radius * sinf(angle);
		count++;
	}

	/* The outline so far. */
	return count;
}

/*
 * Makes the outline of a tab joined to the panel under it and returns its
 * count of points.  Its top corners are rounded and its feet curve out
 * into the panel's top edge.  An inset of one pixel gives the tab's
 * surface inside its one-pixel edge, reaching one pixel into the panel so
 * that it covers the panel's own edge under the tab.  A foot of zero
 * leaves the tab's sides straight.
 */
static int
tabs_outline(
	float *points,
	const struct fm_rect *tab,
	float inset,
	float foot)
{
	float left;
	float right;
	float top;
	float bottom;
	float radius;
	int count;

	/* The tab's box, its sides and top moved in by the inset and its foot line moved down by it. */
	left = (float)tab->x + inset;
	right = (float)(tab->x + tab->width) - inset;
	top = (float)tab->y + inset;
	bottom = (float)(tab->y + tab->height);
	radius = TABS_RADIUS - inset;

	/* The left foot, curving from the panel's edge up into the tab's left side. */
	count = 0;
	count = tabs_arc(points, count, (float)tab->x - foot, bottom - foot, foot + inset, TABS_QUARTER, 0.0f);

	/* The rounded top left and top right corners. */
	count = tabs_arc(points, count, left + radius, top + radius, radius, 2.0f * TABS_QUARTER, 3.0f * TABS_QUARTER);
	count = tabs_arc(points, count, right - radius, top + radius, radius, 3.0f * TABS_QUARTER, 4.0f * TABS_QUARTER);

	/* The right foot, curving from the tab's right side down into the panel's edge. */
	count = tabs_arc(points, count, (float)(tab->x + tab->width) + foot, bottom - foot, foot + inset, 2.0f * TABS_QUARTER, TABS_QUARTER);

	/* The outline, closed along the panel's edge. */
	return count;
}

/*
 * Draws one tab: the shown one of the panel's surface and joined to it,
 * a quiet one as a label, lit under the pointer; the name of its place
 * and a close button.
 */
static void
tabs_draw_one(
	struct fm_app *app,
	struct fm_canvas *canvas,
	int index,
	const struct fm_rect *tab)
{
	float outline[2 * TABS_OUTLINE_POINTS];
	const struct fm_location *location;
	const struct fm_tab *shown;
	struct fm_rect close;
	fm_color ink;
	fm_color cross;
	int count;
	int text_width;
	int baseline;
	int hovered;
	int bold;

	/* Whether the tab or its close button is under the pointer. */
	hovered = 0;
	if (app->hover_kind == FM_HIT_TAB && app->hover_index == index)
		hovered = 1;
	if (app->hover_kind == FM_HIT_TAB_CLOSE && app->hover_index == index)
		hovered = 1;

	/* The shown tab: its edge, then the panel's surface inside it, flowing into the panel. */
	ink = FM_COLOR_TEXT_SECONDARY;
	cross = FM_COLOR_TEXT_FAINT;
	bold = 0;
	if (index == app->tab_index) {
		count = tabs_outline(outline, tab, 0.0f, TABS_FOOT);
		fm_canvas_polygon(canvas, outline, count, FM_COLOR_PANEL_EDGE);
		count = tabs_outline(outline, tab, 1.0f, TABS_FOOT);
		fm_canvas_polygon(canvas, outline, count, FM_COLOR_PANEL);
		ink = FM_COLOR_TEXT;
		cross = FM_COLOR_TEXT_SECONDARY;
		bold = 1;
	} else if (hovered != 0) {
		count = tabs_outline(outline, tab, 1.0f, 0.0f);
		fm_canvas_polygon(canvas, outline, count, TABS_COLOR_HOVER);
		ink = FM_COLOR_TEXT;
		cross = FM_COLOR_TEXT_SECONDARY;
	}

	/* The whole tab is clickable as the tab. */
	fm_ui_hit(app, tab, FM_HIT_TAB, index);

	/* The name of the tab's place, cut to fit before the close button. */
	shown = app->tabs[index];
	location = &shown->history[shown->history_index].location;
	text_width = tab->width - 2 * TABS_PADDING - TABS_CLOSE - 4;
	baseline = fm_text_center(TABS_TEXT, tab->y, tab->height);
	(void)fm_text_draw_fit(app->text, canvas, tab->x + TABS_PADDING, baseline, fm_location_name(location, app->home), TABS_TEXT, bold, text_width, ink);

	/* The close button at the right end, lit under the pointer. */
	close.x = tab->x + tab->width - TABS_PADDING - TABS_CLOSE + 6;
	close.y = tab->y + (tab->height - TABS_CLOSE) / 2;
	close.width = TABS_CLOSE;
	close.height = TABS_CLOSE;
	if (app->hover_kind == FM_HIT_TAB_CLOSE && app->hover_index == index)
		fm_canvas_circle(canvas, (float)close.x + TABS_CLOSE / 2.0f, (float)close.y + TABS_CLOSE / 2.0f, TABS_CLOSE / 2.0f, FM_COLOR_HOVER);
	fm_icon_draw(canvas, FM_ICON_CLOSE, (float)close.x + 4.0f, (float)close.y + 4.0f, (float)(TABS_CLOSE - 8), cross);
	fm_ui_hit(app, &close, FM_HIT_TAB_CLOSE, index);
}
