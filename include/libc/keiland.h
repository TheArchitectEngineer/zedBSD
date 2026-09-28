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
 * desktop (xserver, an application) uses standard Wayland and
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
 *
 * It also holds what every program that follows a finger shares, so that
 * a finger feels the same everywhere: where a touch contact is at the time
 * a frame is drawn, and how fast it moved when it lifted (the touch motion,
 * WS081; its first user is zdesktop itself).
 */

#ifndef KEILAND_H
#define KEILAND_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The interface version this header describes (2: the System Menu; 3: the recent files; 4: the titlebar; 5: the glass panels; 6: context menus; 7: drop targets in the titlebar; 8: the network; 9: the touch motion). */
#define KEILAND_VERSION	9U

/*
 * Reports the interface version of the library that was loaded.
 *
 * A program built against this header may compare the result with
 * KEILAND_VERSION to learn whether the library it runs with is older.
 */
unsigned keiland_version(void);

/*
 * The System Menu.
 *
 * A service is one connection's way to zdesktop's menus; a menu is one tree
 * of items; a window menu shows a menu on one xdg_toplevel.  One menu may be
 * shown on several windows (an application menu shared by its windows); the
 * choice comes back through the window menu it was made on.
 *
 * Items are named by numbers the application chooses (not 0; unique in
 * their menu).  KEILAND_MENU_ROOT is the parent of the top-level items (in
 * a terminal: Shell, Edit, View, Session, Help); a submenu item is the
 * parent of the items under it.  Every change is made between
 * keiland_menu_begin and keiland_menu_commit, and zdesktop shows the
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
struct keiland_menu_service;
struct keiland_menu;
struct keiland_window_menu;

/* The parent of the top-level items. */
#define KEILAND_MENU_ROOT		0U

/* The kinds of item. */
#define KEILAND_MENU_ITEM_NORMAL	0U
#define KEILAND_MENU_ITEM_SEPARATOR	1U
#define KEILAND_MENU_ITEM_CHECKBOX	2U
#define KEILAND_MENU_ITEM_RADIO	3U
#define KEILAND_MENU_ITEM_SUBMENU	4U

/* What an item means to the system (zdesktop may give it an icon or a place of its own). */
#define KEILAND_MENU_ROLE_NONE		0U
#define KEILAND_MENU_ROLE_ABOUT	1U
#define KEILAND_MENU_ROLE_PREFERENCES	2U
#define KEILAND_MENU_ROLE_QUIT		3U
#define KEILAND_MENU_ROLE_UNDO		4U
#define KEILAND_MENU_ROLE_REDO		5U
#define KEILAND_MENU_ROLE_CUT		6U
#define KEILAND_MENU_ROLE_COPY		7U
#define KEILAND_MENU_ROLE_PASTE	8U
#define KEILAND_MENU_ROLE_DELETE	9U
#define KEILAND_MENU_ROLE_SELECT_ALL	10U
#define KEILAND_MENU_ROLE_NEW		11U
#define KEILAND_MENU_ROLE_OPEN		12U
#define KEILAND_MENU_ROLE_SAVE		13U
#define KEILAND_MENU_ROLE_CLOSE	14U
#define KEILAND_MENU_ROLE_FIND		15U
#define KEILAND_MENU_ROLE_HELP		16U
#define KEILAND_MENU_ROLE_FULLSCREEN	17U
#define KEILAND_MENU_ROLE_ZOOM_IN	18U
#define KEILAND_MENU_ROLE_ZOOM_OUT	19U

/* The modifiers of a shortcut, whose key is an XKB keysym ('c', '+', 0xffc8 for F11). */
#define KEILAND_MENU_SHIFT		1U
#define KEILAND_MENU_CTRL		2U
#define KEILAND_MENU_ALT		4U
#define KEILAND_MENU_SUPER		8U

/*
 * What a window menu tells the application.  Any member may be NULL.
 *
 * activated: the user chose an item (its ID and action), by the seat's
 * input of the serial.  opened, closed: the popup of a submenu (a top-level
 * item included) opened or closed; an application may update the menu in
 * answer, and zdesktop redraws the open popup.
 */
struct keiland_window_menu_listener {
	void (*activated)(void *data, struct keiland_window_menu *window_menu, uint32_t item, uint32_t action, struct wl_seat *seat, uint32_t serial);
	void (*opened)(void *data, struct keiland_window_menu *window_menu, uint32_t item);
	void (*closed)(void *data, struct keiland_window_menu *window_menu, uint32_t item);
};

/*
 * Opens the connection's menu service.
 *
 * Returns NULL with errno ENOTSUP when the compositor has no System Menu;
 * the application then draws its own menus.
 */
struct keiland_menu_service *keiland_menu_service_open(struct wl_display *display);

/*
 * Closes a menu service; the menus and window menus made from it stay.
 */
void keiland_menu_service_close(struct keiland_menu_service *service);

/*
 * Makes an empty menu; NULL with errno set when it cannot.
 */
struct keiland_menu *keiland_menu_create(struct keiland_menu_service *service);

/*
 * Destroys a menu; windows showing it show no menu.
 */
void keiland_menu_destroy(struct keiland_menu *menu);

/*
 * Starts a transaction: the changes that follow are shown together at the commit.
 */
int keiland_menu_begin(struct keiland_menu *menu);

/*
 * Ends a transaction, and zdesktop shows its changes at once.
 */
int keiland_menu_commit(struct keiland_menu *menu);

/*
 * Adds an item as the last child of a parent.
 */
int keiland_menu_append(struct keiland_menu *menu, uint32_t id, uint32_t parent, unsigned type, const char *label, uint32_t action);

/*
 * Adds an item before one of a parent's children (0 appends).
 */
int keiland_menu_insert(struct keiland_menu *menu, uint32_t id, uint32_t parent, uint32_t before, unsigned type, const char *label, uint32_t action);

/*
 * Removes an item and everything under it.
 */
int keiland_menu_remove(struct keiland_menu *menu, uint32_t id);

/*
 * Sets an item's label (UTF-8, at most 255 bytes).
 */
int keiland_menu_set_label(struct keiland_menu *menu, uint32_t id, const char *label);

/*
 * Sets the action an item's choice reports.
 */
int keiland_menu_set_action(struct keiland_menu *menu, uint32_t id, uint32_t action);

/*
 * Sets whether an item can be chosen (it is shown pale when it cannot).
 */
int keiland_menu_set_enabled(struct keiland_menu *menu, uint32_t id, int enabled);

/*
 * Sets whether an item is shown at all.
 */
int keiland_menu_set_visible(struct keiland_menu *menu, uint32_t id, int visible);

/*
 * Sets whether a checkbox or radio item is checked.
 */
int keiland_menu_set_checked(struct keiland_menu *menu, uint32_t id, int checked);

/*
 * Sets an item's role (KEILAND_MENU_ROLE_*).
 */
int keiland_menu_set_role(struct keiland_menu *menu, uint32_t id, unsigned role);

/*
 * Sets an item's icon by its icon-theme name ("" for none).
 */
int keiland_menu_set_icon_name(struct keiland_menu *menu, uint32_t id, const char *icon_name);

/*
 * Sets an item's shortcut: KEILAND_MENU_* modifiers and an XKB keysym (0 removes it).
 */
int keiland_menu_set_shortcut(struct keiland_menu *menu, uint32_t id, unsigned modifiers, uint32_t keysym);

/*
 * Makes the place on a window that shows a menu; NULL with errno set when it cannot.
 */
struct keiland_window_menu *keiland_window_menu_create(struct keiland_menu_service *service, struct xdg_toplevel *toplevel,
							  const struct keiland_window_menu_listener *listener, void *data);

/*
 * Shows a menu on the window (NULL shows none).
 */
int keiland_window_menu_set(struct keiland_window_menu *window_menu, struct keiland_menu *menu);

/*
 * Destroys a window's place for a menu; the window shows none.
 */
void keiland_window_menu_destroy(struct keiland_window_menu *window_menu);

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
struct keiland_context_menu;
struct keiland_context_menu_listener {
	void (*activated)(void *data, struct keiland_context_menu *context_menu, uint32_t item, uint32_t action, uint32_t serial);
	void (*done)(void *data, struct keiland_context_menu *context_menu);
};

/*
 * Opens a menu as a context menu at (x, y) of a surface; NULL with errno
 * set: ENOTSUP for a compositor without context menus, ENOMEM, or EINVAL
 * when the listener cannot be installed.
 */
struct keiland_context_menu *keiland_menu_popup(struct keiland_menu_service *service, struct keiland_menu *menu, struct wl_surface *surface,
						  int32_t x, int32_t y, struct wl_seat *seat, uint32_t serial,
						  const struct keiland_context_menu_listener *listener, void *data);

/*
 * Destroys a context menu; one still open closes without telling.
 */
void keiland_context_menu_destroy(struct keiland_context_menu *context_menu);

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
struct keiland_titlebar;

/* The presentation modes. */
#define KEILAND_TITLEBAR_MENU		0U
#define KEILAND_TITLEBAR_CONTROLS	1U
#define KEILAND_TITLEBAR_TABS		2U

/* The controls' roles, which decide how zdesktop draws them. */
#define KEILAND_CONTROL_BACK		1U
#define KEILAND_CONTROL_FORWARD	2U
#define KEILAND_CONTROL_HOME		3U
#define KEILAND_CONTROL_UP		4U
#define KEILAND_CONTROL_BREADCRUMB	5U
#define KEILAND_CONTROL_SEARCH		6U
#define KEILAND_CONTROL_VIEW_GRID	7U
#define KEILAND_CONTROL_VIEW_LIST	8U
#define KEILAND_CONTROL_VIEW_COLUMNS	9U
#define KEILAND_CONTROL_SORT		10U
#define KEILAND_CONTROL_FILTER		11U
#define KEILAND_CONTROL_SIDEBAR	12U
#define KEILAND_CONTROL_PREVIEW	13U
#define KEILAND_CONTROL_PROGRESS	14U
#define KEILAND_CONTROL_PRIMARY_ACTION	15U
#define KEILAND_CONTROL_GENERIC	16U

/* The controls' priorities: the order they give way in when the room runs short. */
#define KEILAND_PRIORITY_PRIMARY	0U
#define KEILAND_PRIORITY_NORMAL	1U
#define KEILAND_PRIORITY_SECONDARY	2U

/* A progress control's value that says the share done is not known. */
#define KEILAND_PROGRESS_UNKNOWN	1001U

/* The tabs' flags, and the tab strip's options. */
#define KEILAND_TAB_ACTIVE		1U
#define KEILAND_TAB_ATTENTION		2U
#define KEILAND_TAB_CLOSABLE		4U
#define KEILAND_TABS_NEW_BUTTON	1U

/* How a text control takes the keyboard, and how its editing ended. */
#define KEILAND_FOCUS_FIELD		0U
#define KEILAND_FOCUS_EDIT		1U
#define KEILAND_TEXT_SUBMITTED		0U
#define KEILAND_TEXT_CANCELLED		1U
#define KEILAND_TEXT_LEFT		2U

/*
 * What zdesktop tells the application about its titlebar: a control chosen
 * (detail is a breadcrumb's part, 0 otherwise), a text control's text as
 * it is typed and when its editing ends, a tab chosen or closed, the
 * new-tab button, and the overflow popup opening.  Any may be NULL.
 *
 * zdesktop gives tabs the keyboard too, when the window's menu has no
 * shortcut for the key: Ctrl+Tab and Ctrl+PageDown activate the next tab,
 * Ctrl+Shift+Tab and Ctrl+PageUp the one before (tab_activated).  Closing
 * a tab and a new tab are the application's keys (its menu's shortcuts),
 * since a terminal's shell needs Ctrl+W and Ctrl+T.
 *
 * drop_target (KEILAND_VERSION 7): while a drag and drop (wl_data_device)
 * is over a part of a breadcrumb in the titlebar, zdesktop makes the
 * window's surface the drag's target (its data device hears enter, motion
 * and drop at the pointer's place, above the surface) and tells the part
 * here first (id and detail as for control_activated); id 0 says the drag
 * is over none of the controls now.  A drop then goes to that part's folder.
 */
struct keiland_titlebar_listener {
	void (*control_activated)(void *data, struct keiland_titlebar *titlebar, uint32_t id, uint32_t detail, struct wl_seat *seat, uint32_t serial);
	void (*text_changed)(void *data, struct keiland_titlebar *titlebar, uint32_t id, const char *text);
	void (*text_done)(void *data, struct keiland_titlebar *titlebar, uint32_t id, const char *text, unsigned how);
	void (*tab_activated)(void *data, struct keiland_titlebar *titlebar, uint32_t id, uint32_t serial);
	void (*tab_close_requested)(void *data, struct keiland_titlebar *titlebar, uint32_t id);
	void (*new_tab_requested)(void *data, struct keiland_titlebar *titlebar, uint32_t serial);
	void (*overflow_menu_opened)(void *data, struct keiland_titlebar *titlebar);
	void (*drop_target)(void *data, struct keiland_titlebar *titlebar, uint32_t id, uint32_t detail);
};

/*
 * Gives a window its titlebar presentation, in menu mode until changed;
 * NULL with errno set (ENOTSUP for a compositor without it).
 */
struct keiland_titlebar *keiland_titlebar_create(struct wl_display *display, struct xdg_toplevel *toplevel,
						   const struct keiland_titlebar_listener *listener, void *data);

/*
 * Takes the titlebar presentation away; the window shows its menu again.
 */
void keiland_titlebar_destroy(struct keiland_titlebar *titlebar);

/*
 * Starts a transaction; the changes until keiland_titlebar_commit are shown together.
 */
int keiland_titlebar_begin(struct keiland_titlebar *titlebar);

/*
 * Ends a transaction; zdesktop shows its changes at once.
 */
int keiland_titlebar_commit(struct keiland_titlebar *titlebar);

/*
 * Chooses the presentation (KEILAND_TITLEBAR_*).
 */
int keiland_titlebar_set_mode(struct keiland_titlebar *titlebar, unsigned mode);

/*
 * Adds a control at the end: its ID (not 0), role, priority, the segmented group it joins (0 for none) and label.
 */
int keiland_titlebar_add_control(struct keiland_titlebar *titlebar, uint32_t id, unsigned role, unsigned priority, unsigned group, const char *label);

/*
 * Removes a control.
 */
int keiland_titlebar_remove_control(struct keiland_titlebar *titlebar, uint32_t id);

/*
 * Sets a control's label.
 */
int keiland_titlebar_set_control_label(struct keiland_titlebar *titlebar, uint32_t id, const char *label);

/*
 * Sets whether a control works now and whether it is checked.
 */
int keiland_titlebar_set_control_state(struct keiland_titlebar *titlebar, uint32_t id, int enabled, int checked);

/*
 * Sets a progress control's share done, in thousandths (KEILAND_PROGRESS_UNKNOWN when not known).
 */
int keiland_titlebar_set_control_value(struct keiland_titlebar *titlebar, uint32_t id, unsigned value);

/*
 * Sets a search's or a breadcrumb's text and what it shows when empty.
 */
int keiland_titlebar_set_control_text(struct keiland_titlebar *titlebar, uint32_t id, const char *text, const char *placeholder);

/*
 * Sets a breadcrumb's parts, from the first (the outermost) to the last.
 */
int keiland_titlebar_set_breadcrumb(struct keiland_titlebar *titlebar, uint32_t id, const char *const *segments, size_t count);

/*
 * Adds a tab at the end, closable and not active.
 */
int keiland_titlebar_add_tab(struct keiland_titlebar *titlebar, uint32_t id, const char *title);

/*
 * Removes a tab.
 */
int keiland_titlebar_remove_tab(struct keiland_titlebar *titlebar, uint32_t id);

/*
 * Sets a tab's title and flags (KEILAND_TAB_*).
 */
int keiland_titlebar_set_tab(struct keiland_titlebar *titlebar, uint32_t id, const char *title, unsigned flags);

/*
 * Sets the tab strip's options (KEILAND_TABS_NEW_BUTTON).
 */
int keiland_titlebar_set_tabs_options(struct keiland_titlebar *titlebar, unsigned options);

/*
 * Gives the keyboard to a committed search or breadcrumb control (KEILAND_FOCUS_*), outside a transaction.
 */
int keiland_titlebar_focus_control(struct keiland_titlebar *titlebar, uint32_t id, unsigned mode);

/*
 * The recent files (WS071).
 *
 * One list of recently used files for all applications, newest first: a
 * file manager shows it as Recents, an application may offer it as "open
 * recent".  An application adds a file when it opens or saves one.  Every
 * call returns 0 or an errno value.
 */

/* The longest path and application name an entry holds, with the terminating NUL. */
#define KEILAND_RECENT_PATH_MAX	4096U
#define KEILAND_RECENT_NAME_MAX	64U

/* How many entries the list keeps (the oldest go first). */
#define KEILAND_RECENT_KEPT		256U

/*
 * One entry of the recent list: the file's absolute path, the application
 * that used it (its app_id) and when, in seconds since the epoch.
 */
struct keiland_recent_item {
	char path[KEILAND_RECENT_PATH_MAX];
	char application[KEILAND_RECENT_NAME_MAX];
	int64_t time;
};

/*
 * Adds a file (an absolute path) to the recent list, or makes it the
 * newest when it is listed.
 */
int keiland_recent_add(const char *path, const char *application);

/*
 * Reads the recent list, newest first, into up to capacity items.
 */
int keiland_recent_list(struct keiland_recent_item *items, size_t capacity, size_t *count);

/*
 * Takes a file off the recent list.
 */
int keiland_recent_remove(const char *path);

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
struct keiland_glass;

/* The kind of panel (the only one so far): a card floating in the window. */
#define KEILAND_GLASS_CARD		0U

/* The most panels a surface has, and the largest corner radius. */
#define KEILAND_GLASS_PANELS_MAX	32U
#define KEILAND_GLASS_RADIUS_MAX	64

/*
 * One panel: its rectangle in the surface's coordinates, the radius of
 * its corners and its kind.
 */
struct keiland_glass_panel {
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
struct keiland_glass *keiland_glass_create(struct wl_display *display, struct wl_surface *surface);

/*
 * Sets the surface's panels for its next commit (count 0: none).
 */
int keiland_glass_set_panels(struct keiland_glass *glass, const struct keiland_glass_panel *panels, size_t count);

/*
 * Takes the glass away: the surface's next commit shows it without panels.
 */
void keiland_glass_destroy(struct keiland_glass *glass);

/*
 * The network (ws035-p013).
 *
 * The desktop's view of the network and its Wi-Fi switch, for the system
 * bar: whether the machine is connected and through what (a wired
 * interface, or a Wi-Fi network by its SSID), the networks the radio sees,
 * and the requests a user makes from a menu (join a network, disconnect,
 * turn Wi-Fi on or off).  The system's network daemon is behind it; the
 * desktop never speaks the daemon's protocol itself.
 *
 * Nothing here waits.  The state arrives when the daemon reports a change;
 * keiland_network_update reads what has arrived and says what changed.  A
 * request is sent at once and its answer arrives through the same update,
 * so a scan or a join that takes seconds does not stop the caller.  One
 * request is outstanding at a time (EBUSY otherwise).
 *
 * Every call that can fail returns 0 or an errno value: ENOENT (the daemon
 * is not running), EACCES or EPERM (the user may not look or act),
 * EBUSY, EINVAL, ENOMEM.
 */
struct keiland_network;

/* The longest SSID shown, as printable text with the terminating NUL. */
#define KEILAND_NETWORK_SSID_MAX	33U

/* The longest interface name, with the terminating NUL. */
#define KEILAND_NETWORK_NAME_MAX	16U

/* The most networks a scan keeps. */
#define KEILAND_NETWORK_SCAN_MAX	24U

/* What carries the connection. */
#define KEILAND_NETWORK_NONE		0U
#define KEILAND_NETWORK_WIRED		1U
#define KEILAND_NETWORK_WIFI		2U

/* The Wi-Fi's state. */
#define KEILAND_WIFI_ABSENT		0U	/* no radio */
#define KEILAND_WIFI_OFF		1U
#define KEILAND_WIFI_SEARCHING		2U
#define KEILAND_WIFI_CONNECTING	3U
#define KEILAND_WIFI_CONNECTED		4U
#define KEILAND_WIFI_DISCONNECTED	5U	/* on, and left unconnected by the user */

/* What keiland_network_update found (bits). */
#define KEILAND_NETWORK_CHANGED_STATE	1U
#define KEILAND_NETWORK_CHANGED_SCAN	2U
#define KEILAND_NETWORK_CHANGED_DONE	4U

/* The requests. */
#define KEILAND_NETWORK_REQUEST_NONE		0U
#define KEILAND_NETWORK_REQUEST_SCAN		1U
#define KEILAND_NETWORK_REQUEST_JOIN		2U
#define KEILAND_NETWORK_REQUEST_DISCONNECT	3U
#define KEILAND_NETWORK_REQUEST_WIFI_ON	4U
#define KEILAND_NETWORK_REQUEST_WIFI_OFF	5U

/*
 * The network as last reported: connected (an interface is up with an
 * address), through what and which interface, the wired interface that is
 * up with an address (empty when none, even while the Wi-Fi carries the
 * connection), and the Wi-Fi's state with the SSID of the network it is on
 * or joining (empty otherwise).  reachable is 0 while the daemon cannot be
 * reached.
 */
struct keiland_network_state {
	unsigned reachable;
	unsigned connected;
	unsigned kind;
	char interface[KEILAND_NETWORK_NAME_MAX];
	char wired[KEILAND_NETWORK_NAME_MAX];
	unsigned wifi;
	char wifi_interface[KEILAND_NETWORK_NAME_MAX];
	char ssid[KEILAND_NETWORK_SSID_MAX];
};

/*
 * One network a scan found: its SSID, its signal in dBm, and whether it
 * asks for a key.  The strongest of the access points of one SSID stands
 * for it.
 */
struct keiland_network_ap {
	char ssid[KEILAND_NETWORK_SSID_MAX];
	int rssi;
	unsigned secured;
};

/*
 * Starts watching the network.  Returns NULL with errno set on ENOMEM; a
 * daemon that is not running yet is tried again by the updates.
 */
struct keiland_network *keiland_network_open(void);

/*
 * Stops watching and drops an outstanding request.
 */
void keiland_network_close(struct keiland_network *network);

/*
 * Reads what has arrived without waiting, and reconnects to a daemon that
 * went away (at most once a second).  *changed gets the
 * KEILAND_NETWORK_CHANGED_* bits of what changed.
 */
int keiland_network_update(struct keiland_network *network, unsigned *changed);

/*
 * Copies the network's state as last reported.
 */
void keiland_network_get_state(const struct keiland_network *network, struct keiland_network_state *state);

/*
 * Copies up to capacity networks of the last scan, the strongest first,
 * and returns how many there are.
 */
size_t keiland_network_get_scan(const struct keiland_network *network, struct keiland_network_ap *aps, size_t capacity);

/*
 * Sends a request (KEILAND_NETWORK_REQUEST_*; a join names the SSID, the
 * others take NULL).  A join uses the network's saved profile.
 */
int keiland_network_request(struct keiland_network *network, unsigned request, const char *ssid);

/*
 * Tells the request outstanding (KEILAND_NETWORK_REQUEST_NONE when none),
 * or, after KEILAND_NETWORK_CHANGED_DONE, the one that finished and its
 * errno value (0 when it succeeded) through *error.
 */
unsigned keiland_network_get_request(const struct keiland_network *network, int *error);

/*
 * The touch motion (WS081, plan/ws081/design.md sections 3 and 6).
 *
 * A cheap touch screen reports 30 to 60 times a second, unevenly, and with
 * noise.  Drawing the last report at each frame makes a scroll or a dragged
 * window judder: some frames get no report and stand still, the next jumps.
 * A motion instead fits a line and a parabola to the contact's last reports
 * and evaluates them a little behind the frame's time, so that the point
 * advances smoothly with the frames and is never extrapolated far past the
 * last report.
 *
 * A device (one touch screen in zdesktop, one seat's touch in a client)
 * keeps what is learned across strokes: the report period, how late
 * reports arrive, the noise, and the mapping of the panel's Scan Time
 * (evdev MSC_TIMESTAMP) onto the host clock.  A motion is one contact's
 * stroke from touch-down to lift.  Neither is shared between threads.
 *
 * Times are CLOCK_MONOTONIC microseconds; positions are logical pixels.
 * zdesktop's wl_touch times are the same clock's milliseconds (the low 32
 * bits), from the time the panel scanned the report when it has a Scan
 * Time, so a client can compare them with its own clock.  Calls that can
 * fail return 0 or an errno value.
 */

/* How far past the last report content that follows a finger may be extrapolated (12 ms). */
#define KEILAND_MOTION_EXTRAPOLATION_CONTENT	12000U

/* How far past the last report the provisional tail of a drawn line may be extrapolated (16 ms). */
#define KEILAND_MOTION_EXTRAPOLATION_INK	16000U

/* A device's timing and noise, learned across strokes (opaque). */
struct keiland_motion_device;

/* One contact's stroke (opaque). */
struct keiland_motion;

/*
 * Creates a device, knowing nothing of it yet.
 *
 * Returns NULL when memory is short.
 */
struct keiland_motion_device *keiland_motion_device_create(void);

/*
 * Destroys a device.  Its motions must be destroyed first.
 */
void keiland_motion_device_destroy(struct keiland_motion_device *device);

/*
 * Turns a report's host time and its panel's Scan Time into the time the
 * panel scanned it, on the host clock.
 *
 * host_us is the evdev time of the report; device_us is its MSC_TIMESTAMP
 * (microseconds, wrapping at 2^32, restarting at 0 after a pause).  The
 * result is never later than host_us and never goes back.  While the Scan
 * Time has not proved itself (it must advance at the host's rate, within
 * 10%, over half a second) the result is host_us.  A device without Scan
 * Time does not call this and uses the host time.
 */
int keiland_motion_device_time(struct keiland_motion_device *device, uint64_t host_us, uint32_t device_us,
    uint64_t *stamp_us);

/* Reports the device's measured report period in microseconds (0: not measured yet). */
uint32_t keiland_motion_device_interval(const struct keiland_motion_device *device);

/* Reports the device's measured noise in pixels (the default until five strokes have been seen). */
double keiland_motion_device_noise(const struct keiland_motion_device *device);

/*
 * Creates a motion for contacts of a device.
 *
 * Returns NULL for a missing device or when memory is short.
 */
struct keiland_motion *keiland_motion_create(struct keiland_motion_device *device);

/* Destroys a motion. */
void keiland_motion_destroy(struct keiland_motion *motion);

/*
 * Starts a stroke: a finger touched.  Whatever the motion held is
 * forgotten, without teaching the device.
 */
void keiland_motion_begin(struct keiland_motion *motion);

/*
 * Adds one report of the stroke.
 *
 * stamp_us is when the finger was there (the evdev time, or the result of
 * keiland_motion_device_time); arrival_us is when the caller read the
 * report, on the same clock.  A report older than the last one is refused
 * with EINVAL.  After a second without reports the older ones are dropped.
 */
int keiland_motion_add(struct keiland_motion *motion, uint64_t stamp_us, uint64_t arrival_us, double x, double y);

/*
 * Gives the point to draw at a frame.
 *
 * now_us is the time the frame is being drawn; extrapolation_us is how far
 * past the last report the point may be predicted
 * (KEILAND_MOTION_EXTRAPOLATION_CONTENT or _INK).  Call it once a frame for
 * each contact: the time it evaluates the stroke at moves at most half a
 * millisecond a call.  Returns ENOENT before the first report.
 */
int keiland_motion_point(struct keiland_motion *motion, uint64_t now_us, uint32_t extrapolation_us, double *x,
    double *y);

/*
 * Gives the velocity (pixels a second) the finger had when it lifted.
 *
 * lift_us is the time of the lift.  The velocity is zero when the finger
 * had come to rest, and when its last report is older than the lift by
 * more than two periods (50 ms at least).  It is at most 8000 px/s.
 * Whether it starts a fling is the caller's decision.
 */
int keiland_motion_velocity(struct keiland_motion *motion, uint64_t lift_us, double *vx, double *vy);

/*
 * Ends a stroke: the finger lifted.  What the stroke measured (period,
 * delay, noise) is folded into the device.
 */
void keiland_motion_end(struct keiland_motion *motion);

#ifdef __cplusplus
}
#endif

#endif
