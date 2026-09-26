/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The interface of zdesktop-files: the frame of the window (the floating
 * toolbar, the sidebar, the content panel) and what the pointer does on it.
 *
 * Each frame is drawn from the app's state, and while it is drawn every
 * clickable part records where it is (fm_ui_hit).  The pointer's input is
 * matched against the regions of the last frame, so a click lands on what
 * the user saw under the pointer.  The content panel is drawn by the view
 * of the place shown (ui-grid.c).
 */

#include "files.h"

#include <errno.h>
#include <pwd.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* The frame's measurements, in pixels. */
#define UI_MARGIN		12
#define UI_TOOLBAR_TOP		10
#define UI_TOOLBAR_HEIGHT	44
#define UI_GAP			10
#define UI_SIDEBAR_WIDTH	212
#define UI_PREVIEW_WIDTH	264
#define UI_PANEL_RADIUS		16.0f
#define UI_BUTTON_SIZE		30
#define UI_SEARCH_WIDTH		232
#define UI_SIDEBAR_ROW		30
#define UI_SIDEBAR_HEADER	30

/* The text sizes of the frame. */
#define UI_TEXT_TOOLBAR		14U
#define UI_TEXT_SIDEBAR		14U
#define UI_TEXT_HEADER		11U

/* A second click this soon after the first on the same region is a double click, in milliseconds. */
#define UI_DOUBLE_CLICK_MS	400U

/* The most crumbs the path shows. */
#define UI_CRUMBS		32

/* The evdev codes of the keys the frame handles itself. */
#define UI_KEY_BACKSPACE	14U
#define UI_KEY_LEFT		105U
#define UI_KEY_RIGHT		106U

/*
 * One part of the path in the toolbar: its label and the place it leads to.
 */
struct ui_crumb {
	char label[FM_NAME_MAX];
	struct fm_location location;
};

static void ui_layout(struct fm_app *app);
static void ui_draw_toolbar(struct fm_app *app, struct fm_canvas *canvas);
static void ui_draw_button(struct fm_app *app, struct fm_canvas *canvas, int x, int y, enum fm_icon icon, int enabled, unsigned kind);
static void ui_draw_crumbs(struct fm_app *app, struct fm_canvas *canvas, int x, int width);
static void ui_draw_search(struct fm_app *app, struct fm_canvas *canvas, const struct fm_rect *field);
static void ui_draw_views(struct fm_app *app, struct fm_canvas *canvas, int x, int y);
static void ui_draw_sidebar(struct fm_app *app, struct fm_canvas *canvas);
static int ui_crumbs(struct fm_app *app, struct ui_crumb *crumbs, int capacity);
static int ui_place_current(struct fm_app *app, const struct fm_place *place);
static int ui_hit_at(struct fm_app *app, int x, int y, unsigned *kind, int *index);
static int ui_contains(const struct fm_rect *rect, int x, int y);
static void ui_motion(struct fm_app *app, const struct fm_event *event);
static void ui_button(struct fm_app *app, const struct fm_event *event);
static void ui_click(struct fm_app *app, unsigned kind, int index, int double_click);
static void ui_scroll(struct fm_app *app, int amount);
static void ui_key(struct fm_app *app, const struct fm_event *event);
static void ui_load(struct fm_app *app, struct fm_tab *tab);
static void ui_back(struct fm_app *app);
static void ui_forward(struct fm_app *app);
static void ui_open_entry(struct fm_app *app, int index);
static const char *ui_location_kind_name(unsigned kind);

/*
 * Sets up the file manager: the home folder, the sidebar, one tab showing
 * a start folder (the home dashboard when start is NULL).
 *
 * Returns 0, or ENOMEM.
 */
int
fm_app_init(
	struct fm_app *app,
	struct fm_text *text,
	const char *start)
{
	struct fm_location location;
	struct passwd *account;
	const char *home;

	/* The defaults: icons by name, sidebar shown, preview hidden, hidden files hidden. */
	memset(app, 0, sizeof(*app));
	app->text = text;
	app->width = FM_WIDTH;
	app->height = FM_HEIGHT;
	app->view = FM_VIEW_ICONS;
	app->sort = FM_SORT_NAME;
	app->show_sidebar = 1;
	app->focused = 1;
	app->dirty = 1;
	app->hover_index = -1;
	app->press_index = -1;
	app->click_index = -1;

	/* The user's account, for the name and the home folder. */
	account = getpwuid(getuid());
	if (account != NULL && account->pw_name != NULL)
		snprintf(app->user, sizeof(app->user), "%s", account->pw_name);

	/* The home folder: HOME, else the account's, else the root. */
	home = getenv("HOME");
	if ((home == NULL || home[0] == '\0') && account != NULL)
		home = account->pw_dir;
	if (home == NULL || home[0] == '\0')
		home = "/";
	snprintf(app->home, sizeof(app->home), "%s", home);

	/* The sidebar. */
	fm_places_init(&app->places, app->home);

	/* The first tab. */
	app->tabs[0] = calloc(1, sizeof(*app->tabs[0]));
	if (app->tabs[0] == NULL)
		return ENOMEM;
	app->tabs[0]->cursor = -1;
	app->tabs[0]->anchor = -1;
	app->tab_count = 1;
	app->tab_index = 0;

	/* It shows the start folder, or the home dashboard. */
	memset(&location, 0, sizeof(location));
	location.kind = FM_LOCATION_HOME;
	snprintf(location.path, sizeof(location.path), "%s", app->home);
	if (start != NULL) {
		location.kind = FM_LOCATION_FOLDER;
		snprintf(location.path, sizeof(location.path), "%s", start);
	}

	/* The tab goes there. */
	fm_ui_go(app, &location);

	/* Succeeded: the app can draw its first frame. */
	return 0;
}

/*
 * Frees the tabs and their listings.
 */
void
fm_app_release(
	struct fm_app *app)
{
	int index;

	/* Each tab's listing, then the tab. */
	for (index = 0; index < app->tab_count; index++) {
		fm_dir_free(&app->tabs[index]->listing);
		free(app->tabs[index]);
		app->tabs[index] = NULL;
	}

	/* No tab is left. */
	app->tab_count = 0;
}

/*
 * Handles one input of the window.
 */
void
fm_ui_event(
	struct fm_app *app,
	const struct fm_event *event)
{
	/* The time of the input, which double clicks are measured by. */
	app->now = event->time;
	app->modifiers = event->modifiers;

	/* Each kind of input. */
	switch (event->type) {
	case FM_EVENT_MOTION:
		ui_motion(app, event);
		break;
	case FM_EVENT_BUTTON:
		ui_motion(app, event);
		ui_button(app, event);
		break;
	case FM_EVENT_AXIS:
		ui_scroll(app, event->scroll);
		break;
	case FM_EVENT_LEAVE:
		app->pointer_inside = 0;
		app->hover_kind = FM_HIT_NONE;
		app->hover_index = -1;
		app->dirty = 1;
		break;
	case FM_EVENT_KEY:
		ui_key(app, event);
		break;
	case FM_EVENT_FOCUS:
		app->focused = event->focused;
		app->dirty = 1;
		break;
	default:
		break;
	}
}

/*
 * Lets time pass: reloads a folder that changed on disk (checked every
 * two seconds).
 */
void
fm_ui_tick(
	struct fm_app *app,
	uint64_t now)
{
	struct fm_tab *tab;
	struct fm_visit *visit;
	struct stat status;
	int error;

	/* The tab shown, checked no more often than every two seconds. */
	app->now = now;
	tab = fm_ui_tab(app);
	if (now < tab->checked_at + 2000U)
		return;
	tab->checked_at = now;

	/* Only a folder is watched. */
	visit = &tab->history[tab->history_index];
	if (visit->location.kind != FM_LOCATION_FOLDER)
		return;

	/* A folder whose modification time moved is read again. */
	error = stat(visit->location.path, &status);
	if (error != 0 || status.st_mtime == tab->listing.modified)
		return;

	/* Reads it again. */
	ui_load(app, tab);
	app->dirty = 1;
}

/*
 * Draws the window's frame onto a canvas of the window's size.
 */
void
fm_ui_draw(
	struct fm_app *app,
	struct fm_canvas *canvas)
{
	struct fm_rect whole;

	/* The panels' places at this size, and no clickable region yet. */
	app->width = canvas->width;
	app->height = canvas->height;
	ui_layout(app);
	app->hit_count = 0;

	/* The window's ground: a quiet light gradient. */
	whole.x = 0;
	whole.y = 0;
	whole.width = canvas->width;
	whole.height = canvas->height;
	fm_canvas_gradient(canvas, &whole, FM_COLOR_BACKGROUND_TOP, FM_COLOR_BACKGROUND_BOTTOM);

	/* The sidebar, when shown. */
	if (app->show_sidebar != 0)
		ui_draw_sidebar(app, canvas);

	/* The content panel, drawn by the view of the place. */
	fm_grid_draw(app, canvas, &app->layout.content);

	/* The toolbar over everything. */
	ui_draw_toolbar(app, canvas);

	/* The frame is up to date. */
	app->dirty = 0;
}

/*
 * Records a clickable region of the frame being drawn.
 */
void
fm_ui_hit(
	struct fm_app *app,
	const struct fm_rect *rect,
	unsigned kind,
	int index)
{
	/* Regions past the table's size are not clickable (they do not happen in practice). */
	if (app->hit_count == FM_HITS)
		return;

	/* The region, after those drawn before it (the later wins where they overlap). */
	app->hits[app->hit_count].rect = *rect;
	app->hits[app->hit_count].kind = kind;
	app->hits[app->hit_count].index = index;
	app->hit_count++;
}

/*
 * Returns the tab shown.
 */
struct fm_tab *
fm_ui_tab(
	struct fm_app *app)
{
	/* The current tab. */
	return app->tabs[app->tab_index];
}

/*
 * Goes to a place in the tab shown: the history keeps where the tab was,
 * and anything forward of it is dropped (as a browser does).
 */
void
fm_ui_go(
	struct fm_app *app,
	const struct fm_location *location)
{
	struct fm_tab *tab;
	struct fm_visit *visit;
	int index;

	/* The place being left keeps its scroll. */
	tab = fm_ui_tab(app);
	if (tab->history_count > 0)
		tab->history[tab->history_index].scroll = tab->scroll;

	/* A full history drops its oldest step. */
	index = tab->history_index + 1;
	if (tab->history_count == 0)
		index = 0;
	if (index == FM_HISTORY) {
		memmove(&tab->history[0], &tab->history[1], sizeof(tab->history[0]) * (FM_HISTORY - 1));
		index = FM_HISTORY - 1;
	}

	/* The new step, and nothing forward of it. */
	visit = &tab->history[index];
	memset(visit, 0, sizeof(*visit));
	visit->location = *location;
	tab->history_index = index;
	tab->history_count = index + 1;

	/* The place's items, from the top. */
	tab->scroll = 0;
	ui_load(app, tab);
	app->dirty = 1;
}

/*
 * Writes one line of the file manager's log on standard error: "ZFILES "
 * and the message.  The tests wait for these lines.
 */
void
fm_log(
	const char *format,
	...)
{
	va_list arguments;

	/* The prefix, the message and the end of the line, at once. */
	fputs("ZFILES ", stderr);
	va_start(arguments, format);
	vfprintf(stderr, format, arguments);
	va_end(arguments);
	fputc('\n', stderr);
	fflush(stderr);
}

/* Places the toolbar, the sidebar, the content and the preview for the window's size. */
static void
ui_layout(
	struct fm_app *app)
{
	struct fm_layout *layout;
	int top;
	int left;
	int right;

	/* The toolbar floats across the top. */
	layout = &app->layout;
	layout->toolbar.x = UI_MARGIN;
	layout->toolbar.y = UI_TOOLBAR_TOP;
	layout->toolbar.width = app->width - 2 * UI_MARGIN;
	layout->toolbar.height = UI_TOOLBAR_HEIGHT;

	/* The panels start under it. */
	top = UI_TOOLBAR_TOP + UI_TOOLBAR_HEIGHT + UI_GAP;
	left = UI_MARGIN;
	right = app->width - UI_MARGIN;

	/* The sidebar on the left, when shown. */
	memset(&layout->sidebar, 0, sizeof(layout->sidebar));
	if (app->show_sidebar != 0) {
		layout->sidebar.x = left;
		layout->sidebar.y = top;
		layout->sidebar.width = UI_SIDEBAR_WIDTH;
		layout->sidebar.height = app->height - top - UI_MARGIN;
		left += UI_SIDEBAR_WIDTH + UI_GAP;
	}

	/* The preview on the right, when shown. */
	memset(&layout->preview, 0, sizeof(layout->preview));
	if (app->show_preview != 0) {
		layout->preview.width = UI_PREVIEW_WIDTH;
		layout->preview.x = right - UI_PREVIEW_WIDTH;
		layout->preview.y = top;
		layout->preview.height = app->height - top - UI_MARGIN;
		right -= UI_PREVIEW_WIDTH + UI_GAP;
	}

	/* The content between them. */
	layout->content.x = left;
	layout->content.y = top;
	layout->content.width = right - left;
	layout->content.height = app->height - top - UI_MARGIN;
}

/* Draws the floating toolbar: back, forward, home, the path, the search field and the view buttons. */
static void
ui_draw_toolbar(
	struct fm_app *app,
	struct fm_canvas *canvas)
{
	const struct fm_rect *bar;
	struct fm_rect field;
	struct fm_tab *tab;
	int crumbs_x;
	int views_x;
	int y;

	/* The bar: a white pill with a soft shadow. */
	bar = &app->layout.toolbar;
	fm_canvas_shadow(canvas, (float)bar->x, (float)bar->y + 3.0f, (float)bar->width, (float)bar->height, (float)bar->height * 0.5f, 12.0f, FM_COLOR_SHADOW);
	fm_canvas_round(canvas, (float)bar->x, (float)bar->y, (float)bar->width, (float)bar->height, (float)bar->height * 0.5f, FM_COLOR_PANEL);

	/* Back and forward, pale when the history has nowhere to go. */
	tab = fm_ui_tab(app);
	y = bar->y + (bar->height - UI_BUTTON_SIZE) / 2;
	ui_draw_button(app, canvas, bar->x + 8, y, FM_ICON_BACK, tab->history_index > 0, FM_HIT_BACK);
	ui_draw_button(app, canvas, bar->x + 8 + UI_BUTTON_SIZE + 4, y, FM_ICON_FORWARD, tab->history_index + 1 < tab->history_count, FM_HIT_FORWARD);

	/* Home. */
	ui_draw_button(app, canvas, bar->x + 8 + 2 * (UI_BUTTON_SIZE + 4) + 6, y, FM_ICON_HOME, 1, FM_HIT_HOME);

	/* The preview and view buttons at the right end. */
	ui_draw_button(app, canvas, bar->x + bar->width - 8 - UI_BUTTON_SIZE, y, FM_ICON_PREVIEW, 1, FM_HIT_PREVIEW);
	views_x = bar->x + bar->width - 8 - UI_BUTTON_SIZE - 10 - 2 * UI_BUTTON_SIZE - 4;
	ui_draw_views(app, canvas, views_x, y);

	/* The search field left of them. */
	field.width = UI_SEARCH_WIDTH;
	field.height = UI_BUTTON_SIZE;
	field.x = views_x - 12 - UI_SEARCH_WIDTH;
	field.y = y;
	ui_draw_search(app, canvas, &field);

	/* The path between home and the search field. */
	crumbs_x = bar->x + 8 + 3 * (UI_BUTTON_SIZE + 4) + 16;
	ui_draw_crumbs(app, canvas, crumbs_x, field.x - 16 - crumbs_x);
}

/* Draws a round toolbar button with its icon, lit under the pointer, and records it. */
static void
ui_draw_button(
	struct fm_app *app,
	struct fm_canvas *canvas,
	int x,
	int y,
	enum fm_icon icon,
	int enabled,
	unsigned kind)
{
	struct fm_rect rect;
	fm_color color;

	/* The button's square. */
	rect.x = x;
	rect.y = y;
	rect.width = UI_BUTTON_SIZE;
	rect.height = UI_BUTTON_SIZE;

	/* A soft circle under the pointer, a stronger one while pressed. */
	if (enabled != 0 && app->hover_kind == kind) {
		fm_canvas_circle(canvas, (float)x + UI_BUTTON_SIZE * 0.5f, (float)y + UI_BUTTON_SIZE * 0.5f, UI_BUTTON_SIZE * 0.5f, FM_COLOR_HOVER);
		if (app->pressing != 0 && app->press_kind == kind)
			fm_canvas_circle(canvas, (float)x + UI_BUTTON_SIZE * 0.5f, (float)y + UI_BUTTON_SIZE * 0.5f, UI_BUTTON_SIZE * 0.5f, FM_COLOR_HOVER);
	}

	/* The preview button shows whether the preview is on. */
	if (kind == FM_HIT_PREVIEW && app->show_preview != 0)
		fm_canvas_circle(canvas, (float)x + UI_BUTTON_SIZE * 0.5f, (float)y + UI_BUTTON_SIZE * 0.5f, UI_BUTTON_SIZE * 0.5f, FM_COLOR_SELECTION);

	/* The icon, pale when the button does nothing now. */
	color = FM_COLOR_ICON;
	if (enabled == 0)
		color = FM_COLOR_TEXT_FAINT;
	fm_icon_draw(canvas, icon, (float)x + 6.0f, (float)y + 6.0f, (float)UI_BUTTON_SIZE - 12.0f, color);

	/* A button that does something can be clicked. */
	if (enabled != 0)
		fm_ui_hit(app, &rect, kind, 0);
}

/* Draws the path of the place shown, each part clickable; the leading parts give way to an ellipsis when it is too long. */
static void
ui_draw_crumbs(
	struct fm_app *app,
	struct fm_canvas *canvas,
	int x,
	int width)
{
	static struct ui_crumb crumbs[UI_CRUMBS];
	struct fm_rect rect;
	fm_color color;
	int widths[UI_CRUMBS];
	int count;
	int first;
	int total;
	int index;
	int baseline;
	int bold;
	int pen;

	/* The parts and their widths (the last part bold). */
	count = ui_crumbs(app, crumbs, UI_CRUMBS);
	if (count == 0 || width <= 0)
		return;
	for (index = 0; index < count; index++) {
		bold = 0;
		if (index == count - 1)
			bold = 1;
		widths[index] = fm_text_width(app->text, crumbs[index].label, strlen(crumbs[index].label), UI_TEXT_TOOLBAR, bold) + 16;
	}

	/* As many parts from the end as fit, with room for the separators and an ellipsis. */
	total = widths[count - 1];
	first = count - 1;
	while (first > 0 && total + widths[first - 1] + 18 + 30 <= width) {
		first--;
		total += widths[first] + 18;
	}

	/* The parts' baseline, centred in the toolbar. */
	baseline = fm_text_center(UI_TEXT_TOOLBAR, app->layout.toolbar.y, app->layout.toolbar.height);
	pen = x;

	/* An ellipsis stands for the parts left out. */
	if (first > 0) {
		pen += fm_text_draw(app->text, canvas, pen, baseline, "\xe2\x80\xa6", 3, UI_TEXT_TOOLBAR, 0, FM_COLOR_TEXT_SECONDARY);
		fm_icon_draw(canvas, FM_ICON_CHEVRON, (float)pen + 2.0f, (float)(baseline - 12), 14.0f, FM_COLOR_TEXT_FAINT);
		pen += 18;
	}

	/* Each part: its label (lit under the pointer) and a separator after all but the last. */
	for (index = first; index < count; index++) {
		rect.x = pen;
		rect.y = app->layout.toolbar.y + 7;
		rect.width = widths[index];
		rect.height = app->layout.toolbar.height - 14;
		if (app->hover_kind == FM_HIT_CRUMB && app->hover_index == index)
			fm_canvas_round(canvas, (float)rect.x, (float)rect.y, (float)rect.width, (float)rect.height, 8.0f, FM_COLOR_HOVER);

		/* The label, the last one bold and dark. */
		bold = 0;
		color = FM_COLOR_TEXT_SECONDARY;
		if (index == count - 1) {
			bold = 1;
			color = FM_COLOR_TEXT;
		}

		/* Draws it and records the part. */
		(void)fm_text_draw_fit(app->text, canvas, pen + 8, baseline, crumbs[index].label, UI_TEXT_TOOLBAR, bold, x + width - pen - 16, color);
		fm_ui_hit(app, &rect, FM_HIT_CRUMB, index);
		pen += widths[index];

		/* A separator before the next part. */
		if (index + 1 < count) {
			fm_icon_draw(canvas, FM_ICON_CHEVRON, (float)pen + 1.0f, (float)(baseline - 12), 14.0f, FM_COLOR_TEXT_FAINT);
			pen += 18;
		}
	}
}

/* Draws the search field: a rounded field with a magnifier and its placeholder. */
static void
ui_draw_search(
	struct fm_app *app,
	struct fm_canvas *canvas,
	const struct fm_rect *field)
{
	int baseline;

	/* The field's ground, a little darker under the pointer. */
	fm_canvas_round(canvas, (float)field->x, (float)field->y, (float)field->width, (float)field->height, (float)field->height * 0.5f, FM_RGB(0xf1f4f8));
	if (app->hover_kind == FM_HIT_SEARCH)
		fm_canvas_round(canvas, (float)field->x, (float)field->y, (float)field->width, (float)field->height, (float)field->height * 0.5f, FM_COLOR_HOVER);

	/* The magnifier and the placeholder. */
	fm_icon_draw(canvas, FM_ICON_SEARCH, (float)field->x + 9.0f, (float)field->y + 7.0f, 16.0f, FM_COLOR_TEXT_SECONDARY);
	baseline = fm_text_center(13U, field->y, field->height);
	(void)fm_text_draw(app->text, canvas, field->x + 32, baseline, "Search", 6, 13U, 0, FM_COLOR_TEXT_FAINT);

	/* The field can be clicked. */
	fm_ui_hit(app, field, FM_HIT_SEARCH, 0);
}

/* Draws the icon and list buttons as one segmented pill, the view shown lit. */
static void
ui_draw_views(
	struct fm_app *app,
	struct fm_canvas *canvas,
	int x,
	int y)
{
	struct fm_rect rect;
	fm_color color;
	int selected_x;

	/* The pill behind both. */
	fm_canvas_round(canvas, (float)x - 2.0f, (float)y, 2.0f * UI_BUTTON_SIZE + 8.0f, (float)UI_BUTTON_SIZE, UI_BUTTON_SIZE * 0.5f, FM_RGB(0xf1f4f8));

	/* The view shown, on a white knob. */
	selected_x = x;
	if (app->view == FM_VIEW_LIST)
		selected_x = x + UI_BUTTON_SIZE + 4;
	fm_canvas_shadow(canvas, (float)selected_x + 1.0f, (float)y + 2.0f, UI_BUTTON_SIZE - 2.0f, UI_BUTTON_SIZE - 4.0f, (UI_BUTTON_SIZE - 4) * 0.5f, 4.0f, FM_COLOR_SHADOW);
	fm_canvas_round(canvas, (float)selected_x + 1.0f, (float)y + 2.0f, UI_BUTTON_SIZE - 2.0f, UI_BUTTON_SIZE - 4.0f, (UI_BUTTON_SIZE - 4) * 0.5f, FM_COLOR_PANEL);

	/* The icon button. */
	color = FM_COLOR_TEXT_SECONDARY;
	if (app->view == FM_VIEW_ICONS)
		color = FM_COLOR_ACCENT;
	fm_icon_draw(canvas, FM_ICON_GRID, (float)x + 7.0f, (float)y + 7.0f, 16.0f, color);
	rect.x = x;
	rect.y = y;
	rect.width = UI_BUTTON_SIZE;
	rect.height = UI_BUTTON_SIZE;
	fm_ui_hit(app, &rect, FM_HIT_VIEW_ICONS, 0);

	/* The list button. */
	color = FM_COLOR_TEXT_SECONDARY;
	if (app->view == FM_VIEW_LIST)
		color = FM_COLOR_ACCENT;
	fm_icon_draw(canvas, FM_ICON_LIST, (float)x + UI_BUTTON_SIZE + 11.0f, (float)y + 7.0f, 16.0f, color);
	rect.x = x + UI_BUTTON_SIZE + 4;
	fm_ui_hit(app, &rect, FM_HIT_VIEW_LIST, 0);
}

/* Draws the sidebar: Favorites, Locations and Tags, the place shown lit. */
static void
ui_draw_sidebar(
	struct fm_app *app,
	struct fm_canvas *canvas)
{
	static const char *const titles[] = { "Favorites", "Locations", "Tags" };
	const struct fm_rect *panel;
	const struct fm_place *place;
	struct fm_rect row;
	fm_color ink;
	unsigned section;
	int current;
	int index;
	int y;

	/* The panel: a light veil over the ground. */
	panel = &app->layout.sidebar;
	fm_canvas_round(canvas, (float)panel->x, (float)panel->y, (float)panel->width, (float)panel->height, UI_PANEL_RADIUS, FM_COLOR_SIDEBAR);
	fm_canvas_round_border(canvas, (float)panel->x, (float)panel->y, (float)panel->width, (float)panel->height, UI_PANEL_RADIUS, 1.0f, FM_RGBA(0xffffff, 170));
	fm_canvas_clip_push(canvas, panel);

	/* Each place under its section's title. */
	section = FM_PLACES;
	y = panel->y + 8;
	for (index = 0; index < app->places.count; index++) {
		place = &app->places.items[index];

		/* A new section starts with its title. */
		if (place->section != section) {
			section = place->section;
			if (index > 0)
				y += 8;
			(void)fm_text_draw(app->text, canvas, panel->x + 16, y + UI_SIDEBAR_HEADER - 10, titles[section], strlen(titles[section]), UI_TEXT_HEADER, 1, FM_COLOR_TEXT_FAINT);
			y += UI_SIDEBAR_HEADER;
		}

		/* The row, lit when it is the place shown or under the pointer. */
		row.x = panel->x + 8;
		row.y = y;
		row.width = panel->width - 16;
		row.height = UI_SIDEBAR_ROW;
		current = ui_place_current(app, place);
		ink = FM_COLOR_TEXT;
		if (place->missing != 0)
			ink = FM_COLOR_TEXT_FAINT;
		if (current != 0) {
			fm_canvas_round(canvas, (float)row.x, (float)row.y, (float)row.width, (float)row.height, 9.0f, FM_COLOR_SELECTION);
			ink = FM_COLOR_ACCENT;
		} else if (app->hover_kind == FM_HIT_PLACE && app->hover_index == index) {
			fm_canvas_round(canvas, (float)row.x, (float)row.y, (float)row.width, (float)row.height, 9.0f, FM_COLOR_HOVER);
		}

		/* The icon, or a tag's dot. */
		if (place->section == FM_SECTION_TAGS) {
			fm_icon_tag(canvas, (float)row.x + 17.0f, (float)row.y + UI_SIDEBAR_ROW * 0.5f, 5.0f, place->color);
		} else {
			fm_icon_draw(canvas, (enum fm_icon)place->icon, (float)row.x + 8.0f, (float)row.y + 6.0f, 18.0f, ink);
		}

		/* The label. */
		(void)fm_text_draw_fit(app->text, canvas, row.x + 36, fm_text_center(UI_TEXT_SIDEBAR, row.y, row.height), place->label, UI_TEXT_SIDEBAR, current, row.width - 44, ink);
		fm_ui_hit(app, &row, FM_HIT_PLACE, index);
		y += UI_SIDEBAR_ROW;
	}

	/* The panel's clip ends. */
	fm_canvas_clip_pop(canvas);
}

/* Fills the parts of the path shown in the toolbar and returns how many there are. */
static int
ui_crumbs(
	struct fm_app *app,
	struct ui_crumb *crumbs,
	int capacity)
{
	struct fm_tab *tab;
	const struct fm_location *location;
	const char *path;
	const char *part;
	const char *end;
	size_t home_length;
	size_t length;
	size_t done;
	int prefix;
	int count;
	int inside;

	/* A place that is not a folder is a single part. */
	tab = fm_ui_tab(app);
	location = &tab->history[tab->history_index].location;
	if (location->kind != FM_LOCATION_FOLDER) {
		snprintf(crumbs[0].label, sizeof(crumbs[0].label), "%s", fm_location_name(location, app->home));
		crumbs[0].location = *location;
		return 1;
	}

	/* A folder under the home folder starts from Home, any other from Computer. */
	path = location->path;
	home_length = strlen(app->home);
	inside = 0;
	prefix = strncmp(path, app->home, home_length);
	if (prefix == 0 &&
	    home_length > 1U &&
	    (path[home_length] == '/' ||
	     path[home_length] == '\0'))
		inside = 1;
	count = 1;
	crumbs[0].location.kind = FM_LOCATION_FOLDER;
	if (inside != 0) {
		snprintf(crumbs[0].label, sizeof(crumbs[0].label), "Home");
		snprintf(crumbs[0].location.path, sizeof(crumbs[0].location.path), "%s", app->home);
		done = home_length;
	} else {
		snprintf(crumbs[0].label, sizeof(crumbs[0].label), "Computer");
		snprintf(crumbs[0].location.path, sizeof(crumbs[0].location.path), "/");
		done = 0;
	}

	/* Each further part of the path, leading to the path up to it. */
	part = path + done;
	while (*part != '\0' && count < capacity) {
		/* Slashes between parts. */
		while (*part == '/')
			part++;
		if (*part == '\0')
			break;

		/* The part runs to the next slash. */
		end = strchr(part, '/');
		if (end == NULL)
			end = part + strlen(part);
		length = (size_t)(end - part);

		/* Its label and the path up to it. */
		if (length >= sizeof(crumbs[count].label))
			length = sizeof(crumbs[count].label) - 1U;
		memcpy(crumbs[count].label, part, length);
		crumbs[count].label[length] = '\0';
		crumbs[count].location.kind = FM_LOCATION_FOLDER;
		length = (size_t)(end - path);
		if (length >= sizeof(crumbs[count].location.path))
			length = sizeof(crumbs[count].location.path) - 1U;
		memcpy(crumbs[count].location.path, path, length);
		crumbs[count].location.path[length] = '\0';
		count++;
		part = end;
	}

	/* Reports how many parts there are. */
	return count;
}

/* Tells whether a sidebar place is the place shown. */
static int
ui_place_current(
	struct fm_app *app,
	const struct fm_place *place)
{
	const struct fm_location *location;
	struct fm_tab *tab;
	int match;

	/* The place shown. */
	tab = fm_ui_tab(app);
	location = &tab->history[tab->history_index].location;

	/* Places of other kinds differ. */
	if (location->kind != place->location.kind)
		return 0;

	/* The dashboard and the recent files are one place each. */
	if (location->kind == FM_LOCATION_HOME || location->kind == FM_LOCATION_RECENTS || location->kind == FM_LOCATION_TRASH)
		return 1;

	/* Others are the same place when their paths are. */
	match = strcmp(location->path, place->location.path);
	if (match != 0)
		return 0;

	/* The same place. */
	return 1;
}

/* Finds the region of the last frame under a point (the last drawn wins); zero when there is none. */
static int
ui_hit_at(
	struct fm_app *app,
	int x,
	int y,
	unsigned *kind,
	int *index)
{
	int hit;
	int inside;

	/* From the last region drawn to the first. */
	for (hit = app->hit_count - 1; hit >= 0; hit--) {
		inside = ui_contains(&app->hits[hit].rect, x, y);
		if (inside != 0) {
			*kind = app->hits[hit].kind;
			*index = app->hits[hit].index;
			return 1;
		}
	}

	/* Nothing clickable is there. */
	*kind = FM_HIT_NONE;
	*index = -1;
	return 0;
}

/* Tells whether a rectangle holds a point. */
static int
ui_contains(
	const struct fm_rect *rect,
	int x,
	int y)
{
	/* Left of it or above it. */
	if (x < rect->x || y < rect->y)
		return 0;

	/* Right of it or below it. */
	if (x >= rect->x + rect->width || y >= rect->y + rect->height)
		return 0;

	/* Inside. */
	return 1;
}

/* Follows the pointer: the region under it is lit. */
static void
ui_motion(
	struct fm_app *app,
	const struct fm_event *event)
{
	unsigned kind;
	int index;

	/* The pointer's place. */
	app->pointer_x = event->x;
	app->pointer_y = event->y;
	app->pointer_inside = 1;

	/* A new region under it needs a new frame. */
	(void)ui_hit_at(app, event->x, event->y, &kind, &index);
	if (kind != app->hover_kind || index != app->hover_index) {
		app->hover_kind = kind;
		app->hover_index = index;
		app->dirty = 1;
	}
}

/* Handles a button: the left button clicks the region under the pointer when it is let go over the one it was pressed on. */
static void
ui_button(
	struct fm_app *app,
	const struct fm_event *event)
{
	unsigned kind;
	int index;
	int double_click;

	/* Only the left button clicks (the right one opens context menus later). */
	if (event->button != FM_BUTTON_LEFT)
		return;

	/* The region under the pointer. */
	(void)ui_hit_at(app, event->x, event->y, &kind, &index);

	/* A press remembers what it pressed, and clicks at once (items select on the press). */
	if (event->pressed != 0) {
		app->pressing = 1;
		app->press_kind = kind;
		app->press_index = index;
		app->dirty = 1;

		/* A second press soon on the same region is a double click. */
		double_click = 0;
		if (kind == app->click_kind && index == app->click_index && event->time - app->click_time < UI_DOUBLE_CLICK_MS)
			double_click = 1;
		app->click_kind = kind;
		app->click_index = index;
		app->click_time = event->time;
		if (double_click != 0)
			app->click_time = 0;
		ui_click(app, kind, index, double_click);
		return;
	}

	/* The release ends the press. */
	app->pressing = 0;
	app->dirty = 1;
}

/* Carries out a click on a region. */
static void
ui_click(
	struct fm_app *app,
	unsigned kind,
	int index,
	int double_click)
{
	static struct ui_crumb crumbs[UI_CRUMBS];
	struct fm_location location;
	int count;

	/* What each region does. */
	switch (kind) {
	case FM_HIT_BACK:
		ui_back(app);
		break;
	case FM_HIT_FORWARD:
		ui_forward(app);
		break;
	case FM_HIT_HOME:
		memset(&location, 0, sizeof(location));
		location.kind = FM_LOCATION_HOME;
		snprintf(location.path, sizeof(location.path), "%s", app->home);
		fm_ui_go(app, &location);
		break;
	case FM_HIT_CRUMB:
		count = ui_crumbs(app, crumbs, UI_CRUMBS);
		if (index >= 0 && index < count - 1)
			fm_ui_go(app, &crumbs[index].location);
		break;
	case FM_HIT_PLACE:
		if (index >= 0 && index < app->places.count)
			fm_ui_go(app, &app->places.items[index].location);
		break;
	case FM_HIT_VIEW_ICONS:
		app->view = FM_VIEW_ICONS;
		app->dirty = 1;
		break;
	case FM_HIT_VIEW_LIST:
		app->view = FM_VIEW_LIST;
		app->dirty = 1;
		break;
	case FM_HIT_PREVIEW:
		app->show_preview = !app->show_preview;
		app->dirty = 1;
		break;
	case FM_HIT_ITEM:
		if (double_click != 0)
			ui_open_entry(app, index);
		break;
	default:
		break;
	}
}

/* Scrolls the content by an amount of pixels (positive is down), kept within what there is. */
static void
ui_scroll(
	struct fm_app *app,
	int amount)
{
	struct fm_tab *tab;
	int limit;

	/* The furthest the content scrolls. */
	tab = fm_ui_tab(app);
	limit = app->layout.content_height - app->layout.content.height;
	if (limit < 0)
		limit = 0;

	/* The new scroll, inside the range. */
	tab->scroll += amount;
	if (tab->scroll > limit)
		tab->scroll = limit;
	if (tab->scroll < 0)
		tab->scroll = 0;
	app->dirty = 1;
}

/* Handles a key press: going back and forward. */
static void
ui_key(
	struct fm_app *app,
	const struct fm_event *event)
{
	/* Releases do nothing. */
	if (event->pressed == 0)
		return;

	/* Backspace and Alt+Left go back, Alt+Right forward (spec §35). */
	if (event->key == UI_KEY_BACKSPACE && event->modifiers == 0U) {
		ui_back(app);
	} else if (event->key == UI_KEY_LEFT && event->modifiers == FM_MOD_ALT) {
		ui_back(app);
	} else if (event->key == UI_KEY_RIGHT && event->modifiers == FM_MOD_ALT) {
		ui_forward(app);
	}
}

/* Reads the items of the place a tab shows, sorted, and logs the place. */
static void
ui_load(
	struct fm_app *app,
	struct fm_tab *tab)
{
	const struct fm_location *location;
	const char *path;

	/* The place, and the folder its items come from. */
	location = &tab->history[tab->history_index].location;
	path = NULL;
	if (location->kind == FM_LOCATION_FOLDER || location->kind == FM_LOCATION_HOME)
		path = location->path;

	/* A folder's items, or none for places the later phases fill. */
	fm_dir_free(&tab->listing);
	if (path != NULL) {
		(void)fm_dir_read(&tab->listing, path, app->show_hidden);
		fm_dir_sort(&tab->listing, app->sort, app->sort_reverse);
	}

	/* Nothing has the cursor yet, and the folder was just checked. */
	tab->cursor = -1;
	tab->anchor = -1;
	tab->checked_at = app->now;

	/* The log line the tests wait for. */
	fm_log("LOCATION kind=%s path=%s items=%lu error=%d", ui_location_kind_name(location->kind), location->path, (unsigned long)tab->listing.count, tab->listing.error);
}

/* Goes one step back in the tab's history. */
static void
ui_back(
	struct fm_app *app)
{
	struct fm_tab *tab;

	/* The first step has nothing behind it. */
	tab = fm_ui_tab(app);
	if (tab->history_index == 0)
		return;

	/* The step before, at the scroll it was left with. */
	tab->history[tab->history_index].scroll = tab->scroll;
	tab->history_index--;
	ui_load(app, tab);
	tab->scroll = tab->history[tab->history_index].scroll;
	app->dirty = 1;
}

/* Goes one step forward in the tab's history. */
static void
ui_forward(
	struct fm_app *app)
{
	struct fm_tab *tab;

	/* The last step has nothing after it. */
	tab = fm_ui_tab(app);
	if (tab->history_index + 1 >= tab->history_count)
		return;

	/* The step after, at the scroll it was left with. */
	tab->history[tab->history_index].scroll = tab->scroll;
	tab->history_index++;
	ui_load(app, tab);
	tab->scroll = tab->history[tab->history_index].scroll;
	app->dirty = 1;
}

/* Opens an item of the listing: a folder in the tab (a file opens in a later phase). */
static void
ui_open_entry(
	struct fm_app *app,
	int index)
{
	struct fm_location location;
	struct fm_tab *tab;
	struct fm_entry *entry;

	/* The item. */
	tab = fm_ui_tab(app);
	if (index < 0 || (size_t)index >= tab->listing.count)
		return;
	entry = &tab->listing.entries[index];

	/* A folder opens in this tab. */
	if (entry->folder != 0) {
		memset(&location, 0, sizeof(location));
		location.kind = FM_LOCATION_FOLDER;
		snprintf(location.path, sizeof(location.path), "%s", entry->path);
		fm_ui_go(app, &location);
		return;
	}

	/* A file is only logged for now. */
	fm_log("OPEN path=%s", entry->path);
}

/* Names a kind of place for the log. */
static const char *
ui_location_kind_name(
	unsigned kind)
{
	/* Each kind's word. */
	switch (kind) {
	case FM_LOCATION_HOME:
		return "home";
	case FM_LOCATION_FOLDER:
		return "folder";
	case FM_LOCATION_RECENTS:
		return "recents";
	case FM_LOCATION_TRASH:
		return "trash";
	case FM_LOCATION_TAG:
		return "tag";
	case FM_LOCATION_SEARCH:
		return "search";
	default:
		break;
	}

	/* A kind this program does not know. */
	return "unknown";
}
