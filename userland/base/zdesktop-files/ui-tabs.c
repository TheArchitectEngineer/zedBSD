/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The tabs of a window (spec §30, design §3).  A new tab opens beside the
 * one shown, at the place given; closing the last tab closes the window.
 * While the window has two tabs or more, a bar of pill-shaped tabs runs
 * across the top of the window and the panels move down under it.
 */

#include "files.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The bar's height and the space under it. */
#define TABS_HEIGHT		34
#define TABS_BELOW		10

/* A tab's widest and narrowest, the gap between tabs, and the corners' radius. */
#define TABS_WIDEST		220
#define TABS_NARROWEST		90
#define TABS_GAP		6
#define TABS_RADIUS		12.0f

/* The close button's size and the padding inside a tab. */
#define TABS_CLOSE		18
#define TABS_PADDING		12

/* The text's size. */
#define TABS_TEXT		12U

/* The colours of a tab that is not shown and of one under the pointer. */
#define TABS_COLOR_IDLE		FM_RGBA(0xffffff, 110)
#define TABS_COLOR_HOVER	FM_RGBA(0xffffff, 170)

static void tabs_leave(struct fm_app *app);
static void tabs_shown(struct fm_app *app);
static void tabs_draw_one(struct fm_app *app, struct fm_canvas *canvas, int index, const struct fm_rect *pill);

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
 * Places the tab bar at the top of the window, when the window has two
 * tabs or more, and returns how far the panels move down for it.
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

	/* The bar across the window. */
	bar->x = left;
	bar->y = top;
	bar->width = right - left;
	bar->height = TABS_HEIGHT;
	return TABS_HEIGHT + TABS_BELOW;
}

/*
 * Draws the tab bar, when there is one: the tabs share its width, up to
 * their widest, the shown one white.
 */
void
fm_tabs_draw(
	struct fm_app *app,
	struct fm_canvas *canvas)
{
	const struct fm_rect *bar;
	struct fm_rect pill;
	int width;
	int index;

	/* No bar for one tab. */
	bar = &app->layout.tabbar;
	if (bar->width <= 0)
		return;

	/* The tabs share the bar, within their widest and narrowest. */
	width = (bar->width - TABS_GAP * (app->tab_count - 1)) / app->tab_count;
	if (width > TABS_WIDEST)
		width = TABS_WIDEST;
	if (width < TABS_NARROWEST)
		width = TABS_NARROWEST;

	/* Each tab, left to right, as far as the bar goes. */
	for (index = 0; index < app->tab_count; index++) {
		pill.x = bar->x + index * (width + TABS_GAP);
		pill.y = bar->y;
		pill.width = width;
		pill.height = bar->height;
		if (pill.x + pill.width > bar->x + bar->width)
			break;
		tabs_draw_one(app, canvas, index, &pill);
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

/*
 * Draws one tab: a pill with the name of its place, and a close button.
 */
static void
tabs_draw_one(
	struct fm_app *app,
	struct fm_canvas *canvas,
	int index,
	const struct fm_rect *pill)
{
	const struct fm_location *location;
	const struct fm_tab *tab;
	struct fm_rect close;
	fm_color color;
	int text_width;
	int baseline;

	/* The shown tab is white and raised, one under the pointer lighter than the rest. */
	color = TABS_COLOR_IDLE;
	if (app->hover_kind == FM_HIT_TAB && app->hover_index == index)
		color = TABS_COLOR_HOVER;
	if (index == app->tab_index) {
		color = FM_COLOR_PANEL;
		fm_canvas_shadow(canvas, (float)pill->x, (float)pill->y + 2.0f, (float)pill->width, (float)pill->height, TABS_RADIUS, 8.0f, FM_COLOR_SHADOW);
	}

	/* The pill and its edge, clickable as the tab. */
	fm_canvas_round(canvas, (float)pill->x, (float)pill->y, (float)pill->width, (float)pill->height, TABS_RADIUS, color);
	fm_canvas_round_border(canvas, (float)pill->x, (float)pill->y, (float)pill->width, (float)pill->height, TABS_RADIUS, 1.0f, FM_COLOR_PANEL_EDGE);
	fm_ui_hit(app, pill, FM_HIT_TAB, index);

	/* The name of the tab's place, cut to fit before the close button. */
	tab = app->tabs[index];
	location = &tab->history[tab->history_index].location;
	text_width = pill->width - 2 * TABS_PADDING - TABS_CLOSE - 4;
	baseline = fm_text_center(TABS_TEXT, pill->y, pill->height);
	(void)fm_text_draw_fit(app->text, canvas, pill->x + TABS_PADDING, baseline, fm_location_name(location, app->home), TABS_TEXT, index == app->tab_index, text_width, FM_COLOR_TEXT);

	/* The close button at the right end, lit under the pointer. */
	close.x = pill->x + pill->width - TABS_PADDING - TABS_CLOSE + 4;
	close.y = pill->y + (pill->height - TABS_CLOSE) / 2;
	close.width = TABS_CLOSE;
	close.height = TABS_CLOSE;
	if (app->hover_kind == FM_HIT_TAB_CLOSE && app->hover_index == index)
		fm_canvas_circle(canvas, (float)close.x + TABS_CLOSE / 2.0f, (float)close.y + TABS_CLOSE / 2.0f, TABS_CLOSE / 2.0f, FM_COLOR_HOVER);
	fm_icon_draw(canvas, FM_ICON_CLOSE, (float)close.x + 3.0f, (float)close.y + 3.0f, (float)(TABS_CLOSE - 6), FM_COLOR_TEXT_SECONDARY);
	fm_ui_hit(app, &close, FM_HIT_TAB_CLOSE, index);
}
