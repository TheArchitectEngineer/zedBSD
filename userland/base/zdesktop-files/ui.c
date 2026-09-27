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



static void ui_layout(struct fm_app *app);
static void ui_draw_toolbar(struct fm_app *app, struct fm_canvas *canvas);
static void ui_draw_button(struct fm_app *app, struct fm_canvas *canvas, int x, int y, enum fm_icon icon, int enabled, unsigned kind);
static void ui_draw_crumbs(struct fm_app *app, struct fm_canvas *canvas, int x, int width);
static void ui_draw_search(struct fm_app *app, struct fm_canvas *canvas, const struct fm_rect *field);
static void ui_draw_views(struct fm_app *app, struct fm_canvas *canvas, int x, int y);
static void ui_draw_sidebar(struct fm_app *app, struct fm_canvas *canvas);
static void ui_draw_progress(struct fm_app *app, struct fm_canvas *canvas, const struct fm_rect *rect);
static int ui_place_current(struct fm_app *app, const struct fm_place *place);
static const char *ui_location_kind_name(unsigned kind);
static void ui_leave(struct fm_tab *tab);
static void ui_mark_cut(struct fm_tab *tab);
static void ui_select_paths(struct fm_app *app, struct fm_tab *tab);

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
	app->columns = FM_COLUMNS_DEFAULT;
	app->wall = time(NULL);
	app->focused = 1;
	app->dirty = 1;
	app->hover_index = -1;
	app->press_index = -1;
	snprintf(app->wallpaper, sizeof(app->wallpaper), "%s", FM_WALLPAPER);
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
	fm_tags_load(&app->tags);
	fm_places_init(&app->places, app->home, &app->tags);

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

	/* The operations, stopped and let go, the hero's pictures, the thumbnails and what the preview read. */
	fm_actions_release(app);
	fm_image_release(&app->hero_source);
	fm_image_release(&app->hero);
	fm_thumb_release(app);
	fm_peek_release(&app->peek);
	fm_info_release(&app->info);

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
		fm_input_motion(app, event);
		break;
	case FM_EVENT_BUTTON:
		fm_input_motion(app, event);
		fm_input_button(app, event);
		break;
	case FM_EVENT_AXIS:
		fm_input_scroll(app, event->scroll);
		break;
	case FM_EVENT_LEAVE:
		app->pointer_inside = 0;
		app->hover_kind = FM_HIT_NONE;
		app->hover_index = -1;
		app->dirty = 1;
		break;
	case FM_EVENT_KEY:
		fm_input_key(app, event);
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
	int made;
	int error;

	/* The time now, which the lists' dates and the messages are measured by. */
	app->now = now;
	app->wall = time(NULL);

	/* The tasks and the search move on. */
	(void)fm_actions_tick(app);
	fm_search_tick(app);

	/* The thumbnail asked for is made, and shown in a new frame. */
	made = fm_thumb_tick(app);
	if (made != 0)
		app->dirty = 1;

	/* The information's checksum moves on. */
	fm_info_tick(app);

	/* A message that has run its time goes. */
	if (app->message[0] != '\0' && now >= app->message_until) {
		app->message[0] = '\0';
		app->dirty = 1;
	}

	/* The tab shown, checked no more often than every two seconds. */
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
	fm_ui_reload(app, tab);
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

	/* The content panel, drawn by the view of the place, and the preview beside it when shown. */
	fm_grid_draw(app, canvas, &app->layout.content);
	if (app->show_preview != 0)
		fm_preview_draw(app, canvas, &app->layout.preview);

	/* The toolbar over everything, the tasks' list under it when open. */
	ui_draw_toolbar(app, canvas);
	if (app->show_tasks != 0 && app->task_count > 0)
		fm_tasks_draw(app, canvas, app->layout.toolbar.x + app->layout.toolbar.width - 8, app->layout.toolbar.y + app->layout.toolbar.height + 6);

	/* Quick Look, the information or Help over all of it, and a question over that. */
	fm_look_draw(app, canvas);
	fm_info_draw(app, canvas);
	fm_help_draw(app, canvas);
	fm_overlay_draw(app, canvas);

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
	int home_folder;
	int root_folder;
	int index;

	/* The place being left keeps its scroll and its cursor. */
	tab = fm_ui_tab(app);
	if (tab->history_count > 0)
		ui_leave(tab);

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

	/* A folder opened is kept among the recent folders (the home folder and the root are not worth it). */
	home_folder = strcmp(location->path, app->home);
	root_folder = strcmp(location->path, "/");
	if (location->kind == FM_LOCATION_FOLDER && home_folder != 0 && root_folder != 0)
		fm_home_folder_opened(location->path);

	/* The place's items, from the top, with nothing selected. */
	tab->scroll = 0;
	fm_dir_free(&tab->listing);
	fm_ui_reload(app, tab);
	app->focus = FM_FOCUS_CONTENT;
	app->band = 0;
	app->dirty = 1;
}

/*
 * Shows a short message in the status pill for a few seconds.
 */
void
fm_ui_message(
	struct fm_app *app,
	const char *message)
{
	/* The message, until three seconds from now. */
	snprintf(app->message, sizeof(app->message), "%s", message);
	app->message_until = app->now + 3000U;
	app->dirty = 1;
	fm_log("MESSAGE %s", message);
}

/*
 * Reports how long the main loop may sleep before the file manager has
 * work again: 0 while a thumbnail, an operation or a search is waiting to
 * move on, a short while when a search is about to start, and -1 when
 * nothing waits (only input wakes it).
 */
int
fm_ui_wait(
	struct fm_app *app)
{
	/* A thumbnail asked for, an operation, a search walking or a checksum: no sleep. */
	if (app->thumb_wanted[0] != '\0')
		return 0;
	if (app->task_count > 0)
		return 0;
	if (app->search.active != 0)
		return 0;
	if (app->info_open != 0 && app->info.checksum_state == FM_CHECKSUM_RUNNING)
		return 0;

	/* A search typed a moment ago starts soon. */
	if (app->search_typed_at != 0U)
		return 20;

	/* Nothing waits. */
	return -1;
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

/*
 * Fills the parts of the path shown in the toolbar and returns how many there are.
 */
int
fm_ui_crumbs(
	struct fm_app *app,
	struct fm_crumb *crumbs,
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

/*
 * Reads the items of the place a tab shows, sorted, and logs the place.
 *
 * The items selected before (and the cursor) stay selected when they are
 * still there, so a folder read again after a change keeps its selection;
 * a place come back to by the history gets its cursor back.
 */
void
fm_ui_reload(
	struct fm_app *app,
	struct fm_tab *tab)
{
	const struct fm_location *location;
	struct fm_visit *visit;
	const char *path;
	char trash[FM_PATH_MAX];
	char **kept;
	char *cursor_name;
	size_t kept_count;
	size_t index;
	int found;
	int error;

	/* The place, and the folder its items come from. */
	visit = &tab->history[tab->history_index];
	location = &visit->location;
	path = NULL;
	if (location->kind == FM_LOCATION_FOLDER)
		path = location->path;

	/* The names of the selected items and of the cursor's, taken from the old listing. */
	kept_count = 0;
	kept = NULL;
	cursor_name = NULL;
	if (tab->listing.count != 0U)
		kept = calloc(tab->listing.count, sizeof(kept[0]));
	for (index = 0; kept != NULL && index < tab->listing.count; index++) {
		if (tab->listing.entries[index].selected == 0)
			continue;
		kept[kept_count] = tab->listing.entries[index].name;
		tab->listing.entries[index].name = NULL;
		kept_count++;
	}

	/* The cursor's name, taken the same way. */
	if (tab->cursor >= 0 &&
	    (size_t)tab->cursor < tab->listing.count &&
	    tab->listing.entries[tab->cursor].name != NULL) {
		cursor_name = tab->listing.entries[tab->cursor].name;
		tab->listing.entries[tab->cursor].name = NULL;
	}

	/* A folder's items, the trash's, or the items of a place that is not one folder. */
	fm_dir_free(&tab->listing);
	if (location->kind != FM_LOCATION_SEARCH)
		fm_search_stop(&app->search);
	if (path != NULL) {
		(void)fm_dir_read(&tab->listing, path, app->show_hidden);
	} else if (location->kind == FM_LOCATION_TRASH) {
		error = fm_trash_path(trash, sizeof(trash));
		if (error == 0)
			(void)fm_dir_read_trash(&tab->listing, trash);
	} else if (location->kind == FM_LOCATION_HOME) {
		fm_home_gather(app);
	} else {
		fm_search_load(app, tab);
	}

	/* The items in the window's order (the recent files stay newest first), with their tags, the cut ones marked. */
	if (location->kind != FM_LOCATION_RECENTS)
		fm_dir_sort(&tab->listing, app->sort, app->sort_reverse);
	for (index = 0; index < tab->listing.count; index++)
		tab->listing.entries[index].tags = fm_tags_of(&app->tags, tab->listing.entries[index].path);
	ui_mark_cut(tab);

	/* Nothing has the cursor yet, and the folder was just checked. */
	tab->cursor = -1;
	tab->anchor = -1;
	tab->checked_at = app->now;

	/* The kept names selected again where they are still there. */
	for (index = 0; index < kept_count; index++) {
		found = fm_select_find(tab, kept[index]);
		if (found >= 0)
			tab->listing.entries[found].selected = 1;
		free(kept[index]);
	}

	/* The names are not needed any more. */
	free(kept);

	/* The cursor on its item again. */
	if (cursor_name != NULL) {
		found = fm_select_find(tab, cursor_name);
		tab->cursor = found;
		tab->anchor = found;
		free(cursor_name);
	}

	/* A place come back to gets the cursor it was left with. */
	if (kept_count == 0U && tab->cursor < 0 && visit->cursor[0] != '\0') {
		found = fm_select_find(tab, visit->cursor);
		fm_select_only(tab, found);
	}

	/* What a finished operation made is selected instead, the first one with the cursor. */
	ui_select_paths(app, tab);

	/* The log line the tests wait for. */
	fm_log("LOCATION kind=%s path=%s items=%lu error=%d", ui_location_kind_name(location->kind), location->path, (unsigned long)tab->listing.count, tab->listing.error);
}

/*
 * Goes one step back in the tab's history.
 */
void
fm_ui_back(
	struct fm_app *app)
{
	struct fm_tab *tab;

	/* The first step has nothing behind it. */
	tab = fm_ui_tab(app);
	if (tab->history_index == 0)
		return;

	/* The step before, at the scroll it was left with. */
	ui_leave(tab);
	tab->history_index--;
	fm_dir_free(&tab->listing);
	fm_ui_reload(app, tab);
	tab->scroll = tab->history[tab->history_index].scroll;
	app->dirty = 1;
}

/*
 * Goes one step forward in the tab's history.
 */
void
fm_ui_forward(
	struct fm_app *app)
{
	struct fm_tab *tab;

	/* The last step has nothing after it. */
	tab = fm_ui_tab(app);
	if (tab->history_index + 1 >= tab->history_count)
		return;

	/* The step after, at the scroll it was left with. */
	ui_leave(tab);
	tab->history_index++;
	fm_dir_free(&tab->listing);
	fm_ui_reload(app, tab);
	tab->scroll = tab->history[tab->history_index].scroll;
	app->dirty = 1;
}

/*
 * Opens an item of the listing: a folder in the tab, a file with its
 * default application.
 */
void
fm_ui_open(
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

	/* A file opens with its default way. */
	fm_open_entry(app, index, 0);
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
	struct fm_rect ring;
	struct fm_tab *tab;
	int crumbs_x;
	int views_x;
	int right;
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

	/* The progress ring left of the search field while operations run. */
	right = field.x - 16;
	if (app->task_count > 0) {
		ring.x = field.x - 12 - UI_BUTTON_SIZE;
		ring.y = y;
		ring.width = UI_BUTTON_SIZE;
		ring.height = UI_BUTTON_SIZE;
		ui_draw_progress(app, canvas, &ring);
		right = ring.x - 8;
	}

	/* The path between home and the search field. */
	crumbs_x = bar->x + 8 + 3 * (UI_BUTTON_SIZE + 4) + 16;
	ui_draw_crumbs(app, canvas, crumbs_x, right - crumbs_x);
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
	static struct fm_crumb crumbs[FM_CRUMBS];
	struct fm_rect field;
	struct fm_rect rect;
	fm_color color;
	int widths[FM_CRUMBS];
	int count;
	int first;
	int total;
	int index;
	int baseline;
	int bold;
	int pen;

	/* While a path is typed (Ctrl+L), the field takes the parts' place. */
	if (app->focus == FM_FOCUS_LOCATION) {
		field.x = x;
		field.y = app->layout.toolbar.y + 7;
		field.width = width;
		field.height = app->layout.toolbar.height - 14;
		fm_canvas_round(canvas, (float)field.x, (float)field.y, (float)field.width, (float)field.height, 8.0f, FM_RGB(0xf1f4f8));
		fm_canvas_round_border(canvas, (float)field.x, (float)field.y, (float)field.width, (float)field.height, 8.0f, 1.5f, FM_RGBA(0x2f7cf6, 150));
		field.x += 10;
		field.width -= 20;
		fm_field_draw(app, canvas, &app->location, &field, UI_TEXT_TOOLBAR, "Go to folder");
		return;
	}

	/* The parts and their widths (the last part bold). */
	count = fm_ui_crumbs(app, crumbs, FM_CRUMBS);
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
	const struct fm_location *location;
	struct fm_tab *tab;
	struct fm_rect text;
	int baseline;

	/* The field's ground, a little darker under the pointer. */
	fm_canvas_round(canvas, (float)field->x, (float)field->y, (float)field->width, (float)field->height, (float)field->height * 0.5f, FM_RGB(0xf1f4f8));
	if (app->hover_kind == FM_HIT_SEARCH)
		fm_canvas_round(canvas, (float)field->x, (float)field->y, (float)field->width, (float)field->height, (float)field->height * 0.5f, FM_COLOR_HOVER);

	/* The magnifier. */
	fm_icon_draw(canvas, FM_ICON_SEARCH, (float)field->x + 9.0f, (float)field->y + 7.0f, 16.0f, FM_COLOR_TEXT_SECONDARY);
	tab = fm_ui_tab(app);
	location = &tab->history[tab->history_index].location;

	/* Being typed in: an accent edge and the text with its cursor. */
	if (app->focus == FM_FOCUS_SEARCH) {
		fm_canvas_round_border(canvas, (float)field->x, (float)field->y, (float)field->width, (float)field->height, (float)field->height * 0.5f, 1.5f, FM_RGBA(0x2f7cf6, 150));
		text.x = field->x + 32;
		text.y = field->y;
		text.width = field->width - 44;
		text.height = field->height;
		fm_field_draw(app, canvas, &app->search_field, &text, 13U, "Search");
	} else if (location->kind == FM_LOCATION_SEARCH) {
		baseline = fm_text_center(13U, field->y, field->height);
		(void)fm_text_draw_fit(app->text, canvas, field->x + 32, baseline, location->path, 13U, 0, field->width - 44, FM_COLOR_TEXT);
	} else {
		baseline = fm_text_center(13U, field->y, field->height);
		(void)fm_text_draw(app->text, canvas, field->x + 32, baseline, "Search", 6, 13U, 0, FM_COLOR_TEXT_FAINT);
	}

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

/* Draws the progress ring of the running operations: the first one's share done, lit under the pointer. */
static void
ui_draw_progress(
	struct fm_app *app,
	struct fm_canvas *canvas,
	const struct fm_rect *rect)
{
	const struct fm_task *task;
	float fraction;
	float cx;
	float cy;

	/* The first task's share of its bytes and items. */
	task = app->tasks[0];
	fraction = 0.0f;
	if (task->bytes_total + task->files_total != 0U)
		fraction = (float)(task->bytes_done + task->files_done * 4096U) / (float)(task->bytes_total + task->files_total * 4096U);
	if (fraction < 0.03f)
		fraction = 0.03f;
	if (fraction > 1.0f)
		fraction = 1.0f;

	/* A faint whole ring and the part done in the accent. */
	cx = (float)rect->x + (float)rect->width * 0.5f;
	cy = (float)rect->y + (float)rect->height * 0.5f;
	if (app->hover_kind == FM_HIT_PROGRESS || app->show_tasks != 0)
		fm_canvas_circle(canvas, cx, cy, (float)rect->width * 0.5f, FM_COLOR_HOVER);
	fm_canvas_ring(canvas, cx, cy, 9.0f, 2.5f, 1.0f, FM_RGB(0xdfe5ee));
	fm_canvas_ring(canvas, cx, cy, 9.0f, 2.5f, fraction, FM_COLOR_ACCENT);

	/* It opens the list of operations. */
	fm_ui_hit(app, rect, FM_HIT_PROGRESS, 0);
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
	struct fm_rect remove;
	fm_color ink;
	unsigned section;
	int current;
	int removable;
	int hovered;
	int index;
	int y;

	/* The panel: a light veil over the ground. */
	panel = &app->layout.sidebar;
	fm_canvas_round(canvas, (float)panel->x, (float)panel->y, (float)panel->width, (float)panel->height, UI_PANEL_RADIUS, FM_COLOR_SIDEBAR);
	fm_canvas_round_border(canvas, (float)panel->x, (float)panel->y, (float)panel->width, (float)panel->height, UI_PANEL_RADIUS, 1.0f, FM_RGBA(0xffffff, 170));
	fm_canvas_clip_push(canvas, panel);

	/* Each place under its section's title, the list scrolled when it is taller than the panel. */
	section = FM_PLACES;
	y = panel->y + 8 - app->sidebar_scroll;
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

		/* A favorite folder under the pointer offers a small button that takes it off the sidebar. */
		removable = 0;
		if (place->section == FM_SECTION_FAVORITES && place->location.kind == FM_LOCATION_FOLDER)
			removable = 1;
		hovered = 0;
		if (app->hover_kind == FM_HIT_PLACE && app->hover_index == index)
			hovered = 1;
		if (app->hover_kind == FM_HIT_BUTTON && app->hover_index == FM_BUTTON_REMOVE_PLACE + index)
			hovered = 1;
		if (removable != 0 && hovered != 0) {
			remove.x = row.x + row.width - 26;
			remove.y = row.y + 5;
			remove.width = 20;
			remove.height = 20;
			fm_canvas_circle(canvas, (float)remove.x + 10.0f, (float)remove.y + 10.0f, 9.0f, FM_RGBA(0x5a6b85, 40));
			fm_icon_draw(canvas, FM_ICON_CLOSE, (float)remove.x + 3.0f, (float)remove.y + 3.0f, 14.0f, FM_COLOR_TEXT_SECONDARY);
			fm_ui_hit(app, &remove, FM_HIT_BUTTON, FM_BUTTON_REMOVE_PLACE + index);
		}

		/* The next row. */
		y += UI_SIDEBAR_ROW;
	}

	/* How tall the list is, which the scrolling is kept within. */
	app->layout.sidebar_height = y + app->sidebar_scroll - panel->y + 8;

	/* The panel's clip ends. */
	fm_canvas_clip_pop(canvas);
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

/* Keeps, in the tab's current step, its scroll and the name of the item with the cursor. */
static void
ui_leave(
	struct fm_tab *tab)
{
	struct fm_visit *visit;

	/* The scroll. */
	visit = &tab->history[tab->history_index];
	visit->scroll = tab->scroll;

	/* The cursor's item by name (the items may be listed in another order when the tab comes back). */
	visit->cursor[0] = '\0';
	if (tab->cursor >= 0 && (size_t)tab->cursor < tab->listing.count)
		snprintf(visit->cursor, sizeof(visit->cursor), "%s", tab->listing.entries[tab->cursor].name);
}

/* Marks the items that are cut on the clipboard (they are drawn faded until the paste). */
static void
ui_mark_cut(
	struct fm_tab *tab)
{
	char **paths;
	unsigned mode;
	size_t count;
	size_t index;
	size_t cut;
	int error;
	int match;

	/* The clipboard; only a cut marks anything. */
	error = fm_clip_get(&mode, &paths, &count);
	if (error != 0 || mode != FM_CLIP_CUT) {
		fm_paths_free(paths, count);
		return;
	}

	/* Each item whose path is on it. */
	for (index = 0; index < tab->listing.count; index++) {
		for (cut = 0; cut < count; cut++) {
			match = strcmp(tab->listing.entries[index].path, paths[cut]);
			if (match == 0)
				tab->listing.entries[index].cut = 1;
		}
	}

	/* The clipboard's paths are not needed any more. */
	fm_paths_free(paths, count);
}

/* Selects the items of the paths a finished operation left (and drops them). */
static void
ui_select_paths(
	struct fm_app *app,
	struct fm_tab *tab)
{
	size_t index;
	size_t wanted;
	int first;
	int match;

	/* Nothing waits to be selected. */
	if (app->select_count == 0U)
		return;

	/* The items of those paths, and nothing else. */
	first = -1;
	fm_select_none(tab);
	for (index = 0; index < tab->listing.count; index++) {
		for (wanted = 0; wanted < app->select_count; wanted++) {
			match = strcmp(tab->listing.entries[index].path, app->select_paths[wanted]);
			if (match != 0)
				continue;
			tab->listing.entries[index].selected = 1;
			if (first < 0)
				first = (int)index;
		}
	}

	/* The first has the cursor. */
	tab->cursor = first;
	tab->anchor = first;

	/* The paths were used. */
	fm_paths_free(app->select_paths, app->select_count);
	app->select_paths = NULL;
	app->select_count = 0;
}
