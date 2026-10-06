/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The menus of PDF Viewer in zdesktop's System Menu: File (Open, Annotate
 * in Notes, Close, Quit), View (the sidebar of page thumbnails, the two
 * modes, the two fits, the zoom) and Go (the pages).  zdesktop draws them and chooses an item for its
 * shortcut; the choice comes back as an action queued among the window's
 * inputs.  A compositor without the System Menu leaves the viewer without
 * menus, and the keys work as they do with them.
 */

#include "window.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

/* The submenus. */
#define MENU_FILE		1U
#define MENU_VIEW		2U
#define MENU_GO			3U

/* The items of File. */
#define MENU_OPEN		10U
#define MENU_ANNOTATE		11U
#define MENU_FILE_LINE		12U
#define MENU_CLOSE		13U
#define MENU_QUIT		14U

/* The items of View. */
#define MENU_THUMBNAILS		19U
#define MENU_THUMBNAILS_LINE	29U
#define MENU_SCROLL		20U
#define MENU_PAGES		21U
#define MENU_VIEW_LINE		22U
#define MENU_FIT_WIDTH		23U
#define MENU_FIT_PAGE		24U
#define MENU_ZOOM_LINE		25U
#define MENU_ZOOM_IN		26U
#define MENU_ZOOM_OUT		27U
#define MENU_ZOOM_RESET		28U

/* The items of Go. */
#define MENU_PREVIOUS		30U
#define MENU_NEXT		31U
#define MENU_GO_LINE		32U
#define MENU_FIRST		33U
#define MENU_LAST		34U

/* The keysyms of the shortcuts' keys that are not letters. */
#define MENU_KEY_PLUS		0x2bU
#define MENU_KEY_MINUS		0x2dU
#define MENU_KEY_ZERO		0x30U

/*
 * One item of the menus as the viewer builds them: its ID, its parent,
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

/* The menus, in the order they are shown. */
static const struct menu_item menu_items[] = {
	{ MENU_FILE, KL_MENU_ROOT, KL_MENU_ITEM_SUBMENU, "File", 0U, KL_MENU_ROLE_NONE, 0U, 0U },
	{ MENU_OPEN, MENU_FILE, KL_MENU_ITEM_NORMAL, "Open...", PV_ACTION_OPEN, KL_MENU_ROLE_OPEN, KL_MENU_CTRL, 'o' },
	{ MENU_ANNOTATE, MENU_FILE, KL_MENU_ITEM_NORMAL, "Annotate in Notes", PV_ACTION_ANNOTATE, KL_MENU_ROLE_NONE, KL_MENU_CTRL, 'e' },
	{ MENU_FILE_LINE, MENU_FILE, KL_MENU_ITEM_SEPARATOR, "", 0U, KL_MENU_ROLE_NONE, 0U, 0U },
	{ MENU_CLOSE, MENU_FILE, KL_MENU_ITEM_NORMAL, "Close", PV_ACTION_CLOSE, KL_MENU_ROLE_CLOSE, KL_MENU_CTRL, 'w' },
	{ MENU_QUIT, MENU_FILE, KL_MENU_ITEM_NORMAL, "Quit PDF Viewer", PV_ACTION_QUIT, KL_MENU_ROLE_QUIT, KL_MENU_CTRL, 'q' },
	{ MENU_VIEW, KL_MENU_ROOT, KL_MENU_ITEM_SUBMENU, "View", 0U, KL_MENU_ROLE_NONE, 0U, 0U },
	{ MENU_THUMBNAILS, MENU_VIEW, KL_MENU_ITEM_CHECKBOX, "Page Thumbnails", PV_ACTION_THUMBNAILS, KL_MENU_ROLE_NONE, 0U, 0U },
	{ MENU_THUMBNAILS_LINE, MENU_VIEW, KL_MENU_ITEM_SEPARATOR, "", 0U, KL_MENU_ROLE_NONE, 0U, 0U },
	{ MENU_SCROLL, MENU_VIEW, KL_MENU_ITEM_RADIO, "Continuous Scroll", PV_ACTION_MODE_SCROLL, KL_MENU_ROLE_NONE, 0U, 0U },
	{ MENU_PAGES, MENU_VIEW, KL_MENU_ITEM_RADIO, "Single Page", PV_ACTION_MODE_PAGE, KL_MENU_ROLE_NONE, 0U, 0U },
	{ MENU_VIEW_LINE, MENU_VIEW, KL_MENU_ITEM_SEPARATOR, "", 0U, KL_MENU_ROLE_NONE, 0U, 0U },
	{ MENU_FIT_WIDTH, MENU_VIEW, KL_MENU_ITEM_RADIO, "Fit Width", PV_ACTION_FIT_WIDTH, KL_MENU_ROLE_NONE, 0U, 0U },
	{ MENU_FIT_PAGE, MENU_VIEW, KL_MENU_ITEM_RADIO, "Fit Page", PV_ACTION_FIT_PAGE, KL_MENU_ROLE_NONE, 0U, 0U },
	{ MENU_ZOOM_LINE, MENU_VIEW, KL_MENU_ITEM_SEPARATOR, "", 0U, KL_MENU_ROLE_NONE, 0U, 0U },
	{ MENU_ZOOM_IN, MENU_VIEW, KL_MENU_ITEM_NORMAL, "Zoom In", PV_ACTION_ZOOM_IN, KL_MENU_ROLE_ZOOM_IN, KL_MENU_CTRL, MENU_KEY_PLUS },
	{ MENU_ZOOM_OUT, MENU_VIEW, KL_MENU_ITEM_NORMAL, "Zoom Out", PV_ACTION_ZOOM_OUT, KL_MENU_ROLE_ZOOM_OUT, KL_MENU_CTRL, MENU_KEY_MINUS },
	{ MENU_ZOOM_RESET, MENU_VIEW, KL_MENU_ITEM_NORMAL, "Reset Zoom", PV_ACTION_ZOOM_RESET, KL_MENU_ROLE_NONE, KL_MENU_CTRL, MENU_KEY_ZERO },
	{ MENU_GO, KL_MENU_ROOT, KL_MENU_ITEM_SUBMENU, "Go", 0U, KL_MENU_ROLE_NONE, 0U, 0U },
	{ MENU_PREVIOUS, MENU_GO, KL_MENU_ITEM_NORMAL, "Previous Page", PV_ACTION_PREVIOUS, KL_MENU_ROLE_NONE, 0U, 0U },
	{ MENU_NEXT, MENU_GO, KL_MENU_ITEM_NORMAL, "Next Page", PV_ACTION_NEXT, KL_MENU_ROLE_NONE, 0U, 0U },
	{ MENU_GO_LINE, MENU_GO, KL_MENU_ITEM_SEPARATOR, "", 0U, KL_MENU_ROLE_NONE, 0U, 0U },
	{ MENU_FIRST, MENU_GO, KL_MENU_ITEM_NORMAL, "First Page", PV_ACTION_FIRST, KL_MENU_ROLE_NONE, 0U, 0U },
	{ MENU_LAST, MENU_GO, KL_MENU_ITEM_NORMAL, "Last Page", PV_ACTION_LAST, KL_MENU_ROLE_NONE, 0U, 0U }
};

static void menu_activated(void *data, struct kl_window_menu *window_menu, uint32_t item, uint32_t action, struct wl_seat *seat, uint32_t serial);
static int menu_build(struct pv_menu *menu);
static int menu_state(struct pv_menu *menu, const struct pv_state *state);

/* What the window menu tells the viewer: only the choices. */
static const struct kl_window_menu_listener menu_listener = {
	menu_activated, NULL, NULL
};

/*
 * Gives zdesktop the window's menus, showing a state.
 *
 * Returns 0, also when the compositor has no System Menu (the viewer then
 * has no menus), or an errno value when the menus could not be made.
 */
int
pv_menu_open(
	struct pv_menu *menu,
	struct pv_window *window,
	const struct pv_state *state)
{
	int error;

	/* Nothing yet but the window. */
	memset(menu, 0, sizeof(*menu));
	menu->window = window;

	/* The connection's menu service; a compositor without one leaves the viewer without menus. */
	menu->service = kl_menu_service_open(kl_window_display(window->kui));
	if (menu->service == NULL) {
		pv_log("MENU none errno=%d", errno);
		return 0;
	}

	/* The menu, empty until it is built. */
	menu->menu = kl_menu_create(menu->service);
	if (menu->menu == NULL)
		return errno;

	/* The window's place for a menu, which tells the viewer what is chosen. */
	menu->window_menu = kl_window_menu_create(menu->service, kl_window_toplevel(window->kui), &menu_listener, menu);
	if (menu->window_menu == NULL)
		return errno;

	/* The items in one transaction. */
	error = menu_build(menu);
	if (error != 0)
		return error;

	/* The window shows the menu from now on. */
	error = kl_window_menu_set(menu->window_menu, menu->menu);
	if (error != 0)
		return error;

	/* The state it shows. */
	error = menu_state(menu, state);
	if (error != 0)
		return error;

	/* Succeeded: the menus are zdesktop's to show. */
	pv_log("MENU ready items=%u", (unsigned)(sizeof(menu_items) / sizeof(menu_items[0])));
	return 0;
}

/*
 * Tells the menus the viewer's state when it differs from what they show.
 */
void
pv_menu_refresh(
	struct pv_menu *menu,
	const struct pv_state *state)
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

	/* The new state; a refusal is logged and the menus stay as they were. */
	error = menu_state(menu, state);
	if (error != 0)
		pv_log("MENU update-failed errno=%d", error);
}

/*
 * Takes the menus away from zdesktop (before the window goes).
 */
void
pv_menu_close(
	struct pv_menu *menu)
{
	/* The window's place, the menu, then the service. */
	if (menu->window_menu != NULL)
		kl_window_menu_destroy(menu->window_menu);
	if (menu->menu != NULL)
		kl_menu_destroy(menu->menu);
	if (menu->service != NULL)
		kl_menu_service_close(menu->service);
	memset(menu, 0, sizeof(*menu));
}

/* Queues a chosen action among the window's inputs. */
static void
menu_activated(
	void *data,
	struct kl_window_menu *window_menu,
	uint32_t item,
	uint32_t action,
	struct wl_seat *seat,
	uint32_t serial)
{
	struct pv_menu *menu;

	/* The menus whose item was chosen. */
	(void)window_menu;
	(void)seat;
	menu = data;

	/* The log line the tests read, and the action. */
	pv_log("MENU item=%u action=%u serial=%u", item, action, serial);
	pv_window_action(menu->window, action);
}

/* Gives zdesktop every item, with its role and shortcut, in one transaction. */
static int
menu_build(
	struct pv_menu *menu)
{
	const struct menu_item *item;
	unsigned index;
	int error;

	/* The transaction. */
	error = kl_menu_begin(menu->menu);
	if (error != 0)
		return error;

	/* Each item in its order under its parent. */
	for (index = 0; index < sizeof(menu_items) / sizeof(menu_items[0]); index++) {
		item = &menu_items[index];
		error = kl_menu_append(menu->menu, item->id, item->parent, item->type, item->label, item->action);
		if (error != 0)
			return error;

		/* Its role, when it has one. */
		if (item->role != KL_MENU_ROLE_NONE) {
			error = kl_menu_set_role(menu->menu, item->id, item->role);
			if (error != 0)
				return error;
		}

		/* Its shortcut, when it has one. */
		if (item->keysym != 0U) {
			error = kl_menu_set_shortcut(menu->menu, item->id, item->modifiers, item->keysym);
			if (error != 0)
				return error;
		}
	}

	/* The items are shown together. */
	error = kl_menu_commit(menu->menu);
	if (error != 0)
		return error;

	/* Succeeded: the menus are built. */
	return 0;
}

/*
 * Shows a state in the menus in one transaction: the document's items
 * enabled while one is open, the mode's and the fit's radio items and the
 * thumbnails' check box checked.
 */
static int
menu_state(
	struct pv_menu *menu,
	const struct pv_state *state)
{
	struct kl_menu *model;
	int can_previous;
	int can_next;
	int scrolling;
	int paging;
	int fitting_width;
	int fitting_page;
	int error;

	/* What the state allows and what it has chosen, as the menus show them. */
	can_previous = 0;
	if (state->has_document && state->page > 0)
		can_previous = 1;
	can_next = 0;
	if (state->has_document && state->page + 1 < state->count)
		can_next = 1;
	scrolling = 0;
	if (state->mode == PV_MODE_SCROLL)
		scrolling = 1;
	paging = 0;
	if (state->mode == PV_MODE_PAGE)
		paging = 1;
	fitting_width = 0;
	if (state->fit == PV_FIT_WIDTH)
		fitting_width = 1;
	fitting_page = 0;
	if (state->fit == PV_FIT_PAGE)
		fitting_page = 1;

	/* The transaction. */
	model = menu->menu;
	error = kl_menu_begin(model);
	if (error != 0)
		return error;

	/* The items that need a document. */
	error = kl_menu_set_enabled(model, MENU_ANNOTATE, state->has_document);
	if (error == 0)
		error = kl_menu_set_enabled(model, MENU_PREVIOUS, can_previous);
	if (error == 0)
		error = kl_menu_set_enabled(model, MENU_NEXT, can_next);
	if (error == 0)
		error = kl_menu_set_enabled(model, MENU_FIRST, state->has_document);
	if (error == 0)
		error = kl_menu_set_enabled(model, MENU_LAST, state->has_document);
	if (error == 0)
		error = kl_menu_set_enabled(model, MENU_ZOOM_IN, state->has_document);
	if (error == 0)
		error = kl_menu_set_enabled(model, MENU_ZOOM_OUT, state->has_document);
	if (error == 0)
		error = kl_menu_set_enabled(model, MENU_THUMBNAILS, state->has_document);

	/* The mode and the fit in force. */
	if (error == 0)
		error = kl_menu_set_checked(model, MENU_SCROLL, scrolling);
	if (error == 0)
		error = kl_menu_set_checked(model, MENU_PAGES, paging);
	if (error == 0)
		error = kl_menu_set_checked(model, MENU_FIT_WIDTH, fitting_width);
	if (error == 0)
		error = kl_menu_set_checked(model, MENU_FIT_PAGE, fitting_page);
	if (error == 0)
		error = kl_menu_set_checked(model, MENU_THUMBNAILS, state->thumbnails);

	/* A refused change still ends the transaction, and is reported. */
	if (error != 0) {
		(void)kl_menu_commit(model);
		return error;
	}

	/* The state is shown together. */
	error = kl_menu_commit(model);
	if (error != 0)
		return error;

	/* Succeeded: the menus show the state. */
	menu->shown = *state;
	return 0;
}
