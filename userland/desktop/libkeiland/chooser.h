/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The file chooser's parts that know nothing of Wayland (ws092-p003): the
 * folder listed, the places, the filters, the name typed, what the keys,
 * the pointer and the fingers mean, and the layout and drawing of the
 * window.  chooser.c puts them in a Wayland window; the host tests call
 * them directly.  Nothing here leaves the library.
 */

#ifndef KEILAND_CHOOSER_H
#define KEILAND_CHOOSER_H

#include "paint.h"

#include <keiland.h>

#include <stddef.h>
#include <stdint.h>

/* The longest path the chooser handles, with its NUL. */
#define KL_CHOOSER_PATH_MAX	4096

/* The longest label of a place or a filter, and of a filter's extensions, with the NUL. */
#define KL_CHOOSER_LABEL_MAX	64
#define KL_CHOOSER_EXTENSIONS_MAX	256

/* The most places in the sidebar, and the most recent files shown. */
#define KL_CHOOSER_PLACES_MAX	8
#define KL_CHOOSER_RECENT_MAX	64

/* The longest message shown, with its NUL. */
#define KL_CHOOSER_MESSAGE_MAX	192

/* The window's size unless the compositor says otherwise, and the smallest it may be. */
#define KL_CHOOSER_WIDTH	760
#define KL_CHOOSER_HEIGHT	480
#define KL_CHOOSER_MIN_WIDTH	520
#define KL_CHOOSER_MIN_HEIGHT	340

/* The evdev codes of the keys the chooser knows. */
#define KL_KEY_ESC		1U
#define KL_KEY_BACKSPACE	14U
#define KL_KEY_TAB		15U
#define KL_KEY_ENTER		28U
#define KL_KEY_A		30U
#define KL_KEY_H		35U
#define KL_KEY_L		38U
#define KL_KEY_KPENTER		96U
#define KL_KEY_HOME		102U
#define KL_KEY_UP		103U
#define KL_KEY_PAGEUP		104U
#define KL_KEY_LEFT		105U
#define KL_KEY_RIGHT		106U
#define KL_KEY_END		107U
#define KL_KEY_DOWN		108U
#define KL_KEY_PAGEDOWN		109U
#define KL_KEY_DELETE		111U

/* The modifiers held, in the chooser's own bits. */
#define KL_MOD_SHIFT		1U
#define KL_MOD_CTRL		2U
#define KL_MOD_ALT		4U

/* What a place in the sidebar is drawn with. */
enum kl_chooser_icon {
	KL_ICON_RECENT,
	KL_ICON_HOME,
	KL_ICON_DESKTOP,
	KL_ICON_DOCUMENTS,
	KL_ICON_DOWNLOADS,
	KL_ICON_COMPUTER
};

/* What the keyboard types into: nothing (the list), the name, or the path. */
enum kl_chooser_focus {
	KL_FOCUS_LIST,
	KL_FOCUS_NAME,
	KL_FOCUS_PATH
};

/* The parts of the window a point can be over. */
enum kl_chooser_part {
	KL_PART_NONE,
	KL_PART_ROW,
	KL_PART_LIST,
	KL_PART_PLACE,
	KL_PART_UP,
	KL_PART_LOCATION,
	KL_PART_NAME,
	KL_PART_FILTER,
	KL_PART_CANCEL,
	KL_PART_ACCEPT,
	KL_PART_KEEP,
	KL_PART_REPLACE
};

/* A rectangle of the window, in surface pixels. */
struct kl_rect {
	int x;
	int y;
	int width;
	int height;
};

/*
 * One item of the list: a folder or a file of the folder shown, or a
 * recent file (whose whole path is its path).  The name and path are the
 * item's own allocations.
 */
struct kl_chooser_entry {
	char *name;
	char *path;
	int folder;
	int64_t size;
	int64_t modified;
};

/* One place of the sidebar: its label, its icon, and its folder (empty for Recent). */
struct kl_chooser_place {
	char label[KL_CHOOSER_LABEL_MAX];
	char path[KL_CHOOSER_PATH_MAX];
	enum kl_chooser_icon icon;
};

/* One filter: its label and its extensions (empty: every file). */
struct kl_chooser_filter {
	char label[KL_CHOOSER_LABEL_MAX];
	char extensions[KL_CHOOSER_EXTENSIONS_MAX];
};

/*
 * A line of text being typed: its bytes, and the cursor and the other end
 * of the selection as byte offsets on character boundaries (equal when
 * nothing is selected).
 */
struct kl_chooser_field {
	char text[KL_CHOOSER_PATH_MAX];
	size_t length;
	size_t cursor;
	size_t anchor;
};

/*
 * Where the window's parts are for its size; made by kl_chooser_layout
 * whenever the size changes, and read by the drawing and the hit tests.
 */
struct kl_chooser_layout {
	struct kl_rect sidebar;
	struct kl_rect content;
	struct kl_rect up;
	struct kl_rect location;
	struct kl_rect header;
	struct kl_rect list;
	struct kl_rect bar;
	struct kl_rect name;
	struct kl_rect filter;
	struct kl_rect cancel;
	struct kl_rect accept;
	struct kl_rect card;
	struct kl_rect keep;
	struct kl_rect replace;
	int places_top;
};

/*
 * One file chooser's state, from keiland_file_chooser_open to its
 * destruction.  The entries array belongs to it and is refilled whenever
 * another folder is shown.  answered becomes 1 once, with result and
 * answer; nothing changes after that.
 */
struct kl_chooser {
	unsigned mode;
	char title[KL_CHOOSER_LABEL_MAX];

	/* The folder shown (a real path), or Recent when recent is 1; why it could not be read. */
	char folder[KL_CHOOSER_PATH_MAX];
	int recent;
	int list_error;

	/* Counts the places shown, so that a second tap can tell the first one showed another. */
	unsigned generation;

	/* Its items, the one selected (-1 for none), and whether hidden items show. */
	struct kl_chooser_entry *entries;
	size_t count;
	size_t capacity;
	int selected;
	int show_hidden;

	/* The sidebar and the filters. */
	struct kl_chooser_place places[KL_CHOOSER_PLACES_MAX];
	size_t place_count;
	struct kl_chooser_filter filters[KEILAND_FILE_CHOOSER_FILTERS_MAX];
	size_t filter_count;
	size_t filter;

	/* The name typed (Save), the path typed (Ctrl+L), and where the keyboard types. */
	struct kl_chooser_field name;
	struct kl_chooser_field path;
	enum kl_chooser_focus focus;

	/* The question before a file is replaced, and the path it would replace. */
	int confirm;
	char confirm_path[KL_CHOOSER_PATH_MAX];

	/* A line that says why something was refused (empty when none). */
	char message[KL_CHOOSER_MESSAGE_MAX];

	/* The window: its size, its layout, whether it has the keyboard, and whether it stands on glass. */
	int width;
	int height;
	struct kl_chooser_layout layout;
	int focused;
	int glass;

	/* The list's scroll in pixels, and whether a key moved it (the finger's scroller follows then). */
	double scroll;
	int scroll_moved;

	/* What the pointer is over, the modifiers held, and the last click (for a double click). */
	enum kl_chooser_part hover_part;
	int hover_index;
	unsigned modifiers;
	uint64_t click_ms;
	enum kl_chooser_part click_part;
	int click_index;

	/* The answer, once it is given. */
	int answered;
	unsigned result;
	char answer[KL_CHOOSER_PATH_MAX];
};

/* The model (chooser-model.c). */
int kl_chooser_init(struct kl_chooser *chooser, const struct keiland_file_chooser_options *options);
void kl_chooser_fini(struct kl_chooser *chooser);
int kl_chooser_go(struct kl_chooser *chooser, const char *path);
void kl_chooser_go_recent(struct kl_chooser *chooser);
void kl_chooser_go_up(struct kl_chooser *chooser);
void kl_chooser_resize(struct kl_chooser *chooser, int width, int height);
double kl_chooser_scroll_max(const struct kl_chooser *chooser);
void kl_chooser_set_scroll(struct kl_chooser *chooser, double scroll);
int kl_chooser_key(struct kl_chooser *chooser, uint32_t key, uint32_t character);
int kl_chooser_motion(struct kl_chooser *chooser, int x, int y);
int kl_chooser_click(struct kl_chooser *chooser, int x, int y, uint64_t time_ms);
int kl_chooser_tap(struct kl_chooser *chooser, int x, int y, int twice);
int kl_chooser_wheel(struct kl_chooser *chooser, double pixels);
void kl_chooser_cancel(struct kl_chooser *chooser);
enum kl_chooser_part kl_chooser_hit(const struct kl_chooser *chooser, int x, int y, int *index);
int kl_chooser_can_accept(const struct kl_chooser *chooser);
uint32_t kl_chooser_character(uint32_t key, unsigned modifiers);

/* The layout and the drawing (chooser-draw.c). */
void kl_chooser_layout(struct kl_chooser *chooser);
void kl_chooser_draw(struct kl_chooser *chooser, struct kl_text *text, struct kl_canvas *canvas);
int kl_chooser_glass_panels(const struct kl_chooser *chooser, struct keiland_glass_panel *panels, size_t capacity);

/* The height of a row of the list. */
#define KL_CHOOSER_ROW		28

/* The height of a place of the sidebar. */
#define KL_CHOOSER_ROW_PLACE	30

#endif
