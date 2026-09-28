/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The Wayland window of Notes: an xdg-shell toplevel, the seat's pointer
 * and keyboard, and the queue of input events the main loop draws from.
 *
 * The pointer's left button draws: its press, the motions while it is
 * held, and its release become NOTES_INPUT_DOWN, _MOTION and _UP events
 * from NOTES_SOURCE_POINTER with a fixed pressure.  Every motion is kept,
 * not only the last of a frame, because each one is a sample of the stroke.
 * A pen tablet (ws079-p003's zwp_tablet_tool_v2) is to feed the same queue
 * through notes_window_input() with the pen's own pressure and tilt, so
 * nothing past this file needs to change when it arrives.
 */

#include "app.h"

#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* The seat version the window understands. */
#define WINDOW_SEAT_VERSION	5U

/* The xdg_toplevel state that says the window is fullscreen. */
#define WINDOW_STATE_FULLSCREEN	2U

/* The evdev code of the left button. */
#define WINDOW_BUTTON_LEFT	0x110U

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
static void window_pointer_event(struct notes_window *window, unsigned kind, uint32_t time);
static int window_state_fullscreen(struct wl_array *states);

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

/* The size and states the compositor gives the window, and its request to close. */
static const struct xdg_toplevel_listener toplevel_listener = {
	window_toplevel_configure, window_toplevel_close, window_toplevel_bounds
};

/* The pointer's events of versions 1 to 5. */
static const struct wl_pointer_listener pointer_listener = {
	window_pointer_enter, window_pointer_leave, window_pointer_motion,
	window_pointer_button, window_pointer_axis, window_pointer_frame,
	window_pointer_axis_source, window_pointer_axis_stop, window_pointer_axis_discrete, NULL, NULL
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
 * Connects to the compositor and makes the toplevel window.
 *
 * A fullscreen window asks for it before its first commit, so that it is
 * mapped fullscreen.  Returns 0 once the first configure is acknowledged,
 * or -1 with errno set.
 */
int
notes_window_open(
	struct notes_window *window,
	uint32_t width,
	uint32_t height,
	int fullscreen)
{
	int status;

	/* The size the window asks for until the compositor gives one. */
	memset(window, 0, sizeof(*window));
	window->width = width;
	window->height = height;
	window->preferred_width = width;
	window->preferred_height = height;

	/* The connection. */
	window->display = wl_display_connect(NULL);
	if (window->display == NULL)
		return -1;

	/* The globals. */
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

	/* The surface. */
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

	/* The title, the application's identity (the gesture finds Notes by it), and fullscreen when asked. */
	xdg_toplevel_set_title(window->toplevel, "Notes");
	xdg_toplevel_set_app_id(window->toplevel, "notes");
	if (fullscreen)
		xdg_toplevel_set_fullscreen(window->toplevel, NULL);
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
 * Waits for the compositor's events, at most a timeout in milliseconds
 * (-1: for ever), and runs them.
 *
 * Returns 0, or -1 when the connection is broken.
 */
int
notes_window_dispatch(
	struct notes_window *window,
	int timeout)
{
	struct pollfd descriptor;
	int status;

	/* Runs what is queued until a read of new events can be reserved. */
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

	/* Nothing to wait for when events are already queued for the main loop. */
	if (window->input_count != 0U ||
	    window->key_count != 0U ||
	    window->action_count != 0U)
		timeout = 0;

	/* Waits for the compositor. */
	descriptor.fd = wl_display_get_fd(window->display);
	descriptor.events = POLLIN;
	descriptor.revents = 0;
	status = poll(&descriptor, 1U, timeout);

	/* Reads the compositor's events, or gives the reservation back. */
	if (status > 0 && (descriptor.revents & POLLIN) != 0) {
		status = wl_display_read_events(window->display);
		if (status < 0)
			return -1;
	} else {
		wl_display_cancel_read(window->display);
		if (status < 0 && errno != EINTR)
			return -1;

		/* A hung-up connection has no more events. */
		if ((descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0)
			return -1;
	}

	/* Runs the events read. */
	status = wl_display_dispatch_pending(window->display);
	if (status < 0)
		return -1;

	/* Succeeded: the events so far have run. */
	return 0;
}

/*
 * Destroys the window's objects and disconnects.
 */
void
notes_window_close(
	struct notes_window *window)
{
	/* The menus, before the window they are shown on. */
	notes_menu_close(window);

	/* The keyboard, the pointer and the seat. */
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
 * Queues an input event for the main loop.
 *
 * The pointer's events come through here, and so will a pen tablet's.  A
 * full queue drops a motion but keeps room for the contact's end, so that
 * a stroke is always finished.
 */
void
notes_window_input(
	struct notes_window *window,
	const struct notes_input *input)
{
	/* The last slot is kept for an end of contact. */
	if (window->input_count + 1U >= NOTES_INPUTS && input->kind != NOTES_INPUT_UP)
		return;
	if (window->input_count >= NOTES_INPUTS)
		return;

	/* Succeeded: queued after the ones before it. */
	window->inputs[window->input_count] = *input;
	window->input_count++;
}

/*
 * Sets the title the compositor shows.
 */
void
notes_window_set_title(
	struct notes_window *window,
	const char *title)
{
	/* The toplevel's title. */
	xdg_toplevel_set_title(window->toplevel, title);
}

/*
 * Asks the compositor to make the window fullscreen, or to end it; the
 * configure that follows says what it did.
 */
void
notes_window_set_fullscreen(
	struct notes_window *window,
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
notes_clock(void)
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
	struct notes_window *window;
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

	/* The seat gives the pointer and the keyboard. */
	match = strcmp(interface, "wl_seat");
	if (match == 0 && window->seat == NULL) {
		if (version > WINDOW_SEAT_VERSION)
			version = WINDOW_SEAT_VERSION;
		window->seat = wl_registry_bind(registry, name, &wl_seat_interface, version);
		if (window->seat != NULL)
			(void)wl_seat_add_listener(window->seat, &seat_listener, window);
	}
}

/* A global going away does not matter to a window that already bound what it needs. */
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
	struct notes_window *window;

	/* The acknowledgement comes before any image of the new state. */
	window = data;
	xdg_surface_ack_configure(surface, serial);
	window->configured = 1;
}

/* Takes the size and the fullscreen state the compositor gives; a zero size keeps the window's own. */
static void
window_toplevel_configure(
	void *data,
	struct xdg_toplevel *toplevel,
	int32_t width,
	int32_t height,
	struct wl_array *states)
{
	struct notes_window *window;
	int fullscreen;

	/* The fullscreen state, which the toolbar and the menu show. */
	(void)toplevel;
	window = data;
	fullscreen = window_state_fullscreen(states);
	if (fullscreen != window->fullscreen) {
		window->fullscreen = fullscreen;
		window->resized = 1;
	}

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

	/* A new width marks the window resized. */
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

/* The compositor asks the window to close (its close button). */
static void
window_toplevel_close(
	void *data,
	struct xdg_toplevel *toplevel)
{
	struct notes_window *window;

	/* The main loop saves and ends Notes. */
	(void)toplevel;
	window = data;
	window->closed = 1;
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
	struct notes_window *window;

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

/* Takes the seat's keyboard and pointer when it has them. */
static void
window_seat_capabilities(
	void *data,
	struct wl_seat *seat,
	uint32_t capabilities)
{
	struct notes_window *window;

	/* A keyboard, once. */
	window = data;
	if ((capabilities & WL_SEAT_CAPABILITY_KEYBOARD) != 0U && window->keyboard == NULL) {
		window->keyboard = wl_seat_get_keyboard(seat);
		if (window->keyboard != NULL)
			(void)wl_keyboard_add_listener(window->keyboard, &keyboard_listener, window);
	}

	/* A pointer, once. */
	if ((capabilities & WL_SEAT_CAPABILITY_POINTER) != 0U && window->pointer == NULL) {
		window->pointer = wl_seat_get_pointer(seat);
		if (window->pointer != NULL)
			(void)wl_pointer_add_listener(window->pointer, &pointer_listener, window);
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

/* Closes the keymap file: keys arrive as evdev codes, which Notes reads directly. */
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

/* Focus arrives; keys already held are not pressed for Notes. */
static void
window_keyboard_enter(
	void *data,
	struct wl_keyboard *keyboard,
	uint32_t serial,
	struct wl_surface *surface,
	struct wl_array *keys)
{
	/* Nothing to do. */
	(void)data;
	(void)keyboard;
	(void)serial;
	(void)surface;
	(void)keys;
}

/* Focus leaves: the modifiers are forgotten. */
static void
window_keyboard_leave(
	void *data,
	struct wl_keyboard *keyboard,
	uint32_t serial,
	struct wl_surface *surface)
{
	struct notes_window *window;

	/* No modifier is held as far as Notes knows. */
	(void)keyboard;
	(void)serial;
	(void)surface;
	window = data;
	window->modifiers = 0U;
}

/* Queues a pressed key with the modifiers held; releases do nothing. */
static void
window_keyboard_key(
	void *data,
	struct wl_keyboard *keyboard,
	uint32_t serial,
	uint32_t time,
	uint32_t key,
	uint32_t state)
{
	struct notes_window *window;

	/* Only presses, while the queue has room. */
	(void)keyboard;
	(void)serial;
	(void)time;
	window = data;
	if (state != WL_KEYBOARD_KEY_STATE_PRESSED || window->key_count >= NOTES_KEYS)
		return;

	/* Succeeded: queued. */
	window->keys[window->key_count].key = key;
	window->keys[window->key_count].modifiers = window->modifiers;
	window->key_count++;
}

/* Keeps the modifiers held, which the shortcuts need. */
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
	struct notes_window *window;

	/* Only the held modifiers count. */
	(void)keyboard;
	(void)serial;
	(void)latched;
	(void)locked;
	(void)group;
	window = data;
	window->modifiers = depressed;
}

/* Notes does not repeat keys. */
static void
window_keyboard_repeat(
	void *data,
	struct wl_keyboard *keyboard,
	int32_t rate,
	int32_t delay)
{
	/* Nothing to do. */
	(void)data;
	(void)keyboard;
	(void)rate;
	(void)delay;
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
	struct notes_window *window;

	/* Its place. */
	(void)pointer;
	(void)serial;
	(void)surface;
	window = data;
	window->pointer_x = (float)wl_fixed_to_double(x);
	window->pointer_y = (float)wl_fixed_to_double(y);
}

/* The pointer leaves the window; a drag in progress keeps its press. */
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

/* The pointer moves: its place, and a motion of the stroke while the button is held. */
static void
window_pointer_motion(
	void *data,
	struct wl_pointer *pointer,
	uint32_t time,
	wl_fixed_t x,
	wl_fixed_t y)
{
	struct notes_window *window;

	/* Its place. */
	(void)pointer;
	window = data;
	window->pointer_x = (float)wl_fixed_to_double(x);
	window->pointer_y = (float)wl_fixed_to_double(y);

	/* A motion without the button is only a place. */
	if (!window->pointer_down)
		return;

	/* A sample of the contact. */
	window_pointer_event(window, NOTES_INPUT_MOTION, time);
}

/* The left button starts and ends a contact. */
static void
window_pointer_button(
	void *data,
	struct wl_pointer *pointer,
	uint32_t serial,
	uint32_t time,
	uint32_t button,
	uint32_t state)
{
	struct notes_window *window;

	/* Only the left button draws. */
	(void)pointer;
	(void)serial;
	window = data;
	if (button != WINDOW_BUTTON_LEFT)
		return;

	/* A press starts the contact, a release ends it. */
	if (state == WL_POINTER_BUTTON_STATE_PRESSED) {
		window->pointer_down = 1;
		window_pointer_event(window, NOTES_INPUT_DOWN, time);
	} else if (window->pointer_down) {
		window->pointer_down = 0;
		window_pointer_event(window, NOTES_INPUT_UP, time);
	}
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

/* A frame groups nothing Notes needs. */
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

/* The wheel is not used. */
static void
window_pointer_axis_discrete(
	void *data,
	struct wl_pointer *pointer,
	uint32_t axis,
	int32_t discrete)
{
	/* Nothing to do. */
	(void)data;
	(void)pointer;
	(void)axis;
	(void)discrete;
}

/* Queues a pointer event at the pointer's place, with the pointer's fixed pressure and no tilt. */
static void
window_pointer_event(
	struct notes_window *window,
	unsigned kind,
	uint32_t time)
{
	struct notes_input input;

	/* The event. */
	memset(&input, 0, sizeof(input));
	input.kind = kind;
	input.source = NOTES_SOURCE_POINTER;
	input.x = window->pointer_x;
	input.y = window->pointer_y;
	input.pressure = NOTES_POINTER_PRESSURE;
	input.time_ms = time;

	/* Queues it like any other source's. */
	notes_window_input(window, &input);
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
