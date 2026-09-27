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
#include "ops.h"

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

/* The most parts the path in the toolbar has. */
#define FM_CRUMBS		32

/* How many clickable regions one frame records. */
#define FM_HITS			1024

/* How many places the sidebar lists. */
#define FM_PLACES		40

/* The list view's header and row heights (ui-list.c; the keyboard and the rubber band use them too). */
#define FM_LIST_HEADER		30
#define FM_LIST_ROW		28

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
 * One part of the path in the toolbar: its label and the place it leads to.
 */
struct fm_crumb {
	char label[FM_NAME_MAX];
	struct fm_location location;
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
 * What the items are sorted by (FM_SORT_COUNT is none).
 */
enum fm_sort {
	FM_SORT_NAME,
	FM_SORT_KIND,
	FM_SORT_SIZE,
	FM_SORT_MODIFIED,
	FM_SORT_COUNT
};

/*
 * The columns of the list view; the app's columns are a mask of their
 * bits.  Location and Date Deleted are added by the places that need them.
 */
enum fm_column {
	FM_COLUMN_NAME,
	FM_COLUMN_KIND,
	FM_COLUMN_SIZE,
	FM_COLUMN_MODIFIED,
	FM_COLUMN_CHANGED,
	FM_COLUMN_TAGS,
	FM_COLUMN_OWNER,
	FM_COLUMN_LOCATION,
	FM_COLUMN_DELETED,
	FM_COLUMN_COUNT
};

/* The columns shown unless the user chooses others. */
#define FM_COLUMNS_DEFAULT	((1U << FM_COLUMN_KIND) | (1U << FM_COLUMN_SIZE) | (1U << FM_COLUMN_MODIFIED))

/*
 * Where the keyboard's input goes inside the window.
 */
enum fm_focus {
	FM_FOCUS_CONTENT,
	FM_FOCUS_LOCATION,
	FM_FOCUS_SEARCH,
	FM_FOCUS_RENAME
};

/*
 * What a key did to a text field.
 */
enum fm_field_result {
	FM_FIELD_NONE,
	FM_FIELD_MOVED,
	FM_FIELD_CHANGED,
	FM_FIELD_ENTER,
	FM_FIELD_CANCEL
};

/*
 * A one-line text field: UTF-8 text, the cursor and the other end of the
 * selection (byte offsets on character boundaries).
 */
struct fm_field {
	char text[FM_PATH_MAX];
	size_t length;
	size_t cursor;
	size_t anchor;
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
	struct fm_rect items;
	int grid_left;
	int columns;
	int cell_width;
	int cell_height;
	int content_height;
	int sidebar_height;
};

/* How many tags the window knows at most. */
#define FM_TAGS			16

/* A search's longest query, how many words of a kind it keeps and how long a word is, and its most results. */
#define FM_SEARCH_QUERY		256
#define FM_SEARCH_WORDS		8
#define FM_SEARCH_WORD		64
#define FM_SEARCH_RESULTS	5000U

/*
 * One tag the window knows: its name and color.
 */
struct fm_tag {
	char name[48];
	fm_color color;
};

/*
 * The tags the window knows, in the sidebar's order (bit n of a tag mask
 * is the n-th).
 */
struct fm_tags {
	struct fm_tag items[FM_TAGS];
	int count;
};

/*
 * A folder a search is walking: its open directory and its path.
 */
struct search_walk {
	void *directory;
	char path[FM_PATH_MAX];
};

/*
 * A search in progress: what it wants (names, extensions, kinds, tags),
 * where it looks, and the folders it is walking.
 */
struct fm_search {
	int active;
	char names[FM_SEARCH_WORDS][FM_SEARCH_WORD];
	int name_count;
	char extensions[FM_SEARCH_WORDS][FM_SEARCH_WORD];
	int extension_count;
	unsigned tag_mask;
	int unknown_tag;
	unsigned categories;
	char base[FM_PATH_MAX];
	int hidden;
	unsigned long visited;
	struct search_walk *walks;
	size_t walk_count;
	size_t walk_capacity;
};

/*
 * Where a search looks: the folder it was started in, the home folder, or
 * the whole computer.
 */
enum fm_scope {
	FM_SCOPE_FOLDER,
	FM_SCOPE_HOME,
	FM_SCOPE_COMPUTER
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
	time_t wall;
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
	unsigned columns;

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

	/* Where typing goes, and the location field (Ctrl+L). */
	unsigned focus;
	struct fm_field location;

	/* A rubber band being dragged over the items: its corners in the items' coordinates (scroll included). */
	int band;
	int band_x0;
	int band_y0;
	int band_x1;
	int band_y1;

	/* What was typed to find an item by its name, and when it was typed last. */
	char typed[64];
	size_t typed_length;
	uint64_t typed_at;

	/* A short message in the status pill, and until when it shows. */
	char message[160];
	uint64_t message_until;

	/* The tasks running, oldest first, whether their list is open, and when their progress was last drawn. */
	struct fm_task *tasks[FM_TASKS];
	int task_count;
	int show_tasks;
	uint64_t task_drawn_at;

	/* The undo and redo histories. */
	struct fm_undo undo;

	/* The name being changed: the field, the item's path and its index when the edit began. */
	struct fm_field rename;
	char rename_path[FM_PATH_MAX];

	/* A question being asked (FM_DIALOG_*), and the paths it is about. */
	unsigned dialog;
	char **dialog_paths;
	size_t dialog_count;

	/* The paths to select once the folder is read again (a finished task's outcome). */
	char **select_paths;
	size_t select_count;

	/* The tags the window knows. */
	struct fm_tags tags;

	/* The search: the field, when it was last typed in (0 when the search is up to date), the scope, the folder it was started from, and the walk. */
	struct fm_field search_field;
	uint64_t search_typed_at;
	unsigned search_scope;
	char search_folder[FM_PATH_MAX];
	struct fm_search search;

	/* How far the sidebar is scrolled (when its places do not fit). */
	int sidebar_scroll;
};

/*
 * The questions the window asks before an action that cannot be undone.
 */
enum fm_dialog {
	FM_DIALOG_NONE,
	FM_DIALOG_DELETE,
	FM_DIALOG_EMPTY_TRASH
};

/* The indexes of the buttons (FM_HIT_BUTTON) the frame records. */
#define FM_BUTTON_CANCEL	0
#define FM_BUTTON_CONFIRM	1
#define FM_BUTTON_PUT_BACK	10
#define FM_BUTTON_EMPTY_TRASH	11
#define FM_BUTTON_TASK_CANCEL	100
#define FM_BUTTON_REMOVE_PLACE	200

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
int fm_ui_crumbs(struct fm_app *app, struct fm_crumb *crumbs, int capacity);
void fm_ui_reload(struct fm_app *app, struct fm_tab *tab);
void fm_ui_back(struct fm_app *app);
void fm_ui_forward(struct fm_app *app);
void fm_ui_open(struct fm_app *app, int index);
void fm_ui_message(struct fm_app *app, const char *message);

/* The pointer and the keyboard (ui-input.c). */
void fm_input_motion(struct fm_app *app, const struct fm_event *event);
void fm_input_button(struct fm_app *app, const struct fm_event *event);
void fm_input_scroll(struct fm_app *app, int amount);
void fm_input_key(struct fm_app *app, const struct fm_event *event);
int fm_input_hit_at(struct fm_app *app, int x, int y, unsigned *kind, int *index);

/* The content panel and the icon view (ui-grid.c). */
void fm_grid_draw(struct fm_app *app, struct fm_canvas *canvas, const struct fm_rect *area);
void fm_grid_entry_icon(struct fm_app *app, struct fm_canvas *canvas, const struct fm_entry *entry, float x, float y, float size);
void fm_view_item_rect(struct fm_app *app, int index, struct fm_rect *rect);

/* The list view (ui-list.c). */
void fm_list_draw(struct fm_app *app, struct fm_canvas *canvas, const struct fm_rect *inner);
void fm_time_text(time_t when, time_t now, char *text, size_t size);
int fm_list_sort_at(struct fm_app *app, int index);

/* The text fields (ui-field.c). */
char fm_key_character(uint32_t key, uint32_t modifiers);
void fm_field_set(struct fm_field *field, const char *text);
void fm_field_select(struct fm_field *field, size_t start, size_t end);
unsigned fm_field_key(struct fm_field *field, uint32_t key, uint32_t modifiers);
void fm_field_insert(struct fm_field *field, const char *text, size_t length);
void fm_field_draw(struct fm_app *app, struct fm_canvas *canvas, const struct fm_field *field, const struct fm_rect *rect, unsigned pixels, const char *placeholder);

/* The tags (tags.c). */
void fm_tags_load(struct fm_tags *tags);
unsigned fm_tags_of(const struct fm_tags *tags, const char *path);
int fm_tags_write(const struct fm_tags *tags, const char *path, unsigned mask);
int fm_tags_paths(const struct fm_tags *tags, int tag, char ***paths, size_t *count);
int fm_tags_find(const struct fm_tags *tags, const char *name);

/* The search (search.c). */
void fm_search_start(struct fm_search *search, const struct fm_tags *tags, const char *query, const char *base, int hidden);
int fm_search_step(struct fm_search *search, const struct fm_tags *tags, struct fm_listing *listing, uint64_t budget_ms);
void fm_search_stop(struct fm_search *search);

/* The places that are not one folder and the search field (ui-search.c). */
void fm_search_focus(struct fm_app *app);
void fm_search_key(struct fm_app *app, const struct fm_event *event);
void fm_search_tick(struct fm_app *app);
void fm_search_scope(struct fm_app *app, unsigned scope);
void fm_search_load(struct fm_app *app, struct fm_tab *tab);
void fm_recent_add(const char *path);

/* The file operations (actions.c). */
void fm_action_copy(struct fm_app *app, int cut);
void fm_action_paste(struct fm_app *app);
void fm_action_duplicate(struct fm_app *app);
void fm_action_trash(struct fm_app *app);
void fm_action_delete(struct fm_app *app);
void fm_action_empty_trash(struct fm_app *app);
void fm_action_confirm(struct fm_app *app, int confirmed);
void fm_action_put_back(struct fm_app *app);
void fm_action_new_folder(struct fm_app *app);
void fm_action_rename_begin(struct fm_app *app);
void fm_action_rename_end(struct fm_app *app, int commit);
void fm_action_undo(struct fm_app *app, int redo);
void fm_action_cancel_task(struct fm_app *app, int index);
void fm_action_toggle_tag(struct fm_app *app, int tag);
void fm_action_add_favorite(struct fm_app *app);
void fm_action_remove_favorite(struct fm_app *app, int place);
int fm_actions_tick(struct fm_app *app);
void fm_actions_release(struct fm_app *app);
const char *fm_current_folder(struct fm_app *app);
int fm_selected_paths(struct fm_app *app, char ***paths, size_t *count);

/* The dialogs and the tasks' list (ui-overlay.c). */
void fm_overlay_draw(struct fm_app *app, struct fm_canvas *canvas);
void fm_tasks_draw(struct fm_app *app, struct fm_canvas *canvas, int x, int y);
void fm_task_text(const struct fm_task *task, char *text, size_t size);

/* The selection (select.c). */
void fm_select_none(struct fm_tab *tab);
void fm_select_only(struct fm_tab *tab, int index);
void fm_select_toggle(struct fm_tab *tab, int index);
void fm_select_range(struct fm_tab *tab, int from, int to);
void fm_select_all(struct fm_tab *tab);
size_t fm_select_count(struct fm_tab *tab, uint64_t *bytes);
int fm_select_first(struct fm_tab *tab);
int fm_select_find(struct fm_tab *tab, const char *name);

/* The listing of a place (dir.c). */
int fm_dir_read(struct fm_listing *listing, const char *path, int hidden);
void fm_dir_sort(struct fm_listing *listing, unsigned sort, int reverse);
void fm_dir_free(struct fm_listing *listing);
struct fm_entry *fm_dir_add(struct fm_listing *listing, const char *folder, const char *name);
int fm_dir_count(const char *path, int hidden);
int fm_dir_read_trash(struct fm_listing *listing, const char *trash);
void fm_dir_size_text(uint64_t size, char *text, size_t length);
void fm_dir_items_text(long count, char *text, size_t length);
void fm_owner_text(uid_t uid, gid_t gid, char *text, size_t length);

/* The file types (mime.c). */
const struct fm_mime *fm_mime_guess(const char *name, mode_t mode);
const struct fm_mime *fm_mime_sniff(const char *path, const struct fm_mime *guess);
fm_color fm_mime_color(unsigned category);
void fm_mime_label(const char *name, char *label, size_t size);

/* The sidebar's places (places.c). */
void fm_places_init(struct fm_places *places, const char *home, const struct fm_tags *tags);
int fm_places_add_favorite(struct fm_places *places, const char *path);
int fm_places_remove_favorite(struct fm_places *places, int removed);
const char *fm_location_name(const struct fm_location *location, const char *home);
void fm_tags_text(struct fm_app *app, unsigned tags, char *text, size_t length);

#endif
