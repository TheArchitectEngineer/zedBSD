/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The desktop's library, with two jobs.
 *
 * It wraps zdesktop's non-standard Wayland (xdg) extensions: a client of the
 * desktop (zdesktop-x11server, an application) uses standard Wayland and
 * Vulkan, and reaches anything only zdesktop offers through this library,
 * never through a private protocol of its own.
 *
 * It is also the desktop's way into the operating system: zdesktop does not
 * talk to networkd, audiod or the other daemons itself.  Everything it needs
 * from the system, other than drawing through Vulkan and its windows through
 * Wayland, comes through this library.  A daemon's protocol or an extension
 * can then change in one place, and moving the desktop to another system
 * means rewriting this library and nothing else.
 *
 * Each feature adds its calls here when it arrives with its first user, so
 * that nothing is promised before it exists.  The first is the System Menu
 * (WS070): an application gives zdesktop the meaning of its menus -- a tree
 * of numbered items with labels, states, actions and shortcuts -- and
 * zdesktop draws them in the window's title bar, or in the system bar while
 * the window is docked, and tells the application what the user chose.
 */

#ifndef ZDESKTOP_H
#define ZDESKTOP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The interface version this header describes (2: the System Menu). */
#define ZDESKTOP_VERSION	2U

/*
 * Reports the interface version of the library that was loaded.
 *
 * A program built against this header may compare the result with
 * ZDESKTOP_VERSION to learn whether the library it runs with is older.
 */
unsigned zdesktop_version(void);

/*
 * The System Menu.
 *
 * A service is one connection's way to zdesktop's menus; a menu is one tree
 * of items; a window menu shows a menu on one xdg_toplevel.  One menu may be
 * shown on several windows (an application menu shared by its windows); the
 * choice comes back through the window menu it was made on.
 *
 * Items are named by numbers the application chooses (not 0; unique in
 * their menu).  ZDESKTOP_MENU_ROOT is the parent of the top-level items (in
 * a terminal: Shell, Edit, View, Session, Help); a submenu item is the
 * parent of the items under it.  Every change is made between
 * zdesktop_menu_begin and zdesktop_menu_commit, and zdesktop shows the
 * changes of one commit together.
 *
 * zdesktop owns the looks and the input.  A checkbox or radio item is not
 * checked by zdesktop when it is chosen; the application sets its state in
 * the next transaction.  A shortcut is shown in the menu and zdesktop
 * chooses the item when its keys are pressed in the focused window.
 *
 * Every call returns 0 or an errno value, and a refused call sends nothing:
 * EINVAL (a malformed argument, a change outside a transaction, a checked
 * state on an item that cannot be checked), EEXIST (an ID in use), ENOENT
 * (an ID that names no item), EBUSY (a transaction already open), E2BIG (a
 * menu or a label too large), ENOMEM.  The requests go out when the
 * application flushes its Wayland connection; the choices arrive when it
 * dispatches the queue its xdg_toplevel is on.
 */
struct wl_display;
struct wl_seat;
struct xdg_toplevel;
struct zdesktop_menu_service;
struct zdesktop_menu;
struct zdesktop_window_menu;

/* The parent of the top-level items. */
#define ZDESKTOP_MENU_ROOT		0U

/* The kinds of item. */
#define ZDESKTOP_MENU_ITEM_NORMAL	0U
#define ZDESKTOP_MENU_ITEM_SEPARATOR	1U
#define ZDESKTOP_MENU_ITEM_CHECKBOX	2U
#define ZDESKTOP_MENU_ITEM_RADIO	3U
#define ZDESKTOP_MENU_ITEM_SUBMENU	4U

/* What an item means to the system (zdesktop may give it an icon or a place of its own). */
#define ZDESKTOP_MENU_ROLE_NONE		0U
#define ZDESKTOP_MENU_ROLE_ABOUT	1U
#define ZDESKTOP_MENU_ROLE_PREFERENCES	2U
#define ZDESKTOP_MENU_ROLE_QUIT		3U
#define ZDESKTOP_MENU_ROLE_UNDO		4U
#define ZDESKTOP_MENU_ROLE_REDO		5U
#define ZDESKTOP_MENU_ROLE_CUT		6U
#define ZDESKTOP_MENU_ROLE_COPY		7U
#define ZDESKTOP_MENU_ROLE_PASTE	8U
#define ZDESKTOP_MENU_ROLE_DELETE	9U
#define ZDESKTOP_MENU_ROLE_SELECT_ALL	10U
#define ZDESKTOP_MENU_ROLE_NEW		11U
#define ZDESKTOP_MENU_ROLE_OPEN		12U
#define ZDESKTOP_MENU_ROLE_SAVE		13U
#define ZDESKTOP_MENU_ROLE_CLOSE	14U
#define ZDESKTOP_MENU_ROLE_FIND		15U
#define ZDESKTOP_MENU_ROLE_HELP		16U
#define ZDESKTOP_MENU_ROLE_FULLSCREEN	17U
#define ZDESKTOP_MENU_ROLE_ZOOM_IN	18U
#define ZDESKTOP_MENU_ROLE_ZOOM_OUT	19U

/* The modifiers of a shortcut, whose key is an XKB keysym ('c', '+', 0xffc8 for F11). */
#define ZDESKTOP_MENU_SHIFT		1U
#define ZDESKTOP_MENU_CTRL		2U
#define ZDESKTOP_MENU_ALT		4U
#define ZDESKTOP_MENU_SUPER		8U

/*
 * What a window menu tells the application.  Any member may be NULL.
 *
 * activated: the user chose an item (its ID and action), by the seat's
 * input of the serial.  opened, closed: the popup of a submenu (a top-level
 * item included) opened or closed; an application may update the menu in
 * answer, and zdesktop redraws the open popup.
 */
struct zdesktop_window_menu_listener {
	void (*activated)(void *data, struct zdesktop_window_menu *window_menu, uint32_t item, uint32_t action, struct wl_seat *seat, uint32_t serial);
	void (*opened)(void *data, struct zdesktop_window_menu *window_menu, uint32_t item);
	void (*closed)(void *data, struct zdesktop_window_menu *window_menu, uint32_t item);
};

/*
 * Opens the connection's menu service.
 *
 * Returns NULL with errno ENOTSUP when the compositor has no System Menu;
 * the application then draws its own menus.
 */
struct zdesktop_menu_service *zdesktop_menu_service_open(struct wl_display *display);

/*
 * Closes a menu service; the menus and window menus made from it stay.
 */
void zdesktop_menu_service_close(struct zdesktop_menu_service *service);

/*
 * Makes an empty menu; NULL with errno set when it cannot.
 */
struct zdesktop_menu *zdesktop_menu_create(struct zdesktop_menu_service *service);

/*
 * Destroys a menu; windows showing it show no menu.
 */
void zdesktop_menu_destroy(struct zdesktop_menu *menu);

/*
 * Starts a transaction: the changes that follow are shown together at the commit.
 */
int zdesktop_menu_begin(struct zdesktop_menu *menu);

/*
 * Ends a transaction, and zdesktop shows its changes at once.
 */
int zdesktop_menu_commit(struct zdesktop_menu *menu);

/*
 * Adds an item as the last child of a parent.
 */
int zdesktop_menu_append(struct zdesktop_menu *menu, uint32_t id, uint32_t parent, unsigned type, const char *label, uint32_t action);

/*
 * Adds an item before one of a parent's children (0 appends).
 */
int zdesktop_menu_insert(struct zdesktop_menu *menu, uint32_t id, uint32_t parent, uint32_t before, unsigned type, const char *label, uint32_t action);

/*
 * Removes an item and everything under it.
 */
int zdesktop_menu_remove(struct zdesktop_menu *menu, uint32_t id);

/*
 * Sets an item's label (UTF-8, at most 255 bytes).
 */
int zdesktop_menu_set_label(struct zdesktop_menu *menu, uint32_t id, const char *label);

/*
 * Sets the action an item's choice reports.
 */
int zdesktop_menu_set_action(struct zdesktop_menu *menu, uint32_t id, uint32_t action);

/*
 * Sets whether an item can be chosen (it is shown pale when it cannot).
 */
int zdesktop_menu_set_enabled(struct zdesktop_menu *menu, uint32_t id, int enabled);

/*
 * Sets whether an item is shown at all.
 */
int zdesktop_menu_set_visible(struct zdesktop_menu *menu, uint32_t id, int visible);

/*
 * Sets whether a checkbox or radio item is checked.
 */
int zdesktop_menu_set_checked(struct zdesktop_menu *menu, uint32_t id, int checked);

/*
 * Sets an item's role (ZDESKTOP_MENU_ROLE_*).
 */
int zdesktop_menu_set_role(struct zdesktop_menu *menu, uint32_t id, unsigned role);

/*
 * Sets an item's icon by its icon-theme name ("" for none).
 */
int zdesktop_menu_set_icon_name(struct zdesktop_menu *menu, uint32_t id, const char *icon_name);

/*
 * Sets an item's shortcut: ZDESKTOP_MENU_* modifiers and an XKB keysym (0 removes it).
 */
int zdesktop_menu_set_shortcut(struct zdesktop_menu *menu, uint32_t id, unsigned modifiers, uint32_t keysym);

/*
 * Makes the place on a window that shows a menu; NULL with errno set when it cannot.
 */
struct zdesktop_window_menu *zdesktop_window_menu_create(struct zdesktop_menu_service *service, struct xdg_toplevel *toplevel,
							  const struct zdesktop_window_menu_listener *listener, void *data);

/*
 * Shows a menu on the window (NULL shows none).
 */
int zdesktop_window_menu_set(struct zdesktop_window_menu *window_menu, struct zdesktop_menu *menu);

/*
 * Destroys a window's place for a menu; the window shows none.
 */
void zdesktop_window_menu_destroy(struct zdesktop_window_menu *window_menu);

#ifdef __cplusplus
}
#endif

#endif
