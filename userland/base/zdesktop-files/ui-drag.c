/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Dragging the selected items within the window (spec §14, design §10.4):
 * a press on an item that moves more than a few pixels drags the whole
 * selection.  Under the pointer a folder among the items, a place of the
 * sidebar or a tab is the target; the release moves the items there (on the
 * same device, else copies them), Ctrl copies, Ctrl+Shift makes links; a
 * tag's place tags them and the Trash throws them away.  Esc gives up.
 *
 * The items travel as their paths inside this window only; a drag to
 * another window is not offered (it needs Wayland's data device).
 */

#include "files.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

/* How far the pointer moves from the press before the items are dragged, in pixels. */
#define DRAG_START		6

/* The size of the dragged icon, and where it is from the pointer, in pixels. */
#define DRAG_ICON		52
#define DRAG_OFFSET_X		2
#define DRAG_OFFSET_Y		14

/* The radius of the badges on the dragged icon, in pixels. */
#define DRAG_BADGE		10

/* The badges' colors: the count's, a copy's and a link's. */
#define DRAG_COLOR_COUNT	FM_COLOR_ACCENT
#define DRAG_COLOR_COPY		FM_RGB(0x2fb45a)
#define DRAG_COLOR_LINK		FM_RGB(0x6b7585)
#define DRAG_COLOR_BADGE_TEXT	FM_RGB(0xffffff)

static void drag_start(struct fm_app *app);
static void drag_find(struct fm_app *app, int x, int y);
static unsigned drag_operation(struct fm_app *app);
static const char *drag_first(struct fm_app *app);
static const char *drag_verb(unsigned operation);
static void drag_drop_folder(struct fm_app *app);
static void drag_drop_tag(struct fm_app *app);
static void drag_end(struct fm_app *app);
static void drag_draw_target(struct fm_app *app, struct fm_canvas *canvas);
static void drag_draw_badges(struct fm_app *app, struct fm_canvas *canvas, float x, float y);

/*
 * Follows the pointer while the left button holds an item: the drag starts
 * once the pointer is far enough from the press, and then its target is
 * the region under the pointer.  Returns nonzero while the items are being
 * dragged.
 */
int
fm_drag_motion(
	struct fm_app *app,
	int x,
	int y)
{
	int dx;
	int dy;

	/* A drag in progress follows the pointer. */
	if (app->drag != 0) {
		drag_find(app, x, y);
		app->dirty = 1;
		return 1;
	}

	/* Not yet far enough from the press. */
	dx = x - app->press_x;
	dy = y - app->press_y;
	if (dx * dx + dy * dy <= DRAG_START * DRAG_START)
		return 0;

	/* The selection is dragged from here on. */
	drag_start(app);
	if (app->drag == 0)
		return 0;
	drag_find(app, x, y);
	app->dirty = 1;
	return 1;
}

/*
 * Ends a drag at the release of the button: the items go to the target, if
 * there is one.  Returns nonzero when a drag ended (the release then does
 * nothing else).
 */
int
fm_drag_release(
	struct fm_app *app)
{
	/* No drag: an ordinary release. */
	if (app->drag == 0)
		return 0;

	/* What the target does with the items. */
	switch (app->drag_target) {
	case FM_DRAG_FOLDER:
		drag_drop_folder(app);
		break;
	case FM_DRAG_TAG:
		drag_drop_tag(app);
		break;
	case FM_DRAG_TRASH:
		fm_log("DRAG drop operation=trash items=%lu", (unsigned long)app->drag_count);
		fm_action_trash(app);
		break;
	default:
		fm_log("DRAG drop operation=none");
		break;
	}

	/* The drag is over. */
	drag_end(app);
	return 1;
}

/*
 * Gives up a drag (Esc): nothing moves, and the press is over.
 */
void
fm_drag_cancel(
	struct fm_app *app)
{
	/* Only a drag in progress. */
	if (app->drag == 0)
		return;

	/* Said in the log, and ended. */
	fm_log("DRAG cancel");
	drag_end(app);
	app->pressing = 0;
}

/*
 * Draws a drag over the frame: the target lit, and the dragged items'
 * icon under the pointer with their count and what the drop will do.
 */
void
fm_drag_draw(
	struct fm_app *app,
	struct fm_canvas *canvas)
{
	struct fm_tab *tab;
	const struct fm_entry *entry;
	float x;
	float y;

	/* Only while dragging. */
	if (app->drag == 0)
		return;

	/* The target. */
	drag_draw_target(app, canvas);

	/* The pressed item stands for them all (the first selected one if it went). */
	tab = fm_ui_tab(app);
	entry = NULL;
	if (app->press_index >= 0 && (size_t)app->press_index < tab->listing.count)
		entry = &tab->listing.entries[app->press_index];
	if (entry == NULL)
		return;

	/* Its icon below and right of the pointer (the target's name stays in sight), on a soft shadow. */
	x = (float)(app->pointer_x + DRAG_OFFSET_X);
	y = (float)(app->pointer_y + DRAG_OFFSET_Y);
	fm_canvas_shadow(canvas, x + 4.0f, y + 6.0f, DRAG_ICON - 8.0f, DRAG_ICON - 8.0f, 8.0f, 10.0f, FM_COLOR_SHADOW);
	fm_grid_entry_icon(app, canvas, entry, x, y, (float)DRAG_ICON);

	/* The count and the operation. */
	drag_draw_badges(app, canvas, x, y);
}

/* Starts dragging the selection, when there is one. */
static void
drag_start(
	struct fm_app *app)
{
	struct fm_tab *tab;
	uint64_t bytes;
	size_t count;

	/* Only a selection is dragged. */
	tab = fm_ui_tab(app);
	count = fm_select_count(tab, &bytes);
	if (count == 0)
		return;

	/* The drag, with no target yet; the selection stays as it is. */
	app->drag = 1;
	app->drag_count = count;
	app->drag_target = FM_DRAG_NONE;
	app->drag_hit_kind = FM_HIT_NONE;
	app->drag_hit_index = -1;
	app->drag_tag = -1;
	app->drag_folder[0] = '\0';
	app->press_deferred = 0;
	app->band = 0;
	fm_log("DRAG start items=%lu", (unsigned long)count);
}

/* Finds the target under a point: a folder, a tag or the Trash; logged when it changes. */
static void
drag_find(
	struct fm_app *app,
	int x,
	int y)
{
	const struct fm_location *location;
	const struct fm_entry *entry;
	const struct fm_place *place;
	struct fm_tab *tab;
	const char *shown;
	unsigned target;
	unsigned kind;
	int index;
	int tag;
	int same;
	char folder[FM_PATH_MAX];

	/* The region under the point, and no target yet. */
	(void)fm_input_hit_at(app, x, y, &kind, &index);
	tab = fm_ui_tab(app);
	target = FM_DRAG_NONE;
	tag = -1;
	folder[0] = '\0';

	/* What each region stands for. */
	switch (kind) {
	case FM_HIT_ITEM:
		/* A folder among the items that is not itself dragged. */
		if (index < 0 || (size_t)index >= tab->listing.count)
			break;
		entry = &tab->listing.entries[index];
		if (entry->folder != 0 && entry->selected == 0) {
			target = FM_DRAG_FOLDER;
			snprintf(folder, sizeof(folder), "%s", entry->path);
		}

		break;
	case FM_HIT_PLACE:
		/* A place of the sidebar: a folder that is there, a tag, or the Trash. */
		if (index < 0 || index >= app->places.count)
			break;
		place = &app->places.items[index];
		if (place->missing != 0)
			break;
		if (place->location.kind == FM_LOCATION_FOLDER) {
			target = FM_DRAG_FOLDER;
			snprintf(folder, sizeof(folder), "%s", place->location.path);
		} else if (place->location.kind == FM_LOCATION_TAG) {
			tag = fm_tags_find(&app->tags, place->location.path);
			if (tag >= 0)
				target = FM_DRAG_TAG;
		} else if (place->location.kind == FM_LOCATION_TRASH) {
			if (tab->history[tab->history_index].location.kind != FM_LOCATION_TRASH)
				target = FM_DRAG_TRASH;
		}

		break;
	case FM_HIT_TAB:
	case FM_HIT_TAB_CLOSE:
		/* Another tab's folder. */
		if (index < 0 || index >= app->tab_count || index == app->tab_index)
			break;
		location = &app->tabs[index]->history[app->tabs[index]->history_index].location;
		if (location->kind == FM_LOCATION_FOLDER) {
			target = FM_DRAG_FOLDER;
			snprintf(folder, sizeof(folder), "%s", location->path);
		}

		break;
	default:
		break;
	}

	/* The folder the items are in already is no target. */
	shown = fm_current_folder(app);
	if (target == FM_DRAG_FOLDER && shown != NULL) {
		same = strcmp(folder, shown);
		if (same == 0)
			target = FM_DRAG_NONE;
	}

	/* Unchanged: nothing more to do. */
	if (target == app->drag_target && kind == app->drag_hit_kind && index == app->drag_hit_index)
		return;

	/* The new target, logged. */
	app->drag_target = target;
	app->drag_hit_kind = kind;
	app->drag_hit_index = index;
	app->drag_tag = tag;
	snprintf(app->drag_folder, sizeof(app->drag_folder), "%s", folder);
	if (target == FM_DRAG_FOLDER)
		fm_log("DRAG target kind=folder path=%s", folder);
	else if (target == FM_DRAG_TAG)
		fm_log("DRAG target kind=tag tag=%s", app->tags.items[tag].name);
	else if (target == FM_DRAG_TRASH)
		fm_log("DRAG target kind=trash");
	else
		fm_log("DRAG target kind=none");
}

/*
 * Works out what a drop on a folder does: Ctrl+Shift links, Ctrl copies,
 * and otherwise the items move within their device and are copied to
 * another one.  Returns an FM_TASK_* kind.
 */
static unsigned
drag_operation(
	struct fm_app *app)
{
	struct stat target;
	struct stat source;
	const char *first;
	int error;

	/* The keys held decide first. */
	if ((app->modifiers & (FM_MOD_CTRL | FM_MOD_SHIFT)) == (FM_MOD_CTRL | FM_MOD_SHIFT))
		return FM_TASK_LINK;
	if ((app->modifiers & FM_MOD_CTRL) != 0U)
		return FM_TASK_COPY;

	/* The target folder's device. */
	error = stat(app->drag_folder, &target);
	if (error != 0)
		return FM_TASK_MOVE;

	/* The first item's device. */
	first = drag_first(app);
	if (first == NULL)
		return FM_TASK_MOVE;
	error = lstat(first, &source);
	if (error != 0)
		return FM_TASK_MOVE;

	/* Another device: a copy. */
	if (target.st_dev != source.st_dev)
		return FM_TASK_COPY;

	/* The same device: a move. */
	return FM_TASK_MOVE;
}

/* Returns the first selected item's path, or NULL. */
static const char *
drag_first(
	struct fm_app *app)
{
	struct fm_tab *tab;
	size_t index;

	/* The first selected entry. */
	tab = fm_ui_tab(app);
	for (index = 0; index < tab->listing.count; index++) {
		if (tab->listing.entries[index].selected != 0)
			return tab->listing.entries[index].path;
	}

	/* None is selected. */
	return NULL;
}

/* Returns the log's word for a drop's operation. */
static const char *
drag_verb(
	unsigned operation)
{
	/* The three a folder takes. */
	if (operation == FM_TASK_LINK)
		return "link";
	if (operation == FM_TASK_COPY)
		return "copy";

	/* A move. */
	return "move";
}

/* Drops the items on a folder: a move, copy or link task into it. */
static void
drag_drop_folder(
	struct fm_app *app)
{
	unsigned operation;
	char **paths;
	size_t count;
	int error;

	/* The operation, and the selection's paths. */
	operation = drag_operation(app);
	error = fm_selected_paths(app, &paths, &count);
	if (error != 0 || count == 0) {
		fm_paths_free(paths, count);
		return;
	}

	/* Logged, and the task started. */
	fm_log("DRAG drop operation=%s items=%lu destination=%s", drag_verb(operation), (unsigned long)count, app->drag_folder);
	(void)fm_action_transfer(app, operation, paths, count, app->drag_folder);
	fm_paths_free(paths, count);
}

/* Drops the items on a tag's place: they get the tag, unless all have it already. */
static void
drag_drop_tag(
	struct fm_app *app)
{
	struct fm_tab *tab;
	size_t index;
	unsigned tags;
	int all;
	char message[96];

	/* Whether every dragged item has the tag. */
	tab = fm_ui_tab(app);
	all = 1;
	for (index = 0; index < tab->listing.count; index++) {
		if (tab->listing.entries[index].selected == 0)
			continue;
		tags = fm_tags_of(&app->tags, tab->listing.entries[index].path);
		if ((tags & (1U << app->drag_tag)) == 0U)
			all = 0;
	}

	/* A drop only adds the tag (the menus take it off). */
	fm_log("DRAG drop operation=tag items=%lu tag=%s", (unsigned long)app->drag_count, app->tags.items[app->drag_tag].name);
	if (all != 0) {
		snprintf(message, sizeof(message), "Already tagged %s", app->tags.items[app->drag_tag].name);
		fm_ui_message(app, message);
		return;
	}

	/* The tag goes on, as Alt+number does. */
	fm_action_toggle_tag(app, app->drag_tag);
}

/* Ends a drag: no target, no ghost. */
static void
drag_end(
	struct fm_app *app)
{
	/* Nothing is dragged any more. */
	app->drag = 0;
	app->drag_count = 0;
	app->drag_target = FM_DRAG_NONE;
	app->drag_hit_kind = FM_HIT_NONE;
	app->drag_hit_index = -1;
	app->drag_tag = -1;
	app->drag_folder[0] = '\0';
	app->press_deferred = 0;
	app->dirty = 1;
}

/* Lights the target's region: a tint and a blue edge. */
static void
drag_draw_target(
	struct fm_app *app,
	struct fm_canvas *canvas)
{
	const struct fm_rect *rect;
	int hit;

	/* Only a target. */
	if (app->drag_target == FM_DRAG_NONE)
		return;

	/* The last region of the frame that is the target's (the last drawn is the one under the pointer). */
	rect = NULL;
	for (hit = app->hit_count - 1; hit >= 0; hit--) {
		if (app->hits[hit].kind == app->drag_hit_kind && app->hits[hit].index == app->drag_hit_index) {
			rect = &app->hits[hit].rect;
			break;
		}
	}

	/* None was drawn (it scrolled away). */
	if (rect == NULL)
		return;

	/* The tint and the edge. */
	fm_canvas_round(canvas, (float)rect->x, (float)rect->y, (float)rect->width, (float)rect->height, 8.0f, FM_COLOR_SELECTION);
	fm_canvas_round_border(canvas, (float)rect->x, (float)rect->y, (float)rect->width, (float)rect->height, 8.0f, 2.0f, FM_COLOR_ACCENT);
}

/* Draws the dragged icon's badges: how many items (more than one), and a plus for a copy or an arrow for a link. */
static void
drag_draw_badges(
	struct fm_app *app,
	struct fm_canvas *canvas,
	float x,
	float y)
{
	unsigned operation;
	char count[24];
	float cx;
	float cy;
	int width;
	int baseline;

	/* The count at the top right. */
	if (app->drag_count > 1U) {
		cx = x + (float)DRAG_ICON - 2.0f;
		cy = y + 2.0f;
		snprintf(count, sizeof(count), "%lu", (unsigned long)app->drag_count);
		fm_canvas_circle(canvas, cx, cy, (float)DRAG_BADGE, DRAG_COLOR_COUNT);
		width = fm_text_width(app->text, count, strlen(count), 11U, 1);
		baseline = fm_text_center(11U, (int)cy - DRAG_BADGE, 2 * DRAG_BADGE);
		(void)fm_text_draw(app->text, canvas, (int)cx - width / 2, baseline, count, strlen(count), 11U, 1, DRAG_COLOR_BADGE_TEXT);
	}

	/* A folder target's operation; a move has no badge. */
	if (app->drag_target != FM_DRAG_FOLDER)
		return;
	operation = drag_operation(app);
	if (operation == FM_TASK_MOVE)
		return;

	/* A plus for a copy, an arrow for a link, at the bottom right. */
	cx = x + (float)DRAG_ICON - 2.0f;
	cy = y + (float)DRAG_ICON - 2.0f;
	if (operation == FM_TASK_COPY) {
		fm_canvas_circle(canvas, cx, cy, (float)DRAG_BADGE, DRAG_COLOR_COPY);
		fm_canvas_line(canvas, cx - 5.0f, cy, cx + 5.0f, cy, 2.0f, DRAG_COLOR_BADGE_TEXT);
		fm_canvas_line(canvas, cx, cy - 5.0f, cx, cy + 5.0f, 2.0f, DRAG_COLOR_BADGE_TEXT);
	} else {
		fm_canvas_circle(canvas, cx, cy, (float)DRAG_BADGE, DRAG_COLOR_LINK);
		fm_canvas_line(canvas, cx - 4.0f, cy + 4.0f, cx + 4.0f, cy - 4.0f, 2.0f, DRAG_COLOR_BADGE_TEXT);
		fm_canvas_line(canvas, cx - 1.0f, cy - 4.0f, cx + 4.0f, cy - 4.0f, 2.0f, DRAG_COLOR_BADGE_TEXT);
		fm_canvas_line(canvas, cx + 4.0f, cy - 4.0f, cx + 4.0f, cy + 1.0f, 2.0f, DRAG_COLOR_BADGE_TEXT);
	}
}
