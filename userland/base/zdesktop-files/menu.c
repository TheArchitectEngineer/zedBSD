/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The menus of zdesktop-files: File, Edit, View, Go, Window and Help
 * (spec §36, §37).
 *
 * zdesktop draws them (in the window's floating title bar, or in the
 * system bar while the window is docked) from the model given through
 * libzdesktop (the System Menu, WS070).  A choice arrives as an action
 * while the window's events are dispatched; it is queued here and carried
 * out by the main loop (fm_ui_action), which then tells the menus the
 * window's state in one transaction.  Without the System Menu the window
 * has no menus, and the keys still work.
 *
 * Only shortcuts with Ctrl or Alt are given to zdesktop, which takes such a
 * key for the menu while its item is enabled; F2, Delete, Space, Enter
 * and the arrows stay the window's own, so a text field keeps them.
 */

#include "window.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

/* The top-level menus. */
#define MENU_FILE		1U
#define MENU_EDIT		2U
#define MENU_VIEW		3U
#define MENU_GO			4U
#define MENU_WINDOW		5U
#define MENU_HELP		6U

/* The submenus. */
#define MENU_OPEN_WITH		10U
#define MENU_TAGS		11U
#define MENU_SORT		12U
#define MENU_COLUMNS		13U

/* The items that do nothing yet (the views and the place of later versions). */
#define MENU_VIEW_COLUMNS	20U
#define MENU_VIEW_GALLERY	21U
#define MENU_GO_NETWORK		22U

/* The separators' IDs start here. */
#define MENU_LINE		30U

/* An item that carries out an action has the action's ID moved past the others. */
#define MENU_ACTION_ID(action)	(1000U + (uint32_t)(action))

/* The keysyms of the shortcuts' keys that are not letters or digits. */
#define MENU_KEY_LEFT		0xff51U
#define MENU_KEY_UP		0xff52U
#define MENU_KEY_RIGHT		0xff53U

/* The modifiers of the shortcuts. */
#define MENU_CTRL		ZDESKTOP_MENU_CTRL
#define MENU_CTRL_SHIFT		(ZDESKTOP_MENU_CTRL | ZDESKTOP_MENU_SHIFT)
#define MENU_CTRL_ALT		(ZDESKTOP_MENU_CTRL | ZDESKTOP_MENU_ALT)
#define MENU_ALT		ZDESKTOP_MENU_ALT

/* The list columns the View menu names, after the name (which is always shown). */
#define MENU_COLUMN_COUNT	6

/*
 * One item of the menus as the window builds them: its ID, its parent,
 * its type, its label, its action, its role and its shortcut.
 */
struct menu_item {
	uint32_t id;
	uint32_t parent;
	unsigned type;
	const char *label;
	uint32_t action;
	unsigned role;
	unsigned modifiers;
	uint32_t keysym;
};

/*
 * The fixed items, in the order they are shown.  The ways to open the
 * selection, the tags and the list columns are added after them by
 * menu_build.
 */
static const struct menu_item menu_items[] = {
	{ MENU_FILE, ZDESKTOP_MENU_ROOT, ZDESKTOP_MENU_ITEM_SUBMENU, "File", 0U, ZDESKTOP_MENU_ROLE_NONE, 0U, 0U },
	{ MENU_ACTION_ID(FM_ACTION_NEW_WINDOW), MENU_FILE, ZDESKTOP_MENU_ITEM_NORMAL, "New Window", FM_ACTION_NEW_WINDOW, ZDESKTOP_MENU_ROLE_NEW, MENU_CTRL, 'n' },
	{ MENU_ACTION_ID(FM_ACTION_NEW_FOLDER), MENU_FILE, ZDESKTOP_MENU_ITEM_NORMAL, "New Folder", FM_ACTION_NEW_FOLDER, ZDESKTOP_MENU_ROLE_NONE, MENU_CTRL_SHIFT, 'n' },
	{ MENU_ACTION_ID(FM_ACTION_OPEN), MENU_FILE, ZDESKTOP_MENU_ITEM_NORMAL, "Open", FM_ACTION_OPEN, ZDESKTOP_MENU_ROLE_OPEN, MENU_CTRL, 'o' },
	{ MENU_OPEN_WITH, MENU_FILE, ZDESKTOP_MENU_ITEM_SUBMENU, "Open With", 0U, ZDESKTOP_MENU_ROLE_NONE, 0U, 0U },
	{ MENU_LINE, MENU_FILE, ZDESKTOP_MENU_ITEM_SEPARATOR, "", 0U, ZDESKTOP_MENU_ROLE_NONE, 0U, 0U },
	{ MENU_ACTION_ID(FM_ACTION_GET_INFO), MENU_FILE, ZDESKTOP_MENU_ITEM_NORMAL, "Get Info", FM_ACTION_GET_INFO, ZDESKTOP_MENU_ROLE_NONE, MENU_CTRL, 'i' },
	{ MENU_ACTION_ID(FM_ACTION_TRASH), MENU_FILE, ZDESKTOP_MENU_ITEM_NORMAL, "Move to Trash", FM_ACTION_TRASH, ZDESKTOP_MENU_ROLE_DELETE, 0U, 0U },
	{ MENU_LINE + 1U, MENU_FILE, ZDESKTOP_MENU_ITEM_SEPARATOR, "", 0U, ZDESKTOP_MENU_ROLE_NONE, 0U, 0U },
	{ MENU_ACTION_ID(FM_ACTION_CLOSE_WINDOW), MENU_FILE, ZDESKTOP_MENU_ITEM_NORMAL, "Close Window", FM_ACTION_CLOSE_WINDOW, ZDESKTOP_MENU_ROLE_CLOSE, MENU_CTRL_SHIFT, 'w' },
	{ MENU_EDIT, ZDESKTOP_MENU_ROOT, ZDESKTOP_MENU_ITEM_SUBMENU, "Edit", 0U, ZDESKTOP_MENU_ROLE_NONE, 0U, 0U },
	{ MENU_ACTION_ID(FM_ACTION_UNDO), MENU_EDIT, ZDESKTOP_MENU_ITEM_NORMAL, "Undo", FM_ACTION_UNDO, ZDESKTOP_MENU_ROLE_UNDO, MENU_CTRL, 'z' },
	{ MENU_ACTION_ID(FM_ACTION_REDO), MENU_EDIT, ZDESKTOP_MENU_ITEM_NORMAL, "Redo", FM_ACTION_REDO, ZDESKTOP_MENU_ROLE_REDO, MENU_CTRL_SHIFT, 'z' },
	{ MENU_LINE + 2U, MENU_EDIT, ZDESKTOP_MENU_ITEM_SEPARATOR, "", 0U, ZDESKTOP_MENU_ROLE_NONE, 0U, 0U },
	{ MENU_ACTION_ID(FM_ACTION_CUT), MENU_EDIT, ZDESKTOP_MENU_ITEM_NORMAL, "Cut", FM_ACTION_CUT, ZDESKTOP_MENU_ROLE_CUT, MENU_CTRL, 'x' },
	{ MENU_ACTION_ID(FM_ACTION_COPY), MENU_EDIT, ZDESKTOP_MENU_ITEM_NORMAL, "Copy", FM_ACTION_COPY, ZDESKTOP_MENU_ROLE_COPY, MENU_CTRL, 'c' },
	{ MENU_ACTION_ID(FM_ACTION_PASTE), MENU_EDIT, ZDESKTOP_MENU_ITEM_NORMAL, "Paste", FM_ACTION_PASTE, ZDESKTOP_MENU_ROLE_PASTE, MENU_CTRL, 'v' },
	{ MENU_ACTION_ID(FM_ACTION_DUPLICATE), MENU_EDIT, ZDESKTOP_MENU_ITEM_NORMAL, "Duplicate", FM_ACTION_DUPLICATE, ZDESKTOP_MENU_ROLE_NONE, MENU_CTRL, 'd' },
	{ MENU_ACTION_ID(FM_ACTION_SELECT_ALL), MENU_EDIT, ZDESKTOP_MENU_ITEM_NORMAL, "Select All", FM_ACTION_SELECT_ALL, ZDESKTOP_MENU_ROLE_SELECT_ALL, MENU_CTRL, 'a' },
	{ MENU_LINE + 3U, MENU_EDIT, ZDESKTOP_MENU_ITEM_SEPARATOR, "", 0U, ZDESKTOP_MENU_ROLE_NONE, 0U, 0U },
	{ MENU_ACTION_ID(FM_ACTION_RENAME), MENU_EDIT, ZDESKTOP_MENU_ITEM_NORMAL, "Rename", FM_ACTION_RENAME, ZDESKTOP_MENU_ROLE_NONE, 0U, 0U },
	{ MENU_TAGS, MENU_EDIT, ZDESKTOP_MENU_ITEM_SUBMENU, "Tags", 0U, ZDESKTOP_MENU_ROLE_NONE, 0U, 0U },
	{ MENU_VIEW, ZDESKTOP_MENU_ROOT, ZDESKTOP_MENU_ITEM_SUBMENU, "View", 0U, ZDESKTOP_MENU_ROLE_NONE, 0U, 0U },
	{ MENU_ACTION_ID(FM_ACTION_VIEW_ICONS), MENU_VIEW, ZDESKTOP_MENU_ITEM_RADIO, "Icons", FM_ACTION_VIEW_ICONS, ZDESKTOP_MENU_ROLE_NONE, MENU_CTRL, '1' },
	{ MENU_ACTION_ID(FM_ACTION_VIEW_LIST), MENU_VIEW, ZDESKTOP_MENU_ITEM_RADIO, "List", FM_ACTION_VIEW_LIST, ZDESKTOP_MENU_ROLE_NONE, MENU_CTRL, '2' },
	{ MENU_VIEW_COLUMNS, MENU_VIEW, ZDESKTOP_MENU_ITEM_RADIO, "Columns", 0U, ZDESKTOP_MENU_ROLE_NONE, 0U, 0U },
	{ MENU_VIEW_GALLERY, MENU_VIEW, ZDESKTOP_MENU_ITEM_RADIO, "Gallery", 0U, ZDESKTOP_MENU_ROLE_NONE, 0U, 0U },
	{ MENU_LINE + 4U, MENU_VIEW, ZDESKTOP_MENU_ITEM_SEPARATOR, "", 0U, ZDESKTOP_MENU_ROLE_NONE, 0U, 0U },
	{ MENU_SORT, MENU_VIEW, ZDESKTOP_MENU_ITEM_SUBMENU, "Sort By", 0U, ZDESKTOP_MENU_ROLE_NONE, 0U, 0U },
	{ MENU_ACTION_ID(FM_ACTION_SORT_NAME), MENU_SORT, ZDESKTOP_MENU_ITEM_RADIO, "Name", FM_ACTION_SORT_NAME, ZDESKTOP_MENU_ROLE_NONE, 0U, 0U },
	{ MENU_ACTION_ID(FM_ACTION_SORT_KIND), MENU_SORT, ZDESKTOP_MENU_ITEM_RADIO, "Kind", FM_ACTION_SORT_KIND, ZDESKTOP_MENU_ROLE_NONE, 0U, 0U },
	{ MENU_ACTION_ID(FM_ACTION_SORT_SIZE), MENU_SORT, ZDESKTOP_MENU_ITEM_RADIO, "Size", FM_ACTION_SORT_SIZE, ZDESKTOP_MENU_ROLE_NONE, 0U, 0U },
	{ MENU_ACTION_ID(FM_ACTION_SORT_MODIFIED), MENU_SORT, ZDESKTOP_MENU_ITEM_RADIO, "Date Modified", FM_ACTION_SORT_MODIFIED, ZDESKTOP_MENU_ROLE_NONE, 0U, 0U },
	{ MENU_COLUMNS, MENU_VIEW, ZDESKTOP_MENU_ITEM_SUBMENU, "List Columns", 0U, ZDESKTOP_MENU_ROLE_NONE, 0U, 0U },
	{ MENU_LINE + 5U, MENU_VIEW, ZDESKTOP_MENU_ITEM_SEPARATOR, "", 0U, ZDESKTOP_MENU_ROLE_NONE, 0U, 0U },
	{ MENU_ACTION_ID(FM_ACTION_SHOW_SIDEBAR), MENU_VIEW, ZDESKTOP_MENU_ITEM_CHECKBOX, "Show Sidebar", FM_ACTION_SHOW_SIDEBAR, ZDESKTOP_MENU_ROLE_NONE, MENU_CTRL_ALT, 's' },
	{ MENU_ACTION_ID(FM_ACTION_SHOW_PREVIEW), MENU_VIEW, ZDESKTOP_MENU_ITEM_CHECKBOX, "Show Preview", FM_ACTION_SHOW_PREVIEW, ZDESKTOP_MENU_ROLE_NONE, MENU_CTRL_ALT, 'p' },
	{ MENU_ACTION_ID(FM_ACTION_SHOW_HIDDEN), MENU_VIEW, ZDESKTOP_MENU_ITEM_CHECKBOX, "Show Hidden Files", FM_ACTION_SHOW_HIDDEN, ZDESKTOP_MENU_ROLE_NONE, MENU_CTRL, 'h' },
	{ MENU_GO, ZDESKTOP_MENU_ROOT, ZDESKTOP_MENU_ITEM_SUBMENU, "Go", 0U, ZDESKTOP_MENU_ROLE_NONE, 0U, 0U },
	{ MENU_ACTION_ID(FM_ACTION_BACK), MENU_GO, ZDESKTOP_MENU_ITEM_NORMAL, "Back", FM_ACTION_BACK, ZDESKTOP_MENU_ROLE_NONE, MENU_ALT, MENU_KEY_LEFT },
	{ MENU_ACTION_ID(FM_ACTION_FORWARD), MENU_GO, ZDESKTOP_MENU_ITEM_NORMAL, "Forward", FM_ACTION_FORWARD, ZDESKTOP_MENU_ROLE_NONE, MENU_ALT, MENU_KEY_RIGHT },
	{ MENU_ACTION_ID(FM_ACTION_ENCLOSING), MENU_GO, ZDESKTOP_MENU_ITEM_NORMAL, "Enclosing Folder", FM_ACTION_ENCLOSING, ZDESKTOP_MENU_ROLE_NONE, MENU_CTRL, MENU_KEY_UP },
	{ MENU_LINE + 6U, MENU_GO, ZDESKTOP_MENU_ITEM_SEPARATOR, "", 0U, ZDESKTOP_MENU_ROLE_NONE, 0U, 0U },
	{ MENU_ACTION_ID(FM_ACTION_GO_HOME), MENU_GO, ZDESKTOP_MENU_ITEM_NORMAL, "Home", FM_ACTION_GO_HOME, ZDESKTOP_MENU_ROLE_NONE, MENU_CTRL_SHIFT, 'h' },
	{ MENU_ACTION_ID(FM_ACTION_GO_DESKTOP), MENU_GO, ZDESKTOP_MENU_ITEM_NORMAL, "Desktop", FM_ACTION_GO_DESKTOP, ZDESKTOP_MENU_ROLE_NONE, MENU_CTRL_SHIFT, 'd' },
	{ MENU_ACTION_ID(FM_ACTION_GO_DOCUMENTS), MENU_GO, ZDESKTOP_MENU_ITEM_NORMAL, "Documents", FM_ACTION_GO_DOCUMENTS, ZDESKTOP_MENU_ROLE_NONE, MENU_CTRL_SHIFT, 'o' },
	{ MENU_ACTION_ID(FM_ACTION_GO_DOWNLOADS), MENU_GO, ZDESKTOP_MENU_ITEM_NORMAL, "Downloads", FM_ACTION_GO_DOWNLOADS, ZDESKTOP_MENU_ROLE_NONE, MENU_CTRL_SHIFT, 'l' },
	{ MENU_ACTION_ID(FM_ACTION_GO_RECENTS), MENU_GO, ZDESKTOP_MENU_ITEM_NORMAL, "Recents", FM_ACTION_GO_RECENTS, ZDESKTOP_MENU_ROLE_NONE, MENU_CTRL_SHIFT, 'r' },
	{ MENU_ACTION_ID(FM_ACTION_GO_COMPUTER), MENU_GO, ZDESKTOP_MENU_ITEM_NORMAL, "Computer", FM_ACTION_GO_COMPUTER, ZDESKTOP_MENU_ROLE_NONE, MENU_CTRL_SHIFT, 'c' },
	{ MENU_GO_NETWORK, MENU_GO, ZDESKTOP_MENU_ITEM_NORMAL, "Network", 0U, ZDESKTOP_MENU_ROLE_NONE, 0U, 0U },
	{ MENU_ACTION_ID(FM_ACTION_GO_TRASH), MENU_GO, ZDESKTOP_MENU_ITEM_NORMAL, "Trash", FM_ACTION_GO_TRASH, ZDESKTOP_MENU_ROLE_NONE, 0U, 0U },
	{ MENU_LINE + 7U, MENU_GO, ZDESKTOP_MENU_ITEM_SEPARATOR, "", 0U, ZDESKTOP_MENU_ROLE_NONE, 0U, 0U },
	{ MENU_ACTION_ID(FM_ACTION_GO_LOCATION), MENU_GO, ZDESKTOP_MENU_ITEM_NORMAL, "Go to Location...", FM_ACTION_GO_LOCATION, ZDESKTOP_MENU_ROLE_NONE, MENU_CTRL, 'l' },
	{ MENU_ACTION_ID(FM_ACTION_FIND), MENU_GO, ZDESKTOP_MENU_ITEM_NORMAL, "Find", FM_ACTION_FIND, ZDESKTOP_MENU_ROLE_FIND, MENU_CTRL, 'f' },
	{ MENU_WINDOW, ZDESKTOP_MENU_ROOT, ZDESKTOP_MENU_ITEM_SUBMENU, "Window", 0U, ZDESKTOP_MENU_ROLE_NONE, 0U, 0U },
	{ MENU_ACTION_ID(FM_ACTION_MINIMIZE), MENU_WINDOW, ZDESKTOP_MENU_ITEM_NORMAL, "Minimize", FM_ACTION_MINIMIZE, ZDESKTOP_MENU_ROLE_NONE, MENU_CTRL, 'm' },
	{ MENU_ACTION_ID(FM_ACTION_ZOOM), MENU_WINDOW, ZDESKTOP_MENU_ITEM_NORMAL, "Zoom", FM_ACTION_ZOOM, ZDESKTOP_MENU_ROLE_NONE, 0U, 0U },
	{ MENU_HELP, ZDESKTOP_MENU_ROOT, ZDESKTOP_MENU_ITEM_SUBMENU, "Help", 0U, ZDESKTOP_MENU_ROLE_NONE, 0U, 0U },
	{ MENU_ACTION_ID(FM_ACTION_HELP), MENU_HELP, ZDESKTOP_MENU_ITEM_NORMAL, "File Manager Help", FM_ACTION_HELP, ZDESKTOP_MENU_ROLE_HELP, 0U, 0U },
	{ MENU_ACTION_ID(FM_ACTION_SHORTCUTS), MENU_HELP, ZDESKTOP_MENU_ITEM_NORMAL, "Keyboard Shortcuts", FM_ACTION_SHORTCUTS, ZDESKTOP_MENU_ROLE_NONE, 0U, 0U },
	{ MENU_LINE + 8U, MENU_HELP, ZDESKTOP_MENU_ITEM_SEPARATOR, "", 0U, ZDESKTOP_MENU_ROLE_NONE, 0U, 0U },
	{ MENU_ACTION_ID(FM_ACTION_ABOUT), MENU_HELP, ZDESKTOP_MENU_ITEM_NORMAL, "About Files", FM_ACTION_ABOUT, ZDESKTOP_MENU_ROLE_ABOUT, 0U, 0U }
};

/* The list columns the View menu names, in the list's order. */
static const unsigned menu_columns[MENU_COLUMN_COUNT] = {
	FM_COLUMN_KIND,
	FM_COLUMN_SIZE,
	FM_COLUMN_MODIFIED,
	FM_COLUMN_CHANGED,
	FM_COLUMN_TAGS,
	FM_COLUMN_OWNER
};

/* The labels of those columns. */
static const char *const menu_column_labels[MENU_COLUMN_COUNT] = {
	"Kind",
	"Size",
	"Date Modified",
	"Changed",
	"Tags",
	"Owner"
};

static void menu_activated(void *data, struct zdesktop_window_menu *window_menu, uint32_t item, uint32_t action, struct wl_seat *seat, uint32_t serial);
static int menu_build(struct fm_menu *menu);
static int menu_add(struct fm_menu *menu, const struct menu_item *item);
static int menu_add_slots(struct fm_menu *menu);
static int menu_state(struct fm_menu *menu, const struct fm_menu_state *state);
static int menu_state_items(struct zdesktop_menu *model, const struct fm_menu_state *state);
static int menu_state_slots(struct zdesktop_menu *model, const struct fm_menu_state *state);

/* What the window menu tells the window: only the choices. */
static const struct zdesktop_window_menu_listener menu_listener = {
	menu_activated, NULL, NULL
};

/*
 * Gives zdesktop the window's menus, showing a state.
 *
 * Returns 0, also when the compositor has no System Menu (the window then
 * has no menus), or an errno value when the menus could not be made.
 */
int
fm_menu_open(
	struct fm_menu *menu,
	struct fm_window *window,
	const struct fm_menu_state *state)
{
	int error;

	/* Nothing yet. */
	memset(menu, 0, sizeof(*menu));

	/* The connection's menu service; a compositor without one leaves the window without menus. */
	menu->service = zdesktop_menu_service_open(window->display);
	if (menu->service == NULL) {
		fm_log("MENU none errno=%d", errno);
		return 0;
	}

	/* The menu, empty until it is built. */
	menu->menu = zdesktop_menu_create(menu->service);
	if (menu->menu == NULL)
		return errno;

	/* The window's place for a menu, which tells the window what is chosen. */
	menu->window_menu = zdesktop_window_menu_create(menu->service, window->toplevel, &menu_listener, menu);
	if (menu->window_menu == NULL)
		return errno;

	/* The items in one transaction. */
	error = menu_build(menu);
	if (error != 0)
		return error;

	/* The window shows the menu from now on. */
	error = zdesktop_window_menu_set(menu->window_menu, menu->menu);
	if (error != 0)
		return error;

	/* The state it shows. */
	error = menu_state(menu, state);
	if (error != 0)
		return error;

	/* Succeeded: the menus are zdesktop's to show. */
	fm_log("MENU ready items=%u", (unsigned)(sizeof(menu_items) / sizeof(menu_items[0])));
	return 0;
}

/*
 * Tells the menus the window's state when it differs from what they show.
 */
void
fm_menu_refresh(
	struct fm_menu *menu,
	const struct fm_menu_state *state)
{
	int same;
	int error;

	/* Without menus nothing is sent. */
	if (menu->menu == NULL)
		return;

	/* Nor when the state is the one the menus show. */
	same = memcmp(state, &menu->shown, sizeof(*state));
	if (same == 0)
		return;

	/* The new state in one transaction; a refusal is reported and the menus stay as they were. */
	error = menu_state(menu, state);
	if (error != 0)
		fm_log("MENU update-failed errno=%d", error);
}

/*
 * Takes the oldest action chosen and not yet carried out (FM_ACTION_NONE
 * when there is none).
 */
unsigned
fm_menu_take(
	struct fm_menu *menu)
{
	uint32_t action;

	/* Nothing waits. */
	if (menu->action_count == 0U)
		return FM_ACTION_NONE;

	/* The oldest leaves the queue. */
	action = menu->actions[0];
	menu->action_count--;
	memmove(menu->actions, menu->actions + 1, menu->action_count * sizeof(menu->actions[0]));

	/* Succeeded: the action to carry out. */
	return action;
}

/*
 * Takes the menus away from zdesktop (before the window goes).
 */
void
fm_menu_close(
	struct fm_menu *menu)
{
	/* The window's place, the menu, then the service. */
	if (menu->window_menu != NULL)
		zdesktop_window_menu_destroy(menu->window_menu);
	if (menu->menu != NULL)
		zdesktop_menu_destroy(menu->menu);
	if (menu->service != NULL)
		zdesktop_menu_service_close(menu->service);

	/* Nothing is left. */
	memset(menu, 0, sizeof(*menu));
}

/* Queues a chosen action for the main loop. */
static void
menu_activated(
	void *data,
	struct zdesktop_window_menu *window_menu,
	uint32_t item,
	uint32_t action,
	struct wl_seat *seat,
	uint32_t serial)
{
	struct fm_menu *menu;

	/* The window's menus. */
	(void)window_menu;
	(void)seat;
	menu = data;

	/* The log line the tests read. */
	fm_log("MENU item=%u action=%u serial=%u", item, action, serial);

	/* A full queue drops the choice (sixteen choices in one round). */
	if (menu->action_count == FM_MENU_ACTIONS)
		return;

	/* The count is how many choices wait for the main loop (fm_menu_take). */
	menu->actions[menu->action_count] = action;
	menu->action_count++;
}

/* Gives zdesktop every item, with its role and shortcut, and the variable items' slots, in one transaction. */
static int
menu_build(
	struct fm_menu *menu)
{
	size_t index;
	int error;

	/* The transaction. */
	error = zdesktop_menu_begin(menu->menu);
	if (error != 0)
		return error;

	/* Each fixed item in its order under its parent. */
	for (index = 0; index < sizeof(menu_items) / sizeof(menu_items[0]); index++) {
		error = menu_add(menu, &menu_items[index]);
		if (error != 0)
			return error;
	}

	/* The slots of the variable items. */
	error = menu_add_slots(menu);
	if (error != 0)
		return error;

	/* The items are shown together. */
	error = zdesktop_menu_commit(menu->menu);
	if (error != 0)
		return error;

	/* Succeeded: the menus are built. */
	return 0;
}

/* Adds one item with its role and shortcut; returns 0 or an errno value. */
static int
menu_add(
	struct fm_menu *menu,
	const struct menu_item *item)
{
	int error;

	/* The item. */
	error = zdesktop_menu_append(menu->menu, item->id, item->parent, item->type, item->label, item->action);
	if (error != 0)
		return error;

	/* Its role, when it has one. */
	if (item->role != ZDESKTOP_MENU_ROLE_NONE) {
		error = zdesktop_menu_set_role(menu->menu, item->id, item->role);
		if (error != 0)
			return error;
	}

	/* Its shortcut, when it has one. */
	if (item->keysym != 0U) {
		error = zdesktop_menu_set_shortcut(menu->menu, item->id, item->modifiers, item->keysym);
		if (error != 0)
			return error;
	}

	/* Succeeded: the item is in the menu. */
	return 0;
}

/*
 * Adds the slots of the variable items: a way to open the selection each
 * (Open With), a checkbox a tag (Tags), a checkbox a list column; the
 * state names them and hides the ones not used.
 */
static int
menu_add_slots(
	struct fm_menu *menu)
{
	struct menu_item item;
	unsigned index;
	int error;

	/* The ways to open the selection. */
	memset(&item, 0, sizeof(item));
	for (index = 0; index < FM_OPENERS; index++) {
		item.id = MENU_ACTION_ID(FM_ACTION_OPEN_WITH_FIRST + index);
		item.parent = MENU_OPEN_WITH;
		item.type = ZDESKTOP_MENU_ITEM_NORMAL;
		item.label = "-";
		item.action = FM_ACTION_OPEN_WITH_FIRST + index;
		error = menu_add(menu, &item);
		if (error != 0)
			return error;
	}

	/* The tags. */
	for (index = 0; index < FM_TAGS; index++) {
		item.id = MENU_ACTION_ID(FM_ACTION_TAG_FIRST + index);
		item.parent = MENU_TAGS;
		item.type = ZDESKTOP_MENU_ITEM_CHECKBOX;
		item.label = "-";
		item.action = FM_ACTION_TAG_FIRST + index;
		error = menu_add(menu, &item);
		if (error != 0)
			return error;
	}

	/* The list columns. */
	for (index = 0; index < MENU_COLUMN_COUNT; index++) {
		item.id = MENU_ACTION_ID(FM_ACTION_COLUMN_FIRST + menu_columns[index]);
		item.parent = MENU_COLUMNS;
		item.type = ZDESKTOP_MENU_ITEM_CHECKBOX;
		item.label = menu_column_labels[index];
		item.action = FM_ACTION_COLUMN_FIRST + menu_columns[index];
		error = menu_add(menu, &item);
		if (error != 0)
			return error;
	}

	/* Succeeded: the slots are there. */
	return 0;
}

/* Shows a state in the menus in one transaction; returns 0 or an errno value. */
static int
menu_state(
	struct fm_menu *menu,
	const struct fm_menu_state *state)
{
	int error;

	/* The transaction. */
	error = zdesktop_menu_begin(menu->menu);
	if (error != 0)
		return error;

	/* The fixed items, then the variable ones. */
	error = menu_state_items(menu->menu, state);
	if (error == 0)
		error = menu_state_slots(menu->menu, state);

	/*
	 * A refused change still ends the transaction, so that the menu is not
	 * left open for changes; the refusal is reported.
	 */
	if (error != 0) {
		(void)zdesktop_menu_commit(menu->menu);
		return error;
	}

	/* The state is shown together. */
	error = zdesktop_menu_commit(menu->menu);
	if (error != 0)
		return error;

	/* Succeeded: the menus show the state. */
	menu->shown = *state;
	fm_log("MENU state selection=%d folder=%d paste=%d undo=%d back=%d view=%u sort=%u openers=%d tags=%d", state->selection, state->folder, state->can_paste, state->can_undo, state->can_back, state->view, state->sort, state->opener_count, state->tag_count);
	return 0;
}

/* Sets which fixed items do something now and which are checked; returns 0 or the first refusal. */
static int
menu_state_items(
	struct zdesktop_menu *model,
	const struct fm_menu_state *state)
{
	int selected;
	int editable;
	int renamable;
	int undo;
	int redo;
	int paste;
	int tags;
	int error;

	/* A selection to act on, outside a text field (whose keys stay its own) and outside the trash. */
	selected = 0;
	if (state->selection > 0 && state->field == 0)
		selected = 1;
	editable = selected;
	if (state->trash != 0)
		editable = 0;

	/* One item to rename, and tags to put on the selection. */
	renamable = 0;
	if (editable != 0 && state->selection == 1)
		renamable = 1;
	tags = 0;
	if (selected != 0 && state->tag_count > 0)
		tags = 1;

	/* The histories and the clipboard, which a text field's own keys leave alone. */
	undo = 0;
	redo = 0;
	paste = 0;
	if (state->field == 0) {
		undo = state->can_undo;
		redo = state->can_redo;
		paste = state->can_paste;
	}

	/* File. */
	error = zdesktop_menu_set_enabled(model, MENU_ACTION_ID(FM_ACTION_NEW_FOLDER), state->folder);
	if (error == 0)
		error = zdesktop_menu_set_enabled(model, MENU_ACTION_ID(FM_ACTION_OPEN), selected);
	if (error == 0)
		error = zdesktop_menu_set_enabled(model, MENU_OPEN_WITH, state->opener_count > 0);
	if (error == 0)
		error = zdesktop_menu_set_enabled(model, MENU_ACTION_ID(FM_ACTION_TRASH), editable);

	/* Edit: the histories, the clipboard and the selection, none of them inside a text field. */
	if (error == 0)
		error = zdesktop_menu_set_enabled(model, MENU_ACTION_ID(FM_ACTION_UNDO), undo);
	if (error == 0)
		error = zdesktop_menu_set_enabled(model, MENU_ACTION_ID(FM_ACTION_REDO), redo);
	if (error == 0)
		error = zdesktop_menu_set_enabled(model, MENU_ACTION_ID(FM_ACTION_CUT), editable);
	if (error == 0)
		error = zdesktop_menu_set_enabled(model, MENU_ACTION_ID(FM_ACTION_COPY), selected);
	if (error == 0)
		error = zdesktop_menu_set_enabled(model, MENU_ACTION_ID(FM_ACTION_PASTE), paste);
	if (error == 0)
		error = zdesktop_menu_set_enabled(model, MENU_ACTION_ID(FM_ACTION_DUPLICATE), editable);
	if (error == 0)
		error = zdesktop_menu_set_enabled(model, MENU_ACTION_ID(FM_ACTION_RENAME), renamable);
	if (error == 0)
		error = zdesktop_menu_set_enabled(model, MENU_TAGS, tags);

	/* View: the view and the sort as radio items, the panels as checkboxes; the views of later versions off. */
	if (error == 0)
		error = zdesktop_menu_set_checked(model, MENU_ACTION_ID(FM_ACTION_VIEW_ICONS), state->view == FM_VIEW_ICONS);
	if (error == 0)
		error = zdesktop_menu_set_checked(model, MENU_ACTION_ID(FM_ACTION_VIEW_LIST), state->view == FM_VIEW_LIST);
	if (error == 0)
		error = zdesktop_menu_set_enabled(model, MENU_VIEW_COLUMNS, 0);
	if (error == 0)
		error = zdesktop_menu_set_enabled(model, MENU_VIEW_GALLERY, 0);
	if (error == 0)
		error = zdesktop_menu_set_checked(model, MENU_ACTION_ID(FM_ACTION_SORT_NAME), state->sort == FM_SORT_NAME);
	if (error == 0)
		error = zdesktop_menu_set_checked(model, MENU_ACTION_ID(FM_ACTION_SORT_KIND), state->sort == FM_SORT_KIND);
	if (error == 0)
		error = zdesktop_menu_set_checked(model, MENU_ACTION_ID(FM_ACTION_SORT_SIZE), state->sort == FM_SORT_SIZE);
	if (error == 0)
		error = zdesktop_menu_set_checked(model, MENU_ACTION_ID(FM_ACTION_SORT_MODIFIED), state->sort == FM_SORT_MODIFIED);
	if (error == 0)
		error = zdesktop_menu_set_checked(model, MENU_ACTION_ID(FM_ACTION_SHOW_SIDEBAR), state->sidebar);
	if (error == 0)
		error = zdesktop_menu_set_checked(model, MENU_ACTION_ID(FM_ACTION_SHOW_PREVIEW), state->preview);
	if (error == 0)
		error = zdesktop_menu_set_checked(model, MENU_ACTION_ID(FM_ACTION_SHOW_HIDDEN), state->hidden);

	/* Go: the history's steps and the folder above; the network of later versions off. */
	if (error == 0)
		error = zdesktop_menu_set_enabled(model, MENU_ACTION_ID(FM_ACTION_BACK), state->can_back);
	if (error == 0)
		error = zdesktop_menu_set_enabled(model, MENU_ACTION_ID(FM_ACTION_FORWARD), state->can_forward);
	if (error == 0)
		error = zdesktop_menu_set_enabled(model, MENU_ACTION_ID(FM_ACTION_ENCLOSING), state->can_enclose);
	if (error == 0)
		error = zdesktop_menu_set_enabled(model, MENU_GO_NETWORK, 0);

	/* Reports the first refusal, or none. */
	if (error != 0)
		return error;

	/* Succeeded: the fixed items show the state. */
	return 0;
}

/* Names, shows and checks the variable items; returns 0 or the first refusal. */
static int
menu_state_slots(
	struct zdesktop_menu *model,
	const struct fm_menu_state *state)
{
	uint32_t id;
	unsigned bit;
	int index;
	int error;

	/* The ways to open the selection, the rest hidden. */
	error = 0;
	for (index = 0; index < FM_OPENERS && error == 0; index++) {
		id = MENU_ACTION_ID(FM_ACTION_OPEN_WITH_FIRST + (unsigned)index);
		if (index < state->opener_count)
			error = zdesktop_menu_set_label(model, id, state->openers[index]);
		if (error == 0)
			error = zdesktop_menu_set_visible(model, id, index < state->opener_count);
	}

	/* The tags, each checked when every selected item has it, the rest hidden. */
	for (index = 0; index < FM_TAGS && error == 0; index++) {
		id = MENU_ACTION_ID(FM_ACTION_TAG_FIRST + (unsigned)index);
		if (index < state->tag_count)
			error = zdesktop_menu_set_label(model, id, state->tags[index]);
		if (error == 0)
			error = zdesktop_menu_set_visible(model, id, index < state->tag_count);
		if (error == 0)
			error = zdesktop_menu_set_checked(model, id, (state->tags_checked & (1U << index)) != 0U);
	}

	/* The list columns shown. */
	for (index = 0; index < MENU_COLUMN_COUNT && error == 0; index++) {
		bit = 1U << menu_columns[index];
		error = zdesktop_menu_set_checked(model, MENU_ACTION_ID(FM_ACTION_COLUMN_FIRST + menu_columns[index]), (state->columns & bit) != 0U);
	}

	/* Reports the first refusal, or none. */
	if (error != 0)
		return error;

	/* Succeeded: the variable items show the state. */
	return 0;
}
