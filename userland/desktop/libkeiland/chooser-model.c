/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The file chooser's model (ws092-p003): the folder shown and its items,
 * the sidebar's places, the filters, the name typed, and what the keys,
 * the pointer and the fingers do to them, up to the answer.
 *
 * Nothing here knows Wayland or draws; chooser.c feeds it the window's
 * input and chooser-draw.c shows it.  Every input function returns 1 when
 * the window must be drawn again.
 */

#include "chooser.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

/* The longest time between the clicks of a double click, in milliseconds. */
#define MODEL_DOUBLE_CLICK_MS	400U

/* How many rows PageUp and PageDown move at least. */
#define MODEL_PAGE_MIN		1

/* How many items the array grows by at first. */
#define MODEL_ENTRIES_FIRST	64U

/* How many codes the character tables cover (up to the space bar). */
#define MODEL_KEYS_TABLE	58U

/*
 * The character each key types without shift, by evdev code; 0 for a key
 * that types none.  zdesktop forwards evdev codes with no keymap, so the
 * chooser carries the US layout, as Terminal and Text Editor do.
 */
static const char model_keys_plain[MODEL_KEYS_TABLE] = {
	0, 0, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', 0, 0,
	'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', 0, 0,
	'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0, '\\',
	'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' '
};

/* The character each key types with shift, by evdev code. */
static const char model_keys_shifted[MODEL_KEYS_TABLE] = {
	0, 0, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', 0, 0,
	'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', 0, 0,
	'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~', 0, '|',
	'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0, '*', 0, ' '
};

static void model_places(struct kl_chooser *chooser);
static void model_add_place(struct kl_chooser *chooser, const char *label, const char *path, enum kl_chooser_icon icon);
static const char *model_home(void);
static int model_is_folder(const char *path);
static int model_read(struct kl_chooser *chooser, const char *folder);
static int model_add_entry(struct kl_chooser *chooser, const char *name, const char *path, const struct stat *status);
static void model_clear(struct kl_chooser *chooser);
static int model_compare(const void *left, const void *right);
static int model_matches(const struct kl_chooser *chooser, const char *name);
static void model_reload(struct kl_chooser *chooser);
static void model_select(struct kl_chooser *chooser, int index);
static void model_move(struct kl_chooser *chooser, int delta);
static void model_visible(struct kl_chooser *chooser, int index);
static int model_page(const struct kl_chooser *chooser);
static int model_activate(struct kl_chooser *chooser, int index, int twice);
static int model_accept(struct kl_chooser *chooser);
static int model_accept_save(struct kl_chooser *chooser);
static int model_accept_path(struct kl_chooser *chooser);
static void model_answer(struct kl_chooser *chooser, const char *path);
static void model_join(const char *folder, const char *name, char *out, size_t size);
static void model_message(struct kl_chooser *chooser, const char *format, const char *name);
static void model_open_path(struct kl_chooser *chooser);
static void model_type_select(struct kl_chooser *chooser, uint32_t character);
static int model_field_key(struct kl_chooser *chooser, struct kl_chooser_field *field, uint32_t key, uint32_t character);
static void model_field_set(struct kl_chooser_field *field, const char *text, int stem);
static void model_field_erase(struct kl_chooser_field *field);
static size_t model_field_prev(const struct kl_chooser_field *field, size_t at);
static size_t model_field_next(const struct kl_chooser_field *field, size_t at);
static const char *model_base(const char *path);

/*
 * Makes a chooser's model from the options an application gave, and lists
 * the folder it starts in.
 *
 * Returns 0, or EINVAL for options it cannot follow.
 */
int
kl_chooser_init(
	struct kl_chooser *chooser,
	const struct keiland_file_chooser_options *options)
{
	const struct keiland_file_filter *filter;
	const char *start;
	size_t index;
	int folder;
	int error;

	/* Nothing is set yet. */
	memset(chooser, 0, sizeof(*chooser));
	chooser->selected = -1;
	chooser->width = KL_CHOOSER_WIDTH;
	chooser->height = KL_CHOOSER_HEIGHT;

	/* Only the two modes, and filters within the bounds. */
	if (options == NULL)
		return EINVAL;
	if (options->mode != KEILAND_FILE_CHOOSER_OPEN && options->mode != KEILAND_FILE_CHOOSER_SAVE)
		return EINVAL;
	if (options->filter_count > KEILAND_FILE_CHOOSER_FILTERS_MAX)
		return EINVAL;
	if (options->filter_count > 0U && options->filters == NULL)
		return EINVAL;
	if (options->filter_count > 0U && options->filter >= options->filter_count)
		return EINVAL;
	chooser->mode = options->mode;

	/* The window's title, given or the mode's. */
	if (options->title != NULL) {
		snprintf(chooser->title, sizeof(chooser->title), "%s", options->title);
	} else if (chooser->mode == KEILAND_FILE_CHOOSER_SAVE) {
		snprintf(chooser->title, sizeof(chooser->title), "Save As");
	} else {
		snprintf(chooser->title, sizeof(chooser->title), "Open");
	}

	/* The filters, copied (the application's strings need not outlive the call). */
	for (index = 0; index < options->filter_count; index++) {
		filter = &options->filters[index];
		if (filter->label != NULL)
			snprintf(chooser->filters[index].label, sizeof(chooser->filters[index].label), "%s", filter->label);
		if (filter->extensions != NULL)
			snprintf(chooser->filters[index].extensions, sizeof(chooser->filters[index].extensions), "%s", filter->extensions);
	}

	/* How many there are, and the one chosen first. */
	chooser->filter_count = options->filter_count;
	chooser->filter = options->filter;

	/* The places of the sidebar. */
	model_places(chooser);

	/* Save starts with the name given, selected up to its extension, and types into it. */
	chooser->focus = KL_FOCUS_LIST;
	if (chooser->mode == KEILAND_FILE_CHOOSER_SAVE) {
		if (options->name != NULL)
			model_field_set(&chooser->name, options->name, 1);
		chooser->focus = KL_FOCUS_NAME;
	}

	/* The folder given when it is one, else the home folder, else the root. */
	start = options->folder;
	folder = 0;
	if (start != NULL)
		folder = model_is_folder(start);
	if (!folder)
		start = model_home();

	/* Its items. */
	kl_chooser_layout(chooser);
	error = kl_chooser_go(chooser, start);
	if (error != 0)
		(void)kl_chooser_go(chooser, "/");

	/* Succeeded: the chooser shows its first folder. */
	return 0;
}

/*
 * Frees what a chooser's model holds.
 */
void
kl_chooser_fini(
	struct kl_chooser *chooser)
{
	/* The items and their array. */
	model_clear(chooser);
	free(chooser->entries);
	chooser->entries = NULL;
	chooser->capacity = 0;
}

/*
 * Shows a folder: its items, none selected, scrolled to the top.
 *
 * Returns 0, or the errno value of finding the folder (the folder shown
 * stays then).  A folder found but not readable is shown empty with the
 * reason.
 */
int
kl_chooser_go(
	struct kl_chooser *chooser,
	const char *path)
{
	char resolved[KL_CHOOSER_PATH_MAX];
	char *found;
	int folder;

	/* The folder's real path. */
	found = realpath(path, resolved);
	if (found == NULL)
		return errno;

	/* Only a folder can be shown. */
	folder = model_is_folder(resolved);
	if (!folder)
		return ENOTDIR;

	/* The folder, then its items. */
	snprintf(chooser->folder, sizeof(chooser->folder), "%s", resolved);
	chooser->recent = 0;
	chooser->generation++;
	chooser->list_error = model_read(chooser, chooser->folder);

	/* Nothing selected, at the top, and no message left from before. */
	chooser->selected = -1;
	chooser->scroll = 0.0;
	chooser->scroll_moved = 1;
	chooser->message[0] = '\0';
	chooser->hover_part = KL_PART_NONE;

	/* Succeeded: the folder is shown. */
	return 0;
}

/*
 * Shows the recent files (for Open): the ones that still exist and pass
 * the filter, newest first.
 */
void
kl_chooser_go_recent(
	struct kl_chooser *chooser)
{
	struct keiland_recent_item *items;
	struct stat status;
	size_t count;
	size_t index;
	int regular;
	int matches;
	int error;

	/* The folder's items go; Recent is shown even when it cannot be read. */
	model_clear(chooser);
	chooser->recent = 1;
	chooser->generation++;
	chooser->list_error = 0;
	chooser->selected = -1;
	chooser->scroll = 0.0;
	chooser->scroll_moved = 1;
	chooser->message[0] = '\0';
	chooser->hover_part = KL_PART_NONE;

	/* Room for the list. */
	items = calloc(KL_CHOOSER_RECENT_MAX, sizeof(*items));
	if (items == NULL) {
		chooser->list_error = ENOMEM;
		return;
	}

	/* The list, newest first. */
	count = 0;
	error = keiland_recent_list(items, KL_CHOOSER_RECENT_MAX, &count);
	if (error != 0) {
		free(items);
		return;
	}

	/* Each file that is still a file and passes the filter. */
	for (index = 0; index < count; index++) {
		error = stat(items[index].path, &status);
		if (error != 0)
			continue;
		regular = S_ISREG(status.st_mode);
		if (!regular)
			continue;
		matches = model_matches(chooser, model_base(items[index].path));
		if (!matches)
			continue;
		error = model_add_entry(chooser, model_base(items[index].path), items[index].path, &status);
		if (error != 0)
			break;
	}

	/* The list read is not needed any more. */
	free(items);
}

/*
 * Shows the folder that holds the one shown (nothing above the root or
 * above Recent).
 */
void
kl_chooser_go_up(
	struct kl_chooser *chooser)
{
	char parent[KL_CHOOSER_PATH_MAX];
	char *slash;
	int root;

	/* Recent and the root have nothing above them. */
	if (chooser->recent)
		return;
	root = strcmp(chooser->folder, "/");
	if (root == 0)
		return;

	/* The path without its last part. */
	snprintf(parent, sizeof(parent), "%s", chooser->folder);
	slash = strrchr(parent, '/');
	if (slash == NULL)
		return;
	if (slash == parent)
		slash[1] = '\0';
	else
		*slash = '\0';

	/* That folder. */
	(void)kl_chooser_go(chooser, parent);
}

/*
 * Gives the window a new size, and lays it out again.
 */
void
kl_chooser_resize(
	struct kl_chooser *chooser,
	int width,
	int height)
{
	/* The smallest the window may be. */
	if (width < KL_CHOOSER_MIN_WIDTH)
		width = KL_CHOOSER_MIN_WIDTH;
	if (height < KL_CHOOSER_MIN_HEIGHT)
		height = KL_CHOOSER_MIN_HEIGHT;

	/* The size and the layout, and a scroll that still fits. */
	chooser->width = width;
	chooser->height = height;
	kl_chooser_layout(chooser);
	kl_chooser_set_scroll(chooser, chooser->scroll);
}

/*
 * Reports how far the list can scroll: its rows' height past the part of
 * the window that shows them.
 */
double
kl_chooser_scroll_max(
	const struct kl_chooser *chooser)
{
	double rows;
	double shown;

	/* The rows' height and what shows of it. */
	rows = (double)chooser->count * (double)KL_CHOOSER_ROW;
	shown = (double)chooser->layout.list.height;

	/* Everything shows: no scroll. */
	if (rows <= shown)
		return 0.0;

	/* Reports the part that does not show. */
	return rows - shown;
}

/*
 * Scrolls the list (a finger may pull it a little past its ends; the
 * rubber band is the scroller's).
 */
void
kl_chooser_set_scroll(
	struct kl_chooser *chooser,
	double scroll)
{
	double maximum;

	/* The farthest a finger can pull, a third of the list's height past either end. */
	maximum = kl_chooser_scroll_max(chooser);
	if (scroll < -(double)chooser->layout.list.height / 3.0)
		scroll = -(double)chooser->layout.list.height / 3.0;
	if (scroll > maximum + (double)chooser->layout.list.height / 3.0)
		scroll = maximum + (double)chooser->layout.list.height / 3.0;

	/* The scroll. */
	chooser->scroll = scroll;
}

/*
 * Carries out a key pressed (character is what it types, 0 for none).
 */
int
kl_chooser_key(
	struct kl_chooser *chooser,
	uint32_t key,
	uint32_t character)
{
	unsigned control;
	unsigned alt;
	int changed;

	/* An answered chooser takes nothing more. */
	if (chooser->answered)
		return 0;
	control = chooser->modifiers & KL_MOD_CTRL;
	alt = chooser->modifiers & KL_MOD_ALT;

	/* The question before replacing: Enter replaces, Esc goes back; nothing else. */
	if (chooser->confirm) {
		if (key == KL_KEY_ENTER || key == KL_KEY_KPENTER) {
			model_answer(chooser, chooser->confirm_path);
			return 1;
		}

		/* Esc keeps the file. */
		if (key == KL_KEY_ESC) {
			chooser->confirm = 0;
			return 1;
		}

		/* Nothing else while the question is asked. */
		return 0;
	}

	/* A message is gone at the next key. */
	changed = 0;
	if (chooser->message[0] != '\0') {
		chooser->message[0] = '\0';
		changed = 1;
	}

	/* The keys that mean the same wherever the keyboard types. */
	switch (key) {
	case KL_KEY_ESC:
		/* Esc closes the path field first, then cancels. */
		if (chooser->focus == KL_FOCUS_PATH) {
			chooser->focus = KL_FOCUS_LIST;
			if (chooser->mode == KEILAND_FILE_CHOOSER_SAVE)
				chooser->focus = KL_FOCUS_NAME;
			return 1;
		}

		/* Otherwise the chooser ends without a path. */
		kl_chooser_cancel(chooser);
		return 1;
	case KL_KEY_ENTER:
	case KL_KEY_KPENTER:
		/* Enter goes to the path typed, into the folder selected, or accepts. */
		if (chooser->focus == KL_FOCUS_PATH) {
			(void)model_accept_path(chooser);
			return 1;
		}

		/* Otherwise into the folder selected, or the answer. */
		(void)model_accept(chooser);
		return 1;
	case KL_KEY_UP:
		/* Alt+Up goes to the folder above; Up alone selects the item above. */
		if (alt != 0U) {
			kl_chooser_go_up(chooser);
			return 1;
		}

		/* Up alone: the item above. */
		model_move(chooser, -1);
		return 1;
	case KL_KEY_DOWN:
		model_move(chooser, 1);
		return 1;
	case KL_KEY_PAGEUP:
		model_move(chooser, -model_page(chooser));
		return 1;
	case KL_KEY_PAGEDOWN:
		model_move(chooser, model_page(chooser));
		return 1;
	case KL_KEY_TAB:
		/* Tab moves the keyboard between the list and the name (Save). */
		if (chooser->mode != KEILAND_FILE_CHOOSER_SAVE)
			return changed;
		if (chooser->focus == KL_FOCUS_NAME)
			chooser->focus = KL_FOCUS_LIST;
		else
			chooser->focus = KL_FOCUS_NAME;
		return 1;
	default:
		break;
	}

	/* Control's commands: hidden items and the path field. */
	if (control != 0U) {
		if (key == KL_KEY_H) {
			chooser->show_hidden = !chooser->show_hidden;
			model_reload(chooser);
			return 1;
		}

		/* Ctrl+L types a path. */
		if (key == KL_KEY_L) {
			model_open_path(chooser);
			return 1;
		}
	}

	/* A field takes the rest when it has the keyboard. */
	if (chooser->focus == KL_FOCUS_NAME) {
		changed |= model_field_key(chooser, &chooser->name, key, character);
		return changed;
	}

	/* And so does the path's field. */
	if (chooser->focus == KL_FOCUS_PATH) {
		changed |= model_field_key(chooser, &chooser->path, key, character);
		return changed;
	}

	/* The list's own keys: the ends, the folder above, and a letter that finds an item. */
	switch (key) {
	case KL_KEY_HOME:
		model_move(chooser, -(int)chooser->count);
		return 1;
	case KL_KEY_END:
		model_move(chooser, (int)chooser->count);
		return 1;
	case KL_KEY_BACKSPACE:
		kl_chooser_go_up(chooser);
		return 1;
	default:
		break;
	}

	/* A character selects the next item whose name starts with it. */
	if (character != 0U && control == 0U) {
		model_type_select(chooser, character);
		return 1;
	}

	/* Anything else changes nothing (but a message that went). */
	return changed;
}

/*
 * Follows the pointer: the part under it is lit.
 */
int
kl_chooser_motion(
	struct kl_chooser *chooser,
	int x,
	int y)
{
	enum kl_chooser_part part;
	int index;

	/* The part under the pointer. */
	index = -1;
	part = kl_chooser_hit(chooser, x, y, &index);

	/* The same as before: nothing to draw. */
	if (part == chooser->hover_part && index == chooser->hover_index)
		return 0;

	/* Succeeded: the new part is lit. */
	chooser->hover_part = part;
	chooser->hover_index = index;
	return 1;
}

/*
 * Carries out a click of the main button at a point (at a time, for a
 * double click).
 */
int
kl_chooser_click(
	struct kl_chooser *chooser,
	int x,
	int y,
	uint64_t time_ms)
{
	enum kl_chooser_part part;
	int twice;
	int index;

	/* An answered chooser takes nothing more. */
	if (chooser->answered)
		return 0;

	/* The part clicked, and whether it is the second click on it. */
	index = -1;
	part = kl_chooser_hit(chooser, x, y, &index);
	twice = 0;
	if (part == chooser->click_part &&
	    index == chooser->click_index &&
	    time_ms - chooser->click_ms <= MODEL_DOUBLE_CLICK_MS)
		twice = 1;
	chooser->click_part = part;
	chooser->click_index = index;
	chooser->click_ms = time_ms;

	/* A double click ends there; the next click starts again. */
	if (twice)
		chooser->click_part = KL_PART_NONE;

	/* A click clears a message. */
	chooser->message[0] = '\0';

	/* What the part does. */
	switch (part) {
	case KL_PART_ROW:
		(void)model_activate(chooser, index, twice);
		break;
	case KL_PART_LIST:
		/* A click on no item leaves none selected. */
		model_select(chooser, -1);
		break;
	case KL_PART_PLACE:
		if (chooser->places[index].path[0] == '\0')
			kl_chooser_go_recent(chooser);
		else
			(void)kl_chooser_go(chooser, chooser->places[index].path);
		break;
	case KL_PART_UP:
		kl_chooser_go_up(chooser);
		break;
	case KL_PART_LOCATION:
		model_open_path(chooser);
		break;
	case KL_PART_NAME:
		/* The name takes the keyboard, its cursor at the end. */
		chooser->focus = KL_FOCUS_NAME;
		chooser->name.cursor = chooser->name.length;
		chooser->name.anchor = chooser->name.length;
		break;
	case KL_PART_FILTER:
		/* The next filter, round to the first. */
		chooser->filter = (chooser->filter + 1U) % chooser->filter_count;
		model_reload(chooser);
		break;
	case KL_PART_CANCEL:
		kl_chooser_cancel(chooser);
		break;
	case KL_PART_ACCEPT:
		(void)model_accept(chooser);
		break;
	case KL_PART_KEEP:
		chooser->confirm = 0;
		break;
	case KL_PART_REPLACE:
		model_answer(chooser, chooser->confirm_path);
		break;
	case KL_PART_NONE:
		break;
	}

	/* Succeeded: the window shows the click's outcome. */
	return 1;
}

/*
 * Carries out a finger's tap (twice: the second of a double tap).  A tap
 * on a folder goes into it; on a file it selects it, and a double tap
 * chooses it.  Elsewhere a tap is a click.
 */
int
kl_chooser_tap(
	struct kl_chooser *chooser,
	int x,
	int y,
	int twice)
{
	enum kl_chooser_part part;
	int index;

	/* An answered chooser takes nothing more. */
	if (chooser->answered)
		return 0;

	/* An item: a folder opens at once, a file waits for the second tap. */
	index = -1;
	part = kl_chooser_hit(chooser, x, y, &index);
	if (part == KL_PART_ROW) {
		chooser->message[0] = '\0';
		if (chooser->entries[index].folder)
			twice = 1;
		(void)model_activate(chooser, index, twice);
		return 1;
	}

	/* The second tap elsewhere was already carried out as the first. */
	if (twice)
		return 0;

	/* Anything else as a click, never taken for a double click. */
	chooser->click_part = KL_PART_NONE;
	(void)kl_chooser_click(chooser, x, y, 0U);
	chooser->click_part = KL_PART_NONE;
	return 1;
}

/*
 * Scrolls the list by a wheel's pixels (down is positive), within its ends.
 */
int
kl_chooser_wheel(
	struct kl_chooser *chooser,
	double pixels)
{
	double maximum;
	double scroll;

	/* The new scroll within the ends. */
	maximum = kl_chooser_scroll_max(chooser);
	scroll = chooser->scroll + pixels;
	if (scroll > maximum)
		scroll = maximum;
	if (scroll < 0.0)
		scroll = 0.0;

	/* Not moved: nothing to draw. */
	if (scroll == chooser->scroll)
		return 0;

	/* Succeeded: scrolled, and the finger's scroller follows. */
	chooser->scroll = scroll;
	chooser->scroll_moved = 1;
	return 1;
}

/*
 * Ends the chooser without a path.
 */
void
kl_chooser_cancel(
	struct kl_chooser *chooser)
{
	/* An answer is given once. */
	if (chooser->answered)
		return;

	/* Cancelled, with no path. */
	chooser->answered = 1;
	chooser->result = KEILAND_FILE_CHOOSER_CANCELLED;
	chooser->answer[0] = '\0';
}

/*
 * Reports the part of the window at a point, and the item or place it is
 * (*index, -1 for the other parts).
 */
enum kl_chooser_part
kl_chooser_hit(
	const struct kl_chooser *chooser,
	int x,
	int y,
	int *index)
{
	const struct kl_chooser_layout *layout;
	const struct kl_rect *rect;
	int place;
	int row;

	/* Nothing is an item until found. */
	*index = -1;
	layout = &chooser->layout;

	/* While the question is asked, only its buttons. */
	if (chooser->confirm) {
		rect = &layout->keep;
		if (x >= rect->x && x < rect->x + rect->width && y >= rect->y && y < rect->y + rect->height)
			return KL_PART_KEEP;
		rect = &layout->replace;
		if (x >= rect->x && x < rect->x + rect->width && y >= rect->y && y < rect->y + rect->height)
			return KL_PART_REPLACE;
		return KL_PART_NONE;
	}

	/* A place of the sidebar. */
	rect = &layout->sidebar;
	if (x >= rect->x && x < rect->x + rect->width && y >= layout->places_top) {
		place = (y - layout->places_top) / KL_CHOOSER_ROW_PLACE;
		if (place >= 0 && (size_t)place < chooser->place_count) {
			*index = place;
			return KL_PART_PLACE;
		}

		/* The sidebar below its places. */
		return KL_PART_NONE;
	}

	/* The button to the folder above, and the location beside it. */
	rect = &layout->up;
	if (x >= rect->x && x < rect->x + rect->width && y >= rect->y && y < rect->y + rect->height)
		return KL_PART_UP;
	rect = &layout->location;
	if (x >= rect->x && x < rect->x + rect->width && y >= rect->y && y < rect->y + rect->height)
		return KL_PART_LOCATION;

	/* The list: an item, or the space after the items. */
	rect = &layout->list;
	if (x >= rect->x && x < rect->x + rect->width && y >= rect->y && y < rect->y + rect->height) {
		row = (int)(((double)(y - rect->y) + chooser->scroll) / (double)KL_CHOOSER_ROW);
		if (row >= 0 && (size_t)row < chooser->count && (double)(y - rect->y) + chooser->scroll >= 0.0) {
			*index = row;
			return KL_PART_ROW;
		}

		/* The space after the items. */
		return KL_PART_LIST;
	}

	/* The name (Save). */
	rect = &layout->name;
	if (chooser->mode == KEILAND_FILE_CHOOSER_SAVE &&
	    x >= rect->x && x < rect->x + rect->width && y >= rect->y && y < rect->y + rect->height)
		return KL_PART_NAME;

	/* The filter, when there is a choice of them and room for it. */
	rect = &layout->filter;
	if (chooser->filter_count > 1U &&
	    rect->width > 0 &&
	    x >= rect->x && x < rect->x + rect->width && y >= rect->y && y < rect->y + rect->height)
		return KL_PART_FILTER;

	/* The two buttons. */
	rect = &layout->cancel;
	if (x >= rect->x && x < rect->x + rect->width && y >= rect->y && y < rect->y + rect->height)
		return KL_PART_CANCEL;
	rect = &layout->accept;
	if (x >= rect->x && x < rect->x + rect->width && y >= rect->y && y < rect->y + rect->height)
		return KL_PART_ACCEPT;

	/* Nothing that answers a click. */
	return KL_PART_NONE;
}

/*
 * Tells whether the main button (Open, Save) can be pressed now.
 */
int
kl_chooser_can_accept(
	const struct kl_chooser *chooser)
{
	/* A selected folder can always be gone into. */
	if (chooser->selected >= 0 && chooser->entries[chooser->selected].folder)
		return 1;

	/* Save needs a name. */
	if (chooser->mode == KEILAND_FILE_CHOOSER_SAVE) {
		if (chooser->name.length == 0U)
			return 0;
		return 1;
	}

	/* Open needs a file selected. */
	if (chooser->selected < 0)
		return 0;
	return 1;
}

/*
 * Reports the character a key types with the modifiers held, or 0 for a
 * key that types none (a key with Control or Alt types none either).
 */
uint32_t
kl_chooser_character(
	uint32_t key,
	unsigned modifiers)
{
	char character;

	/* Control and Alt make a key a command, not a character. */
	if ((modifiers & (KL_MOD_CTRL | KL_MOD_ALT)) != 0U)
		return 0U;

	/* Only the keys of the tables type characters. */
	if (key >= MODEL_KEYS_TABLE)
		return 0U;

	/* The character, shifted or not. */
	character = model_keys_plain[key];
	if ((modifiers & KL_MOD_SHIFT) != 0U)
		character = model_keys_shifted[key];

	/* Succeeded: the character, or 0. */
	return (uint32_t)(unsigned char)character;
}

/* Makes the sidebar's places: Recent (Open), Home and its usual folders, and the root. */
static void
model_places(
	struct kl_chooser *chooser)
{
	static const char *const names[] = { "Desktop", "Documents", "Downloads" };
	static const enum kl_chooser_icon icons[] = { KL_ICON_DESKTOP, KL_ICON_DOCUMENTS, KL_ICON_DOWNLOADS };
	char path[KL_CHOOSER_PATH_MAX];
	const char *home;
	size_t index;
	int folder;
	int root;

	/* Recent files, which only Open can choose from. */
	chooser->place_count = 0;
	if (chooser->mode == KEILAND_FILE_CHOOSER_OPEN)
		model_add_place(chooser, "Recent", "", KL_ICON_RECENT);

	/* Home, when there is one. */
	home = model_home();
	folder = model_is_folder(home);
	root = strcmp(home, "/");
	if (folder && root != 0) {
		model_add_place(chooser, "Home", home, KL_ICON_HOME);

		/* Its usual folders that exist. */
		for (index = 0; index < sizeof(names) / sizeof(names[0]); index++) {
			model_join(home, names[index], path, sizeof(path));
			folder = model_is_folder(path);
			if (folder)
				model_add_place(chooser, names[index], path, icons[index]);
		}
	}

	/* The whole computer. */
	model_add_place(chooser, "Computer", "/", KL_ICON_COMPUTER);
}

/* Adds a place to the sidebar, when there is room. */
static void
model_add_place(
	struct kl_chooser *chooser,
	const char *label,
	const char *path,
	enum kl_chooser_icon icon)
{
	struct kl_chooser_place *place;

	/* No room, no place. */
	if (chooser->place_count >= KL_CHOOSER_PLACES_MAX)
		return;

	/* The next place. */
	place = &chooser->places[chooser->place_count];
	snprintf(place->label, sizeof(place->label), "%s", label);
	snprintf(place->path, sizeof(place->path), "%s", path);
	place->icon = icon;
	chooser->place_count++;
}

/* Reports the home folder: $HOME, or the root without one. */
static const char *
model_home(void)
{
	const char *home;

	/* The environment's. */
	home = getenv("HOME");
	if (home == NULL || home[0] == '\0')
		return "/";

	/* Succeeded: the home folder. */
	return home;
}

/* Tells whether a path names a folder. */
static int
model_is_folder(
	const char *path)
{
	struct stat status;
	int folder;
	int error;

	/* What the path names. */
	error = stat(path, &status);
	if (error != 0)
		return 0;

	/* Only a folder. */
	folder = S_ISDIR(status.st_mode);
	if (!folder)
		return 0;

	/* Succeeded: a folder. */
	return 1;
}

/* Reads a folder's items that pass the filter, sorted; returns 0 or why it could not be read. */
static int
model_read(
	struct kl_chooser *chooser,
	const char *folder)
{
	char path[KL_CHOOSER_PATH_MAX];
	struct dirent *item;
	struct stat status;
	DIR *directory;
	int matches;
	int directory_item;
	int same;
	int error;

	/* The items of the folder shown before go. */
	model_clear(chooser);

	/* The folder. */
	directory = opendir(folder);
	if (directory == NULL)
		return errno;

	/* Each item but the folder itself and its parent. */
	for (;;) {
		item = readdir(directory);
		if (item == NULL)
			break;
		same = strcmp(item->d_name, ".");
		if (same == 0)
			continue;
		same = strcmp(item->d_name, "..");
		if (same == 0)
			continue;

		/* A hidden item only when hidden items show. */
		if (item->d_name[0] == '.' && !chooser->show_hidden)
			continue;

		/* What it is (an item that went meanwhile is skipped). */
		model_join(folder, item->d_name, path, sizeof(path));
		error = stat(path, &status);
		if (error != 0)
			continue;

		/* A file shows only when it passes the filter. */
		directory_item = S_ISDIR(status.st_mode);
		if (!directory_item) {
			matches = model_matches(chooser, item->d_name);
			if (!matches)
				continue;
		}

		/* The item. */
		error = model_add_entry(chooser, item->d_name, path, &status);
		if (error != 0)
			break;
	}

	/* The folder is read. */
	closedir(directory);

	/* Folders first, then by name. */
	if (chooser->count > 1U)
		qsort(chooser->entries, chooser->count, sizeof(chooser->entries[0]), model_compare);

	/* Succeeded: the folder's items are listed. */
	return 0;
}

/* Adds one item to the list; 0 or ENOMEM. */
static int
model_add_entry(
	struct kl_chooser *chooser,
	const char *name,
	const char *path,
	const struct stat *status)
{
	struct kl_chooser_entry *grown;
	struct kl_chooser_entry *entry;
	size_t capacity;
	int folder;

	/* Room for one more. */
	if (chooser->count == chooser->capacity) {
		capacity = chooser->capacity * 2U;
		if (capacity == 0U)
			capacity = MODEL_ENTRIES_FIRST;
		grown = realloc(chooser->entries, capacity * sizeof(chooser->entries[0]));
		if (grown == NULL)
			return ENOMEM;
		chooser->entries = grown;
		chooser->capacity = capacity;
	}

	/* The item's name. */
	entry = &chooser->entries[chooser->count];
	entry->name = strdup(name);
	if (entry->name == NULL)
		return ENOMEM;

	/* Its whole path. */
	entry->path = strdup(path);
	if (entry->path == NULL) {
		free(entry->name);
		return ENOMEM;
	}

	/* What it is, how large, and when it changed. */
	entry->folder = 0;
	folder = S_ISDIR(status->st_mode);
	if (folder)
		entry->folder = 1;
	entry->size = (int64_t)status->st_size;
	entry->modified = (int64_t)status->st_mtime;
	chooser->count++;

	/* Succeeded: listed. */
	return 0;
}

/* Forgets the items listed. */
static void
model_clear(
	struct kl_chooser *chooser)
{
	size_t index;

	/* Each item's strings. */
	for (index = 0; index < chooser->count; index++) {
		free(chooser->entries[index].name);
		free(chooser->entries[index].path);
	}

	/* No items. */
	chooser->count = 0;
}

/* Orders items: folders before files, then by name without regard to case. */
static int
model_compare(
	const void *left,
	const void *right)
{
	const struct kl_chooser_entry *first;
	const struct kl_chooser_entry *second;
	int order;

	/* A folder goes before a file. */
	first = left;
	second = right;
	if (first->folder != second->folder) {
		if (first->folder)
			return -1;
		return 1;
	}

	/* Then the names, ignoring case. */
	order = strcasecmp(first->name, second->name);
	if (order != 0)
		return order;

	/* Names that differ only in case, by their bytes. */
	order = strcmp(first->name, second->name);
	return order;
}

/* Tells whether a file's name passes the filter chosen. */
static int
model_matches(
	const struct kl_chooser *chooser,
	const char *name)
{
	const char *extensions;
	const char *extension;
	const char *word;
	size_t length;
	size_t size;
	int same;

	/* Without filters every file passes, and so it does with an empty filter. */
	if (chooser->filter_count == 0U)
		return 1;
	extensions = chooser->filters[chooser->filter].extensions;
	if (extensions[0] == '\0')
		return 1;

	/* The name's extension: after its last dot, which is not its first character. */
	extension = strrchr(name, '.');
	if (extension == NULL || extension == name)
		return 0;
	extension++;
	length = strlen(extension);

	/* Each word of the filter, compared with it. */
	word = extensions;
	while (*word != '\0') {
		/* The spaces between words. */
		while (*word == ' ')
			word++;
		size = strcspn(word, " ");
		if (size == 0U)
			break;

		/* The same letters, whatever their case. */
		if (size == length) {
			same = strncasecmp(word, extension, size);
			if (same == 0)
				return 1;
		}

		/* The next word. */
		word += size;
	}

	/* No word matched. */
	return 0;
}

/* Lists the place shown again (after the filter or the hidden items changed). */
static void
model_reload(
	struct kl_chooser *chooser)
{
	char folder[KL_CHOOSER_PATH_MAX];

	/* Recent, or the folder. */
	if (chooser->recent) {
		kl_chooser_go_recent(chooser);
		return;
	}

	/* The folder, from a copy (showing it rewrites chooser->folder). */
	snprintf(folder, sizeof(folder), "%s", chooser->folder);
	(void)kl_chooser_go(chooser, folder);
}

/* Selects an item (-1: none) and scrolls it into sight; a file selected in Save gives its name. */
static void
model_select(
	struct kl_chooser *chooser,
	int index)
{
	struct kl_chooser_entry *entry;

	/* The selection. */
	chooser->selected = index;
	if (index < 0)
		return;

	/* In sight. */
	model_visible(chooser, index);

	/* Save takes a file's name as the name to save as. */
	entry = &chooser->entries[index];
	if (chooser->mode == KEILAND_FILE_CHOOSER_SAVE && !entry->folder)
		model_field_set(&chooser->name, entry->name, 1);
}

/* Moves the selection by some items, within the list. */
static void
model_move(
	struct kl_chooser *chooser,
	int delta)
{
	int index;

	/* An empty list has nothing to select. */
	if (chooser->count == 0U)
		return;

	/* From the item selected, or from before the first (after the last going up). */
	index = chooser->selected;
	if (index < 0 && delta > 0)
		index = -1;
	if (index < 0 && delta < 0)
		index = (int)chooser->count;
	index += delta;

	/* Within the list. */
	if (index < 0)
		index = 0;
	if (index >= (int)chooser->count)
		index = (int)chooser->count - 1;

	/* The item. */
	model_select(chooser, index);
}

/* Scrolls the list just enough to show an item whole. */
static void
model_visible(
	struct kl_chooser *chooser,
	int index)
{
	double top;
	double bottom;
	double shown;

	/* The item's place in the rows and the height that shows. */
	top = (double)index * (double)KL_CHOOSER_ROW;
	bottom = top + (double)KL_CHOOSER_ROW;
	shown = (double)chooser->layout.list.height;

	/* Above what shows, or below it. */
	if (top < chooser->scroll) {
		chooser->scroll = top;
		chooser->scroll_moved = 1;
	} else if (bottom > chooser->scroll + shown) {
		chooser->scroll = bottom - shown;
		chooser->scroll_moved = 1;
	}
}

/* Reports how many rows a page is: the rows that show whole, less one. */
static int
model_page(
	const struct kl_chooser *chooser)
{
	int rows;

	/* The whole rows that show, less one kept for the eye. */
	rows = chooser->layout.list.height / KL_CHOOSER_ROW - 1;
	if (rows < MODEL_PAGE_MIN)
		rows = MODEL_PAGE_MIN;

	/* Reports the page. */
	return rows;
}

/* Clicks or taps an item: once selects it, twice goes into a folder or chooses a file. */
static int
model_activate(
	struct kl_chooser *chooser,
	int index,
	int twice)
{
	struct kl_chooser_entry *entry;
	char path[KL_CHOOSER_PATH_MAX];

	/* Once: selected (Save's name follows a file), and the list has the keyboard in Open. */
	entry = &chooser->entries[index];
	if (!twice) {
		model_select(chooser, index);
		return 1;
	}

	/* A folder is gone into. */
	if (entry->folder) {
		snprintf(path, sizeof(path), "%s", entry->path);
		(void)kl_chooser_go(chooser, path);
		return 1;
	}

	/* A file is chosen (Save asks first when it would be replaced). */
	model_select(chooser, index);
	(void)model_accept(chooser);
	return 1;
}

/* The main button (Open, Save) or Enter: into a folder selected, or the answer. */
static int
model_accept(
	struct kl_chooser *chooser)
{
	struct kl_chooser_entry *entry;
	char path[KL_CHOOSER_PATH_MAX];

	/* A folder selected is gone into. */
	if (chooser->selected >= 0) {
		entry = &chooser->entries[chooser->selected];
		if (entry->folder) {
			snprintf(path, sizeof(path), "%s", entry->path);
			(void)kl_chooser_go(chooser, path);
			return 1;
		}
	}

	/* Save checks the name and the folder. */
	if (chooser->mode == KEILAND_FILE_CHOOSER_SAVE) {
		(void)model_accept_save(chooser);
		return 1;
	}

	/* Open chooses the file selected. */
	if (chooser->selected < 0)
		return 0;
	entry = &chooser->entries[chooser->selected];
	model_answer(chooser, entry->path);
	return 1;
}

/* Save's answer: the name in the folder shown, after the checks and the question. */
static int
model_accept_save(
	struct kl_chooser *chooser)
{
	char path[KL_CHOOSER_PATH_MAX];
	struct stat status;
	const char *slash;
	int writable;
	int regular;
	int folder;
	int dots;
	int error;

	/* A name is needed. */
	if (chooser->name.length == 0U)
		return 0;

	/* One part of a path. */
	slash = strchr(chooser->name.text, '/');
	if (slash != NULL) {
		model_message(chooser, "A name can't contain \"/\".", NULL);
		return 0;
	}

	/* Not the folder or its parent. */
	dots = 0;
	if (chooser->name.text[0] == '.') {
		if (chooser->name.text[1] == '\0')
			dots = 1;
		else if (chooser->name.text[1] == '.' && chooser->name.text[2] == '\0')
			dots = 1;
	}

	/* Such a name is refused. */
	if (dots) {
		model_message(chooser, "\"%s\" can't be used as a name.", chooser->name.text);
		return 0;
	}

	/* Recent is not a folder to save in. */
	if (chooser->recent) {
		model_message(chooser, "Choose a folder to save in.", NULL);
		return 0;
	}

	/* The path the name would have. */
	model_join(chooser->folder, chooser->name.text, path, sizeof(path));
	error = stat(path, &status);

	/* What already has that name: a folder, a file, or something else. */
	folder = 0;
	regular = 0;
	if (error == 0) {
		folder = S_ISDIR(status.st_mode);
		regular = S_ISREG(status.st_mode);
	}

	/* A folder of that name is gone into. */
	if (folder) {
		(void)kl_chooser_go(chooser, path);
		model_field_set(&chooser->name, "", 0);
		return 1;
	}

	/* Anything else of that name but a file cannot be replaced. */
	if (error == 0 && !regular) {
		model_message(chooser, "\"%s\" isn't a file.", chooser->name.text);
		return 0;
	}

	/* A folder that cannot be written to cannot take the file. */
	writable = access(chooser->folder, W_OK);
	if (writable != 0) {
		model_message(chooser, "You can't save in \"%s\".", model_base(chooser->folder));
		return 0;
	}

	/* A file of that name: asked first. */
	if (error == 0) {
		snprintf(chooser->confirm_path, sizeof(chooser->confirm_path), "%s", path);
		chooser->confirm = 1;
		return 1;
	}

	/* Succeeded: a new file's path. */
	model_answer(chooser, path);
	return 1;
}

/* The path typed (Ctrl+L): a folder is shown, a file is chosen (Open) or named (Save). */
static int
model_accept_path(
	struct kl_chooser *chooser)
{
	char path[KL_CHOOSER_PATH_MAX];
	char folder[KL_CHOOSER_PATH_MAX];
	const char *typed;
	struct stat status;
	char *slash;
	int folder_named;
	int regular;
	int home;
	int error;

	/* The path: ~ is home, and a relative one is from the folder shown. */
	typed = chooser->path.text;
	home = 0;
	if (typed[0] == '~') {
		if (typed[1] == '/' || typed[1] == '\0')
			home = 1;
	}

	/* The path made whole. */
	if (home) {
		snprintf(path, sizeof(path), "%s%s", model_home(), typed + 1);
	} else if (typed[0] == '/' || chooser->recent) {
		snprintf(path, sizeof(path), "%s", typed);
	} else {
		model_join(chooser->folder, typed, path, sizeof(path));
	}

	/* What the path names. */
	error = stat(path, &status);
	folder_named = 0;
	regular = 0;
	if (error == 0) {
		folder_named = S_ISDIR(status.st_mode);
		regular = S_ISREG(status.st_mode);
	}

	/* A folder is shown, and the keyboard goes back. */
	if (folder_named) {
		(void)kl_chooser_go(chooser, path);
		chooser->focus = KL_FOCUS_LIST;
		if (chooser->mode == KEILAND_FILE_CHOOSER_SAVE)
			chooser->focus = KL_FOCUS_NAME;
		return 1;
	}

	/* Open chooses a file that exists. */
	if (chooser->mode == KEILAND_FILE_CHOOSER_OPEN) {
		if (!regular) {
			model_message(chooser, "There is no file \"%s\".", path);
			return 0;
		}

		/* Succeeded: the file typed is the answer. */
		model_answer(chooser, path);
		return 1;
	}

	/* Save: the folder part is shown and the last part becomes the name. */
	snprintf(folder, sizeof(folder), "%s", path);
	slash = strrchr(folder, '/');
	if (slash == NULL) {
		model_message(chooser, "There is no folder for \"%s\".", path);
		return 0;
	}

	/* The folder part: the root keeps its slash. */
	if (slash == folder)
		slash[1] = '\0';
	else
		*slash = '\0';
	error = kl_chooser_go(chooser, folder);
	if (error != 0) {
		model_message(chooser, "There is no folder \"%s\".", folder);
		return 0;
	}

	/* The name, and Save goes on as if it was typed there. */
	model_field_set(&chooser->name, model_base(path), 0);
	chooser->focus = KL_FOCUS_NAME;
	(void)model_accept_save(chooser);
	return 1;
}

/* Gives the answer: the path chosen. */
static void
model_answer(
	struct kl_chooser *chooser,
	const char *path)
{
	/* An answer is given once. */
	if (chooser->answered)
		return;

	/* Chosen, with its path. */
	snprintf(chooser->answer, sizeof(chooser->answer), "%s", path);
	chooser->answered = 1;
	chooser->result = KEILAND_FILE_CHOOSER_CHOSEN;
	chooser->confirm = 0;
}

/* Writes a folder and a name joined by one slash. */
static void
model_join(
	const char *folder,
	const char *name,
	char *out,
	size_t size)
{
	size_t length;

	/* The root already ends with its slash. */
	length = strlen(folder);
	if (length > 0U && folder[length - 1U] == '/')
		snprintf(out, size, "%s%s", folder, name);
	else
		snprintf(out, size, "%s/%s", folder, name);
}

/* Shows why something was refused (format has one %s for name, or none). */
static void
model_message(
	struct kl_chooser *chooser,
	const char *format,
	const char *name)
{
	/* The line, with the name when it has one. */
	if (name != NULL)
		snprintf(chooser->message, sizeof(chooser->message), format, name);
	else
		snprintf(chooser->message, sizeof(chooser->message), "%s", format);
}

/* Opens the field for a path, holding the folder shown (the home folder for Recent). */
static void
model_open_path(
	struct kl_chooser *chooser)
{
	char text[KL_CHOOSER_PATH_MAX];
	const char *folder;

	/* The folder with a slash after it, ready for a name. */
	folder = chooser->folder;
	if (chooser->recent)
		folder = model_home();
	model_join(folder, "", text, sizeof(text));

	/* The field has the keyboard, its cursor at the end. */
	model_field_set(&chooser->path, text, 0);
	chooser->focus = KL_FOCUS_PATH;
}

/* Selects the next item after the one selected whose name starts with a character. */
static void
model_type_select(
	struct kl_chooser *chooser,
	uint32_t character)
{
	size_t tried;
	size_t index;
	char letter[2];
	int same;

	/* Nothing to find in an empty list. */
	if (chooser->count == 0U)
		return;

	/* The character as a one-letter string. */
	letter[0] = (char)character;
	letter[1] = '\0';

	/* Each item after the one selected, round to it. */
	index = 0;
	if (chooser->selected >= 0)
		index = (size_t)chooser->selected + 1U;
	for (tried = 0; tried < chooser->count; tried++) {
		index %= chooser->count;
		same = strncasecmp(chooser->entries[index].name, letter, 1U);
		if (same == 0) {
			model_select(chooser, (int)index);
			return;
		}

		/* The item after it. */
		index++;
	}
}

/* Carries out a key in a field: moving, erasing and typing; 1 when it changed. */
static int
model_field_key(
	struct kl_chooser *chooser,
	struct kl_chooser_field *field,
	uint32_t key,
	uint32_t character)
{
	unsigned shift;
	size_t at;

	/* Shift keeps the other end of the selection where it is. */
	shift = chooser->modifiers & KL_MOD_SHIFT;

	/* The keys that move and erase. */
	switch (key) {
	case KL_KEY_LEFT:
		/* Left: to the start of a selection, or a character back. */
		at = field->cursor;
		if (field->cursor == field->anchor || shift != 0U)
			at = model_field_prev(field, field->cursor);
		else if (field->anchor < field->cursor)
			at = field->anchor;
		field->cursor = at;
		if (shift == 0U)
			field->anchor = at;
		return 1;
	case KL_KEY_RIGHT:
		/* Right: to the end of a selection, or a character on. */
		at = field->cursor;
		if (field->cursor == field->anchor || shift != 0U)
			at = model_field_next(field, field->cursor);
		else if (field->anchor > field->cursor)
			at = field->anchor;
		field->cursor = at;
		if (shift == 0U)
			field->anchor = at;
		return 1;
	case KL_KEY_HOME:
		field->cursor = 0;
		if (shift == 0U)
			field->anchor = 0;
		return 1;
	case KL_KEY_END:
		field->cursor = field->length;
		if (shift == 0U)
			field->anchor = field->length;
		return 1;
	case KL_KEY_BACKSPACE:
		/* The selection, or the character before the cursor. */
		if (field->cursor == field->anchor)
			field->anchor = model_field_prev(field, field->cursor);
		model_field_erase(field);
		break;
	case KL_KEY_DELETE:
		/* The selection, or the character after the cursor. */
		if (field->cursor == field->anchor)
			field->anchor = model_field_next(field, field->cursor);
		model_field_erase(field);
		break;
	case KL_KEY_A:
		/* Ctrl+A selects the whole text. */
		if ((chooser->modifiers & KL_MOD_CTRL) != 0U) {
			field->anchor = 0;
			field->cursor = field->length;
			return 1;
		}

		/* Without Control, A types its letter. */
		break;
	default:
		break;
	}

	/* A character replaces the selection. */
	if (key != KL_KEY_BACKSPACE && key != KL_KEY_DELETE) {
		if (character == 0U || character >= 0x80U)
			return 0;
		model_field_erase(field);
		if (field->length + 1U >= sizeof(field->text))
			return 1;
		memmove(field->text + field->cursor + 1U, field->text + field->cursor, field->length - field->cursor + 1U);
		field->text[field->cursor] = (char)character;
		field->length++;
		field->cursor++;
		field->anchor = field->cursor;
	}

	/* A name typed is a new name: no item stays selected for it. */
	if (field == &chooser->name)
		chooser->selected = -1;

	/* Succeeded: the field changed. */
	return 1;
}

/* Sets a field's text; stem 1 selects it up to its extension, 0 puts the cursor at the end. */
static void
model_field_set(
	struct kl_chooser_field *field,
	const char *text,
	int stem)
{
	const char *dot;

	/* The text, cut to the field. */
	snprintf(field->text, sizeof(field->text), "%s", text);
	field->length = strlen(field->text);

	/* The cursor at the end, nothing selected. */
	field->cursor = field->length;
	field->anchor = field->length;
	if (!stem)
		return;

	/* The name up to its extension selected (all of a name without one). */
	field->anchor = 0;
	dot = strrchr(field->text, '.');
	if (dot != NULL && dot != field->text)
		field->cursor = (size_t)(dot - field->text);
}

/* Erases the selection of a field (nothing when the cursor and the anchor meet). */
static void
model_field_erase(
	struct kl_chooser_field *field)
{
	size_t start;
	size_t end;

	/* The selection's ends in order. */
	start = field->anchor;
	end = field->cursor;
	if (start > end) {
		start = field->cursor;
		end = field->anchor;
	}

	/* The bytes after it close up. */
	memmove(field->text + start, field->text + end, field->length - end + 1U);
	field->length -= end - start;
	field->cursor = start;
	field->anchor = start;
}

/* Reports the start of the character before an offset of a field. */
static size_t
model_field_prev(
	const struct kl_chooser_field *field,
	size_t at)
{
	/* Nothing before the start. */
	if (at == 0U)
		return 0U;

	/* Back over the continuation bytes to the character's first. */
	at--;
	while (at > 0U && ((unsigned char)field->text[at] & 0xc0U) == 0x80U)
		at--;

	/* Reports the character's start. */
	return at;
}

/* Reports the offset after the character at an offset of a field. */
static size_t
model_field_next(
	const struct kl_chooser_field *field,
	size_t at)
{
	/* Nothing after the end. */
	if (at >= field->length)
		return field->length;

	/* On over the character's continuation bytes. */
	at++;
	while (at < field->length && ((unsigned char)field->text[at] & 0xc0U) == 0x80U)
		at++;

	/* Reports the next character's start. */
	return at;
}

/* Reports a path's last part (the root's is "/"). */
static const char *
model_base(
	const char *path)
{
	const char *slash;

	/* After the last slash, unless that is all there is. */
	slash = strrchr(path, '/');
	if (slash == NULL)
		return path;
	if (slash[1] == '\0')
		return path;

	/* Reports the last part. */
	return slash + 1;
}
