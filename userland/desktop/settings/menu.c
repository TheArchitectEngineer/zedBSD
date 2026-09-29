/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The menus of Settings: File, Edit (Find), View, Go (the history and every page, by
 * group), Window and Help.
 *
 * zdesktop draws them (in the window's floating title bar, or in the
 * system bar while the window is docked) from the model given through
 * libkeiland (the System Menu, WS070), as the file manager's are.  A choice
 * arrives as an action while the window's events are dispatched; it is
 * queued with the window's inputs (SE_EVENT_ACTION) and carried out by
 * se_ui_action, after which the main loop tells the menus the window's
 * state.  Without the System Menu the window has no menus, and the keys
 * still work.
 */

#include "window.h"

#include <errno.h>
#include <string.h>

/* The top-level menus. */
#define MENU_FILE		1U
#define MENU_VIEW		2U
#define MENU_GO			3U
#define MENU_WINDOW		4U
#define MENU_HELP		5U
#define MENU_EDIT		6U

/* The Go menu's submenus, one a group of pages. */
#define MENU_GROUP_FIRST	10U

/* The separators' IDs start here. */
#define MENU_LINE		30U

/* An item that carries out an action has the action's ID moved past the others. */
#define MENU_ACTION_ID(action)	(1000U + (uint32_t)(action))

/* The keysyms of the shortcuts' keys that are not letters. */
#define MENU_KEY_LEFT		0xff51U
#define MENU_KEY_RIGHT		0xff53U

/* The modifiers of the shortcuts. */
#define MENU_CTRL		KEILAND_MENU_CTRL
#define MENU_CTRL_SHIFT		(KEILAND_MENU_CTRL | KEILAND_MENU_SHIFT)
#define MENU_CTRL_ALT		(KEILAND_MENU_CTRL | KEILAND_MENU_ALT)
#define MENU_ALT		KEILAND_MENU_ALT

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
 * The fixed items, in the order they are shown.  The pages are added
 * after them, under Go's groups, by menu_build.
 */
static const struct menu_item menu_items[] = {
	{ MENU_FILE, KEILAND_MENU_ROOT, KEILAND_MENU_ITEM_SUBMENU, "File", 0U, KEILAND_MENU_ROLE_NONE, 0U, 0U },
	{ MENU_ACTION_ID(SE_ACTION_CLOSE_WINDOW), MENU_FILE, KEILAND_MENU_ITEM_NORMAL, "Close Window", SE_ACTION_CLOSE_WINDOW, KEILAND_MENU_ROLE_CLOSE, MENU_CTRL, 'w' },
	{ MENU_EDIT, KEILAND_MENU_ROOT, KEILAND_MENU_ITEM_SUBMENU, "Edit", 0U, KEILAND_MENU_ROLE_NONE, 0U, 0U },
	{ MENU_ACTION_ID(SE_ACTION_FIND), MENU_EDIT, KEILAND_MENU_ITEM_NORMAL, "Find", SE_ACTION_FIND, KEILAND_MENU_ROLE_FIND, MENU_CTRL, 'f' },
	{ MENU_VIEW, KEILAND_MENU_ROOT, KEILAND_MENU_ITEM_SUBMENU, "View", 0U, KEILAND_MENU_ROLE_NONE, 0U, 0U },
	{ MENU_ACTION_ID(SE_ACTION_SHOW_SIDEBAR), MENU_VIEW, KEILAND_MENU_ITEM_CHECKBOX, "Show Sidebar", SE_ACTION_SHOW_SIDEBAR, KEILAND_MENU_ROLE_NONE, MENU_CTRL_ALT, 's' },
	{ MENU_GO, KEILAND_MENU_ROOT, KEILAND_MENU_ITEM_SUBMENU, "Go", 0U, KEILAND_MENU_ROLE_NONE, 0U, 0U },
	{ MENU_ACTION_ID(SE_ACTION_BACK), MENU_GO, KEILAND_MENU_ITEM_NORMAL, "Back", SE_ACTION_BACK, KEILAND_MENU_ROLE_NONE, MENU_ALT, MENU_KEY_LEFT },
	{ MENU_ACTION_ID(SE_ACTION_FORWARD), MENU_GO, KEILAND_MENU_ITEM_NORMAL, "Forward", SE_ACTION_FORWARD, KEILAND_MENU_ROLE_NONE, MENU_ALT, MENU_KEY_RIGHT },
	{ MENU_ACTION_ID(SE_ACTION_HOME), MENU_GO, KEILAND_MENU_ITEM_NORMAL, "Home", SE_ACTION_HOME, KEILAND_MENU_ROLE_NONE, MENU_CTRL_SHIFT, 'h' },
	{ MENU_LINE, MENU_GO, KEILAND_MENU_ITEM_SEPARATOR, "", 0U, KEILAND_MENU_ROLE_NONE, 0U, 0U },
	{ MENU_GROUP_FIRST + SE_GROUP_CONNECTIVITY, MENU_GO, KEILAND_MENU_ITEM_SUBMENU, "Connectivity", 0U, KEILAND_MENU_ROLE_NONE, 0U, 0U },
	{ MENU_GROUP_FIRST + SE_GROUP_PERSONALIZATION, MENU_GO, KEILAND_MENU_ITEM_SUBMENU, "Personalization", 0U, KEILAND_MENU_ROLE_NONE, 0U, 0U },
	{ MENU_GROUP_FIRST + SE_GROUP_DEVICES, MENU_GO, KEILAND_MENU_ITEM_SUBMENU, "Devices", 0U, KEILAND_MENU_ROLE_NONE, 0U, 0U },
	{ MENU_GROUP_FIRST + SE_GROUP_SYSTEM, MENU_GO, KEILAND_MENU_ITEM_SUBMENU, "System", 0U, KEILAND_MENU_ROLE_NONE, 0U, 0U },
	{ MENU_WINDOW, KEILAND_MENU_ROOT, KEILAND_MENU_ITEM_SUBMENU, "Window", 0U, KEILAND_MENU_ROLE_NONE, 0U, 0U },
	{ MENU_ACTION_ID(SE_ACTION_MINIMIZE), MENU_WINDOW, KEILAND_MENU_ITEM_NORMAL, "Minimize", SE_ACTION_MINIMIZE, KEILAND_MENU_ROLE_NONE, MENU_CTRL, 'm' },
	{ MENU_ACTION_ID(SE_ACTION_ZOOM), MENU_WINDOW, KEILAND_MENU_ITEM_NORMAL, "Zoom", SE_ACTION_ZOOM, KEILAND_MENU_ROLE_NONE, 0U, 0U },
	{ MENU_HELP, KEILAND_MENU_ROOT, KEILAND_MENU_ITEM_SUBMENU, "Help", 0U, KEILAND_MENU_ROLE_NONE, 0U, 0U },
	{ MENU_ACTION_ID(SE_ACTION_ABOUT), MENU_HELP, KEILAND_MENU_ITEM_NORMAL, "About Kei", SE_ACTION_ABOUT, KEILAND_MENU_ROLE_ABOUT, 0U, 0U }
};

static void menu_activated(void *data, struct keiland_window_menu *window_menu, uint32_t item, uint32_t action, struct wl_seat *seat, uint32_t serial);
static int menu_build(struct se_menu *menu);
static int menu_add(struct se_menu *menu, const struct menu_item *item);
static int menu_state(struct se_menu *menu, const struct se_menu_state *state);
static int menu_state_items(struct keiland_menu *model, const struct se_menu_state *state);

/* What the window menu tells the window: only the choices. */
static const struct keiland_window_menu_listener menu_listener = {
	menu_activated, NULL, NULL
};

/*
 * Gives zdesktop the window's menus, showing a state.
 *
 * Returns 0, also when the compositor has no System Menu (the window then
 * has no menus), or an errno value when the menus could not be made.
 */
int
se_menu_open(
	struct se_menu *menu,
	struct se_window *window,
	const struct se_menu_state *state)
{
	int error;

	/* Nothing yet but the window the choices go to. */
	memset(menu, 0, sizeof(*menu));
	menu->window = window;

	/* The connection's menu service; a compositor without one leaves the window without menus. */
	menu->service = keiland_menu_service_open(window->display);
	if (menu->service == NULL) {
		se_log("MENU none errno=%d", errno);
		return 0;
	}

	/* The menu, empty until it is built. */
	menu->menu = keiland_menu_create(menu->service);
	if (menu->menu == NULL)
		return errno;

	/* The window's place for a menu, which tells the window what is chosen. */
	menu->window_menu = keiland_window_menu_create(menu->service, window->toplevel, &menu_listener, menu);
	if (menu->window_menu == NULL)
		return errno;

	/* The items in one transaction. */
	error = menu_build(menu);
	if (error != 0)
		return error;

	/* The window shows the menu from now on. */
	error = keiland_window_menu_set(menu->window_menu, menu->menu);
	if (error != 0)
		return error;

	/* The state it shows. */
	error = menu_state(menu, state);
	if (error != 0)
		return error;

	/* Succeeded: the menus are zdesktop's to show. */
	se_log("MENU ready items=%u", (unsigned)(sizeof(menu_items) / sizeof(menu_items[0])));
	return 0;
}

/*
 * Tells the menus the window's state when it differs from what they show.
 */
void
se_menu_refresh(
	struct se_menu *menu,
	const struct se_menu_state *state)
{
	int same;
	int error;

	/* Without menus nothing is sent. */
	if (menu->menu == NULL)
		return;

	/* Nor when the state is the one the menus show. */
	same = memcmp(state, &menu->shown, sizeof(*state));
	if (menu->sent != 0 && same == 0)
		return;

	/* The new state in one transaction; a refusal is reported and the menus stay as they were. */
	error = menu_state(menu, state);
	if (error != 0)
		se_log("MENU update-failed errno=%d", error);
}

/*
 * Takes the menus away from zdesktop (before the window goes).
 */
void
se_menu_close(
	struct se_menu *menu)
{
	/* The window's place, the menu, then the service. */
	if (menu->window_menu != NULL)
		keiland_window_menu_destroy(menu->window_menu);
	if (menu->menu != NULL)
		keiland_menu_destroy(menu->menu);
	if (menu->service != NULL)
		keiland_menu_service_close(menu->service);

	/* Nothing is left. */
	memset(menu, 0, sizeof(*menu));
}

/* Queues a chosen action among the window's inputs. */
static void
menu_activated(
	void *data,
	struct keiland_window_menu *window_menu,
	uint32_t item,
	uint32_t action,
	struct wl_seat *seat,
	uint32_t serial)
{
	struct se_menu *menu;

	/* The window's menus. */
	(void)window_menu;
	(void)seat;
	menu = data;

	/* The log line the tests read, then the action after the inputs that came before it. */
	se_log("MENU item=%u action=%u serial=%u", item, action, serial);
	se_window_action(menu->window, action);
}

/* Gives zdesktop every item in one transaction: the fixed ones, then each page under its group. */
static int
menu_build(
	struct se_menu *menu)
{
	struct menu_item item;
	size_t index;
	unsigned id;
	int error;

	/* The transaction. */
	error = keiland_menu_begin(menu->menu);
	if (error != 0)
		return error;

	/* The fixed items in their order. */
	for (index = 0; index < sizeof(menu_items) / sizeof(menu_items[0]); index++) {
		error = menu_add(menu, &menu_items[index]);
		if (error != 0) {
			(void)keiland_menu_commit(menu->menu);
			return error;
		}
	}

	/* Each page after Home, a radio item under its group (the page shown is checked). */
	memset(&item, 0, sizeof(item));
	for (id = SE_PAGE_HOME + 1; id < SE_PAGES; id++) {
		item.id = MENU_ACTION_ID(SE_ACTION_PAGE_FIRST + id);
		item.parent = MENU_GROUP_FIRST + se_pages[id].group;
		item.type = KEILAND_MENU_ITEM_RADIO;
		item.label = se_pages[id].name;
		item.action = SE_ACTION_PAGE_FIRST + id;
		error = menu_add(menu, &item);
		if (error != 0) {
			(void)keiland_menu_commit(menu->menu);
			return error;
		}
	}

	/* The items are shown together. */
	error = keiland_menu_commit(menu->menu);
	if (error != 0)
		return error;

	/* Succeeded: the menus are built. */
	return 0;
}

/* Adds one item with its role and shortcut; returns 0 or an errno value. */
static int
menu_add(
	struct se_menu *menu,
	const struct menu_item *item)
{
	int error;

	/* The item. */
	error = keiland_menu_append(menu->menu, item->id, item->parent, item->type, item->label, item->action);
	if (error != 0)
		return error;

	/* Its role, when it has one. */
	if (item->role != KEILAND_MENU_ROLE_NONE) {
		error = keiland_menu_set_role(menu->menu, item->id, item->role);
		if (error != 0)
			return error;
	}

	/* Its shortcut, when it has one. */
	if (item->keysym != 0U) {
		error = keiland_menu_set_shortcut(menu->menu, item->id, item->modifiers, item->keysym);
		if (error != 0)
			return error;
	}

	/* Succeeded: the item is in the menu. */
	return 0;
}

/* Shows a state in the menus in one transaction; returns 0 or an errno value. */
static int
menu_state(
	struct se_menu *menu,
	const struct se_menu_state *state)
{
	int error;

	/* The transaction. */
	error = keiland_menu_begin(menu->menu);
	if (error != 0)
		return error;

	/* The items' state. */
	error = menu_state_items(menu->menu, state);

	/*
	 * A refused change still ends the transaction, so that the menu is not
	 * left open for changes; the refusal is reported.
	 */
	if (error != 0) {
		(void)keiland_menu_commit(menu->menu);
		return error;
	}

	/* The state is shown together. */
	error = keiland_menu_commit(menu->menu);
	if (error != 0)
		return error;

	/* Succeeded: the menus show the state. */
	menu->shown = *state;
	menu->sent = 1;
	se_log("MENU state back=%d forward=%d sidebar=%d page=%s", state->can_back, state->can_forward, state->sidebar, se_pages[state->page].word);
	return 0;
}

/* Sets which items do something now and which are checked; returns 0 or the first refusal. */
static int
menu_state_items(
	struct keiland_menu *model,
	const struct se_menu_state *state)
{
	unsigned id;
	int error;

	/* The history's steps, and the list of pages. */
	error = keiland_menu_set_enabled(model, MENU_ACTION_ID(SE_ACTION_BACK), state->can_back);
	if (error == 0)
		error = keiland_menu_set_enabled(model, MENU_ACTION_ID(SE_ACTION_FORWARD), state->can_forward);
	if (error == 0)
		error = keiland_menu_set_checked(model, MENU_ACTION_ID(SE_ACTION_SHOW_SIDEBAR), state->sidebar);

	/* The page shown, checked among the pages. */
	for (id = SE_PAGE_HOME + 1; error == 0 && id < SE_PAGES; id++)
		error = keiland_menu_set_checked(model, MENU_ACTION_ID(SE_ACTION_PAGE_FIRST + id), state->page == id);

	/* Reports the first refusal, or none. */
	if (error != 0)
		return error;

	/* Succeeded: the items show the state. */
	return 0;
}
