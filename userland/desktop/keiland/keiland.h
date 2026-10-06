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
 * It is also an application's way to the desktop's system: the network, the
 * sound, the power and the devices (kl_system_*) and the desktop's settings
 * (kl_settings_*) are the compositor's, asked for through Keiland's system
 * extension.  The library itself speaks to no daemon and reads none of the
 * system's files (WS131 p011): the compositor's libkeiland-backend does, on
 * each operating system.
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
 * WS081; its first user is zdesktop itself), and what content a finger
 * scrolls does after the finger lets go, and what the fingers mean (the
 * scroller and the gestures, WS081 p005).
 */

#ifndef KEILAND_H
#define KEILAND_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The interface version this header describes (2: the System Menu; 3: the recent files; 4: the titlebar; 5: the glass panels; 6: context menus; 7: drop targets in the titlebar; 8: the network; 9: the touch motion; 10: the scroller and the gestures; 11: the network's links, DNS and saved keys; 12: the file chooser (moved to the widgets, <keiui.h>, with 16); 13: the desktop's preferences; 14: the desktop surface; 15: the sound output's volume; 16: the file chooser removed, now libkeiland's kui_file_chooser; 17: kl_glass_set_blur; 18: the keyboard inset; 19: the editing operations; 20: the titlebar's sheet mode; 21: whether a sound service runs; 22: the network and the sound moved to kl_system_*, keiland_network_* and keiland_audio_* removed; 23: the machine's monitor, kl_system_monitor_*; 24: kl_system_network_set_scanning; 25: kl_titlebar_set_suggestions; 26: the application, kl_app_*, and the declarative menus, controls and glass of a window; 27: kl_system_account_set_password; 28: the removable volumes, kl_system_devices_mount; 29: the wired interfaces' configuration, kl_system_network_configure_wired; 30: Remote Login, kl_system_sharing_*; 31: the administration of the accounts, kl_system_account_administer; 32: one copy of a program and the activation, kl_instance_* and kl_activation_*; 33: a removable device's file system and size, kl_system_devices_info; 34: the lock screen's PIN, kl_system_account_set_pin; 35: the desktop's appearance, light or dark, kl_appearance_*; 36: what the user has enrolled, kl_system_account_enrolled; 37: the translations, kl_tr_*; 38: an input method's text for the focused widget, kl_ui_text and kl_ui_text_wanted; 39: a network link's speed, kl_network_link's link_mbps; 40: a touch pad's scrolling that flies on, kl_window_event's axis_source and KL_WINDOW_AXIS_STOP, kl_ui_axis, kl_scroll_axis and kl_axis_track; 41: what a window shows, kl_window_set_content_type). */
#define KL_VERSION	41U

/*
 * Reports the interface version of the library that was loaded.
 *
 * A program built against this header may compare the result with
 * KL_VERSION to learn whether the library it runs with is older.
 */
unsigned kl_version(void);

/*
 * The System Menu.
 *
 * A service is one connection's way to zdesktop's menus; a menu is one tree
 * of items; a window menu shows a menu on one xdg_toplevel.  One menu may be
 * shown on several windows (an application menu shared by its windows); the
 * choice comes back through the window menu it was made on.
 *
 * Items are named by numbers the application chooses (not 0; unique in
 * their menu).  KL_MENU_ROOT is the parent of the top-level items (in
 * a terminal: Shell, Edit, View, Session, Help); a submenu item is the
 * parent of the items under it.  Every change is made between
 * kl_menu_begin and kl_menu_commit, and zdesktop shows the
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
struct kl_menu_service;
struct kl_menu;
struct kl_window_menu;

/* The parent of the top-level items. */
#define KL_MENU_ROOT		0U

/* The kinds of item. */
#define KL_MENU_ITEM_NORMAL	0U
#define KL_MENU_ITEM_SEPARATOR	1U
#define KL_MENU_ITEM_CHECKBOX	2U
#define KL_MENU_ITEM_RADIO	3U
#define KL_MENU_ITEM_SUBMENU	4U

/* What an item means to the system (zdesktop may give it an icon or a place of its own). */
#define KL_MENU_ROLE_NONE		0U
#define KL_MENU_ROLE_ABOUT	1U
#define KL_MENU_ROLE_PREFERENCES	2U
#define KL_MENU_ROLE_QUIT		3U
#define KL_MENU_ROLE_UNDO		4U
#define KL_MENU_ROLE_REDO		5U
#define KL_MENU_ROLE_CUT		6U
#define KL_MENU_ROLE_COPY		7U
#define KL_MENU_ROLE_PASTE	8U
#define KL_MENU_ROLE_DELETE	9U
#define KL_MENU_ROLE_SELECT_ALL	10U
#define KL_MENU_ROLE_NEW		11U
#define KL_MENU_ROLE_OPEN		12U
#define KL_MENU_ROLE_SAVE		13U
#define KL_MENU_ROLE_CLOSE	14U
#define KL_MENU_ROLE_FIND		15U
#define KL_MENU_ROLE_HELP		16U
#define KL_MENU_ROLE_FULLSCREEN	17U
#define KL_MENU_ROLE_ZOOM_IN	18U
#define KL_MENU_ROLE_ZOOM_OUT	19U

/* The modifiers of a shortcut, whose key is an XKB keysym ('c', '+', 0xffc8 for F11). */
#define KL_MENU_SHIFT		1U
#define KL_MENU_CTRL		2U
#define KL_MENU_ALT		4U
#define KL_MENU_SUPER		8U

/*
 * What a window menu tells the application.  Any member may be NULL.
 *
 * activated: the user chose an item (its ID and action), by the seat's
 * input of the serial.  opened, closed: the popup of a submenu (a top-level
 * item included) opened or closed; an application may update the menu in
 * answer, and zdesktop redraws the open popup.
 */
struct kl_window_menu_listener {
	void (*activated)(void *data, struct kl_window_menu *window_menu, uint32_t item, uint32_t action, struct wl_seat *seat, uint32_t serial);
	void (*opened)(void *data, struct kl_window_menu *window_menu, uint32_t item);
	void (*closed)(void *data, struct kl_window_menu *window_menu, uint32_t item);
};

/*
 * Opens the connection's menu service.
 *
 * Returns NULL with errno ENOTSUP when the compositor has no System Menu;
 * the application then draws its own menus.
 */
struct kl_menu_service *kl_menu_service_open(struct wl_display *display);

/*
 * Closes a menu service; the menus and window menus made from it stay.
 */
void kl_menu_service_close(struct kl_menu_service *service);

/*
 * Makes an empty menu; NULL with errno set when it cannot.
 */
struct kl_menu *kl_menu_create(struct kl_menu_service *service);

/*
 * Destroys a menu; windows showing it show no menu.
 */
void kl_menu_destroy(struct kl_menu *menu);

/*
 * Starts a transaction: the changes that follow are shown together at the commit.
 */
int kl_menu_begin(struct kl_menu *menu);

/*
 * Ends a transaction, and zdesktop shows its changes at once.
 */
int kl_menu_commit(struct kl_menu *menu);

/*
 * Adds an item as the last child of a parent.
 */
int kl_menu_append(struct kl_menu *menu, uint32_t id, uint32_t parent, unsigned type, const char *label, uint32_t action);

/*
 * Adds an item before one of a parent's children (0 appends).
 */
int kl_menu_insert(struct kl_menu *menu, uint32_t id, uint32_t parent, uint32_t before, unsigned type, const char *label, uint32_t action);

/*
 * Removes an item and everything under it.
 */
int kl_menu_remove(struct kl_menu *menu, uint32_t id);

/*
 * Sets an item's label (UTF-8, at most 255 bytes).
 */
int kl_menu_set_label(struct kl_menu *menu, uint32_t id, const char *label);

/*
 * Sets the action an item's choice reports.
 */
int kl_menu_set_action(struct kl_menu *menu, uint32_t id, uint32_t action);

/*
 * Sets whether an item can be chosen (it is shown pale when it cannot).
 */
int kl_menu_set_enabled(struct kl_menu *menu, uint32_t id, int enabled);

/*
 * Sets whether an item is shown at all.
 */
int kl_menu_set_visible(struct kl_menu *menu, uint32_t id, int visible);

/*
 * Sets whether a checkbox or radio item is checked.
 */
int kl_menu_set_checked(struct kl_menu *menu, uint32_t id, int checked);

/*
 * Sets an item's role (KL_MENU_ROLE_*).
 */
int kl_menu_set_role(struct kl_menu *menu, uint32_t id, unsigned role);

/*
 * Sets an item's icon by its icon-theme name ("" for none).
 */
int kl_menu_set_icon_name(struct kl_menu *menu, uint32_t id, const char *icon_name);

/*
 * Sets an item's shortcut: KL_MENU_* modifiers and an XKB keysym (0 removes it).
 */
int kl_menu_set_shortcut(struct kl_menu *menu, uint32_t id, unsigned modifiers, uint32_t keysym);

/*
 * Makes the place on a window that shows a menu; NULL with errno set when it cannot.
 */
struct kl_window_menu *kl_window_menu_create(struct kl_menu_service *service, struct xdg_toplevel *toplevel,
							  const struct kl_window_menu_listener *listener, void *data);

/*
 * Shows a menu on the window (NULL shows none).
 */
int kl_window_menu_set(struct kl_window_menu *window_menu, struct kl_menu *menu);

/*
 * Destroys a window's place for a menu; the window shows none.
 */
void kl_window_menu_destroy(struct kl_window_menu *window_menu);

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
struct kl_context_menu;
struct kl_context_menu_listener {
	void (*activated)(void *data, struct kl_context_menu *context_menu, uint32_t item, uint32_t action, uint32_t serial);
	void (*done)(void *data, struct kl_context_menu *context_menu);
};

/*
 * Opens a menu as a context menu at (x, y) of a surface; NULL with errno
 * set: ENOTSUP for a compositor without context menus, ENOMEM, or EINVAL
 * when the listener cannot be installed.
 */
struct kl_context_menu *kl_menu_popup(struct kl_menu_service *service, struct kl_menu *menu, struct wl_surface *surface,
						  int32_t x, int32_t y, struct wl_seat *seat, uint32_t serial,
						  const struct kl_context_menu_listener *listener, void *data);

/*
 * Destroys a context menu; one still open closes without telling.
 */
void kl_context_menu_destroy(struct kl_context_menu *context_menu);

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
struct kl_titlebar;

/*
 * The presentation modes.  A sheet (KL_VERSION 20, ws090-p014) has no
 * titlebar of its own: the window hangs under its parent's titlebar
 * (xdg_toplevel_set_parent), in front of the parent, which takes no input
 * but its titlebar's while the sheet is open.  A compositor older than the
 * sheet refuses it (ENOTSUP) and the window stays a window of its own.
 */
#define KL_TITLEBAR_MENU		0U
#define KL_TITLEBAR_CONTROLS	1U
#define KL_TITLEBAR_TABS		2U
#define KL_TITLEBAR_SHEET		3U

/* The controls' roles, which decide how zdesktop draws them. */
#define KL_CONTROL_BACK		1U
#define KL_CONTROL_FORWARD	2U
#define KL_CONTROL_HOME		3U
#define KL_CONTROL_UP		4U
#define KL_CONTROL_BREADCRUMB	5U
#define KL_CONTROL_SEARCH		6U
#define KL_CONTROL_VIEW_GRID	7U
#define KL_CONTROL_VIEW_LIST	8U
#define KL_CONTROL_VIEW_COLUMNS	9U
#define KL_CONTROL_SORT		10U
#define KL_CONTROL_FILTER		11U
#define KL_CONTROL_SIDEBAR	12U
#define KL_CONTROL_PREVIEW	13U
#define KL_CONTROL_PROGRESS	14U
#define KL_CONTROL_PRIMARY_ACTION	15U
#define KL_CONTROL_GENERIC	16U

/* The controls' priorities: the order they give way in when the room runs short. */
#define KL_PRIORITY_PRIMARY	0U
#define KL_PRIORITY_NORMAL	1U
#define KL_PRIORITY_SECONDARY	2U

/* A progress control's value that says the share done is not known. */
#define KL_PROGRESS_UNKNOWN	1001U

/* The tabs' flags, and the tab strip's options. */
#define KL_TAB_ACTIVE		1U
#define KL_TAB_ATTENTION		2U
#define KL_TAB_CLOSABLE		4U
#define KL_TABS_NEW_BUTTON	1U

/* How a text control takes the keyboard, and how its editing ended. */
#define KL_FOCUS_FIELD		0U
#define KL_FOCUS_EDIT		1U
#define KL_TEXT_SUBMITTED		0U
#define KL_TEXT_CANCELLED		1U
#define KL_TEXT_LEFT		2U

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
 * drop_target (KL_VERSION 7): while a drag and drop (wl_data_device)
 * is over a part of a breadcrumb in the titlebar, zdesktop makes the
 * window's surface the drag's target (its data device hears enter, motion
 * and drop at the pointer's place, above the surface) and tells the part
 * here first (id and detail as for control_activated); id 0 says the drag
 * is over none of the controls now.  A drop then goes to that part's folder.
 */
struct kl_titlebar_listener {
	void (*control_activated)(void *data, struct kl_titlebar *titlebar, uint32_t id, uint32_t detail, struct wl_seat *seat, uint32_t serial);
	void (*text_changed)(void *data, struct kl_titlebar *titlebar, uint32_t id, const char *text);
	void (*text_done)(void *data, struct kl_titlebar *titlebar, uint32_t id, const char *text, unsigned how);
	void (*tab_activated)(void *data, struct kl_titlebar *titlebar, uint32_t id, uint32_t serial);
	void (*tab_close_requested)(void *data, struct kl_titlebar *titlebar, uint32_t id);
	void (*new_tab_requested)(void *data, struct kl_titlebar *titlebar, uint32_t serial);
	void (*overflow_menu_opened)(void *data, struct kl_titlebar *titlebar);
	void (*drop_target)(void *data, struct kl_titlebar *titlebar, uint32_t id, uint32_t detail);
};

/*
 * Gives a window its titlebar presentation, in menu mode until changed;
 * NULL with errno set (ENOTSUP for a compositor without it).
 */
struct kl_titlebar *kl_titlebar_create(struct wl_display *display, struct xdg_toplevel *toplevel,
						   const struct kl_titlebar_listener *listener, void *data);

/*
 * Takes the titlebar presentation away; the window shows its menu again.
 */
void kl_titlebar_destroy(struct kl_titlebar *titlebar);

/*
 * Starts a transaction; the changes until kl_titlebar_commit are shown together.
 */
int kl_titlebar_begin(struct kl_titlebar *titlebar);

/*
 * Ends a transaction; zdesktop shows its changes at once.
 */
int kl_titlebar_commit(struct kl_titlebar *titlebar);

/*
 * Chooses the presentation (KL_TITLEBAR_*).
 */
int kl_titlebar_set_mode(struct kl_titlebar *titlebar, unsigned mode);

/*
 * Adds a control at the end: its ID (not 0), role, priority, the segmented group it joins (0 for none) and label.
 */
int kl_titlebar_add_control(struct kl_titlebar *titlebar, uint32_t id, unsigned role, unsigned priority, unsigned group, const char *label);

/*
 * Removes a control.
 */
int kl_titlebar_remove_control(struct kl_titlebar *titlebar, uint32_t id);

/*
 * Sets a control's label.
 */
int kl_titlebar_set_control_label(struct kl_titlebar *titlebar, uint32_t id, const char *label);

/*
 * Sets whether a control works now and whether it is checked.
 */
int kl_titlebar_set_control_state(struct kl_titlebar *titlebar, uint32_t id, int enabled, int checked);

/*
 * Sets a progress control's share done, in thousandths (KL_PROGRESS_UNKNOWN when not known).
 */
int kl_titlebar_set_control_value(struct kl_titlebar *titlebar, uint32_t id, unsigned value);

/*
 * Sets a search's or a breadcrumb's text and what it shows when empty.
 */
int kl_titlebar_set_control_text(struct kl_titlebar *titlebar, uint32_t id, const char *text, const char *placeholder);

/*
 * Sets a breadcrumb's parts, from the first (the outermost) to the last.
 */
int kl_titlebar_set_breadcrumb(struct kl_titlebar *titlebar, uint32_t id, const char *const *segments, size_t count);

/*
 * Adds a tab at the end, closable and not active.
 */
int kl_titlebar_add_tab(struct kl_titlebar *titlebar, uint32_t id, const char *title);

/*
 * Removes a tab.
 */
int kl_titlebar_remove_tab(struct kl_titlebar *titlebar, uint32_t id);

/*
 * Sets a tab's title and flags (KL_TAB_*).
 */
int kl_titlebar_set_tab(struct kl_titlebar *titlebar, uint32_t id, const char *title, unsigned flags);

/*
 * Sets the tab strip's options (KL_TABS_NEW_BUTTON).
 */
int kl_titlebar_set_tabs_options(struct kl_titlebar *titlebar, unsigned options);

/*
 * Gives the keyboard to a committed search or breadcrumb control (KL_FOCUS_*), outside a transaction.
 */
int kl_titlebar_focus_control(struct kl_titlebar *titlebar, uint32_t id, unsigned mode);

/* The most suggestions a field shows (kl_titlebar_set_suggestions). */
#define KL_TITLEBAR_SUGGESTIONS_MAX	12U

/*
 * Gives a committed search or breadcrumb field suggestions, outside a
 * transaction (ws127-p010): count pairs, each a label shown in a list
 * under the field and the text it puts in the field when chosen (Up, Down
 * and Enter, or a click or a tap; the field then reports the text as
 * changed, as typing does).  The list shows while the field has the
 * keyboard and goes when its text changes or its editing ends; count 0
 * takes it away.  Returns 0, ENOTSUP for a compositor without suggestions
 * (one older than KL_VERSION 25), ENOENT, EINVAL or E2BIG.
 */
int kl_titlebar_set_suggestions(struct kl_titlebar *titlebar, uint32_t id, const char *const *labels, const char *const *texts, size_t count);

/*
 * The recent files (WS071).
 *
 * One list of recently used files for all applications, newest first: a
 * file manager shows it as Recents, an application may offer it as "open
 * recent".  An application adds a file when it opens or saves one.  Every
 * call returns 0 or an errno value.
 */

/* The longest path and application name an entry holds, with the terminating NUL. */
#define KL_RECENT_PATH_MAX	4096U
#define KL_RECENT_NAME_MAX	64U

/* How many entries the list keeps (the oldest go first). */
#define KL_RECENT_KEPT		256U

/*
 * One entry of the recent list: the file's absolute path, the application
 * that used it (its app_id) and when, in seconds since the epoch.
 */
struct kl_recent_item {
	char path[KL_RECENT_PATH_MAX];
	char application[KL_RECENT_NAME_MAX];
	int64_t time;
};

/*
 * Adds a file (an absolute path) to the recent list, or makes it the
 * newest when it is listed.
 */
int kl_recent_add(const char *path, const char *application);

/*
 * Reads the recent list, newest first, into up to capacity items.
 */
int kl_recent_list(struct kl_recent_item *items, size_t capacity, size_t *count);

/*
 * Takes a file off the recent list.
 */
int kl_recent_remove(const char *path);

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
struct kl_glass;

/* The kind of panel (the only one so far): a card floating in the window. */
#define KL_GLASS_CARD		0U

/* The most panels a surface has, and the largest corner radius. */
#define KL_GLASS_PANELS_MAX	32U
#define KL_GLASS_RADIUS_MAX	64

/*
 * One panel: its rectangle in the surface's coordinates, the radius of
 * its corners and its kind.
 */
struct kl_glass_panel {
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
struct kl_glass *kl_glass_create(struct wl_display *display, struct wl_surface *surface);

/*
 * Sets the surface's panels for its next commit (count 0: none).
 */
int kl_glass_set_panels(struct kl_glass *glass, const struct kl_glass_panel *panels, size_t count);

/*
 * Chooses whether the surface's glass (its panels and its title bar) shows
 * the windows under it blurred (enabled) or only the blurred wallpaper (the
 * default: the compositor draws nothing again for it), from the surface's
 * next commit (KL_VERSION 17).  Returns ENOTSUP when the compositor's
 * glass has no choice.
 */
int kl_glass_set_blur(struct kl_glass *glass, int enabled);

/*
 * Takes the glass away: the surface's next commit shows it without panels.
 */
void kl_glass_destroy(struct kl_glass *glass);

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
#define KL_MOTION_EXTRAPOLATION_CONTENT	12000U

/* How far past the last report the provisional tail of a drawn line may be extrapolated (16 ms). */
#define KL_MOTION_EXTRAPOLATION_INK	16000U

/* A device's timing and noise, learned across strokes (opaque). */
struct kl_motion_device;

/* One contact's stroke (opaque). */
struct kl_motion;

/*
 * Creates a device, knowing nothing of it yet.
 *
 * Returns NULL when memory is short.
 */
struct kl_motion_device *kl_motion_device_create(void);

/*
 * Destroys a device.  Its motions must be destroyed first.
 */
void kl_motion_device_destroy(struct kl_motion_device *device);

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
int kl_motion_device_time(struct kl_motion_device *device, uint64_t host_us, uint32_t device_us,
    uint64_t *stamp_us);

/* Reports the device's measured report period in microseconds (0: not measured yet). */
uint32_t kl_motion_device_interval(const struct kl_motion_device *device);

/* Reports the device's measured noise in pixels (the default until five strokes have been seen). */
double kl_motion_device_noise(const struct kl_motion_device *device);

/*
 * Creates a motion for contacts of a device.
 *
 * Returns NULL for a missing device or when memory is short.
 */
struct kl_motion *kl_motion_create(struct kl_motion_device *device);

/* Destroys a motion. */
void kl_motion_destroy(struct kl_motion *motion);

/*
 * Starts a stroke: a finger touched.  Whatever the motion held is
 * forgotten, without teaching the device.
 */
void kl_motion_begin(struct kl_motion *motion);

/*
 * Adds one report of the stroke.
 *
 * stamp_us is when the finger was there (the evdev time, or the result of
 * kl_motion_device_time); arrival_us is when the caller read the
 * report, on the same clock.  A report older than the last one is refused
 * with EINVAL.  After a second without reports the older ones are dropped.
 */
int kl_motion_add(struct kl_motion *motion, uint64_t stamp_us, uint64_t arrival_us, double x, double y);

/*
 * Gives the point to draw at a frame.
 *
 * now_us is the time the frame is being drawn; extrapolation_us is how far
 * past the last report the point may be predicted
 * (KL_MOTION_EXTRAPOLATION_CONTENT or _INK).  Call it once a frame for
 * each contact: the time it evaluates the stroke at moves at most half a
 * millisecond a call.  Returns ENOENT before the first report.
 */
int kl_motion_point(struct kl_motion *motion, uint64_t now_us, uint32_t extrapolation_us, double *x,
    double *y);

/*
 * Gives the velocity (pixels a second) the finger had when it lifted.
 *
 * lift_us is the time of the lift.  The velocity is zero when the finger
 * had come to rest, and when its last report is older than the lift by
 * more than two periods (50 ms at least).  It is at most 8000 px/s.
 * Whether it starts a fling is the caller's decision.
 */
int kl_motion_velocity(struct kl_motion *motion, uint64_t lift_us, double *vx, double *vy);

/*
 * Ends a stroke: the finger lifted.  What the stroke measured (period,
 * delay, noise) is folded into the device.
 */
void kl_motion_end(struct kl_motion *motion);

/*
 * The scroller (WS081 p005, plan/ws081/design.md section 5): what content
 * that a finger scrolls does after the finger lets go, the same in every
 * program.
 *
 * A scroller holds a position on up to two axes within bounds.  A drag
 * moves it with the finger; past a bound the content resists (rubber
 * band).  Let go fast enough, it glides on and slows down by
 * dv/dt = -v/tau - mu sign(v) (tau 0.45 s, mu 300 px/s^2), which stops at a
 * finite time; past a bound it springs back (critically damped, 16/s).  A
 * press while it glides catches it (stops it, and the press should not
 * activate what is under it); a fling within 400 ms of a catch, the same
 * way within 30 degrees, adds the caught speed.  The position is a pure
 * function of the time while it glides, so frames may come unevenly.
 *
 * Positions are logical pixels of content offset (a larger position shows
 * content further right or down); a finger moving down by d moves the
 * position up by d.  Times are CLOCK_MONOTONIC microseconds.
 */
struct kl_scroller;

/* The slowest fling (px/s): a release slower than this only settles. */
#define KL_SCROLLER_FLING_MIN	300.0

/*
 * Creates a scroller at position 0 with bounds 0..0 on both axes (it
 * scrolls nowhere until kl_scroller_set_bounds).
 *
 * Returns NULL when memory is short.
 */
struct kl_scroller *kl_scroller_create(void);

/* Destroys a scroller. */
void kl_scroller_destroy(struct kl_scroller *scroller);

/*
 * Sets the bounds of the position on each axis and the viewport's size (the
 * rubber band's scale).  An axis whose maximum equals its minimum does not
 * scroll.  A position left outside the new bounds springs back.  Refuses a
 * maximum below its minimum or a viewport not above zero with EINVAL.
 */
int kl_scroller_set_bounds(struct kl_scroller *scroller, double minimum_x, double maximum_x, double minimum_y,
    double maximum_y, double viewport_width, double viewport_height);

/* Moves the position at once (clamped to the bounds), stopping any motion. */
void kl_scroller_set_position(struct kl_scroller *scroller, double x, double y);

/*
 * A finger touches: starts a drag from the current position.  Returns 1
 * when it caught moving content (the touch should not tap), otherwise 0.
 */
int kl_scroller_press(struct kl_scroller *scroller, uint64_t now_us);

/*
 * The finger has moved by dx, dy since the press (the total, not a step;
 * kl_gesture_drag_offset gives it resampled for the frame).  The first
 * time it has moved 8 px, a drag within 22.5 degrees of one axis locks the
 * other (on a scroller that scrolls both ways).
 */
void kl_scroller_drag(struct kl_scroller *scroller, double dx, double dy);

/*
 * The finger lifts with a velocity (px/s, as the finger moved;
 * kl_motion_velocity or the gesture's DRAG_END gives it): a fling
 * when it is fast enough, otherwise the content settles.
 */
void kl_scroller_release(struct kl_scroller *scroller, uint64_t now_us, double vx, double vy);

/* The touch was taken away (wl_touch.cancel): no fling; content past a bound springs back. */
void kl_scroller_cancel(struct kl_scroller *scroller, uint64_t now_us);

/*
 * Gives the position to draw at a time (past a bound, as the rubber band
 * shows it).  Returns 1 while the content moves by itself (keep drawing
 * frames), 0 when it rests or follows a finger.
 */
int kl_scroller_step(struct kl_scroller *scroller, uint64_t now_us, double *x, double *y);

/*
 * The gestures of a touch surface (WS081 p005, design section 5.6): what
 * the fingers of one wl_touch surface mean, so that a tap, a long press and
 * a drag are told apart the same way in every program.
 *
 * The program passes the surface's wl_touch events on (in surface-local
 * pixels, with the event's time and the time it read the event, both in
 * microseconds) and reads the gestures with kl_gesture_next, which
 * also keeps the time (a long press is found by the clock, so the program
 * calls it at least every frame while a finger is down).
 *
 * - TAP: a finger lifted within 8 px of where it touched, before 500 ms.
 *   DOUBLE_TAP follows a TAP within 300 ms and 16 px of the last TAP.
 * - LONG_PRESS: a finger held within 8 px for 500 ms (a context menu, or
 *   the start of a selection); the lift then taps nothing.
 * - DRAG_BEGIN: the fingers moved 8 px (after_long_press says whether a
 *   long press came first); while dragging, kl_gesture_drag_offset
 *   gives the fingers' centroid's movement since, resampled for a frame.
 *   DRAG_END: the last finger lifted, with its velocity (for a fling).
 * - Two fingers: the drag follows their centroid (a finger added or
 *   lifted does not make it jump), and kl_gesture_pinch gives the
 *   change of their distance since the second touched.
 * - CANCEL: kl_gesture_cancel was called (wl_touch.cancel): whatever
 *   was going on ends without its lift.
 */
struct kl_gesture;

#define KL_GESTURE_TAP		1U
#define KL_GESTURE_DOUBLE_TAP	2U
#define KL_GESTURE_LONG_PRESS	3U
#define KL_GESTURE_DRAG_BEGIN	4U
#define KL_GESTURE_DRAG_END	5U
#define KL_GESTURE_CANCEL		6U

/*
 * One gesture: its kind (KL_GESTURE_*), where (the touch's point for a
 * tap or a long press, the centroid where a drag began), the velocity of a
 * DRAG_END (px/s), the fingers down, and for a DRAG_BEGIN whether a long
 * press came first.
 */
struct kl_gesture_event {
	unsigned kind;
	double x;
	double y;
	double vx;
	double vy;
	unsigned fingers;
	int after_long_press;
};

/*
 * Creates the gestures of one surface, with a touch motion device of its
 * own (the seat's touch screen as the program sees it).
 *
 * Returns NULL when memory is short.
 */
struct kl_gesture *kl_gesture_create(void);

/* Destroys the gestures of a surface. */
void kl_gesture_destroy(struct kl_gesture *gesture);

/*
 * A finger touches (wl_touch.down): its id, the event's time and the time
 * the program read it, and its place.  Refuses a sixth finger, and an id
 * already down, with EBUSY and EEXIST.
 */
int kl_gesture_down(struct kl_gesture *gesture, int32_t id, uint64_t time_us, uint64_t arrival_us, double x,
    double y);

/* A finger moves (wl_touch.motion).  Refuses an id that is not down with ENOENT. */
int kl_gesture_motion(struct kl_gesture *gesture, int32_t id, uint64_t time_us, uint64_t arrival_us, double x,
    double y);

/* A finger lifts (wl_touch.up).  Refuses an id that is not down with ENOENT. */
int kl_gesture_up(struct kl_gesture *gesture, int32_t id, uint64_t time_us);

/* The compositor took the fingers (wl_touch.cancel). */
void kl_gesture_cancel(struct kl_gesture *gesture);

/*
 * Takes the next gesture, after judging the time now (a long press).
 * Returns 1 with a gesture in *event, 0 when there is none.
 */
int kl_gesture_next(struct kl_gesture *gesture, uint64_t now_us, struct kl_gesture_event *event);

/*
 * Gives how far the dragging fingers' centroid has moved since the drag
 * began, resampled for a frame drawn at now_us.  Returns ENOENT when no
 * drag is going on.
 */
int kl_gesture_drag_offset(struct kl_gesture *gesture, uint64_t now_us, double *dx, double *dy);

/*
 * Gives the ratio of two fingers' distance to their distance when the
 * second touched, and their centroid, for a frame drawn at now_us.
 * Returns ENOENT unless two fingers are down.
 */
int kl_gesture_pinch(struct kl_gesture *gesture, uint64_t now_us, double *scale, double *x, double *y);

/*
 * The file chooser of KL_VERSION 12 (ws092-p003) moved to the widgets
 * with KL_VERSION 16 (ws090-p006): kui_file_chooser_* in <keiui.h>,
 * made of the desktop's widgets.  keiland_file_chooser_* is gone.
 */

/*
 * The desktop's preferences (ws089-p007, KL_VERSION 13) are gone: the
 * desktop's settings are kl_settings_* below (WS135), and desktop.conf is
 * the compositor's own file.
 */

/*
 * The desktop surface (KL_VERSION 14, ws094-p003).
 *
 * zdesktop starts the program that shows the icons of ~/Desktop with a
 * token in its environment (KEILAND_DESKTOP_TOKEN); with the token, the
 * program's surface lies over the wallpaper and under every window, on
 * every virtual desktop, has no window of its own and hears the pointer,
 * the keyboard, the touch screen and drag and drop where no window is.
 * The program copies the token and takes it out of its environment before
 * it starts anything, so that no program it starts can take the role.
 */
struct kl_desktop;

/*
 * What the desktop surface hears: configure, the place on the output
 * (x, y) and the size the surface is to have; the program acknowledges it
 * (kl_desktop_ack) and draws at that size.
 */
struct kl_desktop_listener {
	void (*configure)(void *data, struct kl_desktop *desktop, uint32_t serial, int32_t x, int32_t y, int32_t width, int32_t height);
};

/*
 * Gives a surface the desktop's role with the token.  Returns NULL with
 * errno set: EINVAL without a token, ENOTSUP for a compositor without the
 * desktop, ENOMEM.  A token the compositor does not know ends the
 * connection.
 */
struct kl_desktop *kl_desktop_create(struct wl_display *display, struct wl_surface *surface, const char *token, const struct kl_desktop_listener *listener, void *data);

/*
 * Acknowledges a configure: the next commit is drawn for it.
 */
void kl_desktop_ack(struct kl_desktop *desktop, uint32_t serial);

/*
 * Gives the desktop's role up.
 */
void kl_desktop_destroy(struct kl_desktop *desktop);

/*
 * The keyboard inset (KL_VERSION 18, ws102-p015).
 *
 * A window hears how much of it the on-screen keyboard covers, in the
 * window's pixels from its right edge (the flick panel's column) and from
 * its bottom edge (the QWERTY row), when the keyboard opens, closes or
 * changes the window's size or place (before that configure).  The window
 * may then keep what matters -- the caret -- where it can be seen.  Both
 * are 0 when the keyboard has closed or does not cover the window.
 */
struct kl_keyboard_inset;
struct xdg_toplevel;

/* Why the inset changed: no keyboard, the right column's, the bottom row's. */
#define KL_KEYBOARD_INSET_NONE	0U
#define KL_KEYBOARD_INSET_RIGHT	1U
#define KL_KEYBOARD_INSET_BOTTOM	2U

/* The application's callback: the covered widths from the right and bottom edges, and the reason. */
typedef void (*kl_keyboard_inset_fn)(void *data, int32_t right, int32_t bottom, uint32_t reason);

/*
 * Asks for a window's keyboard inset; callback runs on the application's
 * default queue.  Returns NULL with errno set: ENOTSUP for a compositor
 * without it (nothing more to do), EINVAL, ENOMEM.
 */
struct kl_keyboard_inset *kl_keyboard_inset_create(struct wl_display *display, struct xdg_toplevel *toplevel, kl_keyboard_inset_fn callback, void *data);

/*
 * Stops hearing the keyboard.
 */
void kl_keyboard_inset_destroy(struct kl_keyboard_inset *inset);

/*
 * The editing operations (KL_VERSION 19, ws102-p017).
 *
 * A window says which editing operations it carries out and its state --
 * whether it has a selection, something to paste, something to undo or to
 * redo, whether a selection is being made -- and hears the operations the
 * on-screen keyboard's buttons ask for (the buttons are grey when the
 * state rules one out).  A window without it is sent the keys instead
 * (Ctrl+C, X, V, Z, Y, A).
 */
struct kl_edit;

/* The operations, as the callback hears them; a window's operations are the bits 1 << operation. */
#define KL_EDIT_COPY		0U
#define KL_EDIT_CUT		1U
#define KL_EDIT_PASTE		2U
#define KL_EDIT_UNDO		3U
#define KL_EDIT_REDO		4U
#define KL_EDIT_SELECT_ALL		5U
#define KL_EDIT_SELECT_BEGIN	6U
#define KL_EDIT_SELECT_END		7U

/* The state's bits. */
#define KL_EDIT_HAS_SELECTION	1U
#define KL_EDIT_CAN_PASTE		2U
#define KL_EDIT_CAN_UNDO		4U
#define KL_EDIT_CAN_REDO		8U
#define KL_EDIT_SELECTING		16U

/* The application's callback: the operation asked for. */
typedef void (*kl_edit_fn)(void *data, uint32_t operation);

/*
 * Asks for a window's edit object; callback runs on the application's
 * default queue.  Returns NULL with errno set: ENOTSUP for a compositor
 * without it, EINVAL, ENOMEM.
 */
struct kl_edit *kl_edit_create(struct wl_display *display, struct xdg_toplevel *toplevel, kl_edit_fn callback, void *data);

/*
 * Says the operations the window carries out (bits 1 << KL_EDIT_*) and
 * its state (KL_EDIT_HAS_SELECTION ...); an unchanged pair is not sent.
 */
void kl_edit_set_state(struct kl_edit *edit, uint32_t operations, uint32_t state);

/*
 * Stops taking operations.
 */
void kl_edit_destroy(struct kl_edit *edit);

/*
 * The desktop's settings (WS135, plan/ws135/design.md section 3): every
 * application reads, changes and watches them here, and opens no settings
 * file itself.  Each key is resolved where it lives: the compositor's
 * (the wallpaper, the windows' opacity, the pointer, the keyboards'
 * repeat, the sound) through Keiland's system extension, an application's
 * own (terminal.*) in its file under ~/.config/keiland.  The application
 * cannot tell the two apart.
 *
 * Changes are watched, not polled: the compositor tells every client each
 * change, and kl_settings_dispatch, called after the display's events are
 * read, runs the watches.  One thread uses one kl_settings.
 */

/* The longest key and value, with their NUL. */
#define KL_SETTINGS_KEY_MAX	64U
#define KL_SETTINGS_VALUE_MAX	256U

/* A value the user did not choose: its resolver's default. */
#define KL_SETTINGS_DEFAULT	0x1U

/*
 * One application's view of the desktop's settings: the compositor's
 * values it was told, its own file's, its watches and its requests not
 * answered yet.  It lives from kl_settings_open to kl_settings_close.
 */
struct kl_settings;

/*
 * Called from kl_settings_dispatch for a key whose value changed; value is
 * NULL when the key has none now (not reported yet, or the compositor
 * went).
 */
typedef void (*kl_settings_watch_fn)(void *data, const char *key, const char *value, unsigned flags);

/*
 * Opens the settings on a display (app names the application's own
 * settings, "terminal" for terminal.*; NULL for none).  It waits once for
 * the compositor's settings.  Returns NULL with errno ENOMEM; without
 * Keiland's extension it opens all the same and the compositor's keys
 * answer ENOTSUP.
 */
struct kl_settings *kl_settings_open(struct wl_display *display, const char *app);

/*
 * Closes the settings.
 */
void kl_settings_close(struct kl_settings *settings);

/*
 * Copies a key's value and its flags (flags may be NULL).  Returns 0,
 * ENOENT for a key the desktop does not have, ENOTSUP for a compositor's
 * key without the compositor's extension, EAGAIN while it is not reported
 * yet (the sound before audiod), or ERANGE when it does not fit.
 */
int kl_settings_get(const struct kl_settings *settings, const char *key, char *value, size_t size, unsigned *flags);

/*
 * Reports a key's value as a whole number; fallback when it is not known
 * or not a number.
 */
int kl_settings_get_int(const struct kl_settings *settings, const char *key, int fallback);

/*
 * Asks for a key to take a value; request (may be NULL) names the answer
 * kl_settings_take_result gives.  The value comes back as a change.
 * Returns 0 when asked, ENOENT, ENOTSUP, EPERM (a key only reported),
 * EINVAL (a value outside the key's type or range), or an errno value of
 * the application's file.
 */
int kl_settings_set(struct kl_settings *settings, const char *key, const char *value, uint32_t *request);

/*
 * Asks for a key to take a whole number, as kl_settings_set.
 */
int kl_settings_set_int(struct kl_settings *settings, const char *key, int value, uint32_t *request);

/*
 * Asks for a key to go back to its default, as kl_settings_set.
 */
int kl_settings_reset(struct kl_settings *settings, const char *key, uint32_t *request);

/*
 * Watches the keys that start with prefix ("" for every key); *watch (may
 * be NULL) names the watch for kl_settings_unwatch.  Returns 0, EINVAL or
 * ENOMEM.
 */
int kl_settings_watch(struct kl_settings *settings, const char *prefix, kl_settings_watch_fn fn, void *data, unsigned *watch);

/*
 * Stops a watch (also from within a watch's callback).
 */
void kl_settings_unwatch(struct kl_settings *settings, unsigned watch);

/*
 * Takes the compositor's events the display has read, then runs the
 * watches of the keys that changed.  It never waits.  Returns 0, or EPIPE
 * once the compositor went (its keys answer ENOTSUP from then on).
 */
int kl_settings_dispatch(struct kl_settings *settings);

/*
 * Takes one finished request: 1 with its number and its error (0, EPERM,
 * ENOTSUP, EBUSY, EINVAL, ENODEV, EIO), 0 when none is finished.
 */
int kl_settings_take_result(struct kl_settings *settings, uint32_t *request, int *error);

/*
 * The desktop's system for applications (WS131 p010, plan/ws131/design.md
 * section 4.4): the network, the sound, the power and the removable
 * devices, as the compositor holds them through Keiland's system
 * extension.  An application asks the compositor and never reaches a
 * daemon or the operating system itself.
 *
 * The state is the compositor's, told as one state at a time: an
 * application sees the state before a change or after it, never half of
 * it.  A request is answered once, by a result kl_system_take_result
 * gives; a change it makes comes as a new state.  One network request is
 * outstanding at a time, the system bar's included; another is answered
 * EBUSY.  One thread uses one kl_system.
 */

/* The longest SSID, interface name and dotted IPv4 address, with their NULs. */
#define KL_NETWORK_SSID_MAX	33U
#define KL_NETWORK_NAME_MAX	16U
#define KL_NETWORK_ADDRESS_MAX	16U

/* The most networks a scan, interfaces, DNS servers and saved networks a kl_system keeps. */
#define KL_NETWORK_SCAN_MAX	24U
#define KL_NETWORK_LINKS_MAX	16U
#define KL_NETWORK_DNS_MAX	4U
#define KL_NETWORK_SAVED_MAX	24U

/* A WPA key's length. */
#define KL_NETWORK_KEY_MIN	8U
#define KL_NETWORK_KEY_MAX	63U

/* What carries the connection. */
#define KL_NETWORK_NONE		0U
#define KL_NETWORK_WIRED	1U
#define KL_NETWORK_WIFI		2U

/* The Wi-Fi's state. */
#define KL_WIFI_ABSENT		0U	/* no radio */
#define KL_WIFI_OFF		1U
#define KL_WIFI_SEARCHING	2U
#define KL_WIFI_CONNECTING	3U
#define KL_WIFI_CONNECTED	4U
#define KL_WIFI_DISCONNECTED	5U	/* on, and left unconnected by the user */

/* The network's requests (kl_system_network_request). */
#define KL_NETWORK_SCAN		1U
#define KL_NETWORK_JOIN		2U	/* names the network; its key must be saved */
#define KL_NETWORK_DISCONNECT	3U
#define KL_NETWORK_WIFI_ON	4U
#define KL_NETWORK_WIFI_OFF	5U

/* The power's actions, their bits in the state's actions, and where the power comes from. */
#define KL_POWER_POWEROFF	1U
#define KL_POWER_REBOOT		2U
#define KL_POWER_SUSPEND	3U
#define KL_POWER_ACTION_BIT(action)	(1U << (action))
#define KL_POWER_SOURCE_UNKNOWN	0U
#define KL_POWER_SOURCE_AC	1U
#define KL_POWER_SOURCE_BATTERY	2U

/* The longest device ID, name and location, with their NULs, and the most devices kept. */
#define KL_DEVICE_TEXT_MAX	64U
#define KL_DEVICES_MAX		16U

/* What the compositor offers (kl_system_capabilities). */
#define KL_SYSTEM_HAS_NETWORK	0x2U
#define KL_SYSTEM_HAS_AUDIO	0x4U
#define KL_SYSTEM_HAS_POWER	0x8U
#define KL_SYSTEM_HAS_DEVICES	0x10U
#define KL_SYSTEM_HAS_MONITOR	0x20U	/* kl_system_monitor_open (WS134 p012) */
#define KL_SYSTEM_HAS_ACCOUNT	0x40U	/* kl_system_account_set_password (KL_VERSION 27, ws160-p002) */
#define KL_SYSTEM_HAS_SHARING	0x80U	/* kl_system_sharing_* (KL_VERSION 30, ws089-p025) */
#define KL_SYSTEM_HAS_ADMINISTER	0x100U	/* kl_system_account_administer (KL_VERSION 31, ws089-p026) */
#define KL_SYSTEM_HAS_PIN	0x200U	/* kl_system_account_set_pin (KL_VERSION 34, ws163-p003) */

/* What a kl_system_dispatch found changed. */
#define KL_SYSTEM_CHANGED_NETWORK	0x1U	/* the network's state */
#define KL_SYSTEM_CHANGED_SCAN		0x2U	/* the networks of the scan */
#define KL_SYSTEM_CHANGED_DETAILS	0x4U	/* the interfaces, DNS servers and saved networks asked for */
#define KL_SYSTEM_CHANGED_AUDIO		0x8U
#define KL_SYSTEM_CHANGED_POWER		0x10U
#define KL_SYSTEM_CHANGED_DEVICES	0x20U
#define KL_SYSTEM_CHANGED_RESULT	0x40U	/* a request was answered */
#define KL_SYSTEM_CHANGED_SHARING	0x80U	/* Remote Login's state (KL_VERSION 30) */
#define KL_SYSTEM_CHANGED_ENROLLED	0x100U	/* the user's PIN and security keys (KL_VERSION 36) */

/*
 * The network: whether the daemon is reached, whether the machine is
 * connected and through what and which interface, the wired interface up
 * with an address (empty when none), and the Wi-Fi's state, interface and
 * the network it is on or joining.
 */
struct kl_network_state {
	unsigned reachable;
	unsigned connected;
	unsigned kind;
	char interface[KL_NETWORK_NAME_MAX];
	char wired[KL_NETWORK_NAME_MAX];
	unsigned wifi;
	char wifi_interface[KL_NETWORK_NAME_MAX];
	char ssid[KL_NETWORK_SSID_MAX];
};

/* One network a scan found: its SSID, its signal in dBm, and whether it asks for a key. */
struct kl_network_ap {
	char ssid[KL_NETWORK_SSID_MAX];
	int rssi;
	unsigned secured;
};

/*
 * One interface: its name, whether it is up, has its link and is the
 * loopback, its IPv4 address and netmask (empty when none), its hardware
 * address as text, its MTU, and the bytes it has received and sent; for a
 * wired one (KL_VERSION 29) how it is configured (KL_WIRED_*, unknown for
 * any other or an older compositor) and the router it was given; and
 * (KL_VERSION 39) its link's speed in Mb/s, 0 while it is not known (or
 * from an older compositor).
 */
struct kl_network_link {
	char name[KL_NETWORK_NAME_MAX];
	unsigned up;
	unsigned running;
	unsigned loopback;
	char address[KL_NETWORK_ADDRESS_MAX];
	char netmask[KL_NETWORK_ADDRESS_MAX];
	char hardware[18];
	unsigned mtu;
	uint64_t received_bytes;
	uint64_t sent_bytes;
	unsigned wired_mode;
	char router[KL_NETWORK_ADDRESS_MAX];
	unsigned link_mbps;
};

/* How a wired interface is configured (KL_VERSION 29). */
#define KL_WIRED_UNKNOWN	0U
#define KL_WIRED_DHCP		1U
#define KL_WIRED_STATIC		2U

/*
 * A wired interface's configuration asked for (KL_VERSION 29): its name,
 * DHCP or a static IPv4 address (KL_WIRED_*), the address, netmask and
 * router of a static one (empty otherwise; the router may be empty), and
 * up to two DNS servers (empty: the servers DHCP gives).
 */
struct kl_network_wired_config {
	char interface[KL_NETWORK_NAME_MAX];
	unsigned mode;
	char address[KL_NETWORK_ADDRESS_MAX];
	char netmask[KL_NETWORK_ADDRESS_MAX];
	char router[KL_NETWORK_ADDRESS_MAX];
	char dns[2][KL_NETWORK_ADDRESS_MAX];
};

/*
 * The sound output: whether the sound service is reached and has a
 * device, the device's rate and channels, the volume of each channel
 * (0 to 100) and whether it is muted.
 */
struct kl_audio_state {
	unsigned reachable;
	unsigned device;
	unsigned rate;
	unsigned channels;
	unsigned left;
	unsigned right;
	unsigned muted;
};

/*
 * The power: where it comes from, the battery's charge in percent (-1 when
 * unknown), whether it charges, and the KL_POWER_ACTION_BIT of each action
 * the user may take now.
 */
struct kl_power_state {
	unsigned source;
	int percent;
	unsigned charging;
	unsigned actions;
};

/*
 * One removable device (KL_VERSION 28, ws132-p004: a volume volumed lists):
 * its ID (the disk's name), kind (KL_DEVICE_STORAGE), state (the
 * KL_DEVICE_* bits: mounted; new, inserted and never mounted since), name
 * (the label, or the disk's name) and where it is mounted ("" when not).
 */
#define KL_DEVICE_STORAGE	1U
#define KL_DEVICE_MOUNTED	0x1U
#define KL_DEVICE_NEW		0x2U

struct kl_device {
	char id[KL_DEVICE_TEXT_MAX];
	unsigned kind;
	unsigned state;
	char name[KL_DEVICE_TEXT_MAX];
	char location[KL_DEVICE_TEXT_MAX];
};

/*
 * A removable device's file system ("fat", "ufs"; "" when the compositor
 * did not tell) and size in bytes (0 when not told), KL_VERSION 33
 * (ws132-p009): what a mount's confirmation shows.
 */
#define KL_DEVICE_FS_MAX	8U

struct kl_device_info {
	char fs[KL_DEVICE_FS_MAX];
	uint64_t bytes;
};

/*
 * One application's view of the system: the state the compositor told,
 * and the requests not answered yet.  It lives from kl_system_open to
 * kl_system_close.
 */
struct kl_system;

/*
 * Opens the system on a display and waits once for its first state.
 * Returns NULL with errno ENOTSUP without Keiland's system extension (not
 * Keiland, or another user's compositor), EPIPE when the compositor went,
 * or ENOMEM.
 */
struct kl_system *kl_system_open(struct wl_display *display);

/*
 * Closes the system.
 */
void kl_system_close(struct kl_system *system);

/*
 * Takes the compositor's events the display has read; *changed (may be
 * NULL) has the KL_SYSTEM_CHANGED_* bits of what changed since the last
 * dispatch.  It never waits.  Returns 0, or EPIPE once the compositor went.
 */
int kl_system_dispatch(struct kl_system *system, unsigned *changed);

/*
 * Reports the KL_SYSTEM_HAS_* bits of what the compositor offers.
 */
unsigned kl_system_capabilities(const struct kl_system *system);

/*
 * Takes one answered request: 1 with its number and its error (0, EPERM,
 * ENOTSUP, EBUSY, EINVAL, ENODEV, EIO; a join's and a save_key's own
 * ENOENT: no key saved, EACCES: the network refused the key, ENETUNREACH:
 * the network is out of reach), 0 when none is answered.
 */
int kl_system_take_result(struct kl_system *system, uint32_t *request, int *error);

/*
 * Copies the network's state.
 */
void kl_system_network_get_state(const struct kl_system *system, struct kl_network_state *state);

/*
 * Copies up to capacity networks of the last scan, the strongest first,
 * and returns how many were copied.
 */
size_t kl_system_network_get_scan(const struct kl_system *system, struct kl_network_ap *aps, size_t capacity);

/*
 * Asks for a network request (KL_NETWORK_*; a join names the SSID, the
 * others take NULL); request (may be NULL) names its answer.  Returns 0
 * when asked, ENOTSUP, or EINVAL.
 */
int kl_system_network_request(struct kl_system *system, unsigned what, const char *ssid, uint32_t *request);

/*
 * Asks for a Wi-Fi network's key to be saved in the user's store and the
 * network joined, answered once it is joined or it failed.  Returns 0 when
 * asked, ENOTSUP, or EINVAL (an SSID or key outside its bounds).  The key
 * is not kept.
 */
int kl_system_network_save_key(struct kl_system *system, const char *ssid, const char *key, uint32_t *request);

/*
 * Asks for the network's details: the interfaces, the DNS servers and the
 * saved networks come (KL_SYSTEM_CHANGED_DETAILS) before the answer.  It
 * is no request of the network daemon's and is asked alongside one.
 * Returns 0 when asked, or ENOTSUP.
 */
int kl_system_network_query_details(struct kl_system *system, uint32_t *request);

/*
 * Asks the compositor to keep the radios scanning (on 1) while the
 * application shows the networks around, and no longer (on 0) when it
 * stops showing them (ws089-p021).  It is no request and has no answer: a
 * new scan comes as KL_SYSTEM_CHANGED_SCAN.  The compositor counts every
 * application that asked (and its own Wi-Fi menu), and an application that
 * closes its system or ends asks no longer.  One asking holds a minute: an
 * application that shows the networks longer asks again (every 30 seconds,
 * say), so that one that hangs does not keep the radios scanning.  Returns
 * 0, or ENOTSUP when
 * the compositor does not know it (one older than KL_VERSION 24).
 */
int kl_system_network_set_scanning(struct kl_system *system, unsigned on);

/*
 * Asks for a wired interface to be configured (KL_VERSION 29, ws089-p022):
 * DHCP or a static IPv4 address, with its router and DNS servers; the
 * network daemon keeps it for the next start too.  request (may be NULL)
 * names its answer, which comes once it is applied or refused.  Returns 0
 * when asked, ENOTSUP (an older compositor), or EINVAL (a field that is
 * too long, or a mode that is neither).
 */
int kl_system_network_configure_wired(struct kl_system *system, const struct kl_network_wired_config *config, uint32_t *request);

/*
 * Remote Login (KL_VERSION 30, ws089-p025): whether the system has it,
 * whether it starts with the system and runs now, its port, whether this
 * user may change it (root or a member of wheel), whether the state was
 * ever read, and the host key's fingerprint ("SHA256:...", or empty).
 */
#define KL_SHARING_FINGERPRINT_MAX	64U
struct kl_sharing_state {
	unsigned available;
	unsigned enabled;
	unsigned running;
	unsigned port;
	unsigned allowed;
	char fingerprint[KL_SHARING_FINGERPRINT_MAX];
};

/* Copies Remote Login's state (all zero without KL_SYSTEM_HAS_SHARING). */
void kl_system_sharing_get_state(const struct kl_system *system, struct kl_sharing_state *state);

/*
 * Turns Remote Login on (on 1) or off (0), now and at every start; the
 * answer names request and comes after the new state.  Returns 0 when
 * asked, or ENOTSUP without KL_SYSTEM_HAS_SHARING.  The answer is EPERM
 * for a user who may not.
 */
int kl_system_sharing_set_ssh(struct kl_system *system, unsigned on, uint32_t *request);

/* Reads Remote Login's state again (KL_SYSTEM_CHANGED_SHARING follows).  Returns 0 or ENOTSUP. */
int kl_system_sharing_query(struct kl_system *system, uint32_t *request);

/*
 * Copy up to capacity of the details last asked for and return how many
 * were copied.
 */
size_t kl_system_network_get_links(const struct kl_system *system, struct kl_network_link *links, size_t capacity);
size_t kl_system_network_get_dns(const struct kl_system *system, char (*servers)[KL_NETWORK_ADDRESS_MAX], size_t capacity);
size_t kl_system_network_get_saved(const struct kl_system *system, char (*ssids)[KL_NETWORK_SSID_MAX], size_t capacity);

/*
 * Copies the sound output's state.
 */
void kl_system_audio_get_state(const struct kl_system *system, struct kl_audio_state *state);

/*
 * Asks for each channel's volume (0 to 100) and the mute.  Returns 0 when
 * asked, ENOTSUP, or EINVAL.
 */
int kl_system_audio_set_volume(struct kl_system *system, unsigned left, unsigned right, unsigned muted, uint32_t *request);

/*
 * Asks for the short feedback sound at the device volume.  Returns 0 when
 * asked, or ENOTSUP.
 */
int kl_system_audio_feedback(struct kl_system *system, uint32_t *request);

/*
 * Copies the power's state.
 */
void kl_system_power_get_state(const struct kl_system *system, struct kl_power_state *state);

/*
 * Asks for a power action (KL_POWER_*); one not in the state's actions is
 * answered ENOTSUP.  Returns 0 when asked, ENOTSUP, or EINVAL.
 */
int kl_system_power_action(struct kl_system *system, unsigned action, uint32_t *request);

/*
 * Copies up to capacity removable devices and returns how many were
 * copied.
 */
size_t kl_system_devices_get(const struct kl_system *system, struct kl_device *devices, size_t capacity);

/*
 * Copies a listed device's file system and size: 1 when the device is in
 * the list (its info is empty from a compositor older than version 9), 0
 * when it is not.
 */
int kl_system_devices_info(const struct kl_system *system, const char *id, struct kl_device_info *info);

/*
 * Asks for a removable device to be ejected (unmounted; a device that is not
 * mounted may simply be taken out), answered by a result: 0, EBUSY while a
 * program uses it (kl_system_devices_busy_program names it), EACCES for a
 * user who is not the session's.  Returns 0 when asked, ENOTSUP, or EINVAL.
 */
int kl_system_devices_eject(struct kl_system *system, const char *id, uint32_t *request);

/*
 * Asks for a removable device to be mounted under /media (KL_VERSION 28,
 * ws132-p004), answered by a result: 0 (its location then comes with the
 * devices' change), EACCES, or another errno value.  Returns 0 when asked,
 * ENOTSUP (a compositor without it), or EINVAL.
 */
int kl_system_devices_mount(struct kl_system *system, const char *id, uint32_t *request);

/*
 * Copies the program that keeps a device from being ejected, for a request
 * answered EBUSY (KL_VERSION 28).  Returns 1 with it, 0 without one.
 */
int kl_system_devices_busy_program(const struct kl_system *system, uint32_t request, char *program, size_t size);

/*
 * Asks for the password of the user the desktop runs as to be changed
 * from current to fresh (KL_VERSION 27, ws160-p002), answered by a result
 * (kl_system_take_result): 0, EPERM when current is wrong, EINVAL when
 * fresh breaks the system's rules (on zedBSD: at least 8 characters, not
 * the old one), ENOTSUP where the desktop cannot change it, EBUSY while
 * another change is under way, EIO when it failed.
 * Returns 0 when asked, ENOTSUP without KL_SYSTEM_HAS_ACCOUNT, or EINVAL
 * (an empty password, or one longer than 256 bytes).  The caller wipes
 * its copies; the library keeps none.
 */
int kl_system_account_set_password(struct kl_system *system, const char *current, const char *fresh, uint32_t *request);

/* The longest operation kl_system_account_administer takes (without its NUL), and the room of a refusal's word (with it). */
#define KL_ACCOUNT_OPERATION_MAX	1024U
#define KL_ACCOUNT_REASON_SIZE		32U

/*
 * Asks for an administrator's change of the people's accounts (KL_VERSION
 * 31, ws089-p026; docs/architecture/security.md): password is the
 * caller's own, operation the request's lines after it, each ended by a
 * line end ("add\nNAME\nDISPLAY\nPASSWORD\nadmin|user\n", "remove\nNAME\n
 * keep-home|remove-home\n", "reset-password\nNAME\nPASSWORD\n",
 * "group-add\nNAME\nwheel|network\n", "group-remove\n..."), answered by
 * a result (kl_system_take_result): 0, EPERM when the caller is not an
 * administrator or the password is wrong, EINVAL for another refusal,
 * ENOTSUP, EBUSY while another change is under way, EIO when it failed.
 * A refusal's word comes first (kl_system_account_refusal).  Returns 0
 * when asked, ENOTSUP without KL_SYSTEM_HAS_ADMINISTER, or EINVAL.  The
 * caller wipes its copies; the library keeps none.
 */
int kl_system_account_administer(struct kl_system *system, const char *password, const char *operation, uint32_t *request);

/*
 * Asks for the six-digit PIN of the user the desktop runs as to be set to
 * pin, or removed when pin is empty (KL_VERSION 34, ws163-p003; since
 * ws172-p002 the session manager keeps it, in /etc/passkey on zedBSD),
 * answered by a result (kl_system_take_result): 0, EPERM when current (the
 * user's password) is wrong (the refusal's word, as
 * kl_system_account_refusal gives it, says why: bad-secret, locked, ...),
 * EINVAL when pin is not six digits, ENOTSUP without a session manager,
 * EBUSY while another change is under way, EIO when it failed.  Returns 0
 * when asked, ENOTSUP without KL_SYSTEM_HAS_PIN, or EINVAL.  The caller
 * wipes its copies; the library keeps none.
 */
int kl_system_account_set_pin(struct kl_system *system, const char *current, const char *pin, uint32_t *request);

/*
 * Gives whether the user has a PIN (pin 1) and how many security keys are
 * registered (KL_VERSION 36, ws172-p002), as the compositor last told
 * (KL_SYSTEM_CHANGED_ENROLLED says it changed).  Returns 1 when known, 0
 * before the compositor told it (or one that cannot: both 0).
 */
int kl_system_account_enrolled(const struct kl_system *system, unsigned *pin, unsigned *keys);

/*
 * Copies the word of a refused kl_system_account_administer request
 * (not-administrator, bad-password, no-such-user, name-taken, bad-name,
 * weak-password, last-administrator, self, root, busy, home-exists,
 * bad-request, failed).  Returns 1 with it, 0 without one.
 */
int kl_system_account_refusal(const struct kl_system *system, uint32_t request, char *reason, size_t size);

/*
 * The machine's monitor (WS134 p012, plan/ws134/design.md section 1.3):
 * what the System Monitor shows, sampled by the compositor and made into
 * rates here.  A monitor is opened on a kl_system and its samples come
 * with kl_system_dispatch; kl_system_monitor_take gives the newest frame:
 * the rates over the time since the sample before it (the CPUs' shares,
 * bytes and operations a second, the disks' mean latency, the GPUs'
 * shares) and the present values.  The first frame comes with the second
 * sample.  A device that came again, or a counter that went back, has no
 * rate for that frame.  Close every monitor before its kl_system.
 */

/* The most CPUs, GPUs, disks and links a monitor follows. */
#define KL_MONITOR_CPU_MAX	256U
#define KL_MONITOR_GPU_MAX	4U
#define KL_MONITOR_DISK_MAX	8U
#define KL_MONITOR_LINK_MAX	16U

/* What a frame has (struct kl_monitor_frame's valid). */
#define KL_MONITOR_FRAME_CPU		0x001U
#define KL_MONITOR_FRAME_MEMORY		0x002U
#define KL_MONITOR_FRAME_SWAP		0x004U
#define KL_MONITOR_FRAME_LINKS		0x008U
#define KL_MONITOR_FRAME_DISKS		0x010U
#define KL_MONITOR_FRAME_GPU_BUSY	0x020U
#define KL_MONITOR_FRAME_GPU_MEMORY	0x040U
#define KL_MONITOR_FRAME_GPU_FREQ	0x080U
#define KL_MONITOR_FRAME_TEMPERATURE	0x100U
#define KL_MONITOR_FRAME_POWER		0x200U

/* A disk's kind (struct kl_monitor_info's disk kind). */
#define KL_MONITOR_KIND_OTHER	1U
#define KL_MONITOR_KIND_NVME	2U
#define KL_MONITOR_KIND_USB	3U
#define KL_MONITOR_KIND_UAS	4U
#define KL_MONITOR_KIND_IDE	5U
#define KL_MONITOR_KIND_SDMMC	6U
#define KL_MONITOR_KIND_SCSI	7U

/* One GPU of the info: its id, name and driver. */
struct kl_monitor_gpu_info {
	uint64_t id;
	char name[48];
	char driver[16];
};

/* One disk of the info: its id, name and kind. */
struct kl_monitor_disk_info {
	uint64_t id;
	char name[32];
	unsigned kind;
};

/* One link of the info: its id and name. */
struct kl_monitor_link_info {
	uint64_t id;
	char name[16];
};

/* What does not change from frame to frame: the CPUs, the machine's name, the devices. */
struct kl_monitor_info {
	unsigned cpu_count;
	char host[64];
	unsigned gpu_count;
	unsigned disk_count;
	unsigned link_count;
	struct kl_monitor_gpu_info gpu[KL_MONITOR_GPU_MAX];
	struct kl_monitor_disk_info disk[KL_MONITOR_DISK_MAX];
	struct kl_monitor_link_info link[KL_MONITOR_LINK_MAX];
};

/* One link's bytes a second, received and sent, and whether it is up. */
struct kl_monitor_link_rate {
	uint64_t id;
	double rx_rate;
	double tx_rate;
	unsigned up;
};

/* One disk's bytes and operations a second, its mean latency in milliseconds, and its busy share (0 to 1). */
struct kl_monitor_disk_rate {
	uint64_t id;
	double read_rate;
	double write_rate;
	double read_ops;
	double write_ops;
	double latency_ms;
	double busy;
};

/* One GPU's busy share (0 to 1), its memory, frequencies, temperature and power. */
struct kl_monitor_gpu_rate {
	uint64_t id;
	double busy;
	uint64_t memory_used;
	uint64_t memory_total;
	unsigned cur_mhz;
	unsigned max_mhz;
	int milli_celsius;
	unsigned milli_watts;
};

/*
 * One frame: when (CLOCK_MONOTONIC, ns) and over how many seconds, what it
 * has, the CPUs' busy shares (0 to 1, all and each), the memory in bytes,
 * the links' and the disks' rates (each, and their sums; the disks'
 * latency is the mean over every operation), the GPUs, and the CPU's
 * temperature.
 */
struct kl_monitor_frame {
	uint64_t time_ns;
	double seconds;
	unsigned valid;
	double cpu;
	unsigned cpu_count;
	double cpu_core[KL_MONITOR_CPU_MAX];
	uint64_t memory_total;
	uint64_t memory_free;
	uint64_t memory_cache;
	uint64_t memory_reclaimable;
	uint64_t swap_total;
	uint64_t swap_used;
	unsigned link_count;
	struct kl_monitor_link_rate link[KL_MONITOR_LINK_MAX];
	double rx_rate;
	double tx_rate;
	unsigned disk_count;
	struct kl_monitor_disk_rate disk[KL_MONITOR_DISK_MAX];
	double read_rate;
	double write_rate;
	double disk_latency_ms;
	unsigned gpu_count;
	struct kl_monitor_gpu_rate gpu[KL_MONITOR_GPU_MAX];
	int cpu_milli_celsius;
};

/* A monitor on a kl_system. */
struct kl_system_monitor;

/*
 * Opens a monitor sampled every period_ms (250 to 10000; 0 is 1000).
 * Returns NULL with errno ENOTSUP when the compositor offers none, or
 * ENOMEM.
 */
struct kl_system_monitor *kl_system_monitor_open(struct kl_system *system, unsigned period_ms);

/*
 * Copies the newest frame into *frame: 1 when one came since the last
 * take, 0 when none did (*frame is left as it was).
 */
int kl_system_monitor_take(struct kl_system_monitor *monitor, struct kl_monitor_frame *frame);

/*
 * The info as last told (all zero before the first), and how many times it
 * changed (a new number means the devices changed).
 */
const struct kl_monitor_info *kl_system_monitor_info(const struct kl_system_monitor *monitor, unsigned *changes);

/*
 * Closes a monitor.
 */
void kl_system_monitor_close(struct kl_system_monitor *monitor);

/*
 * One copy of a program, and the activation (ws089-p016).
 *
 * A program that keeps one window (Settings) runs once per user: a second
 * start hands its request -- a line of text the program gives meaning to,
 * such as a page to open -- to the copy that runs, with an activation
 * token, and ends.  The copy that runs takes the request, carries it out
 * and brings its window to the front with the token (kl_activate).  The
 * copies find each other through a socket in the user's runtime directory
 * (XDG_RUNTIME_DIR), which must be the user's own and closed to others.
 *
 * An activation token is a compositor's permission to bring a window to
 * the front (xdg_activation_v1).  A program started by the desktop has one
 * in XDG_ACTIVATION_TOKEN; a program may ask the compositor for one to
 * hand to a program it starts (kl_activation_token).
 */

/* The longest request (its NUL counted), and the longest activation token. */
#define KL_INSTANCE_REQUEST_MAX		256U
#define KL_ACTIVATION_TOKEN_MAX		256U

/* The copy of a program that runs: its socket. */
struct kl_instance;

/*
 * Makes this program the one copy of the name (lowercase letters, digits
 * and '-'), or hands request to the copy that runs.  Returns 0 with
 * *instance set when this is the one copy (it listens for later starts),
 * 0 with *instance NULL when the request was handed over (the program
 * ends), or an error (the program runs on its own, as without the call):
 * EINVAL for a bad name or request, ENOTSUP without a private runtime
 * directory.  The token handed over is one asked of the compositor (which
 * grants it to a program that has just started), or XDG_ACTIVATION_TOKEN's
 * when none can be asked for; the variable is removed either way.
 */
int kl_instance_open(const char *name, const char *request, struct kl_instance **instance);

/*
 * The descriptor that becomes readable when a later start hands a request
 * over (for the program's poll), or -1 for NULL.
 */
int kl_instance_fd(const struct kl_instance *instance);

/*
 * Takes one request handed over: 1 with the request and the token (an
 * empty string when there is none) copied, 0 when none waits.
 */
int kl_instance_take(struct kl_instance *instance, char *request, size_t request_size, char *token, size_t token_size);

/*
 * Stops being the one copy: the socket goes.
 */
void kl_instance_close(struct kl_instance *instance);

/*
 * Asks the compositor for an activation token for a program to be started
 * (app_id, NULL for none), from a surface the user works in (NULL for
 * none).  Returns 0 with the token copied, ENOTSUP when the compositor has
 * no activation, or another error.
 */
int kl_activation_token(struct wl_display *display, struct wl_surface *surface, const char *app_id, char *token, size_t size);

/*
 * Brings a surface's window to the front with an activation token (the
 * compositor decides whether the token allows it).  Returns 0 when it was
 * asked, ENOTSUP when the compositor has no activation.
 */
int kl_activate(struct wl_display *display, struct wl_surface *surface, const char *token);

/*
 * The desktop's appearance (KL_VERSION 35, ws089-p017): light or dark, as
 * the user chose in Settings.  The compositor tells it when the program
 * asks and again whenever it changes (keiland_theme_v1); a program draws
 * in it and draws again when it changes.  kl_theme_default gives the
 * theme of the appearance the program was told last (the same pointer,
 * its contents changed), and an application of kl_app_open is told with
 * a KL_APP_THEME event.  A compositor without the appearance is light.
 */
#define KL_APPEARANCE_LIGHT	0U
#define KL_APPEARANCE_DARK	1U

/* The appearance watched on one connection. */
struct kl_appearance;

/* Called with the new appearance (KL_APPEARANCE_*) when it changes, within the dispatch of the display's default queue. */
typedef void (*kl_appearance_fn)(void *data, unsigned appearance);

/*
 * Watches the appearance on a display: learns it now (with a roundtrip
 * of the library's own queue, which runs none of the program's events)
 * and calls changed (NULL for none) whenever it changes.  Returns 0 with
 * *appearance set, ENOTSUP when the compositor has no appearance (the
 * program is light), ENOMEM or EPROTO.
 */
int kl_appearance_open(struct wl_display *display, kl_appearance_fn changed, void *data, struct kl_appearance **appearance);

/*
 * The appearance (KL_APPEARANCE_*) a watch was told last; for NULL, the
 * one the program was told last on any watch (light before any).
 */
unsigned kl_appearance_get(const struct kl_appearance *appearance);

/*
 * Stops watching (NULL does nothing).
 */
void kl_appearance_close(struct kl_appearance *appearance);

/*
 * The translations of the user interface's text (WS158).  The English
 * text in the source is the key: kl_tr("Open") gives the text of the
 * language chosen, or "Open" itself when the catalog has none.  The
 * catalogs are UTF-8 text files, KEILAND_DATADIR/keiland/locale/LANGUAGE/
 * DOMAIN.tr, one for each program (its domain) and one the desktop's
 * programs share ("keiland").  The language is the desktop's setting
 * ui.language (0 English, 1 Japanese), which kl_tr_follow watches, so
 * that a program changes its language while it runs.  One thread uses
 * them; a text they give lives until the language or the domain changes.
 */

/* The domain the desktop's programs share, looked in after a program's own. */
#define KL_TR_SHARED_DOMAIN	"keiland"

/* The longest name of a language or a domain. */
#define KL_TR_NAME_MAX		31U

/*
 * Called by kl_tr_follow after the language changed and its catalogs were
 * read: the program draws its text again.  language is "en", "ja", ...
 */
typedef void (*kl_tr_changed_fn)(void *data, const char *language);

/*
 * Reads a program's catalogs (domain and KL_TR_SHARED_DOMAIN) in a
 * language from the installed directory.  English needs none.  A catalog
 * that is not there leaves its texts in English.  Returns 0, EINVAL for a
 * name that is not lower-case letters, digits, '-' and '_', or ENOMEM; the
 * texts are English after a failure.
 */
int kl_tr_open(const char *domain, const char *language);

/*
 * Reads them from another directory, which holds LANGUAGE/DOMAIN.tr (a
 * program installed elsewhere, or a test).  Returns as kl_tr_open.
 */
int kl_tr_open_directory(const char *directory, const char *domain, const char *language);

/*
 * Forgets the catalogs: every text is English again.
 */
void kl_tr_close(void);

/*
 * Reports the language read ("en" before any).
 */
const char *kl_tr_language(void);

/*
 * Reports the language a value of ui.language names ("en" for 0, "ja"
 * for 1), or NULL for a value no language has.
 */
const char *kl_tr_language_code(int setting);

/*
 * Gives the text of the language for an English text.
 */
const char *kl_tr(const char *english);

/*
 * Gives the text for an English text in a context, for an English word
 * that is translated in more than one way ("Open" a verb or an adjective).
 */
const char *kl_trc(const char *context, const char *english);

/*
 * Gives the text for a number of things: the singular or the plural in
 * English, and the form the language takes for count.  The number itself
 * goes in with kl_tr_format.
 */
const char *kl_trn(const char *singular, const char *plural, unsigned long count);

/*
 * Writes a text of the language with its places filled: {1} to {9} are
 * the strings after pattern, in order, which a NULL ends; {{ is a brace.
 * The places can come in any order, so that a language puts the words
 * where its grammar wants them.  out always ends with a NUL.  Returns 0,
 * ERANGE when the text was cut to fit, or EINVAL for a place without its
 * string.
 */
int kl_tr_format(char *out, size_t size, const char *pattern, ...);

/*
 * Follows the desktop's language: reads the program's catalogs in the
 * language of ui.language now, and again whenever it changes, calling
 * changed (may be NULL) after each change.  The watch runs from
 * kl_settings_dispatch, and ends with the settings.  Returns 0, EINVAL or
 * ENOMEM.
 */
int kl_tr_follow(struct kl_settings *settings, const char *domain, kl_tr_changed_fn changed, void *data);

#ifdef __cplusplus
}
#endif

/* The desktop's widgets and controls (WS090; part of libkeiland since WS131 p012). */
#include <keiland-ui.h>

/*
 * The old names of the library (keiland_, KEILAND_ until WS131 p014), each a macro of the
 * name it has now: a program written with them builds unchanged.  A program that defines
 * KL_COMPAT before it includes this header sees only the new names.  Generated by
 * plan/ws131/tools/rename-map.py apply-keiland; do not edit.
 */
#ifndef KL_COMPAT
#define keiland_context_menu kl_context_menu
#define keiland_context_menu_destroy kl_context_menu_destroy
#define keiland_context_menu_listener kl_context_menu_listener
#define keiland_desktop kl_desktop
#define keiland_desktop_ack kl_desktop_ack
#define keiland_desktop_create kl_desktop_create
#define keiland_desktop_destroy kl_desktop_destroy
#define keiland_desktop_listener kl_desktop_listener
#define keiland_edit kl_edit
#define keiland_edit_create kl_edit_create
#define keiland_edit_destroy kl_edit_destroy
#define keiland_edit_fn kl_edit_fn
#define keiland_edit_set_state kl_edit_set_state
#define keiland_gesture kl_gesture
#define keiland_gesture_cancel kl_gesture_cancel
#define keiland_gesture_create kl_gesture_create
#define keiland_gesture_destroy kl_gesture_destroy
#define keiland_gesture_down kl_gesture_down
#define keiland_gesture_drag_offset kl_gesture_drag_offset
#define keiland_gesture_event kl_gesture_event
#define keiland_gesture_motion kl_gesture_motion
#define keiland_gesture_next kl_gesture_next
#define keiland_gesture_pinch kl_gesture_pinch
#define keiland_gesture_up kl_gesture_up
#define keiland_glass kl_glass
#define keiland_glass_create kl_glass_create
#define keiland_glass_destroy kl_glass_destroy
#define keiland_glass_panel kl_glass_panel
#define keiland_glass_set_blur kl_glass_set_blur
#define keiland_glass_set_panels kl_glass_set_panels
#define keiland_keyboard_inset kl_keyboard_inset
#define keiland_keyboard_inset_create kl_keyboard_inset_create
#define keiland_keyboard_inset_destroy kl_keyboard_inset_destroy
#define keiland_keyboard_inset_fn kl_keyboard_inset_fn
#define keiland_menu kl_menu
#define keiland_menu_append kl_menu_append
#define keiland_menu_begin kl_menu_begin
#define keiland_menu_commit kl_menu_commit
#define keiland_menu_create kl_menu_create
#define keiland_menu_destroy kl_menu_destroy
#define keiland_menu_insert kl_menu_insert
#define keiland_menu_popup kl_menu_popup
#define keiland_menu_remove kl_menu_remove
#define keiland_menu_service kl_menu_service
#define keiland_menu_service_close kl_menu_service_close
#define keiland_menu_service_open kl_menu_service_open
#define keiland_menu_set_action kl_menu_set_action
#define keiland_menu_set_checked kl_menu_set_checked
#define keiland_menu_set_enabled kl_menu_set_enabled
#define keiland_menu_set_icon_name kl_menu_set_icon_name
#define keiland_menu_set_label kl_menu_set_label
#define keiland_menu_set_role kl_menu_set_role
#define keiland_menu_set_shortcut kl_menu_set_shortcut
#define keiland_menu_set_visible kl_menu_set_visible
#define keiland_motion kl_motion
#define keiland_motion_add kl_motion_add
#define keiland_motion_begin kl_motion_begin
#define keiland_motion_create kl_motion_create
#define keiland_motion_destroy kl_motion_destroy
#define keiland_motion_device kl_motion_device
#define keiland_motion_device_create kl_motion_device_create
#define keiland_motion_device_destroy kl_motion_device_destroy
#define keiland_motion_device_interval kl_motion_device_interval
#define keiland_motion_device_noise kl_motion_device_noise
#define keiland_motion_device_time kl_motion_device_time
#define keiland_motion_end kl_motion_end
#define keiland_motion_point kl_motion_point
#define keiland_motion_velocity kl_motion_velocity
#define keiland_recent_add kl_recent_add
#define keiland_recent_item kl_recent_item
#define keiland_recent_list kl_recent_list
#define keiland_recent_remove kl_recent_remove
#define keiland_scroller kl_scroller
#define keiland_scroller_cancel kl_scroller_cancel
#define keiland_scroller_create kl_scroller_create
#define keiland_scroller_destroy kl_scroller_destroy
#define keiland_scroller_drag kl_scroller_drag
#define keiland_scroller_press kl_scroller_press
#define keiland_scroller_release kl_scroller_release
#define keiland_scroller_set_bounds kl_scroller_set_bounds
#define keiland_scroller_set_position kl_scroller_set_position
#define keiland_scroller_step kl_scroller_step
#define keiland_titlebar kl_titlebar
#define keiland_titlebar_add_control kl_titlebar_add_control
#define keiland_titlebar_add_tab kl_titlebar_add_tab
#define keiland_titlebar_begin kl_titlebar_begin
#define keiland_titlebar_commit kl_titlebar_commit
#define keiland_titlebar_create kl_titlebar_create
#define keiland_titlebar_destroy kl_titlebar_destroy
#define keiland_titlebar_focus_control kl_titlebar_focus_control
#define keiland_titlebar_listener kl_titlebar_listener
#define keiland_titlebar_remove_control kl_titlebar_remove_control
#define keiland_titlebar_remove_tab kl_titlebar_remove_tab
#define keiland_titlebar_set_breadcrumb kl_titlebar_set_breadcrumb
#define keiland_titlebar_set_control_label kl_titlebar_set_control_label
#define keiland_titlebar_set_control_state kl_titlebar_set_control_state
#define keiland_titlebar_set_control_text kl_titlebar_set_control_text
#define keiland_titlebar_set_control_value kl_titlebar_set_control_value
#define keiland_titlebar_set_mode kl_titlebar_set_mode
#define keiland_titlebar_set_suggestions kl_titlebar_set_suggestions
#define keiland_titlebar_set_tab kl_titlebar_set_tab
#define keiland_titlebar_set_tabs_options kl_titlebar_set_tabs_options
#define keiland_version kl_version
#define keiland_window_menu kl_window_menu
#define keiland_window_menu_create kl_window_menu_create
#define keiland_window_menu_destroy kl_window_menu_destroy
#define keiland_window_menu_listener kl_window_menu_listener
#define keiland_window_menu_set kl_window_menu_set
#define KEILAND_CONTROL_BACK KL_CONTROL_BACK
#define KEILAND_CONTROL_BREADCRUMB KL_CONTROL_BREADCRUMB
#define KEILAND_CONTROL_FILTER KL_CONTROL_FILTER
#define KEILAND_CONTROL_FORWARD KL_CONTROL_FORWARD
#define KEILAND_CONTROL_GENERIC KL_CONTROL_GENERIC
#define KEILAND_CONTROL_HOME KL_CONTROL_HOME
#define KEILAND_CONTROL_PREVIEW KL_CONTROL_PREVIEW
#define KEILAND_CONTROL_PRIMARY_ACTION KL_CONTROL_PRIMARY_ACTION
#define KEILAND_CONTROL_PROGRESS KL_CONTROL_PROGRESS
#define KEILAND_CONTROL_SEARCH KL_CONTROL_SEARCH
#define KEILAND_CONTROL_SIDEBAR KL_CONTROL_SIDEBAR
#define KEILAND_CONTROL_SORT KL_CONTROL_SORT
#define KEILAND_CONTROL_UP KL_CONTROL_UP
#define KEILAND_CONTROL_VIEW_COLUMNS KL_CONTROL_VIEW_COLUMNS
#define KEILAND_CONTROL_VIEW_GRID KL_CONTROL_VIEW_GRID
#define KEILAND_CONTROL_VIEW_LIST KL_CONTROL_VIEW_LIST
#define KEILAND_EDIT_CAN_PASTE KL_EDIT_CAN_PASTE
#define KEILAND_EDIT_CAN_REDO KL_EDIT_CAN_REDO
#define KEILAND_EDIT_CAN_UNDO KL_EDIT_CAN_UNDO
#define KEILAND_EDIT_COPY KL_EDIT_COPY
#define KEILAND_EDIT_CUT KL_EDIT_CUT
#define KEILAND_EDIT_HAS_SELECTION KL_EDIT_HAS_SELECTION
#define KEILAND_EDIT_PASTE KL_EDIT_PASTE
#define KEILAND_EDIT_REDO KL_EDIT_REDO
#define KEILAND_EDIT_SELECTING KL_EDIT_SELECTING
#define KEILAND_EDIT_SELECT_ALL KL_EDIT_SELECT_ALL
#define KEILAND_EDIT_SELECT_BEGIN KL_EDIT_SELECT_BEGIN
#define KEILAND_EDIT_SELECT_END KL_EDIT_SELECT_END
#define KEILAND_EDIT_UNDO KL_EDIT_UNDO
#define KEILAND_FOCUS_EDIT KL_FOCUS_EDIT
#define KEILAND_FOCUS_FIELD KL_FOCUS_FIELD
#define KEILAND_GESTURE_CANCEL KL_GESTURE_CANCEL
#define KEILAND_GESTURE_DOUBLE_TAP KL_GESTURE_DOUBLE_TAP
#define KEILAND_GESTURE_DRAG_BEGIN KL_GESTURE_DRAG_BEGIN
#define KEILAND_GESTURE_DRAG_END KL_GESTURE_DRAG_END
#define KEILAND_GESTURE_LONG_PRESS KL_GESTURE_LONG_PRESS
#define KEILAND_GESTURE_TAP KL_GESTURE_TAP
#define KEILAND_GLASS_CARD KL_GLASS_CARD
#define KEILAND_GLASS_PANELS_MAX KL_GLASS_PANELS_MAX
#define KEILAND_GLASS_RADIUS_MAX KL_GLASS_RADIUS_MAX
#define KEILAND_KEYBOARD_INSET_BOTTOM KL_KEYBOARD_INSET_BOTTOM
#define KEILAND_KEYBOARD_INSET_NONE KL_KEYBOARD_INSET_NONE
#define KEILAND_KEYBOARD_INSET_RIGHT KL_KEYBOARD_INSET_RIGHT
#define KEILAND_MENU_ALT KL_MENU_ALT
#define KEILAND_MENU_CTRL KL_MENU_CTRL
#define KEILAND_MENU_ITEM_CHECKBOX KL_MENU_ITEM_CHECKBOX
#define KEILAND_MENU_ITEM_NORMAL KL_MENU_ITEM_NORMAL
#define KEILAND_MENU_ITEM_RADIO KL_MENU_ITEM_RADIO
#define KEILAND_MENU_ITEM_SEPARATOR KL_MENU_ITEM_SEPARATOR
#define KEILAND_MENU_ITEM_SUBMENU KL_MENU_ITEM_SUBMENU
#define KEILAND_MENU_ROLE_ABOUT KL_MENU_ROLE_ABOUT
#define KEILAND_MENU_ROLE_CLOSE KL_MENU_ROLE_CLOSE
#define KEILAND_MENU_ROLE_COPY KL_MENU_ROLE_COPY
#define KEILAND_MENU_ROLE_CUT KL_MENU_ROLE_CUT
#define KEILAND_MENU_ROLE_DELETE KL_MENU_ROLE_DELETE
#define KEILAND_MENU_ROLE_FIND KL_MENU_ROLE_FIND
#define KEILAND_MENU_ROLE_FULLSCREEN KL_MENU_ROLE_FULLSCREEN
#define KEILAND_MENU_ROLE_HELP KL_MENU_ROLE_HELP
#define KEILAND_MENU_ROLE_NEW KL_MENU_ROLE_NEW
#define KEILAND_MENU_ROLE_NONE KL_MENU_ROLE_NONE
#define KEILAND_MENU_ROLE_OPEN KL_MENU_ROLE_OPEN
#define KEILAND_MENU_ROLE_PASTE KL_MENU_ROLE_PASTE
#define KEILAND_MENU_ROLE_PREFERENCES KL_MENU_ROLE_PREFERENCES
#define KEILAND_MENU_ROLE_QUIT KL_MENU_ROLE_QUIT
#define KEILAND_MENU_ROLE_REDO KL_MENU_ROLE_REDO
#define KEILAND_MENU_ROLE_SAVE KL_MENU_ROLE_SAVE
#define KEILAND_MENU_ROLE_SELECT_ALL KL_MENU_ROLE_SELECT_ALL
#define KEILAND_MENU_ROLE_UNDO KL_MENU_ROLE_UNDO
#define KEILAND_MENU_ROLE_ZOOM_IN KL_MENU_ROLE_ZOOM_IN
#define KEILAND_MENU_ROLE_ZOOM_OUT KL_MENU_ROLE_ZOOM_OUT
#define KEILAND_MENU_ROOT KL_MENU_ROOT
#define KEILAND_MENU_SHIFT KL_MENU_SHIFT
#define KEILAND_MENU_SUPER KL_MENU_SUPER
#define KEILAND_MOTION_EXTRAPOLATION_CONTENT KL_MOTION_EXTRAPOLATION_CONTENT
#define KEILAND_MOTION_EXTRAPOLATION_INK KL_MOTION_EXTRAPOLATION_INK
#define KEILAND_PRIORITY_NORMAL KL_PRIORITY_NORMAL
#define KEILAND_PRIORITY_PRIMARY KL_PRIORITY_PRIMARY
#define KEILAND_PRIORITY_SECONDARY KL_PRIORITY_SECONDARY
#define KEILAND_PROGRESS_UNKNOWN KL_PROGRESS_UNKNOWN
#define KEILAND_RECENT_KEPT KL_RECENT_KEPT
#define KEILAND_RECENT_NAME_MAX KL_RECENT_NAME_MAX
#define KEILAND_RECENT_PATH_MAX KL_RECENT_PATH_MAX
#define KEILAND_SCROLLER_FLING_MIN KL_SCROLLER_FLING_MIN
#define KEILAND_TABS_NEW_BUTTON KL_TABS_NEW_BUTTON
#define KEILAND_TAB_ACTIVE KL_TAB_ACTIVE
#define KEILAND_TAB_ATTENTION KL_TAB_ATTENTION
#define KEILAND_TAB_CLOSABLE KL_TAB_CLOSABLE
#define KEILAND_TEXT_CANCELLED KL_TEXT_CANCELLED
#define KEILAND_TEXT_LEFT KL_TEXT_LEFT
#define KEILAND_TEXT_SUBMITTED KL_TEXT_SUBMITTED
#define KEILAND_TITLEBAR_CONTROLS KL_TITLEBAR_CONTROLS
#define KEILAND_TITLEBAR_MENU KL_TITLEBAR_MENU
#define KEILAND_TITLEBAR_SHEET KL_TITLEBAR_SHEET
#define KEILAND_TITLEBAR_TABS KL_TITLEBAR_TABS
#define KEILAND_VERSION KL_VERSION
#endif /* KL_COMPAT */

#endif
