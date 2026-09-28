/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The Wayland window of terminal: an xdg-shell toplevel, the
 * seat's keyboard and the repeating of a held key.
 *
 * A key press is turned into bytes at once (keys.c) and kept until the main
 * loop writes them to the shell.  zdesktop does not repeat keys itself, so a key
 * held past the repeat delay is pressed again on each interval.
 * ws081-p011: the seat's touch screen queues its wl_touch events for
 * touch.c (a window with wl_touch hears fingers only by it).
 */

#include "terminal.h"

#include <errno.h>
#include <poll.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* The seat and keyboard version the window understands. */
#define WINDOW_SEAT_VERSION	5U

/* The xdg_toplevel state that says the window is fullscreen. */
#define WINDOW_STATE_FULLSCREEN	2U

/* The repeat's delay and interval when the compositor gives none, in milliseconds. */
#define WINDOW_REPEAT_DELAY	400U
#define WINDOW_REPEAT_INTERVAL	40U

/* The evdev codes of the modifier keys, which never repeat. */
#define WINDOW_KEY_LEFTCTRL	29U
#define WINDOW_KEY_LEFTSHIFT	42U
#define WINDOW_KEY_RIGHTSHIFT	54U
#define WINDOW_KEY_LEFTALT	56U
#define WINDOW_KEY_CAPSLOCK	58U
#define WINDOW_KEY_RIGHTCTRL	97U
#define WINDOW_KEY_RIGHTALT	100U
#define WINDOW_KEY_LEFTMETA	125U
#define WINDOW_KEY_RIGHTMETA	126U

/* The evdev codes of Page Up and Page Down, which with Shift scroll the view (ws035-p114). */
#define WINDOW_KEY_PAGEUP	104U
#define WINDOW_KEY_PAGEDOWN	109U

/* The wl_pointer axis of the vertical wheel. */
#define WINDOW_AXIS_VERTICAL	0U

static void window_global(void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version);
static void window_global_remove(void *data, struct wl_registry *registry, uint32_t name);
static void window_ping(void *data, struct xdg_wm_base *shell, uint32_t serial);
static void window_configure(void *data, struct xdg_surface *surface, uint32_t serial);
static void window_toplevel_configure(void *data, struct xdg_toplevel *toplevel, int32_t width, int32_t height, struct wl_array *states);
static void window_toplevel_close(void *data, struct xdg_toplevel *toplevel);
static void window_toplevel_bounds(void *data, struct xdg_toplevel *toplevel, int32_t width, int32_t height);
static void window_seat_capabilities(void *data, struct wl_seat *seat, uint32_t capabilities);
static void window_seat_name(void *data, struct wl_seat *seat, const char *name);
static void window_keyboard_keymap(void *data, struct wl_keyboard *keyboard, uint32_t format, int32_t fd, uint32_t size);
static void window_keyboard_enter(void *data, struct wl_keyboard *keyboard, uint32_t serial, struct wl_surface *surface, struct wl_array *keys);
static void window_keyboard_leave(void *data, struct wl_keyboard *keyboard, uint32_t serial, struct wl_surface *surface);
static void window_keyboard_key(void *data, struct wl_keyboard *keyboard, uint32_t serial, uint32_t time, uint32_t key, uint32_t state);
static void window_keyboard_modifiers(void *data, struct wl_keyboard *keyboard, uint32_t serial, uint32_t depressed, uint32_t latched, uint32_t locked, uint32_t group);
static void window_keyboard_repeat(void *data, struct wl_keyboard *keyboard, int32_t rate, int32_t delay);
static void window_pointer_enter(void *data, struct wl_pointer *pointer, uint32_t serial, struct wl_surface *surface, wl_fixed_t x, wl_fixed_t y);
static void window_pointer_leave(void *data, struct wl_pointer *pointer, uint32_t serial, struct wl_surface *surface);
static void window_pointer_motion(void *data, struct wl_pointer *pointer, uint32_t time, wl_fixed_t x, wl_fixed_t y);
static void window_pointer_button(void *data, struct wl_pointer *pointer, uint32_t serial, uint32_t time, uint32_t button, uint32_t state);
static void window_pointer_axis(void *data, struct wl_pointer *pointer, uint32_t time, uint32_t axis, wl_fixed_t value);
static void window_pointer_frame(void *data, struct wl_pointer *pointer);
static void window_pointer_axis_source(void *data, struct wl_pointer *pointer, uint32_t source);
static void window_pointer_axis_stop(void *data, struct wl_pointer *pointer, uint32_t time, uint32_t axis);
static void window_pointer_axis_discrete(void *data, struct wl_pointer *pointer, uint32_t axis, int32_t discrete);
static void window_pointer_event(struct terminal_window *window, unsigned kind, uint32_t time, uint32_t serial);
static void window_touch_push(struct terminal_window *window, unsigned type, uint32_t time, uint32_t serial, int32_t id, wl_fixed_t x, wl_fixed_t y);
static void window_touch_down(void *data, struct wl_touch *touch, uint32_t serial, uint32_t time, struct wl_surface *surface, int32_t id, wl_fixed_t x, wl_fixed_t y);
static void window_touch_up(void *data, struct wl_touch *touch, uint32_t serial, uint32_t time, int32_t id);
static void window_touch_motion(void *data, struct wl_touch *touch, uint32_t time, int32_t id, wl_fixed_t x, wl_fixed_t y);
static void window_touch_frame(void *data, struct wl_touch *touch);
static void window_touch_cancel(void *data, struct wl_touch *touch);
static void window_press(struct terminal_window *window, uint32_t key);
static int window_state_fullscreen(struct wl_array *states);
static int window_modifier_key(uint32_t key);

/* The registry's callbacks, for as long as the registry lives. */
static const struct wl_registry_listener registry_listener = {
	window_global, window_global_remove
};

/* The shell's liveness check. */
static const struct xdg_wm_base_listener shell_listener = {
	window_ping
};

/* The configure acknowledgement of the window's role. */
static const struct xdg_surface_listener surface_listener = {
	window_configure
};

/* The size the compositor gives the window, and its request to close. */
static const struct xdg_toplevel_listener toplevel_listener = {
	window_toplevel_configure, window_toplevel_close, window_toplevel_bounds
};

/* The pointer's events of versions 1 to 5 (ws035-p093: the left button selects). */
static const struct wl_pointer_listener pointer_listener = {
	window_pointer_enter, window_pointer_leave, window_pointer_motion,
	window_pointer_button, window_pointer_axis, window_pointer_frame,
	window_pointer_axis_source, window_pointer_axis_stop, window_pointer_axis_discrete, NULL, NULL
};

/* The touch screen's events of versions 1 to 5 (shape and orientation, of version 6, are never called). */
static const struct wl_touch_listener touch_listener = {
	window_touch_down, window_touch_up, window_touch_motion, window_touch_frame, window_touch_cancel, NULL, NULL
};

/* The seat's devices and name. */
static const struct wl_seat_listener seat_listener = {
	window_seat_capabilities, window_seat_name
};

/* The keyboard's events of versions 1 to 5. */
static const struct wl_keyboard_listener keyboard_listener = {
	window_keyboard_keymap, window_keyboard_enter, window_keyboard_leave,
	window_keyboard_key, window_keyboard_modifiers, window_keyboard_repeat
};

/*
 * Connects to the compositor and makes a toplevel window of a size.
 *
 * Returns 0 once the first configure is acknowledged, or -1 with errno set.
 */
int
terminal_window_open(
	struct terminal_window *window,
	const char *display,
	uint32_t width,
	uint32_t height)
{
	int status;

	/* The size the window asks for until the compositor gives one. */
	memset(window, 0, sizeof(*window));
	window->width = width;
	window->height = height;
	window->preferred_width = width;
	window->preferred_height = height;
	window->repeat_delay = WINDOW_REPEAT_DELAY;
	window->repeat_interval = WINDOW_REPEAT_INTERVAL;

	/* The connection. */
	window->display = wl_display_connect(display);
	if (window->display == NULL)
		return -1;

	/* The globals: the compositor, the shell and the seat. */
	window->registry = wl_display_get_registry(window->display);
	if (window->registry == NULL)
		return -1;

	/* Listens for the globals the compositor announces. */
	status = wl_registry_add_listener(window->registry, &registry_listener, window);
	if (status != 0)
		return -1;

	/* Waits until every global has been announced. */
	status = wl_display_roundtrip(window->display);
	if (status < 0)
		return -1;

	/* A window needs a compositor and a shell. */
	if (window->compositor == NULL || window->shell == NULL) {
		errno = EOPNOTSUPP;
		return -1;
	}

	/* The seat's data device, for the clipboard (clipboard.c). */
	terminal_clipboard_start(window);
	terminal_primary_start(window);

	/* The surface and its toplevel role. */
	window->surface = wl_compositor_create_surface(window->compositor);
	if (window->surface == NULL)
		return -1;

	/* The surface becomes an xdg surface. */
	window->role = xdg_wm_base_get_xdg_surface(window->shell, window->surface);
	if (window->role == NULL)
		return -1;

	/* Listens for its configures. */
	status = xdg_surface_add_listener(window->role, &surface_listener, window);
	if (status != 0)
		return -1;

	/* The xdg surface becomes a toplevel window. */
	window->toplevel = xdg_surface_get_toplevel(window->role);
	if (window->toplevel == NULL)
		return -1;

	/* Listens for its size and its close request. */
	status = xdg_toplevel_add_listener(window->toplevel, &toplevel_listener, window);
	if (status != 0)
		return -1;

	/* The title the compositor shows and the application's identity. */
	xdg_toplevel_set_title(window->toplevel, "Terminal");
	xdg_toplevel_set_app_id(window->toplevel, "terminal");
	wl_surface_commit(window->surface);

	/* The first configure (and the seat's devices) before anything is drawn. */
	status = wl_display_roundtrip(window->display);
	if (status < 0)
		return -1;

	/* A compositor that did not configure the window cannot take its images. */
	if (window->configured == 0) {
		errno = EPROTO;
		return -1;
	}

	/* Succeeded: the window can be drawn into. */
	window->resized = 0;
	return 0;
}

/*
 * Waits for the compositor or another descriptor (the shell's) and runs
 * the compositor's events.
 *
 * `timeout` is in milliseconds (-1 waits for ever); *other_ready tells
 * whether the other descriptor has something to read or has hung up.
 * Returns 0, or -1 when the connection is broken.
 */
int
terminal_window_dispatch(
	struct terminal_window *window,
	const int *others,
	unsigned count,
	int timeout,
	int *ready)
{
	struct pollfd descriptors[1U + TERMINAL_TABS];
	unsigned index;
	unsigned used;
	int status;

	/* Runs what is queued until a read of new events can be reserved (no other descriptor is ready yet). */
	for (index = 0; index < count; index++)
		ready[index] = 0;
	for (;;) {
		status = wl_display_dispatch_pending(window->display);
		if (status < 0)
			return -1;

		/* A reserved read means nothing is queued any more. */
		status = wl_display_prepare_read(window->display);
		if (status == 0)
			break;

		/* EAGAIN asks for another dispatch; anything else is a broken connection. */
		if (errno != EAGAIN)
			return -1;
	}

	/* Sends what the window asked for. */
	status = wl_display_flush(window->display);
	if (status < 0 && errno != EAGAIN) {
		wl_display_cancel_read(window->display);
		return -1;
	}

	/* The compositor's descriptor, then each other one (at most TERMINAL_TABS). */
	descriptors[0].fd = wl_display_get_fd(window->display);
	descriptors[0].events = POLLIN;
	descriptors[0].revents = 0;
	used = 1;
	for (index = 0; index < count && index < TERMINAL_TABS; index++) {
		descriptors[used].fd = others[index];
		descriptors[used].events = POLLIN;
		descriptors[used].revents = 0;
		used++;
	}

	/* Waits for any of them. */
	status = poll(descriptors, (nfds_t)used, timeout);

	/* Reads the compositor's events, or gives the reservation back. */
	if (status > 0 && (descriptors[0].revents & POLLIN) != 0) {
		status = wl_display_read_events(window->display);
		if (status < 0)
			return -1;
	} else {
		wl_display_cancel_read(window->display);
		if (status < 0 && errno != EINTR)
			return -1;

		/* A hung-up connection has no more events. */
		if ((descriptors[0].revents & (POLLERR | POLLHUP | POLLNVAL)) != 0)
			return -1;
	}

	/* Each other descriptor that has bytes, or whose end has gone. */
	for (index = 1; index < used; index++) {
		/* A descriptor that is not ready stays not ready. */
		if ((descriptors[index].revents & (POLLIN | POLLHUP | POLLERR)) != 0)
			ready[index - 1U] = 1;
	}

	/* Runs the events read. */
	status = wl_display_dispatch_pending(window->display);
	if (status < 0)
		return -1;

	/* Succeeded: the events so far have run. */
	return 0;
}

/*
 * Presses the held key again when its repeat is due.
 */
void
terminal_window_repeat(
	struct terminal_window *window,
	uint64_t now)
{
	/* No key held, or not yet time. */
	if (window->repeat_key == 0U || now < window->repeat_at)
		return;

	/* The key's bytes again, and the next repeat one interval later. */
	window_press(window, window->repeat_key);
	window->repeat_at = now + window->repeat_interval;
}

/*
 * Destroys the window's objects and disconnects.
 */
void
terminal_window_close(
	struct terminal_window *window)
{
	/* The menus, before the window they are shown on, and the clipboard before the seat. */
	terminal_menu_close(window);
	terminal_primary_close(window);
	terminal_clipboard_close(window);

	/* The keyboard, the pointer, the touch screen and the seat. */
	if (window->touch != NULL)
		wl_touch_destroy(window->touch);
	if (window->keyboard != NULL)
		wl_keyboard_destroy(window->keyboard);
	if (window->pointer != NULL)
		wl_pointer_destroy(window->pointer);
	if (window->seat != NULL)
		wl_seat_destroy(window->seat);

	/* The roles before the surface, the surface before the globals that made it. */
	if (window->toplevel != NULL)
		xdg_toplevel_destroy(window->toplevel);
	if (window->role != NULL)
		xdg_surface_destroy(window->role);
	if (window->surface != NULL)
		wl_surface_destroy(window->surface);
	if (window->shell != NULL)
		xdg_wm_base_destroy(window->shell);
	if (window->compositor != NULL)
		wl_compositor_destroy(window->compositor);
	if (window->registry != NULL)
		wl_registry_destroy(window->registry);

	/* The connection last. */
	if (window->display != NULL)
		wl_display_disconnect(window->display);
	memset(window, 0, sizeof(*window));
}

/*
 * Adds bytes to what the shell reads next, as if typed (the Session menu's
 * interrupt and end of file); what does not fit is dropped.
 */
void
terminal_window_type(
	struct terminal_window *window,
	const char *bytes,
	size_t length)
{
	/* Only what fits in the buffer. */
	if (length > sizeof(window->input) - window->input_length)
		length = sizeof(window->input) - window->input_length;

	/* The bytes after those typed before. */
	memcpy(window->input + window->input_length, bytes, length);
	window->input_length += length;
}

/*
 * Asks the compositor to make the window fullscreen, or to end it; the
 * configure that follows says what it did.
 */
void
terminal_window_set_fullscreen(
	struct terminal_window *window,
	int fullscreen)
{
	/* On the default output, or back to a window. */
	if (fullscreen)
		xdg_toplevel_set_fullscreen(window->toplevel, NULL);
	else
		xdg_toplevel_unset_fullscreen(window->toplevel);
}

/*
 * Returns a monotonic time in milliseconds (0 when the clock cannot be read).
 */
uint64_t
terminal_clock(void)
{
	struct timespec now;
	int status;

	/* The monotonic clock. */
	status = clock_gettime(CLOCK_MONOTONIC, &now);
	if (status != 0)
		return 0U;

	/* Reports it in milliseconds. */
	return (uint64_t)now.tv_sec * 1000U + (uint64_t)now.tv_nsec / 1000000U;
}

/* Binds the compositor, the shell and the first seat. */
static void
window_global(
	void *data,
	struct wl_registry *registry,
	uint32_t name,
	const char *interface,
	uint32_t version)
{
	struct terminal_window *window;
	int match;

	/* The compositor makes surfaces; version 4 is enough. */
	window = data;
	match = strcmp(interface, "wl_compositor");
	if (match == 0 && window->compositor == NULL) {
		if (version > 4U)
			version = 4U;
		window->compositor = wl_registry_bind(registry, name, &wl_compositor_interface, version);
		return;
	}

	/* The shell gives the surface its window role. */
	match = strcmp(interface, "xdg_wm_base");
	if (match == 0 && window->shell == NULL) {
		if (version > 4U)
			version = 4U;
		window->shell = wl_registry_bind(registry, name, &xdg_wm_base_interface, version);
		if (window->shell != NULL)
			(void)xdg_wm_base_add_listener(window->shell, &shell_listener, window);
		return;
	}

	/* The seat gives the keyboard; without one the terminal only shows. */
	match = strcmp(interface, "wl_seat");
	if (match == 0 && window->seat == NULL) {
		if (version > WINDOW_SEAT_VERSION)
			version = WINDOW_SEAT_VERSION;
		window->seat = wl_registry_bind(registry, name, &wl_seat_interface, version);
		if (window->seat != NULL)
			(void)wl_seat_add_listener(window->seat, &seat_listener, window);
		return;
	}

	/* The data device manager shares the clipboard with other clients (clipboard.c). */
	match = strcmp(interface, "wl_data_device_manager");
	if (match == 0 && window->data_manager == NULL) {
		terminal_clipboard_bind(window, registry, name, version);
		return;
	}

	/* The primary selection manager shares the selected text with other clients (primary.c). */
	match = strcmp(interface, "zwp_primary_selection_device_manager_v1");
	if (match == 0 && window->primary_manager == NULL)
		terminal_primary_bind(window, registry, name);
}

/* A global going away does not matter to a terminal that already bound what it needs. */
static void
window_global_remove(
	void *data,
	struct wl_registry *registry,
	uint32_t name)
{
	/* Nothing to do. */
	(void)data;
	(void)registry;
	(void)name;
}

/* Answers the compositor's liveness check. */
static void
window_ping(
	void *data,
	struct xdg_wm_base *shell,
	uint32_t serial)
{
	/* The same serial back. */
	(void)data;
	xdg_wm_base_pong(shell, serial);
}

/* Acknowledges a configure; the next frame is drawn at the size it gave. */
static void
window_configure(
	void *data,
	struct xdg_surface *surface,
	uint32_t serial)
{
	struct terminal_window *window;

	/* The acknowledgement comes before any image of the new state. */
	window = data;
	xdg_surface_ack_configure(surface, serial);
	window->configured = 1;
}

/* Takes the size the compositor gives; zero keeps the window's own. */
static void
window_toplevel_configure(
	void *data,
	struct xdg_toplevel *toplevel,
	int32_t width,
	int32_t height,
	struct wl_array *states)
{
	struct terminal_window *window;

	/* Of the states only fullscreen matters (the View menu shows it); the others change nothing but the size. */
	(void)toplevel;
	window = data;
	window->fullscreen = window_state_fullscreen(states);

	/* A width left to the window is the one it would like, within the compositor's bounds. */
	if (width <= 0) {
		width = (int32_t)window->preferred_width;
		if (window->bounds_width > 0U && window->preferred_width > window->bounds_width)
			width = (int32_t)window->bounds_width;
	}

	/* And so is a height. */
	if (height <= 0) {
		height = (int32_t)window->preferred_height;
		if (window->bounds_height > 0U && window->preferred_height > window->bounds_height)
			height = (int32_t)window->bounds_height;
	}

	/* A new width or height marks the window resized. */
	if (width > 0 && (uint32_t)width != window->width) {
		window->width = (uint32_t)width;
		window->resized = 1;
	}

	/* And so does a new height. */
	if (height > 0 && (uint32_t)height != window->height) {
		window->height = (uint32_t)height;
		window->resized = 1;
	}
}

/*
 * Keeps the largest size the compositor lets the window choose (xdg-shell
 * version 4); the configure that follows applies it.  A zero is a size the
 * compositor does not know.
 */
static void
window_toplevel_bounds(
	void *data,
	struct xdg_toplevel *toplevel,
	int32_t width,
	int32_t height)
{
	struct terminal_window *window;

	/* The width, when known. */
	(void)toplevel;
	window = data;
	window->bounds_width = 0U;
	if (width > 0)
		window->bounds_width = (uint32_t)width;

	/* The height, when known. */
	window->bounds_height = 0U;
	if (height > 0)
		window->bounds_height = (uint32_t)height;
}

/* The compositor asks the window to close (its close button). */
static void
window_toplevel_close(
	void *data,
	struct xdg_toplevel *toplevel)
{
	struct terminal_window *window;

	/* The main loop ends the terminal. */
	(void)toplevel;
	window = data;
	window->closed = 1;
}

/* Takes the seat's keyboard when it has one. */
static void
window_seat_capabilities(
	void *data,
	struct wl_seat *seat,
	uint32_t capabilities)
{
	struct terminal_window *window;

	/* A keyboard, once. */
	window = data;
	if ((capabilities & WL_SEAT_CAPABILITY_KEYBOARD) != 0U && window->keyboard == NULL) {
		window->keyboard = wl_seat_get_keyboard(seat);
		if (window->keyboard != NULL)
			(void)wl_keyboard_add_listener(window->keyboard, &keyboard_listener, window);
	}

	/* A pointer, once (ws035-p093). */
	if ((capabilities & WL_SEAT_CAPABILITY_POINTER) != 0U && window->pointer == NULL) {
		window->pointer = wl_seat_get_pointer(seat);
		if (window->pointer != NULL)
			(void)wl_pointer_add_listener(window->pointer, &pointer_listener, window);
	}

	/* A touch screen, once (ws081-p011). */
	if ((capabilities & WL_SEAT_CAPABILITY_TOUCH) != 0U && window->touch == NULL) {
		window->touch = wl_seat_get_touch(seat);
		if (window->touch != NULL)
			(void)wl_touch_add_listener(window->touch, &touch_listener, window);
	}
}

/* The seat's name is not used. */
static void
window_seat_name(
	void *data,
	struct wl_seat *seat,
	const char *name)
{
	/* Nothing to do. */
	(void)data;
	(void)seat;
	(void)name;
}

/* Closes the keymap file: keys arrive as evdev codes and the terminal has its own layout. */
static void
window_keyboard_keymap(
	void *data,
	struct wl_keyboard *keyboard,
	uint32_t format,
	int32_t fd,
	uint32_t size)
{
	/* The descriptor is the window's to close. */
	(void)data;
	(void)keyboard;
	(void)format;
	(void)size;
	if (fd >= 0)
		(void)close(fd);
}

/* Focus arrives: no key is held yet as far as the terminal is concerned. */
static void
window_keyboard_enter(
	void *data,
	struct wl_keyboard *keyboard,
	uint32_t serial,
	struct wl_surface *surface,
	struct wl_array *keys)
{
	struct terminal_window *window;

	/* Keys already held when focus came are not typed. */
	(void)keyboard;
	(void)serial;
	(void)surface;
	(void)keys;
	window = data;
	window->repeat_key = 0U;
}

/* Focus leaves: nothing repeats any more. */
static void
window_keyboard_leave(
	void *data,
	struct wl_keyboard *keyboard,
	uint32_t serial,
	struct wl_surface *surface)
{
	struct terminal_window *window;

	/* The held key stops repeating, and modifiers are forgotten. */
	(void)keyboard;
	(void)serial;
	(void)surface;
	window = data;
	window->repeat_key = 0U;
	window->modifiers = 0U;
}

/* Types a pressed key and starts its repeat; a release stops the repeat. */
static void
window_keyboard_key(
	void *data,
	struct wl_keyboard *keyboard,
	uint32_t serial,
	uint32_t time,
	uint32_t key,
	uint32_t state)
{
	struct terminal_window *window;
	int modifier;

	/* The serial names a selection the key sets (Copy); the time is not needed. */
	(void)keyboard;
	(void)time;
	window = data;
	window->serial = serial;

	/* A release of the repeating key stops it. */
	if (state != WL_KEYBOARD_KEY_STATE_PRESSED) {
		if (key == window->repeat_key)
			window->repeat_key = 0U;
		return;
	}

	/* The press types, and a key that is not a modifier repeats while held. */
	window_press(window, key);
	modifier = window_modifier_key(key);
	if (modifier == 0) {
		window->repeat_key = key;
		window->repeat_at = terminal_clock() + window->repeat_delay;
	}
}

/* Keeps the modifiers held (shift, control, alt), which change what keys type. */
static void
window_keyboard_modifiers(
	void *data,
	struct wl_keyboard *keyboard,
	uint32_t serial,
	uint32_t depressed,
	uint32_t latched,
	uint32_t locked,
	uint32_t group)
{
	struct terminal_window *window;

	/* Only the held modifiers count; zdesktop latches and locks nothing. */
	(void)keyboard;
	(void)serial;
	(void)latched;
	(void)locked;
	(void)group;
	window = data;
	window->modifiers = depressed;
}

/* Takes the compositor's repeat rate and delay, when it gives a rate. */
static void
window_keyboard_repeat(
	void *data,
	struct wl_keyboard *keyboard,
	int32_t rate,
	int32_t delay)
{
	struct terminal_window *window;

	/* A rate of zero turns repeat off; a positive rate is keys per second. */
	(void)keyboard;
	window = data;
	if (rate > 0)
		window->repeat_interval = 1000U / (uint32_t)rate;
	if (delay > 0)
		window->repeat_delay = (uint32_t)delay;
}

/* Adds a key's bytes to what the shell reads next. */
static void
window_press(
	struct terminal_window *window,
	uint32_t key)
{
	size_t length;
	int shift;

	/* Shift with Page Up scrolls the view a page back instead of typing (ws035-p114). */
	shift = 0;
	if ((window->modifiers & TERMINAL_MODIFIER_SHIFT) != 0U)
		shift = 1;
	if (shift && key == WINDOW_KEY_PAGEUP) {
		window->scroll_pages++;
		return;
	}

	/* Shift with Page Down scrolls a page toward the live screen. */
	if (shift && key == WINDOW_KEY_PAGEDOWN) {
		window->scroll_pages--;
		return;
	}

	/* The bytes, if they fit in what is left of the buffer. */
	length = terminal_key_bytes(key, window->modifiers, window->input + window->input_length, sizeof(window->input) - window->input_length);
	window->input_length += length;
}

/* Tells whether a configure's states include fullscreen. */
static int
window_state_fullscreen(
	struct wl_array *states)
{
	const uint32_t *state;
	size_t count;
	size_t index;

	/* No array, no states. */
	if (states == NULL || states->data == NULL)
		return 0;

	/* The states are 32-bit values. */
	state = states->data;
	count = states->size / sizeof(uint32_t);
	for (index = 0; index < count; index++) {
		/* Fullscreen is among them. */
		if (state[index] == WINDOW_STATE_FULLSCREEN)
			return 1;
	}

	/* Not fullscreen. */
	return 0;
}

/* Tells whether a key is a modifier (which types nothing and does not repeat). */
static int
window_modifier_key(
	uint32_t key)
{
	/* The shifts, controls, alts, metas and caps lock. */
	switch (key) {
	case WINDOW_KEY_LEFTCTRL:
	case WINDOW_KEY_RIGHTCTRL:
	case WINDOW_KEY_LEFTSHIFT:
	case WINDOW_KEY_RIGHTSHIFT:
	case WINDOW_KEY_LEFTALT:
	case WINDOW_KEY_RIGHTALT:
	case WINDOW_KEY_LEFTMETA:
	case WINDOW_KEY_RIGHTMETA:
	case WINDOW_KEY_CAPSLOCK:
		return 1;
	default:
		break;
	}

	/* Every other key is typed and repeats. */
	return 0;
}

/* The pointer comes over the window: where it is. */
static void
window_pointer_enter(
	void *data,
	struct wl_pointer *pointer,
	uint32_t serial,
	struct wl_surface *surface,
	wl_fixed_t x,
	wl_fixed_t y)
{
	struct terminal_window *window;

	/* Its place. */
	(void)pointer;
	(void)serial;
	(void)surface;
	window = data;
	window->pointer_x = wl_fixed_to_int(x);
	window->pointer_y = wl_fixed_to_int(y);
}

/* The pointer leaves the window: nothing to do (a drag keeps its press). */
static void
window_pointer_leave(
	void *data,
	struct wl_pointer *pointer,
	uint32_t serial,
	struct wl_surface *surface)
{
	/* Nothing to do. */
	(void)data;
	(void)pointer;
	(void)serial;
	(void)surface;
}

/* The pointer moves: its place, and a motion for the main loop (the last one replaces one not yet taken). */
static void
window_pointer_motion(
	void *data,
	struct wl_pointer *pointer,
	uint32_t time,
	wl_fixed_t x,
	wl_fixed_t y)
{
	struct terminal_window *window;
	struct terminal_pointer_event *last;

	/* Its place. */
	(void)pointer;
	window = data;
	window->pointer_x = wl_fixed_to_int(x);
	window->pointer_y = wl_fixed_to_int(y);

	/* A motion after a motion replaces it. */
	if (window->pointer_event_count > 0U) {
		last = &window->pointer_events[window->pointer_event_count - 1U];
		if (last->kind == TERMINAL_POINTER_MOTION) {
			last->x = window->pointer_x;
			last->y = window->pointer_y;
			last->time = time;
			return;
		}
	}

	/* Otherwise it is added. */
	window_pointer_event(window, TERMINAL_POINTER_MOTION, time, 0U);
}

/* The left button pressed or released, for the main loop. */
static void
window_pointer_button(
	void *data,
	struct wl_pointer *pointer,
	uint32_t serial,
	uint32_t time,
	uint32_t button,
	uint32_t state)
{
	struct terminal_window *window;
	unsigned kind;

	/* The middle button's press pastes the primary selection (BTN_MIDDLE, ws035-p100). */
	(void)pointer;
	window = data;
	if (button == 0x112U) {
		if (state == WL_POINTER_BUTTON_STATE_PRESSED)
			window_pointer_event(window, TERMINAL_POINTER_MIDDLE, time, serial);
		return;
	}

	/* Otherwise only the left button (BTN_LEFT). */
	if (button != 0x110U)
		return;

	/* Pressed or released. */
	kind = TERMINAL_POINTER_RELEASE;
	if (state == WL_POINTER_BUTTON_STATE_PRESSED)
		kind = TERMINAL_POINTER_PRESS;
	window_pointer_event(window, kind, time, serial);
}

/* The wheel is not used. */
static void
window_pointer_axis(
	void *data,
	struct wl_pointer *pointer,
	uint32_t time,
	uint32_t axis,
	wl_fixed_t value)
{
	/* Nothing to do. */
	(void)data;
	(void)pointer;
	(void)time;
	(void)axis;
	(void)value;
}

/* A frame groups nothing the terminal needs. */
static void
window_pointer_frame(
	void *data,
	struct wl_pointer *pointer)
{
	/* Nothing to do. */
	(void)data;
	(void)pointer;
}

/* The wheel is not used. */
static void
window_pointer_axis_source(
	void *data,
	struct wl_pointer *pointer,
	uint32_t source)
{
	/* Nothing to do. */
	(void)data;
	(void)pointer;
	(void)source;
}

/* The wheel is not used. */
static void
window_pointer_axis_stop(
	void *data,
	struct wl_pointer *pointer,
	uint32_t time,
	uint32_t axis)
{
	/* Nothing to do. */
	(void)data;
	(void)pointer;
	(void)time;
	(void)axis;
}

/*
 * The wheel turns by notches: each notch toward the user (positive) goes
 * forward toward the live screen, away from the user back into the
 * scrollback (ws035-p114).
 */
static void
window_pointer_axis_discrete(
	void *data,
	struct wl_pointer *pointer,
	uint32_t axis,
	int32_t discrete)
{
	struct terminal_window *window;

	/* Only the vertical wheel scrolls. */
	(void)pointer;
	window = data;
	if (axis != WINDOW_AXIS_VERTICAL)
		return;

	/* Kept for the main loop, back as positive. */
	window->scroll_notches -= discrete;
}

/* Adds a pointer event at the pointer's place for the main loop (a full queue drops it). */
static void
window_pointer_event(
	struct terminal_window *window,
	unsigned kind,
	uint32_t time,
	uint32_t serial)
{
	struct terminal_pointer_event *event;

	/* Room for it. */
	if (window->pointer_event_count >= TERMINAL_POINTER_EVENTS)
		return;

	/* Succeeded: kept. */
	event = &window->pointer_events[window->pointer_event_count++];
	event->kind = kind;
	event->x = window->pointer_x;
	event->y = window->pointer_y;
	event->time = time;
	event->serial = serial;
	event->modifiers = window->modifiers;
}

/* Queues a touch input with the time the window read it; a full queue drops it. */
static void
window_touch_push(
	struct terminal_window *window,
	unsigned type,
	uint32_t time,
	uint32_t serial,
	int32_t id,
	wl_fixed_t x,
	wl_fixed_t y)
{
	struct terminal_touch_event *event;

	/* A full queue drops the input (the fingers are far ahead of the program). */
	if (window->touch_count >= TERMINAL_TOUCH_EVENTS)
		return;

	/* The input, after the ones before it. */
	event = &window->touches[window->touch_count];
	window->touch_count++;
	memset(event, 0, sizeof(*event));
	event->type = type;
	event->id = id;
	event->x = (float)wl_fixed_to_double(x);
	event->y = (float)wl_fixed_to_double(y);
	event->time = time;
	event->serial = serial;
	event->arrival = terminal_touch_clock();
}

/* A finger touches the window. */
static void
window_touch_down(
	void *data,
	struct wl_touch *touch,
	uint32_t serial,
	uint32_t time,
	struct wl_surface *surface,
	int32_t id,
	wl_fixed_t x,
	wl_fixed_t y)
{
	/* Queued with its serial; the window has one surface. */
	(void)touch;
	(void)surface;
	window_touch_push(data, TERMINAL_TOUCH_DOWN, time, serial, id, x, y);
}

/* A finger lifts. */
static void
window_touch_up(
	void *data,
	struct wl_touch *touch,
	uint32_t serial,
	uint32_t time,
	int32_t id)
{
	/* Queued, with no place. */
	(void)touch;
	(void)serial;
	window_touch_push(data, TERMINAL_TOUCH_UP, time, 0U, id, 0, 0);
}

/* A finger moves. */
static void
window_touch_motion(
	void *data,
	struct wl_touch *touch,
	uint32_t time,
	int32_t id,
	wl_fixed_t x,
	wl_fixed_t y)
{
	/* Queued. */
	(void)touch;
	window_touch_push(data, TERMINAL_TOUCH_MOTION, time, 0U, id, x, y);
}

/* The end of a frame of touch events: each event was queued as it came. */
static void
window_touch_frame(
	void *data,
	struct wl_touch *touch)
{
	/* Nothing to do. */
	(void)data;
	(void)touch;
}

/* The compositor took the fingers. */
static void
window_touch_cancel(
	void *data,
	struct wl_touch *touch)
{
	/* Queued, for every finger. */
	(void)touch;
	window_touch_push(data, TERMINAL_TOUCH_CANCEL, 0U, 0U, -1, 0, 0);
}
