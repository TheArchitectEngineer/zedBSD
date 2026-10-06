/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The menus of Image Viewer in zdesktop's System Menu: File (Open, Close,
 * Quit), View (the fit, 100 %, the zoom, the turns, the full screen), Go
 * (the images of the folder) and Help; and the context menu of a right
 * press or a long press on the image.  zdesktop draws them and chooses an
 * item for its shortcut; the choice comes back as an action queued among
 * the window's inputs.  A compositor without the System Menu leaves the
 * viewer without menus, and the keys work as they do with them.
 */

#include "window.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

/* The submenus. */
#define MENU_FILE		1U
#define MENU_VIEW		2U
#define MENU_GO			3U
#define MENU_HELP		4U

/* The items of File. */
#define MENU_OPEN		10U
#define MENU_FILE_LINE		11U
#define MENU_CLOSE		12U
#define MENU_QUIT		13U
#define MENU_OPEN_WITH		14U
#define MENU_TRASH		15U
#define MENU_TRASH_LINE		16U

/*
 * The items of File > Open With (ws128-p005): one slot for each
 * application, the first's ID and each further one's the next, labelled
 * and shown for the image shown.
 */
#define MENU_OPENER_FIRST	60U

/* The items of View. */
#define MENU_FIT		20U
#define MENU_ACTUAL		21U
#define MENU_ZOOM_IN		22U
#define MENU_ZOOM_OUT		23U
#define MENU_VIEW_LINE		24U
#define MENU_ROTATE_RIGHT	25U
#define MENU_ROTATE_LEFT	26U
#define MENU_TURN_LINE		27U
#define MENU_PLAY		28U
#define MENU_FULLSCREEN		29U
#define MENU_SLIDESHOW		35U

/* The items of Go. */
#define MENU_PREVIOUS		30U
#define MENU_NEXT		31U
#define MENU_GO_LINE		32U
#define MENU_FIRST		33U
#define MENU_LAST		34U

/* The items of Help. */
#define MENU_ABOUT		40U

/* The items of the context menu (a menu of its own). */
#define CONTEXT_FIT		50U
#define CONTEXT_ACTUAL		51U
#define CONTEXT_ROTATE_RIGHT	52U
#define CONTEXT_ROTATE_LEFT	53U
#define CONTEXT_FULLSCREEN	54U
#define CONTEXT_LINE		55U
#define CONTEXT_OPEN		56U
#define CONTEXT_TRASH		57U
#define CONTEXT_TRASH_LINE	58U

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
	{ MENU_OPEN, MENU_FILE, KL_MENU_ITEM_NORMAL, "Open...", IV_ACTION_OPEN, KL_MENU_ROLE_OPEN, KL_MENU_CTRL, 'o' },
	{ MENU_OPEN_WITH, MENU_FILE, KL_MENU_ITEM_SUBMENU, "Open With", 0U, KL_MENU_ROLE_NONE, 0U, 0U },
	{ MENU_OPENER_FIRST, MENU_OPEN_WITH, KL_MENU_ITEM_NORMAL, "-", IV_ACTION_OPEN_WITH_FIRST, KL_MENU_ROLE_NONE, 0U, 0U },
	{ MENU_OPENER_FIRST + 1U, MENU_OPEN_WITH, KL_MENU_ITEM_NORMAL, "-", IV_ACTION_OPEN_WITH_FIRST + 1U, KL_MENU_ROLE_NONE, 0U, 0U },
	{ MENU_OPENER_FIRST + 2U, MENU_OPEN_WITH, KL_MENU_ITEM_NORMAL, "-", IV_ACTION_OPEN_WITH_FIRST + 2U, KL_MENU_ROLE_NONE, 0U, 0U },
	{ MENU_OPENER_FIRST + 3U, MENU_OPEN_WITH, KL_MENU_ITEM_NORMAL, "-", IV_ACTION_OPEN_WITH_FIRST + 3U, KL_MENU_ROLE_NONE, 0U, 0U },
	{ MENU_OPENER_FIRST + 4U, MENU_OPEN_WITH, KL_MENU_ITEM_NORMAL, "-", IV_ACTION_OPEN_WITH_FIRST + 4U, KL_MENU_ROLE_NONE, 0U, 0U },
	{ MENU_OPENER_FIRST + 5U, MENU_OPEN_WITH, KL_MENU_ITEM_NORMAL, "-", IV_ACTION_OPEN_WITH_FIRST + 5U, KL_MENU_ROLE_NONE, 0U, 0U },
	{ MENU_OPENER_FIRST + 6U, MENU_OPEN_WITH, KL_MENU_ITEM_NORMAL, "-", IV_ACTION_OPEN_WITH_FIRST + 6U, KL_MENU_ROLE_NONE, 0U, 0U },
	{ MENU_OPENER_FIRST + 7U, MENU_OPEN_WITH, KL_MENU_ITEM_NORMAL, "-", IV_ACTION_OPEN_WITH_FIRST + 7U, KL_MENU_ROLE_NONE, 0U, 0U },
	{ MENU_FILE_LINE, MENU_FILE, KL_MENU_ITEM_SEPARATOR, "", 0U, KL_MENU_ROLE_NONE, 0U, 0U },
	{ MENU_TRASH, MENU_FILE, KL_MENU_ITEM_NORMAL, "Move to Trash", IV_ACTION_TRASH, KL_MENU_ROLE_NONE, 0U, 0U },
	{ MENU_TRASH_LINE, MENU_FILE, KL_MENU_ITEM_SEPARATOR, "", 0U, KL_MENU_ROLE_NONE, 0U, 0U },
	{ MENU_CLOSE, MENU_FILE, KL_MENU_ITEM_NORMAL, "Close", IV_ACTION_CLOSE, KL_MENU_ROLE_CLOSE, KL_MENU_CTRL, 'w' },
	{ MENU_QUIT, MENU_FILE, KL_MENU_ITEM_NORMAL, "Quit Image Viewer", IV_ACTION_QUIT, KL_MENU_ROLE_QUIT, KL_MENU_CTRL, 'q' },
	{ MENU_VIEW, KL_MENU_ROOT, KL_MENU_ITEM_SUBMENU, "View", 0U, KL_MENU_ROLE_NONE, 0U, 0U },
	{ MENU_FIT, MENU_VIEW, KL_MENU_ITEM_CHECKBOX, "Fit to Window", IV_ACTION_FIT, KL_MENU_ROLE_NONE, 0U, 0U },
	{ MENU_ACTUAL, MENU_VIEW, KL_MENU_ITEM_NORMAL, "Actual Size", IV_ACTION_ACTUAL, KL_MENU_ROLE_NONE, 0U, 0U },
	{ MENU_ZOOM_IN, MENU_VIEW, KL_MENU_ITEM_NORMAL, "Zoom In", IV_ACTION_ZOOM_IN, KL_MENU_ROLE_ZOOM_IN, 0U, 0U },
	{ MENU_ZOOM_OUT, MENU_VIEW, KL_MENU_ITEM_NORMAL, "Zoom Out", IV_ACTION_ZOOM_OUT, KL_MENU_ROLE_ZOOM_OUT, 0U, 0U },
	{ MENU_VIEW_LINE, MENU_VIEW, KL_MENU_ITEM_SEPARATOR, "", 0U, KL_MENU_ROLE_NONE, 0U, 0U },
	{ MENU_ROTATE_RIGHT, MENU_VIEW, KL_MENU_ITEM_NORMAL, "Rotate Right", IV_ACTION_ROTATE_RIGHT, KL_MENU_ROLE_NONE, 0U, 0U },
	{ MENU_ROTATE_LEFT, MENU_VIEW, KL_MENU_ITEM_NORMAL, "Rotate Left", IV_ACTION_ROTATE_LEFT, KL_MENU_ROLE_NONE, 0U, 0U },
	{ MENU_TURN_LINE, MENU_VIEW, KL_MENU_ITEM_SEPARATOR, "", 0U, KL_MENU_ROLE_NONE, 0U, 0U },
	{ MENU_PLAY, MENU_VIEW, KL_MENU_ITEM_CHECKBOX, "Play Animation", IV_ACTION_PLAY, KL_MENU_ROLE_NONE, 0U, 0U },
	{ MENU_FULLSCREEN, MENU_VIEW, KL_MENU_ITEM_CHECKBOX, "Full Screen", IV_ACTION_FULLSCREEN, KL_MENU_ROLE_FULLSCREEN, 0U, 0U },
	{ MENU_SLIDESHOW, MENU_VIEW, KL_MENU_ITEM_CHECKBOX, "Slideshow", IV_ACTION_SLIDESHOW, KL_MENU_ROLE_NONE, 0U, 0U },
	{ MENU_GO, KL_MENU_ROOT, KL_MENU_ITEM_SUBMENU, "Go", 0U, KL_MENU_ROLE_NONE, 0U, 0U },
	{ MENU_PREVIOUS, MENU_GO, KL_MENU_ITEM_NORMAL, "Previous Image", IV_ACTION_PREVIOUS, KL_MENU_ROLE_NONE, 0U, 0U },
	{ MENU_NEXT, MENU_GO, KL_MENU_ITEM_NORMAL, "Next Image", IV_ACTION_NEXT, KL_MENU_ROLE_NONE, 0U, 0U },
	{ MENU_GO_LINE, MENU_GO, KL_MENU_ITEM_SEPARATOR, "", 0U, KL_MENU_ROLE_NONE, 0U, 0U },
	{ MENU_FIRST, MENU_GO, KL_MENU_ITEM_NORMAL, "First Image", IV_ACTION_FIRST, KL_MENU_ROLE_NONE, 0U, 0U },
	{ MENU_LAST, MENU_GO, KL_MENU_ITEM_NORMAL, "Last Image", IV_ACTION_LAST, KL_MENU_ROLE_NONE, 0U, 0U },
	{ MENU_HELP, KL_MENU_ROOT, KL_MENU_ITEM_SUBMENU, "Help", 0U, KL_MENU_ROLE_NONE, 0U, 0U },
	{ MENU_ABOUT, MENU_HELP, KL_MENU_ITEM_NORMAL, "About Image Viewer", IV_ACTION_ABOUT, KL_MENU_ROLE_ABOUT, 0U, 0U }
};

/* The context menu's items, all top-level. */
static const struct menu_item context_items[] = {
	{ CONTEXT_FIT, KL_MENU_ROOT, KL_MENU_ITEM_NORMAL, "Fit to Window", IV_ACTION_FIT, KL_MENU_ROLE_NONE, 0U, 0U },
	{ CONTEXT_ACTUAL, KL_MENU_ROOT, KL_MENU_ITEM_NORMAL, "Actual Size", IV_ACTION_ACTUAL, KL_MENU_ROLE_NONE, 0U, 0U },
	{ CONTEXT_ROTATE_RIGHT, KL_MENU_ROOT, KL_MENU_ITEM_NORMAL, "Rotate Right", IV_ACTION_ROTATE_RIGHT, KL_MENU_ROLE_NONE, 0U, 0U },
	{ CONTEXT_ROTATE_LEFT, KL_MENU_ROOT, KL_MENU_ITEM_NORMAL, "Rotate Left", IV_ACTION_ROTATE_LEFT, KL_MENU_ROLE_NONE, 0U, 0U },
	{ CONTEXT_FULLSCREEN, KL_MENU_ROOT, KL_MENU_ITEM_NORMAL, "Full Screen", IV_ACTION_FULLSCREEN, KL_MENU_ROLE_NONE, 0U, 0U },
	{ CONTEXT_LINE, KL_MENU_ROOT, KL_MENU_ITEM_SEPARATOR, "", 0U, KL_MENU_ROLE_NONE, 0U, 0U },
	{ CONTEXT_OPEN, KL_MENU_ROOT, KL_MENU_ITEM_NORMAL, "Open...", IV_ACTION_OPEN, KL_MENU_ROLE_NONE, 0U, 0U },
	{ CONTEXT_TRASH_LINE, KL_MENU_ROOT, KL_MENU_ITEM_SEPARATOR, "", 0U, KL_MENU_ROLE_NONE, 0U, 0U },
	{ CONTEXT_TRASH, KL_MENU_ROOT, KL_MENU_ITEM_NORMAL, "Move to Trash", IV_ACTION_TRASH, KL_MENU_ROLE_NONE, 0U, 0U }
};

static void menu_activated(void *data, struct kl_window_menu *window_menu, uint32_t item, uint32_t action, struct wl_seat *seat, uint32_t serial);
static void menu_context_activated(void *data, struct kl_context_menu *context_menu, uint32_t item, uint32_t action, uint32_t serial);
static void menu_context_done(void *data, struct kl_context_menu *context_menu);
static int menu_build(struct kl_menu *model, const struct menu_item *items, size_t count);
static int menu_state(struct iv_menu *menu, const struct iv_state *state);
static int menu_state_items(struct kl_menu *model, const struct iv_state *state);

/* What the window menu tells the viewer: only the choices. */
static const struct kl_window_menu_listener menu_listener = {
	menu_activated,
	NULL,
	NULL
};

/* What a context menu tells the viewer: the choice, and that it closed. */
static const struct kl_context_menu_listener menu_context_listener = {
	menu_context_activated,
	menu_context_done
};

/*
 * Gives zdesktop the window's menus, showing a state, and makes the
 * context menu's model.
 *
 * Returns 0, also when the compositor has no System Menu (the viewer then
 * has no menus), or an errno value when the menus could not be made.
 */
int
iv_menu_open(
	struct iv_menu *menu,
	struct iv_window *window,
	const struct iv_state *state)
{
	int error;

	/* Nothing yet but the window; Open With's slots are not set yet. */
	memset(menu, 0, sizeof(*menu));
	menu->window = window;
	menu->opener_count = -1;

	/* The connection's menu service; a compositor without one leaves the viewer without menus. */
	menu->service = kl_menu_service_open(kl_window_display(window->kui));
	if (menu->service == NULL) {
		iv_log("MENU none errno=%d", errno);
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
	error = menu_build(menu->menu, menu_items, sizeof(menu_items) / sizeof(menu_items[0]));
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

	/* The context menu's model. */
	menu->context = kl_menu_create(menu->service);
	if (menu->context == NULL)
		return errno;

	/* Its items, built once. */
	error = menu_build(menu->context, context_items, sizeof(context_items) / sizeof(context_items[0]));
	if (error != 0)
		return error;

	/* Logs the menus for the tests. */
	iv_log("MENU ready items=%u", (unsigned)(sizeof(menu_items) / sizeof(menu_items[0])));

	/* Succeeded: the menus are zdesktop's to show. */
	return 0;
}

/*
 * Tells the menus the viewer's state when it differs from what they show,
 * and lets go of a context menu zdesktop closed.
 */
void
iv_menu_refresh(
	struct iv_menu *menu,
	const struct iv_state *state)
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
		iv_log("MENU update-failed errno=%d", error);
}

/*
 * Opens the context menu at a point of the window, for the press whose
 * serial the window kept (the right button's, or a long press's finger).
 */
void
iv_menu_context(
	struct iv_menu *menu,
	const struct iv_state *state,
	int x,
	int y)
{
	/* Without menus, or without an image, nothing opens. */
	if (menu->service == NULL ||
	    menu->context == NULL ||
	    !state->has_image)
		return;

	/* A context menu still open is replaced. */
	if (menu->popup != NULL) {
		kl_context_menu_destroy(menu->popup);
		menu->popup = NULL;
	}

	/* zdesktop shows it at the press. */
	menu->popup = kl_menu_popup(menu->service,
					 menu->context,
					 kl_window_surface(menu->window->kui),
					 x,
					 y,
					 kl_window_seat(menu->window->kui),
					 kl_window_press_serial(menu->window->kui),
					 &menu_context_listener,
					 menu);
	if (menu->popup == NULL) {
		iv_log("CONTEXT-MENU none errno=%d", errno);
		return;
	}

	/* The log line the tests read. */
	iv_log("CONTEXT-MENU open x=%d y=%d serial=%u", x, y, kl_window_press_serial(menu->window->kui));
}

/*
 * Shows the applications of File > Open With for the image shown: each
 * slot takes an application's name and is shown, the slots left over are
 * hidden (ws128-p005).  The same names again send nothing.
 */
void
iv_menu_openers(
	struct iv_menu *menu,
	char names[][IV_OPENER_NAME],
	int count)
{
	const char *first;
	int index;
	int same;
	int match;
	int error;

	/* Without menus nothing is sent. */
	if (menu->menu == NULL)
		return;

	/* The same number of names as the menu shows, each the same, sends nothing. */
	same = 0;
	if (count == menu->opener_count)
		same = 1;
	for (index = 0; same != 0 && index < count; index++) {
		match = strcmp(names[index], menu->openers[index]);
		if (match != 0)
			same = 0;
	}

	/* Nothing changed. */
	if (same != 0)
		return;

	/* The slots in one transaction. */
	error = kl_menu_begin(menu->menu);
	if (error != 0) {
		iv_log("MENU openers-failed errno=%d", error);
		return;
	}

	/* Each slot: an application's name and shown, or hidden. */
	for (index = 0; index < IV_OPENERS && error == 0; index++) {
		if (index < count) {
			error = kl_menu_set_label(menu->menu, MENU_OPENER_FIRST + (uint32_t)index, names[index]);
			if (error == 0)
				error = kl_menu_set_visible(menu->menu, MENU_OPENER_FIRST + (uint32_t)index, 1);
		} else {
			error = kl_menu_set_visible(menu->menu, MENU_OPENER_FIRST + (uint32_t)index, 0);
		}
	}

	/* The slots are shown together; a refusal is logged and they stay as they were. */
	(void)kl_menu_commit(menu->menu);
	if (error != 0) {
		iv_log("MENU openers-failed errno=%d", error);
		return;
	}

	/* The menu shows these names from now on. */
	for (index = 0; index < count; index++)
		snprintf(menu->openers[index], IV_OPENER_NAME, "%s", names[index]);
	menu->opener_count = count;

	/* The log line the tests read: how many, and the first (the default). */
	first = "-";
	if (count > 0)
		first = names[0];
	iv_log("MENU openers count=%d first=%s", count, first);
}

/*
 * Takes the menus away from zdesktop (before the window goes).
 */
void
iv_menu_close(
	struct iv_menu *menu)
{
	/* A context menu, its model, the window's place, the menu, then the service. */
	if (menu->popup != NULL)
		kl_context_menu_destroy(menu->popup);
	if (menu->context != NULL)
		kl_menu_destroy(menu->context);
	if (menu->window_menu != NULL)
		kl_window_menu_destroy(menu->window_menu);
	if (menu->menu != NULL)
		kl_menu_destroy(menu->menu);
	if (menu->service != NULL)
		kl_menu_service_close(menu->service);

	/* Nothing of the menus is left. */
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
	struct iv_menu *menu;

	UNUSED_PARAMETER(window_menu);
	UNUSED_PARAMETER(seat);

	/* The menus whose item was chosen. */
	menu = data;

	/* The log line the tests read, and the action. */
	iv_log("MENU item=%u action=%u serial=%u", item, action, serial);
	iv_window_action(menu->window, action);
}

/* Queues a context menu's chosen action among the window's inputs. */
static void
menu_context_activated(
	void *data,
	struct kl_context_menu *context_menu,
	uint32_t item,
	uint32_t action,
	uint32_t serial)
{
	struct iv_menu *menu;

	UNUSED_PARAMETER(context_menu);

	/* The menus whose context menu was chosen from. */
	menu = data;

	/* The log line the tests read, and the action. */
	iv_log("CONTEXT-MENU item=%u action=%u serial=%u", item, action, serial);
	iv_window_action(menu->window, action);
}

/* A context menu closed: it is destroyed (told last, once). */
static void
menu_context_done(
	void *data,
	struct kl_context_menu *context_menu)
{
	struct iv_menu *menu;

	/* The menus the context menu belonged to. */
	menu = data;

	/* The one open is the one that closed. */
	if (menu->popup == context_menu) {
		kl_context_menu_destroy(menu->popup);
		menu->popup = NULL;
	}

	/* The log line the tests read. */
	iv_log("CONTEXT-MENU done");
}

/* Gives zdesktop a menu's items, with their roles and shortcuts, in one transaction. */
static int
menu_build(
	struct kl_menu *model,
	const struct menu_item *items,
	size_t count)
{
	const struct menu_item *item;
	size_t index;
	int error;

	/* The transaction. */
	error = kl_menu_begin(model);
	if (error != 0)
		return error;

	/* Each item in its order under its parent. */
	for (index = 0; index < count; index++) {
		/* The item under its parent. */
		item = &items[index];
		error = kl_menu_append(model, item->id, item->parent, item->type, item->label, item->action);
		if (error != 0)
			return error;

		/* Its role, when it has one. */
		if (item->role != KL_MENU_ROLE_NONE) {
			error = kl_menu_set_role(model, item->id, item->role);
			if (error != 0)
				return error;
		}

		/* Its shortcut, when it has one. */
		if (item->keysym != 0U) {
			error = kl_menu_set_shortcut(model, item->id, item->modifiers, item->keysym);
			if (error != 0)
				return error;
		}
	}

	/* The items are shown together. */
	error = kl_menu_commit(model);
	if (error != 0)
		return error;

	/* Succeeded: the menu is built. */
	return 0;
}

/*
 * Shows a state in the menus in one transaction: the image's items
 * enabled while one can be shown, the folder's by where it is, the fit,
 * the animation and the full screen checked.
 */
static int
menu_state(
	struct iv_menu *menu,
	const struct iv_state *state)
{
	struct kl_menu *model;
	int error;

	/* The transaction. */
	model = menu->menu;
	error = kl_menu_begin(model);
	if (error != 0)
		return error;

	/* The items' marks; a refused change still ends the transaction, and is reported. */
	error = menu_state_items(model, state);
	if (error != 0) {
		(void)kl_menu_commit(model);
		return error;
	}

	/* The state is shown together. */
	error = kl_menu_commit(model);
	if (error != 0)
		return error;

	/* The menus show this state from now on. */
	menu->shown = *state;

	/* Succeeded: the menus show the state. */
	return 0;
}

/* Sets the items' enabled and checked marks for a state inside an open transaction; stops at the first refusal. */
static int
menu_state_items(
	struct kl_menu *model,
	const struct iv_state *state)
{
	int can_previous;
	int can_next;
	int can_go;
	int error;

	/* There is an image before the one shown. */
	can_previous = 0;
	if (state->has_image && state->index > 0)
		can_previous = 1;

	/* There is an image after it. */
	can_next = 0;
	if (state->has_image && state->index + 1 < state->count)
		can_next = 1;

	/* The folder has other images to go to. */
	can_go = 0;
	if (state->has_image && state->count > 1)
		can_go = 1;

	/* The fit needs an image that can be shown. */
	error = kl_menu_set_enabled(model, MENU_FIT, state->can_show);
	if (error != 0)
		return error;

	/* So does the actual size. */
	error = kl_menu_set_enabled(model, MENU_ACTUAL, state->can_show);
	if (error != 0)
		return error;

	/* So does zooming in. */
	error = kl_menu_set_enabled(model, MENU_ZOOM_IN, state->can_show);
	if (error != 0)
		return error;

	/* So does zooming out. */
	error = kl_menu_set_enabled(model, MENU_ZOOM_OUT, state->can_show);
	if (error != 0)
		return error;

	/* So does a turn to the right. */
	error = kl_menu_set_enabled(model, MENU_ROTATE_RIGHT, state->can_show);
	if (error != 0)
		return error;

	/* So does a turn to the left. */
	error = kl_menu_set_enabled(model, MENU_ROTATE_LEFT, state->can_show);
	if (error != 0)
		return error;

	/* Only an animated image plays. */
	error = kl_menu_set_enabled(model, MENU_PLAY, state->animated);
	if (error != 0)
		return error;

	/* The previous image, when there is one. */
	error = kl_menu_set_enabled(model, MENU_PREVIOUS, can_previous);
	if (error != 0)
		return error;

	/* The next image, when there is one. */
	error = kl_menu_set_enabled(model, MENU_NEXT, can_next);
	if (error != 0)
		return error;

	/* The first image, in a folder of several. */
	error = kl_menu_set_enabled(model, MENU_FIRST, can_go);
	if (error != 0)
		return error;

	/* The last image, likewise. */
	error = kl_menu_set_enabled(model, MENU_LAST, can_go);
	if (error != 0)
		return error;

	/* The fit is checked while the image follows the window. */
	error = kl_menu_set_checked(model, MENU_FIT, state->fit);
	if (error != 0)
		return error;

	/* The animation is checked while it plays. */
	error = kl_menu_set_checked(model, MENU_PLAY, state->playing);
	if (error != 0)
		return error;

	/* The full screen is checked while the window fills it. */
	error = kl_menu_set_checked(model, MENU_FULLSCREEN, state->fullscreen);
	if (error != 0)
		return error;

	/* Open With and Move to Trash need an image (ws128-p005). */
	error = kl_menu_set_enabled(model, MENU_OPEN_WITH, state->has_image);
	if (error != 0)
		return error;

	/* Move to Trash likewise. */
	error = kl_menu_set_enabled(model, MENU_TRASH, state->has_image);
	if (error != 0)
		return error;

	/* A slideshow needs an image to start from. */
	error = kl_menu_set_enabled(model, MENU_SLIDESHOW, state->has_image);
	if (error != 0)
		return error;

	/* The slideshow is checked while it runs. */
	error = kl_menu_set_checked(model, MENU_SLIDESHOW, state->slideshow);
	if (error != 0)
		return error;

	/* Succeeded: every item shows the state. */
	return 0;
}
