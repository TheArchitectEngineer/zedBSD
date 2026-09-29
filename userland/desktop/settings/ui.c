/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The interface of Settings (plan/ws089/design.md section 3): the list of
 * pages on the left and the page on the right, each a card floating on
 * zdesktop's frosted glass; the history of pages that the titlebar's Back,
 * Forward, Home and breadcrumb walk; the pointer, the wheel and the keys.
 *
 * A frame is drawn whole whenever something changed.  Drawing records the
 * clickable regions, and the next input is matched against them.
 */

#include "settings.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/*
 * On glass the panes reach the window's edges, so that their outer edges
 * line up with the floating titlebar's, and stand apart by zdesktop's gap
 * between the titlebar and the window; docked, they keep that gap from the
 * screen's edges too (the file manager's measurements, ws071-p017).
 * Without glass they keep a margin on the window's own ground.
 */
#define UI_MARGIN		12
#define UI_GAP			10
#define UI_GLASS_GAP		8

/* The list of pages' width, narrower in a narrow window, and the width that counts as narrow. */
#define UI_SIDEBAR_WIDTH	248
#define UI_SIDEBAR_NARROW	208
#define UI_NARROW_WINDOW	900

/* The panes' corners, and the list's rows and the lines between its groups. */
#define UI_PANEL_RADIUS		16.0f
#define UI_ROW_HEIGHT		34
#define UI_ROW_GAP		2
#define UI_GROUP_GAP		13

/* The page's margins inside its pane, and the space between its header and its cards. */
#define UI_PAGE_SIDE		32
#define UI_PAGE_TOP		28
#define UI_PAGE_BOTTOM		32
#define UI_PAGE_HEADER_GAP	20

/* The text size of the list. */
#define UI_TEXT_ROW		15U

/* How far a key's page step leaves of the page on screen, in pixels. */
#define UI_PAGE_OVERLAP		60

/* How far the arrows scroll the page, in pixels. */
#define UI_KEY_STEP		48

static void ui_layout(struct se_app *app);
static void ui_draw_sidebar(struct se_app *app, struct fm_canvas *canvas);
static void ui_reveal(struct se_app *app, const struct fm_rect *current);
static void ui_draw_page(struct se_app *app, struct fm_canvas *canvas);
static int ui_hit_at(struct se_app *app, int x, int y, unsigned *kind, int *index);
static void ui_motion(struct se_app *app, const struct se_event *event);
static void ui_button(struct se_app *app, const struct se_event *event);
static void ui_click(struct se_app *app, unsigned kind, int index);
static void ui_scroll(struct se_app *app, const struct se_event *event);
static void ui_key(struct se_app *app, const struct se_event *event);
static void ui_step_page(struct se_app *app, int direction);
static void ui_scroll_page(struct se_app *app, int amount);
static void ui_back(struct se_app *app);
static void ui_forward(struct se_app *app);
static int ui_clamp(int value, int minimum, int maximum);

/*
 * Makes the interface ready, showing a page first.
 */
void
se_ui_init(
	struct se_app *app,
	struct fm_text *text,
	unsigned page)
{
	/* A page outside the table is Home. */
	if (page >= SE_PAGES)
		page = SE_PAGE_HOME;

	/* The fonts, the list shown, the focus, and nothing under the pointer. */
	app->text = text;
	app->show_sidebar = 1;
	app->focused = 1;
	app->hover_kind = SE_HIT_NONE;
	app->hover_index = -1;
	app->press_kind = SE_HIT_NONE;
	app->press_index = -1;

	/* The history starts at the first page, which the list shows. */
	app->page = page;
	app->reveal = 1;
	app->history[0] = page;
	app->history_count = 1;
	app->history_index = 0;

	/* The first frame is due. */
	app->dirty = 1;
	se_log("PAGE %s", se_pages[page].word);
}

/*
 * Carries out one input from the window.
 */
void
se_ui_event(
	struct se_app *app,
	const struct se_event *event)
{
	/* The time of the input, for whatever measures time. */
	app->now = event->time;

	/* Each kind of input. */
	switch (event->type) {
	case SE_EVENT_MOTION:
		ui_motion(app, event);
		break;
	case SE_EVENT_BUTTON:
		ui_button(app, event);
		break;
	case SE_EVENT_AXIS:
		ui_scroll(app, event);
		break;
	case SE_EVENT_LEAVE:
		/* Nothing is lit once the pointer is gone. */
		app->hover_kind = SE_HIT_NONE;
		app->hover_index = -1;
		app->dirty = 1;
		break;
	case SE_EVENT_KEY:
		ui_key(app, event);
		break;
	case SE_EVENT_FOCUS:
		/* The chosen page's row is drawn in the accent only while the window has the focus. */
		app->focused = event->focused;
		app->dirty = 1;
		break;
	case SE_EVENT_ACTION:
		se_ui_action(app, event->action);
		break;
	default:
		break;
	}
}

/*
 * Carries out an action of the menus or the keys.
 */
void
se_ui_action(
	struct se_app *app,
	uint32_t action)
{
	/* A page of the Go menu. */
	if (action >= SE_ACTION_PAGE_FIRST && action < SE_ACTION_PAGE_FIRST + SE_PAGES) {
		se_ui_go(app, action - SE_ACTION_PAGE_FIRST);
		return;
	}

	/* Each other action. */
	switch (action) {
	case SE_ACTION_CLOSE_WINDOW:
	case SE_ACTION_QUIT:
		app->request = SE_REQUEST_CLOSE;
		break;
	case SE_ACTION_BACK:
		ui_back(app);
		break;
	case SE_ACTION_FORWARD:
		ui_forward(app);
		break;
	case SE_ACTION_HOME:
		se_ui_go(app, SE_PAGE_HOME);
		break;
	case SE_ACTION_SHOW_SIDEBAR:
		/* The list of pages comes or goes; the page takes the room. */
		app->show_sidebar = !app->show_sidebar;
		app->dirty = 1;
		se_log("SIDEBAR shown=%d", app->show_sidebar);
		break;
	case SE_ACTION_MINIMIZE:
		app->request = SE_REQUEST_MINIMIZE;
		break;
	case SE_ACTION_ZOOM:
		app->request = SE_REQUEST_ZOOM;
		break;
	case SE_ACTION_ABOUT:
		se_ui_go(app, SE_PAGE_ABOUT);
		break;
	default:
		break;
	}
}

/*
 * Lets time pass: a page that shows the time since the machine started
 * (About) is drawn again when the minute changes.
 */
void
se_ui_tick(
	struct se_app *app,
	uint64_t now)
{
	uint64_t minute;

	/* The minute now, and the time for whatever is drawn. */
	minute = now / 60000U;
	app->now = now;
	if (minute == app->minute)
		return;

	/* A new minute: About shows it. */
	app->minute = minute;
	if (app->page == SE_PAGE_ABOUT)
		app->dirty = 1;
}

/*
 * Shows a page: the history keeps the page being left, and anything
 * forward of it is dropped (as a browser does).
 */
void
se_ui_go(
	struct se_app *app,
	unsigned page)
{
	int index;

	/* A page outside the table, or the one shown, changes nothing. */
	if (page >= SE_PAGES || page == app->page)
		return;

	/* A full history drops its oldest step. */
	index = app->history_index + 1;
	if (index >= SE_HISTORY) {
		memmove(app->history, app->history + 1, sizeof(app->history[0]) * (SE_HISTORY - 1));
		index = SE_HISTORY - 1;
	}

	/* The page becomes the history's newest step. */
	app->history[index] = page;
	app->history_index = index;
	app->history_count = index + 1;

	/* It is shown from its top. */
	app->page = page;
	app->page_scroll = 0;
	app->reveal = 1;
	app->dirty = 1;
	se_log("PAGE %s", se_pages[page].word);
}

/*
 * Draws a frame of the window into a canvas the window's size, recording
 * its clickable regions.
 */
void
se_ui_draw(
	struct se_app *app,
	struct fm_canvas *canvas)
{
	struct fm_rect whole;

	/*
	 * The frame is being brought up to date; drawing may ask for another
	 * (a scroll it corrected), which the main loop then draws at once.
	 */
	app->dirty = 0;

	/* The panes' places at this size, and no clickable region yet. */
	app->width = canvas->width;
	app->height = canvas->height;
	ui_layout(app);
	app->hit_count = 0;

	/* The window's ground: clear on glass (the desktop shows between the panes), else a quiet light gradient. */
	whole.x = 0;
	whole.y = 0;
	whole.width = canvas->width;
	whole.height = canvas->height;
	if (app->glass != 0) {
		fm_canvas_clear(canvas);
	} else {
		fm_canvas_gradient(canvas, &whole, SE_COLOR_BACKGROUND_TOP, SE_COLOR_BACKGROUND_BOTTOM);
	}

	/* The list of pages, when shown, then the page. */
	if (app->show_sidebar != 0)
		ui_draw_sidebar(app, canvas);
	ui_draw_page(app, canvas);
}

/*
 * Lists the parts of the last frame that stand on zdesktop's glass: the
 * list of pages and the page, into up to capacity panels.  Returns how
 * many there are.
 */
size_t
se_ui_panels(
	struct se_app *app,
	struct se_panel *panels,
	size_t capacity)
{
	const struct fm_rect *cards[2];
	size_t count;
	unsigned index;

	/* The two panes, in the order they are drawn. */
	cards[0] = &app->layout.sidebar;
	cards[1] = &app->layout.page;
	count = 0;

	/* Each pane that is shown (a hidden one has no size). */
	for (index = 0; index < 2U; index++) {
		if (cards[index]->width <= 0 || cards[index]->height <= 0)
			continue;
		if (count == capacity)
			break;
		panels[count].rect = *cards[index];
		panels[count].radius = (int)UI_PANEL_RADIUS;
		panels[count].kind = SE_PANEL_CARD;
		count++;
	}

	/* The panes shown. */
	return count;
}

/*
 * Records a clickable region of the frame being drawn; a region recorded
 * later wins where two overlap.
 */
void
se_ui_hit(
	struct se_app *app,
	const struct fm_rect *rect,
	unsigned kind,
	int index)
{
	/* Regions past the table's size are not clickable (they do not happen in practice). */
	if (app->hit_count == SE_HITS)
		return;

	/* The region, after those drawn before it. */
	app->hits[app->hit_count].rect = *rect;
	app->hits[app->hit_count].kind = kind;
	app->hits[app->hit_count].index = index;
	app->hit_count++;
}

/*
 * Tells whether a region is lit: under the pointer, or pressed.
 */
int
se_ui_lit(
	const struct se_app *app,
	unsigned kind,
	int index)
{
	/* The region under the pointer. */
	if (app->hover_kind == kind && app->hover_index == index)
		return 1;

	/* The region a press is holding down. */
	if (app->press_kind == kind && app->press_index == index)
		return 1;

	/* Neither. */
	return 0;
}

/*
 * Works out what the titlebar shows: the history's steps, the breadcrumb
 * (Settings, then the page) and whether the list of pages is shown.
 */
void
se_ui_titlebar_state(
	struct se_app *app,
	struct se_titlebar_state *state)
{
	/* Nothing yet. */
	memset(state, 0, sizeof(*state));

	/* The history's steps. */
	state->can_back = 0;
	if (app->history_index > 0)
		state->can_back = 1;
	state->can_forward = 0;
	if (app->history_index + 1 < app->history_count)
		state->can_forward = 1;

	/* The breadcrumb: Settings, and the page unless it is Home. */
	(void)snprintf(state->parts[0], sizeof(state->parts[0]), "%s", "Settings");
	state->part_count = 1;
	if (app->page != SE_PAGE_HOME) {
		(void)snprintf(state->parts[1], sizeof(state->parts[1]), "%s", se_pages[app->page].name);
		state->part_count = 2;
	}

	/* The list of pages. */
	state->sidebar = app->show_sidebar;
}

/*
 * Carries out what was done with the titlebar.
 */
void
se_ui_titlebar(
	struct se_app *app,
	const struct se_titlebar_event *event)
{
	/* Only a control chosen does anything yet (the search comes in ws089-p008). */
	if (event->kind != SE_TITLEBAR_ACTIVATED)
		return;

	/* Each control. */
	switch (event->id) {
	case SE_CONTROL_BACK:
		ui_back(app);
		break;
	case SE_CONTROL_FORWARD:
		ui_forward(app);
		break;
	case SE_CONTROL_HOME:
		se_ui_go(app, SE_PAGE_HOME);
		break;
	case SE_CONTROL_PATH:
		/* The breadcrumb's first part is Home; the last is the page shown. */
		if (event->detail == 0U)
			se_ui_go(app, SE_PAGE_HOME);
		break;
	case SE_CONTROL_SIDEBAR:
		se_ui_action(app, SE_ACTION_SHOW_SIDEBAR);
		break;
	default:
		break;
	}
}

/*
 * Works out what the menus show: the history's steps, the list shown and
 * the page shown.
 */
void
se_ui_menu_state(
	struct se_app *app,
	struct se_menu_state *state)
{
	/* Nothing yet. */
	memset(state, 0, sizeof(*state));

	/* The history's steps, the list and the page. */
	if (app->history_index > 0)
		state->can_back = 1;
	if (app->history_index + 1 < app->history_count)
		state->can_forward = 1;
	state->sidebar = app->show_sidebar;
	state->page = app->page;
}

/*
 * Writes one line to standard error, prefixed ZSETTINGS (the tests read
 * these lines).
 */
void
se_log(
	const char *format,
	...)
{
	va_list arguments;

	/* The prefix, the message and the end of the line, at once. */
	fputs("ZSETTINGS ", stderr);
	va_start(arguments, format);
	vfprintf(stderr, format, arguments);
	va_end(arguments);
	fputc('\n', stderr);
	fflush(stderr);
}

/* Works out the panes' places at the window's size. */
static void
ui_layout(
	struct se_app *app)
{
	struct se_layout *layout;
	int margin;
	int gap;
	int left;
	int width;

	/* The margin around the panes and the gap between them: on glass, the titlebar's (see UI_GLASS_GAP). */
	margin = UI_MARGIN;
	gap = UI_GAP;
	if (app->glass != 0) {
		margin = 0;
		if (app->docked != 0)
			margin = UI_GLASS_GAP;
		gap = UI_GLASS_GAP;
	}

	/* The list on the left, when shown, narrower in a narrow window. */
	layout = &app->layout;
	left = margin;
	memset(&layout->sidebar, 0, sizeof(layout->sidebar));
	if (app->show_sidebar != 0) {
		width = UI_SIDEBAR_WIDTH;
		if (app->width < UI_NARROW_WINDOW)
			width = UI_SIDEBAR_NARROW;
		layout->sidebar.x = left;
		layout->sidebar.y = margin;
		layout->sidebar.width = width;
		layout->sidebar.height = app->height - 2 * margin;
		left += width + gap;
	}

	/* The page takes the rest. */
	layout->page.x = left;
	layout->page.y = margin;
	layout->page.width = app->width - margin - left;
	layout->page.height = app->height - 2 * margin;
}

/* Draws the list of pages by group, the page shown lit, scrolled when it is taller than its pane. */
static void
ui_draw_sidebar(
	struct se_app *app,
	struct fm_canvas *canvas)
{
	const struct fm_rect *panel;
	const struct se_page *page;
	struct fm_rect row;
	struct fm_rect current_row;
	fm_color ink;
	fm_color glyph;
	unsigned group;
	unsigned id;
	int current;
	int bold;
	int limit;
	int y;

	/* The pane: a light veil over zdesktop's glass, or a whiter card with a bright edge over the window's ground. */
	panel = &app->layout.sidebar;
	if (app->glass != 0) {
		fm_canvas_round(canvas, (float)panel->x, (float)panel->y, (float)panel->width, (float)panel->height, UI_PANEL_RADIUS, SE_COLOR_GLASS_SIDEBAR);
	} else {
		fm_canvas_round(canvas, (float)panel->x, (float)panel->y, (float)panel->width, (float)panel->height, UI_PANEL_RADIUS, SE_COLOR_PANEL);
		fm_canvas_round_border(canvas, (float)panel->x, (float)panel->y, (float)panel->width, (float)panel->height, UI_PANEL_RADIUS, 1.0f, SE_COLOR_PANEL_EDGE);
	}

	/* The pane itself is a region the wheel scrolls; the rows over it are recorded after it. */
	se_ui_hit(app, panel, SE_HIT_SIDEBAR, 0);

	/* The rows stay inside the pane. */
	fm_canvas_clip_push(canvas, panel);

	/* Each page after Home, a line between two groups; the page shown's row is kept (none for Home). */
	memset(&current_row, 0, sizeof(current_row));
	group = se_pages[SE_PAGE_HOME + 1].group;
	y = panel->y + 10 - app->sidebar_scroll;
	for (id = SE_PAGE_HOME + 1; id < SE_PAGES; id++) {
		page = &se_pages[id];

		/* A new group starts after a thin line. */
		if (page->group != group) {
			group = page->group;
			y += UI_GROUP_GAP / 2;
			fm_canvas_line(canvas, (float)panel->x + 18.0f, (float)y + 0.5f, (float)(panel->x + panel->width) - 18.0f, (float)y + 0.5f, 1.0f, SE_COLOR_SEPARATOR);
			y += UI_GROUP_GAP - UI_GROUP_GAP / 2;
		}

		/* The row, lit when it is the page shown or under the pointer. */
		row.x = panel->x + 10;
		row.y = y;
		row.width = panel->width - 20;
		row.height = UI_ROW_HEIGHT;
		current = 0;
		if (app->page == id) {
			current = 1;
			current_row = row;
		}

		/* The row's ink, and its ground: the accent's for the page shown (grey without the focus), a shade under the pointer. */
		ink = SE_COLOR_TEXT;
		glyph = SE_COLOR_ICON;
		bold = 0;
		if (current != 0 && app->focused != 0) {
			fm_canvas_round(canvas, (float)row.x, (float)row.y, (float)row.width, (float)row.height, 10.0f, SE_COLOR_SELECTION);
			ink = SE_COLOR_ACCENT;
			glyph = SE_COLOR_ACCENT;
			bold = 1;
		} else if (current != 0) {
			fm_canvas_round(canvas, (float)row.x, (float)row.y, (float)row.width, (float)row.height, 10.0f, SE_COLOR_SELECTION_INACTIVE);
			bold = 1;
		} else if (app->hover_kind == SE_HIT_PAGE_ROW && app->hover_index == (int)id) {
			fm_canvas_round(canvas, (float)row.x, (float)row.y, (float)row.width, (float)row.height, 10.0f, SE_COLOR_HOVER);
		}

		/* The picture and the name. */
		se_glyph_draw(canvas, page->glyph, (float)row.x + 10.0f, (float)row.y + 7.0f, 20.0f, glyph);
		(void)fm_text_draw_fit(app->text, canvas, row.x + 42, fm_text_center(UI_TEXT_ROW, row.y, row.height), page->name, UI_TEXT_ROW, bold, row.width - 50, ink);
		se_ui_hit(app, &row, SE_HIT_PAGE_ROW, (int)id);
		y += UI_ROW_HEIGHT + UI_ROW_GAP;
	}

	/* How tall the list is, for its scroll; a list that shrank below its scroll comes back up next frame. */
	fm_canvas_clip_pop(canvas);
	app->sidebar_extent = y + app->sidebar_scroll - panel->y + 8;
	limit = app->sidebar_extent - panel->height;
	if (limit < 0)
		limit = 0;
	if (app->sidebar_scroll > limit) {
		app->sidebar_scroll = limit;
		app->dirty = 1;
	}

	/* A page just shown is scrolled into sight. */
	if (app->reveal != 0)
		ui_reveal(app, &current_row);
}

/* Scrolls the list so that the page shown's row is in sight (at the next frame), once after the page changed. */
static void
ui_reveal(
	struct se_app *app,
	const struct fm_rect *current)
{
	const struct fm_rect *panel;
	int scroll;
	int limit;

	/* Only once for a change of page; Home has no row. */
	app->reveal = 0;
	if (current->height <= 0)
		return;

	/* A row above the pane's top comes down to it, one below its bottom comes up to it. */
	panel = &app->layout.sidebar;
	scroll = app->sidebar_scroll;
	if (current->y < panel->y + 8)
		scroll -= panel->y + 8 - current->y;
	if (current->y + current->height > panel->y + panel->height - 8)
		scroll += current->y + current->height - (panel->y + panel->height - 8);

	/* Within the list's extent. */
	limit = app->sidebar_extent - panel->height;
	scroll = ui_clamp(scroll, 0, limit);

	/* A change needs a frame. */
	if (scroll != app->sidebar_scroll) {
		app->sidebar_scroll = scroll;
		app->dirty = 1;
	}
}

/* Draws the page shown in its pane: its header and its cards, scrolled. */
static void
ui_draw_page(
	struct se_app *app,
	struct fm_canvas *canvas)
{
	const struct fm_rect *panel;
	const struct se_page *page;
	int bottom;
	int limit;
	int x;
	int width;

	/* The pane: a light veil over zdesktop's glass, or a whiter card with a bright edge over the window's ground. */
	panel = &app->layout.page;
	if (app->glass != 0) {
		fm_canvas_round(canvas, (float)panel->x, (float)panel->y, (float)panel->width, (float)panel->height, UI_PANEL_RADIUS, SE_COLOR_GLASS_PAGE);
	} else {
		fm_canvas_round(canvas, (float)panel->x, (float)panel->y, (float)panel->width, (float)panel->height, UI_PANEL_RADIUS, SE_COLOR_PANEL);
		fm_canvas_round_border(canvas, (float)panel->x, (float)panel->y, (float)panel->width, (float)panel->height, UI_PANEL_RADIUS, 1.0f, SE_COLOR_PANEL_EDGE);
	}

	/* The pane itself is a region the wheel scrolls; the page's controls are recorded after it. */
	se_ui_hit(app, panel, SE_HIT_PAGE, 0);

	/* The page stays inside the pane, in a column within its margins. */
	fm_canvas_clip_push(canvas, panel);
	x = panel->x + UI_PAGE_SIDE;
	width = panel->width - 2 * UI_PAGE_SIDE;
	page = &se_pages[app->page];

	/* The header, then the page's own cards. */
	bottom = se_page_header(app, canvas, page, x, panel->y + UI_PAGE_TOP - app->page_scroll, width);
	if (page->draw != NULL)
		bottom = page->draw(app, canvas, x, bottom + UI_PAGE_HEADER_GAP, width);
	fm_canvas_clip_pop(canvas);

	/* How tall the page is, for its scroll; a page that shrank below its scroll comes back up next frame. */
	app->page_extent = bottom + app->page_scroll - panel->y + UI_PAGE_BOTTOM;
	limit = app->page_extent - panel->height;
	if (limit < 0)
		limit = 0;
	if (app->page_scroll > limit) {
		app->page_scroll = limit;
		app->dirty = 1;
	}
}

/*
 * Finds the region of the last frame at a point: the latest recorded one
 * that holds it.  A region of the list or of the page counts only inside
 * its pane (a row scrolled out of sight is not clicked).  Returns 1 with
 * its kind and index, or 0 when there is none.
 */
static int
ui_hit_at(
	struct se_app *app,
	int x,
	int y,
	unsigned *kind,
	int *index)
{
	const struct se_hit *hit;
	const struct fm_rect *pane;
	int found;

	/* From the latest region back, so that the one drawn over wins. */
	for (found = app->hit_count - 1; found >= 0; found--) {
		hit = &app->hits[found];
		if (x < hit->rect.x ||
		    y < hit->rect.y ||
		    x >= hit->rect.x + hit->rect.width ||
		    y >= hit->rect.y + hit->rect.height)
			continue;

		/* The pane the region lies in: the list's rows in the list, the page's controls in the page. */
		pane = NULL;
		if (hit->kind == SE_HIT_PAGE_ROW)
			pane = &app->layout.sidebar;
		if (hit->kind == SE_HIT_TILE || hit->kind == SE_HIT_CONTROL)
			pane = &app->layout.page;

		/* A region scrolled out of its pane does not take the point. */
		if (pane != NULL) {
			if (x < pane->x ||
			    y < pane->y ||
			    x >= pane->x + pane->width ||
			    y >= pane->y + pane->height)
				continue;
		}

		/* The region. */
		*kind = hit->kind;
		*index = hit->index;
		return 1;
	}

	/* Nothing clickable there. */
	*kind = SE_HIT_NONE;
	*index = -1;
	return 0;
}

/* Lights the region under the pointer. */
static void
ui_motion(
	struct se_app *app,
	const struct se_event *event)
{
	unsigned kind;
	int index;

	/* The region under the pointer; only a row, a tile or a control is lit. */
	(void)ui_hit_at(app, event->x, event->y, &kind, &index);
	if (kind == SE_HIT_SIDEBAR || kind == SE_HIT_PAGE) {
		kind = SE_HIT_NONE;
		index = -1;
	}

	/* A change of the lit region needs a frame. */
	if (kind != app->hover_kind || index != app->hover_index) {
		app->hover_kind = kind;
		app->hover_index = index;
		app->dirty = 1;
	}
}

/* Takes a press and a release of the left button: a click is a release over the region the press was on. */
static void
ui_button(
	struct se_app *app,
	const struct se_event *event)
{
	unsigned kind;
	int index;

	/* Only the left button does anything. */
	if (event->button != SE_BUTTON_LEFT)
		return;

	/* The region under the pointer. */
	(void)ui_hit_at(app, event->x, event->y, &kind, &index);

	/* A press holds the region down. */
	if (event->pressed != 0) {
		app->press_kind = kind;
		app->press_index = index;
		app->dirty = 1;
		return;
	}

	/* A release over the same region clicks it. */
	if (kind == app->press_kind && index == app->press_index)
		ui_click(app, kind, index);

	/* Nothing is held down any more. */
	app->press_kind = SE_HIT_NONE;
	app->press_index = -1;
	app->dirty = 1;
}

/* Carries out a click on a region. */
static void
ui_click(
	struct se_app *app,
	unsigned kind,
	int index)
{
	const struct se_page *page;

	/* A row of the list and a tile of Home both open their page. */
	if (kind == SE_HIT_PAGE_ROW || kind == SE_HIT_TILE) {
		se_ui_go(app, (unsigned)index);
		return;
	}

	/* A page's own control is the page's to carry out. */
	if (kind == SE_HIT_CONTROL) {
		page = &se_pages[app->page];
		if (page->press != NULL)
			page->press(app, index);
		app->dirty = 1;
	}
}

/* Scrolls the pane under the pointer by the wheel. */
static void
ui_scroll(
	struct se_app *app,
	const struct se_event *event)
{
	const struct fm_rect *pane;
	int limit;

	/* The list scrolls when the pointer is over it. */
	pane = &app->layout.sidebar;
	if (event->x >= pane->x &&
	    event->x < pane->x + pane->width &&
	    event->y >= pane->y &&
	    event->y < pane->y + pane->height) {
		limit = app->sidebar_extent - pane->height;
		app->sidebar_scroll = ui_clamp(app->sidebar_scroll + event->scroll, 0, limit);
		app->dirty = 1;
		return;
	}

	/* Anywhere else the page scrolls. */
	ui_scroll_page(app, event->scroll);
}

/* Carries out a key: the history, the pages in the list's order, the page's scroll, and closing. */
static void
ui_key(
	struct se_app *app,
	const struct se_event *event)
{
	const struct fm_rect *pane;
	const struct se_page *page;
	int used;

	/* Only a press does anything. */
	if (event->pressed == 0)
		return;

	/* The page takes the key first (a text field that has the keyboard). */
	page = &se_pages[app->page];
	if (page->key != NULL) {
		used = page->key(app, event);
		if (used != 0) {
			app->dirty = 1;
			return;
		}
	}

	/* Alt with the arrows walks the history. */
	if ((event->modifiers & SE_MOD_ALT) != 0U) {
		if (event->key == SE_KEY_LEFT)
			ui_back(app);
		if (event->key == SE_KEY_RIGHT)
			ui_forward(app);
		return;
	}

	/* Ctrl+W and Ctrl+Q close the window (when zdesktop's menus did not take them). */
	if ((event->modifiers & SE_MOD_CTRL) != 0U) {
		if (event->key == SE_KEY_W || event->key == SE_KEY_Q)
			app->request = SE_REQUEST_CLOSE;
		return;
	}

	/* Each key alone. */
	pane = &app->layout.page;
	switch (event->key) {
	case SE_KEY_UP:
		ui_step_page(app, -1);
		break;
	case SE_KEY_DOWN:
		ui_step_page(app, 1);
		break;
	case SE_KEY_PAGE_UP:
		ui_scroll_page(app, -(pane->height - UI_PAGE_OVERLAP));
		break;
	case SE_KEY_PAGE_DOWN:
		ui_scroll_page(app, pane->height - UI_PAGE_OVERLAP);
		break;
	case SE_KEY_HOME:
		ui_scroll_page(app, -app->page_extent);
		break;
	case SE_KEY_END:
		ui_scroll_page(app, app->page_extent);
		break;
	case SE_KEY_ESC:
		/* Esc goes back, as a browser does. */
		ui_back(app);
		break;
	default:
		break;
	}
}

/* Shows the page before or after the one shown, in the list's order (Home goes to the first). */
static void
ui_step_page(
	struct se_app *app,
	int direction)
{
	unsigned page;

	/* From Home, down is the first page and up stays. */
	if (app->page == SE_PAGE_HOME) {
		if (direction > 0)
			se_ui_go(app, SE_PAGE_HOME + 1);
		return;
	}

	/* The neighbour in the list; the ends stay. */
	page = app->page;
	if (direction < 0 && page > SE_PAGE_HOME + 1)
		page--;
	if (direction > 0 && page + 1 < SE_PAGES)
		page++;
	se_ui_go(app, page);
}

/* Scrolls the page by an amount of pixels, within its extent. */
static void
ui_scroll_page(
	struct se_app *app,
	int amount)
{
	int limit;
	int scroll;

	/* The furthest the page scrolls is its extent less its pane. */
	limit = app->page_extent - app->layout.page.height;
	scroll = ui_clamp(app->page_scroll + amount, 0, limit);

	/* A change needs a frame. */
	if (scroll != app->page_scroll) {
		app->page_scroll = scroll;
		app->dirty = 1;
	}
}

/* Shows the page before in the history. */
static void
ui_back(
	struct se_app *app)
{
	/* The first step has nothing before it. */
	if (app->history_index <= 0)
		return;

	/* The step before, from its top. */
	app->history_index--;
	app->page = app->history[app->history_index];
	app->page_scroll = 0;
	app->reveal = 1;
	app->dirty = 1;
	se_log("PAGE %s back", se_pages[app->page].word);
}

/* Shows the page after in the history. */
static void
ui_forward(
	struct se_app *app)
{
	/* The newest step has nothing after it. */
	if (app->history_index + 1 >= app->history_count)
		return;

	/* The step after, from its top. */
	app->history_index++;
	app->page = app->history[app->history_index];
	app->page_scroll = 0;
	app->reveal = 1;
	app->dirty = 1;
	se_log("PAGE %s forward", se_pages[app->page].word);
}

/* Keeps a value between a minimum and a maximum (a maximum below the minimum counts as the minimum). */
static int
ui_clamp(
	int value,
	int minimum,
	int maximum)
{
	/* A maximum below the minimum leaves only the minimum. */
	if (maximum < minimum)
		maximum = minimum;

	/* Below the range. */
	if (value < minimum)
		return minimum;

	/* Above the range. */
	if (value > maximum)
		return maximum;

	/* Within it. */
	return value;
}
