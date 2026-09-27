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

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The interface version this header describes (2: the System Menu; 3: the recent files; 4: the titlebar; 5: the glass panels; 6: context menus; 7: drop targets in the titlebar). */
#define ZDESKTOP_VERSION	7U

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

/*
 * Context menus (ws071-p009): a menu's top-level items shown once as a
 * popup at a point of a surface, in answer to a press (its seat and
 * serial; zdesktop opens only for the latest press).  zdesktop owns the
 * looks and the input as for the menubar.  activated: the user chose an
 * item (its ID and action); done: the context menu closed, after a choice
 * or without one -- told once, last; the application destroys it then.
 * Either member may be NULL.
 */
struct wl_surface;
struct zdesktop_context_menu;
struct zdesktop_context_menu_listener {
	void (*activated)(void *data, struct zdesktop_context_menu *context_menu, uint32_t item, uint32_t action, uint32_t serial);
	void (*done)(void *data, struct zdesktop_context_menu *context_menu);
};

/*
 * Opens a menu as a context menu at (x, y) of a surface; NULL with errno
 * set: ENOTSUP for a compositor without context menus, ENOMEM, or EINVAL
 * when the listener cannot be installed.
 */
struct zdesktop_context_menu *zdesktop_menu_popup(struct zdesktop_menu_service *service, struct zdesktop_menu *menu, struct wl_surface *surface,
						  int32_t x, int32_t y, struct wl_seat *seat, uint32_t serial,
						  const struct zdesktop_context_menu_listener *listener, void *data);

/*
 * Destroys a context menu; one still open closes without telling.
 */
void zdesktop_context_menu_destroy(struct zdesktop_context_menu *context_menu);

/*
 * The Titlebar Presentation (WS070 p008, plan/ws070/titlebar-design.md).
 *
 * zdesktop draws a window's titlebar: its mark and title, a presentation,
 * and the window's buttons, in the floating titlebar or, while the window
 * is maximized, in the system bar.  The presentation is one of three
 * models the application gives: the menu (the System Menu above, the
 * default), controls (back, forward, a breadcrumb, a search field, a view
 * selector...), or tabs.  The application gives only what they mean;
 * zdesktop decides how they look and where they go, and tells the
 * application what the user does with them.  Changes are made in
 * transactions, like a menu's.  Every call that returns an int returns 0
 * or an errno value.
 */
struct zdesktop_titlebar;

/* The presentation modes. */
#define ZDESKTOP_TITLEBAR_MENU		0U
#define ZDESKTOP_TITLEBAR_CONTROLS	1U
#define ZDESKTOP_TITLEBAR_TABS		2U

/* The controls' roles, which decide how zdesktop draws them. */
#define ZDESKTOP_CONTROL_BACK		1U
#define ZDESKTOP_CONTROL_FORWARD	2U
#define ZDESKTOP_CONTROL_HOME		3U
#define ZDESKTOP_CONTROL_UP		4U
#define ZDESKTOP_CONTROL_BREADCRUMB	5U
#define ZDESKTOP_CONTROL_SEARCH		6U
#define ZDESKTOP_CONTROL_VIEW_GRID	7U
#define ZDESKTOP_CONTROL_VIEW_LIST	8U
#define ZDESKTOP_CONTROL_VIEW_COLUMNS	9U
#define ZDESKTOP_CONTROL_SORT		10U
#define ZDESKTOP_CONTROL_FILTER		11U
#define ZDESKTOP_CONTROL_SIDEBAR	12U
#define ZDESKTOP_CONTROL_PREVIEW	13U
#define ZDESKTOP_CONTROL_PROGRESS	14U
#define ZDESKTOP_CONTROL_PRIMARY_ACTION	15U
#define ZDESKTOP_CONTROL_GENERIC	16U

/* The controls' priorities: the order they give way in when the room runs short. */
#define ZDESKTOP_PRIORITY_PRIMARY	0U
#define ZDESKTOP_PRIORITY_NORMAL	1U
#define ZDESKTOP_PRIORITY_SECONDARY	2U

/* A progress control's value that says the share done is not known. */
#define ZDESKTOP_PROGRESS_UNKNOWN	1001U

/* The tabs' flags, and the tab strip's options. */
#define ZDESKTOP_TAB_ACTIVE		1U
#define ZDESKTOP_TAB_ATTENTION		2U
#define ZDESKTOP_TAB_CLOSABLE		4U
#define ZDESKTOP_TABS_NEW_BUTTON	1U

/* How a text control takes the keyboard, and how its editing ended. */
#define ZDESKTOP_FOCUS_FIELD		0U
#define ZDESKTOP_FOCUS_EDIT		1U
#define ZDESKTOP_TEXT_SUBMITTED		0U
#define ZDESKTOP_TEXT_CANCELLED		1U
#define ZDESKTOP_TEXT_LEFT		2U

/*
 * What zdesktop tells the application about its titlebar: a control chosen
 * (detail is a breadcrumb's part, 0 otherwise), a text control's text as
 * it is typed and when its editing ends, a tab chosen or closed, the
 * new-tab button, and the overflow popup opening.  Any may be NULL.
 *
 * zdesktop gives tabs the keyboard too, when the window's menu has no
 * shortcut for the key: Ctrl+Tab and Ctrl+PageDown activate the next tab,
 * Ctrl+Shift+Tab and Ctrl+PageUp the one before (tab_activated), Ctrl+W
 * asks to close the active tab when it is closable (tab_close_requested),
 * and Ctrl+T asks for a new one when the strip has the new-tab button
 * (new_tab_requested).
 *
 * drop_target (ZDESKTOP_VERSION 7): while a drag and drop (wl_data_device)
 * is over a part of a breadcrumb in the titlebar, zdesktop makes the
 * window's surface the drag's target (its data device hears enter, motion
 * and drop at the pointer's place, above the surface) and tells the part
 * here first (id and detail as for control_activated); id 0 says the drag
 * is over none of the controls now.  A drop then goes to that part's folder.
 */
struct zdesktop_titlebar_listener {
	void (*control_activated)(void *data, struct zdesktop_titlebar *titlebar, uint32_t id, uint32_t detail, struct wl_seat *seat, uint32_t serial);
	void (*text_changed)(void *data, struct zdesktop_titlebar *titlebar, uint32_t id, const char *text);
	void (*text_done)(void *data, struct zdesktop_titlebar *titlebar, uint32_t id, const char *text, unsigned how);
	void (*tab_activated)(void *data, struct zdesktop_titlebar *titlebar, uint32_t id, uint32_t serial);
	void (*tab_close_requested)(void *data, struct zdesktop_titlebar *titlebar, uint32_t id);
	void (*new_tab_requested)(void *data, struct zdesktop_titlebar *titlebar, uint32_t serial);
	void (*overflow_menu_opened)(void *data, struct zdesktop_titlebar *titlebar);
	void (*drop_target)(void *data, struct zdesktop_titlebar *titlebar, uint32_t id, uint32_t detail);
};

/*
 * Gives a window its titlebar presentation, in menu mode until changed;
 * NULL with errno set (ENOTSUP for a compositor without it).
 */
struct zdesktop_titlebar *zdesktop_titlebar_create(struct wl_display *display, struct xdg_toplevel *toplevel,
						   const struct zdesktop_titlebar_listener *listener, void *data);

/*
 * Takes the titlebar presentation away; the window shows its menu again.
 */
void zdesktop_titlebar_destroy(struct zdesktop_titlebar *titlebar);

/*
 * Starts a transaction; the changes until zdesktop_titlebar_commit are shown together.
 */
int zdesktop_titlebar_begin(struct zdesktop_titlebar *titlebar);

/*
 * Ends a transaction; zdesktop shows its changes at once.
 */
int zdesktop_titlebar_commit(struct zdesktop_titlebar *titlebar);

/*
 * Chooses the presentation (ZDESKTOP_TITLEBAR_*).
 */
int zdesktop_titlebar_set_mode(struct zdesktop_titlebar *titlebar, unsigned mode);

/*
 * Adds a control at the end: its ID (not 0), role, priority, the segmented group it joins (0 for none) and label.
 */
int zdesktop_titlebar_add_control(struct zdesktop_titlebar *titlebar, uint32_t id, unsigned role, unsigned priority, unsigned group, const char *label);

/*
 * Removes a control.
 */
int zdesktop_titlebar_remove_control(struct zdesktop_titlebar *titlebar, uint32_t id);

/*
 * Sets a control's label.
 */
int zdesktop_titlebar_set_control_label(struct zdesktop_titlebar *titlebar, uint32_t id, const char *label);

/*
 * Sets whether a control works now and whether it is checked.
 */
int zdesktop_titlebar_set_control_state(struct zdesktop_titlebar *titlebar, uint32_t id, int enabled, int checked);

/*
 * Sets a progress control's share done, in thousandths (ZDESKTOP_PROGRESS_UNKNOWN when not known).
 */
int zdesktop_titlebar_set_control_value(struct zdesktop_titlebar *titlebar, uint32_t id, unsigned value);

/*
 * Sets a search's or a breadcrumb's text and what it shows when empty.
 */
int zdesktop_titlebar_set_control_text(struct zdesktop_titlebar *titlebar, uint32_t id, const char *text, const char *placeholder);

/*
 * Sets a breadcrumb's parts, from the first (the outermost) to the last.
 */
int zdesktop_titlebar_set_breadcrumb(struct zdesktop_titlebar *titlebar, uint32_t id, const char *const *segments, size_t count);

/*
 * Adds a tab at the end, closable and not active.
 */
int zdesktop_titlebar_add_tab(struct zdesktop_titlebar *titlebar, uint32_t id, const char *title);

/*
 * Removes a tab.
 */
int zdesktop_titlebar_remove_tab(struct zdesktop_titlebar *titlebar, uint32_t id);

/*
 * Sets a tab's title and flags (ZDESKTOP_TAB_*).
 */
int zdesktop_titlebar_set_tab(struct zdesktop_titlebar *titlebar, uint32_t id, const char *title, unsigned flags);

/*
 * Sets the tab strip's options (ZDESKTOP_TABS_NEW_BUTTON).
 */
int zdesktop_titlebar_set_tabs_options(struct zdesktop_titlebar *titlebar, unsigned options);

/*
 * Gives the keyboard to a committed search or breadcrumb control (ZDESKTOP_FOCUS_*), outside a transaction.
 */
int zdesktop_titlebar_focus_control(struct zdesktop_titlebar *titlebar, uint32_t id, unsigned mode);

/*
 * The recent files (WS071).
 *
 * One list of recently used files for all applications, newest first: a
 * file manager shows it as Recents, an application may offer it as "open
 * recent".  An application adds a file when it opens or saves one.  Every
 * call returns 0 or an errno value.
 */

/* The longest path and application name an entry holds, with the terminating NUL. */
#define ZDESKTOP_RECENT_PATH_MAX	4096U
#define ZDESKTOP_RECENT_NAME_MAX	64U

/* How many entries the list keeps (the oldest go first). */
#define ZDESKTOP_RECENT_KEPT		256U

/*
 * One entry of the recent list: the file's absolute path, the application
 * that used it (its app_id) and when, in seconds since the epoch.
 */
struct zdesktop_recent_item {
	char path[ZDESKTOP_RECENT_PATH_MAX];
	char application[ZDESKTOP_RECENT_NAME_MAX];
	int64_t time;
};

/*
 * Adds a file (an absolute path) to the recent list, or makes it the
 * newest when it is listed.
 */
int zdesktop_recent_add(const char *path, const char *application);

/*
 * Reads the recent list, newest first, into up to capacity items.
 */
int zdesktop_recent_list(struct zdesktop_recent_item *items, size_t capacity, size_t *count);

/*
 * Takes a file off the recent list.
 */
int zdesktop_recent_remove(const char *path);

/*
 * The glass panels (ws035-p083).
 *
 * A window whose Vulkan swapchain is see-through
 * (VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR) names the parts of itself
 * that stand on the system's frosted glass: cards floating in the window.
 * zdesktop draws the glass under them -- the desktop behind, blurred and
 * lightened, a bright rim, the card's shadow --
 * and the window's image over it by its alpha; between the panels the
 * desktop shows as it is.  The window says what its parts are, not how
 * the glass looks.
 *
 * The panels, in the surface's coordinates, take effect with the surface's
 * next commit (a Vulkan present), so they move with the frame drawn for
 * them.  Every call returns 0 or an errno value, and a refused call sends
 * nothing: EINVAL (an empty panel, a radius past the largest, an unknown
 * kind), E2BIG (too many panels).
 */
struct zdesktop_glass;

/* The kind of panel (the only one so far): a card floating in the window. */
#define ZDESKTOP_GLASS_CARD		0U

/* The most panels a surface has, and the largest corner radius. */
#define ZDESKTOP_GLASS_PANELS_MAX	32U
#define ZDESKTOP_GLASS_RADIUS_MAX	64

/*
 * One panel: its rectangle in the surface's coordinates, the radius of
 * its corners and its kind.
 */
struct zdesktop_glass_panel {
	int32_t x;
	int32_t y;
	int32_t width;
	int32_t height;
	int32_t radius;
	unsigned kind;
};

/*
 * Gives a surface its glass, with no panels yet.  Returns NULL with errno
 * set: ENOTSUP for a compositor without glass, ENOMEM.
 */
struct zdesktop_glass *zdesktop_glass_create(struct wl_display *display, struct wl_surface *surface);

/*
 * Sets the surface's panels for its next commit (count 0: none).
 */
int zdesktop_glass_set_panels(struct zdesktop_glass *glass, const struct zdesktop_glass_panel *panels, size_t count);

/*
 * Takes the glass away: the surface's next commit shows it without panels.
 */
void zdesktop_glass_destroy(struct zdesktop_glass *glass);

#ifdef __cplusplus
}
#endif

#endif
