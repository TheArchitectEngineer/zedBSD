/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The parts of zdesktop-files, the file manager of the zedBSD desktop.
 *
 * The interface (ui*.c) draws the window's frame on a CPU canvas and reads
 * the window's input as fm_event values; it knows nothing of Wayland or
 * Vulkan.  window.c turns the Wayland window's input into those events,
 * present.c shows each drawn frame through Vulkan, menu.c gives zdesktop
 * the menus through libzdesktop, and main.c ties them together.  The model
 * (dir.c, nav.c, mime.c, places.c and the files after them) keeps what the
 * interface shows: the listed places, the history, the selection, the file
 * operations.
 */

#ifndef ZDESKTOP_FILES_H
#define ZDESKTOP_FILES_H

#include "canvas.h"

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include <time.h>

/* The longest path and name the file manager handles, with the terminating NUL. */
#define FM_PATH_MAX		1024
#define FM_NAME_MAX		256

/* How many tabs a window has, how far back a tab's history goes. */
#define FM_TABS			8
#define FM_HISTORY		64

/* How many clickable regions one frame records. */
#define FM_HITS			1024

/* How many places the sidebar lists. */
#define FM_PLACES		40

/* The window's size when the compositor leaves it to the program. */
#define FM_WIDTH		1120
#define FM_HEIGHT		720

/*
 * The colors of the interface (spec §38: quiet, color only for the
 * selection and the tags).
 */
#define FM_COLOR_BACKGROUND_TOP		FM_RGB(0xeef2f7)
#define FM_COLOR_BACKGROUND_BOTTOM	FM_RGB(0xe6ebf3)
#define FM_COLOR_PANEL			FM_RGB(0xffffff)
#define FM_COLOR_PANEL_EDGE		FM_RGB(0xe2e7ef)
#define FM_COLOR_SIDEBAR		FM_RGBA(0xffffff, 120)
#define FM_COLOR_SHADOW			FM_RGBA(0x1f3a66, 34)
#define FM_COLOR_TEXT			FM_RGB(0x1e2632)
#define FM_COLOR_TEXT_SECONDARY		FM_RGB(0x6b7585)
#define FM_COLOR_TEXT_FAINT		FM_RGB(0xa3abb8)
#define FM_COLOR_ICON			FM_RGB(0x46526a)
#define FM_COLOR_ACCENT			FM_RGB(0x2f7cf6)
#define FM_COLOR_SELECTION		FM_RGBA(0x2f7cf6, 40)
#define FM_COLOR_SELECTION_INACTIVE	FM_RGBA(0x7a8699, 38)
#define FM_COLOR_HOVER			FM_RGBA(0x5a6b85, 18)
#define FM_COLOR_FOLDER			FM_RGB(0x5aa2f5)
#define FM_COLOR_SEPARATOR		FM_RGB(0xe8ecf2)

/* The modifier keys held with an input, the file manager's own bits. */
#define FM_MOD_SHIFT		0x01U
#define FM_MOD_CTRL		0x02U
#define FM_MOD_ALT		0x04U
#define FM_MOD_SUPER		0x08U

/* The pointer buttons, as evdev codes (BTN_LEFT, BTN_RIGHT, BTN_MIDDLE). */
#define FM_BUTTON_LEFT		0x110U
#define FM_BUTTON_RIGHT		0x111U
#define FM_BUTTON_MIDDLE	0x112U

/*
 * The kinds of input the window gives the interface.
 */
enum fm_event_type {
	FM_EVENT_MOTION,
	FM_EVENT_BUTTON,
	FM_EVENT_AXIS,
	FM_EVENT_LEAVE,
	FM_EVENT_KEY,
	FM_EVENT_FOCUS
};

/*
 * One input: where the pointer is, which button or key, and the modifiers
 * held.  serial is the compositor's serial of a button press (a context
 * menu answers it).
 */
struct fm_event {
	unsigned type;
	int x;
	int y;
	uint32_t button;
	int pressed;
	int scroll;
	uint32_t key;
	uint32_t modifiers;
	uint32_t serial;
	uint64_t time;
	int focused;
};

/*
 * What an entry is, which decides its icon and how it opens.
 */
enum fm_category {
	FM_CATEGORY_FOLDER,
	FM_CATEGORY_FILE,
	FM_CATEGORY_TEXT,
	FM_CATEGORY_CODE,
	FM_CATEGORY_IMAGE,
	FM_CATEGORY_AUDIO,
	FM_CATEGORY_VIDEO,
	FM_CATEGORY_ARCHIVE,
	FM_CATEGORY_PDF,
	FM_CATEGORY_EXECUTABLE,
	FM_CATEGORY_DOCUMENT,
	FM_CATEGORY_FONT,
	FM_CATEGORY_MODEL
};

/*
 * A file's type: its MIME name, what the interface calls it, and its
 * category.  The table of them lives in mime.c for the whole run.
 */
struct fm_mime {
	const char *type;
	const char *kind;
	unsigned category;
};

/*
 * One item of a listed place: a file or a folder with what was learned
 * of it when it was listed.
 *
 * The name and the path are allocated with the listing and freed with it.
 * detail is a second line some places show (a search result's folder, a
 * trashed file's original place); NULL otherwise.
 */
struct fm_entry {
	char *name;
	char *path;
	char *detail;
	const struct fm_mime *mime;
	uint64_t size;
	mode_t mode;
	uid_t uid;
	gid_t gid;
	time_t modified;
	time_t changed;
	time_t accessed;
	time_t extra_time;
	int folder;
	int link;
	int child_count;
	unsigned tags;
	int selected;
	int cut;
};

/*
 * The items of one place, as last read.
 *
 * error is the errno value of a place that could not be read (0 when it
 * was); modified is the folder's own modification time when it was read,
 * which tells whether it changed since.
 */
struct fm_listing {
	struct fm_entry *entries;
	size_t count;
	size_t capacity;
	int error;
	time_t modified;
};

/*
 * The kinds of place a tab can show.
 */
enum fm_location_kind {
	FM_LOCATION_HOME,
	FM_LOCATION_FOLDER,
	FM_LOCATION_RECENTS,
	FM_LOCATION_TRASH,
	FM_LOCATION_TAG,
	FM_LOCATION_SEARCH
};

/*
 * A place a tab shows: its kind and its path (a folder), tag name or
 * query.
 */
struct fm_location {
	unsigned kind;
	char path[FM_PATH_MAX];
};

/*
 * One step of a tab's history, with how the place was left: its scroll
 * and the item that had the keyboard's cursor.
 */
struct fm_visit {
	struct fm_location location;
	int scroll;
	char cursor[FM_NAME_MAX];
};

/*
 * One tab: its history (like a browser's), the place it shows now and
 * where in it the user is.
 */
struct fm_tab {
	struct fm_visit history[FM_HISTORY];
	int history_count;
	int history_index;
	struct fm_listing listing;
	int scroll;
	int cursor;
	int anchor;
	uint64_t checked_at;
};

/*
 * The sections of the sidebar.
 */
enum fm_place_section {
	FM_SECTION_FAVORITES,
	FM_SECTION_LOCATIONS,
	FM_SECTION_TAGS
};

/*
 * One place in the sidebar: its section, icon, label and where it leads.
 * color is a tag's; missing marks a favorite whose folder is not there.
 */
struct fm_place {
	unsigned section;
	unsigned icon;
	char label[64];
	struct fm_location location;
	fm_color color;
	int missing;
};

/*
 * The sidebar's places, in their order.
 */
struct fm_places {
	struct fm_place items[FM_PLACES];
	int count;
};

/*
 * How the items of a folder are shown.
 */
enum fm_view {
	FM_VIEW_ICONS,
	FM_VIEW_LIST
};

/*
 * What the items are sorted by.
 */
enum fm_sort {
	FM_SORT_NAME,
	FM_SORT_KIND,
	FM_SORT_SIZE,
	FM_SORT_MODIFIED
};

/*
 * The kinds of clickable region a frame records, which the pointer's
 * input is matched against.
 */
enum fm_hit_kind {
	FM_HIT_NONE,
	FM_HIT_BACK,
	FM_HIT_FORWARD,
	FM_HIT_HOME,
	FM_HIT_CRUMB,
	FM_HIT_SEARCH,
	FM_HIT_VIEW_ICONS,
	FM_HIT_VIEW_LIST,
	FM_HIT_PREVIEW,
	FM_HIT_PROGRESS,
	FM_HIT_PLACE,
	FM_HIT_ITEM,
	FM_HIT_CONTENT,
	FM_HIT_HEADER,
	FM_HIT_CARD,
	FM_HIT_RECENT,
	FM_HIT_SHOW_ALL,
	FM_HIT_TAB,
	FM_HIT_TAB_CLOSE,
	FM_HIT_SCOPE,
	FM_HIT_OVERLAY,
	FM_HIT_BUTTON
};

/*
 * One clickable region of the last frame: where it is, what it is and
 * which one (an item's index, a place's, a crumb's).
 */
struct fm_hit {
	struct fm_rect rect;
	unsigned kind;
	int index;
};

/*
 * The panels of the last frame, where the drawing put them.
 */
struct fm_layout {
	struct fm_rect toolbar;
	struct fm_rect tabbar;
	struct fm_rect sidebar;
	struct fm_rect content;
	struct fm_rect preview;
	int columns;
	int cell_width;
	int cell_height;
	int content_height;
};

/*
 * The file manager of one window: its settings, its tabs, what the last
 * frame drew and what the pointer and the keyboard are doing.
 *
 * One lives for the whole run.  The tabs are allocated (each holds its
 * history) and freed with the app.
 */
struct fm_app {
	/* The fonts, the window's size, the time of the input being handled, and whether a new frame is due. */
	struct fm_text *text;
	int width;
	int height;
	uint64_t now;
	int dirty;
	int focused;

	/* The user's home folder and name. */
	char home[FM_PATH_MAX];
	char user[64];

	/* How folders are shown. */
	unsigned view;
	unsigned sort;
	int sort_reverse;
	int show_sidebar;
	int show_preview;
	int show_hidden;

	/* The sidebar and the tabs. */
	struct fm_places places;
	struct fm_tab *tabs[FM_TABS];
	int tab_count;
	int tab_index;

	/* The last frame's panels and clickable regions. */
	struct fm_layout layout;
	struct fm_hit hits[FM_HITS];
	int hit_count;

	/* The pointer: where it is, the region under it, and the press in progress. */
	int pointer_x;
	int pointer_y;
	int pointer_inside;
	unsigned hover_kind;
	int hover_index;
	unsigned press_kind;
	int press_index;
	int pressing;

	/* The last click, which a second one soon after on the same region makes a double click. */
	uint64_t click_time;
	unsigned click_kind;
	int click_index;

	/* The modifiers held. */
	uint32_t modifiers;
};

/* The interface (ui.c). */
int fm_app_init(struct fm_app *app, struct fm_text *text, const char *start);
void fm_app_release(struct fm_app *app);
void fm_ui_event(struct fm_app *app, const struct fm_event *event);
void fm_ui_tick(struct fm_app *app, uint64_t now);
void fm_ui_draw(struct fm_app *app, struct fm_canvas *canvas);
void fm_ui_hit(struct fm_app *app, const struct fm_rect *rect, unsigned kind, int index);
struct fm_tab *fm_ui_tab(struct fm_app *app);
void fm_ui_go(struct fm_app *app, const struct fm_location *location);
void fm_log(const char *format, ...);

/* The icon and list views (ui-grid.c). */
void fm_grid_draw(struct fm_app *app, struct fm_canvas *canvas, const struct fm_rect *area);
void fm_grid_entry_icon(struct fm_app *app, struct fm_canvas *canvas, const struct fm_entry *entry, float x, float y, float size);

/* The listing of a place (dir.c). */
int fm_dir_read(struct fm_listing *listing, const char *path, int hidden);
void fm_dir_sort(struct fm_listing *listing, unsigned sort, int reverse);
void fm_dir_free(struct fm_listing *listing);
struct fm_entry *fm_dir_add(struct fm_listing *listing, const char *folder, const char *name);
int fm_dir_count(const char *path, int hidden);
void fm_dir_size_text(uint64_t size, char *text, size_t length);
void fm_dir_items_text(long count, char *text, size_t length);

/* The file types (mime.c). */
const struct fm_mime *fm_mime_guess(const char *name, mode_t mode);
const struct fm_mime *fm_mime_sniff(const char *path, const struct fm_mime *guess);
fm_color fm_mime_color(unsigned category);
void fm_mime_label(const char *name, char *label, size_t size);

/* The sidebar's places (places.c). */
void fm_places_init(struct fm_places *places, const char *home);
const char *fm_location_name(const struct fm_location *location, const char *home);

#endif
