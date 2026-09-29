/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The Wayland window of Text Editor (PDF Viewer's window.c): an xdg-shell
 * toplevel and the seat's pointer and keyboard, whose input becomes
 * te_event values in a queue the main loop hands to the editor, and the
 * seat's touch screen, whose wl_touch events wait in a queue of their own
 * for touch.c.  The keyboard's focus coming and going is an input too (the
 * cursor blinks only with it), and the last input's serial is kept for the
 * clipboard and the primary selection (clipboard.c, primary.c), whose
 * managers are bound here.
 *
 * zdesktop does not repeat keys, so a key held past the repeat delay is
 * pressed again on each interval.
 */

#include "window.h"

#include <errno.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* The seat version the window understands (frames and discrete axes). */
#define WINDOW_SEAT_VERSION	5U

/* The repeat's delay and interval when the compositor gives none, in milliseconds. */
#define WINDOW_REPEAT_DELAY	400U
#define WINDOW_REPEAT_INTERVAL	40U

/* How many pixels one unit of scrolling moves (zdesktop sends 15 units a wheel notch). */
#define WINDOW_SCROLL_SCALE	4

/* The modifier bits of wl_keyboard.modifiers as zdesktop reports them. */
#define WINDOW_WAYLAND_SHIFT	0x01U
#define WINDOW_WAYLAND_CTRL	0x04U
#define WINDOW_WAYLAND_ALT	0x08U
#define WINDOW_WAYLAND_SUPER	0x40U

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

static void window_global(void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version);
static void window_global_remove(void *data, struct wl_registry *registry, uint32_t name);
static void window_ping(void *data, struct xdg_wm_base *shell, uint32_t serial);
static void window_configure(void *data, struct xdg_surface *surface, uint32_t serial);
static void window_toplevel_configure(void *data, struct xdg_toplevel *toplevel, int32_t width, int32_t height, struct wl_array *states);
static void window_toplevel_close(void *data, struct xdg_toplevel *toplevel);
static void window_toplevel_bounds(void *data, struct xdg_toplevel *toplevel, int32_t width, int32_t height);
static void window_seat_capabilities(void *data, struct wl_seat *seat, uint32_t capabilities);
static void window_seat_name(void *data, struct wl_seat *seat, const char *name);
static void window_pointer_enter(void *data, struct wl_pointer *pointer, uint32_t serial, struct wl_surface *surface, wl_fixed_t x, wl_fixed_t y);
static void window_pointer_leave(void *data, struct wl_pointer *pointer, uint32_t serial, struct wl_surface *surface);
static void window_pointer_motion(void *data, struct wl_pointer *pointer, uint32_t time, wl_fixed_t x, wl_fixed_t y);
static void window_pointer_button(void *data, struct wl_pointer *pointer, uint32_t serial, uint32_t time, uint32_t button, uint32_t state);
static void window_pointer_axis(void *data, struct wl_pointer *pointer, uint32_t time, uint32_t axis, wl_fixed_t value);
static void window_pointer_frame(void *data, struct wl_pointer *pointer);
static void window_pointer_axis_source(void *data, struct wl_pointer *pointer, uint32_t source);
static void window_pointer_axis_stop(void *data, struct wl_pointer *pointer, uint32_t time, uint32_t axis);
static void window_pointer_axis_discrete(void *data, struct wl_pointer *pointer, uint32_t axis, int32_t discrete);
static void window_keyboard_keymap(void *data, struct wl_keyboard *keyboard, uint32_t format, int32_t fd, uint32_t size);
static void window_keyboard_enter(void *data, struct wl_keyboard *keyboard, uint32_t serial, struct wl_surface *surface, struct wl_array *keys);
static void window_keyboard_leave(void *data, struct wl_keyboard *keyboard, uint32_t serial, struct wl_surface *surface);
static void window_keyboard_key(void *data, struct wl_keyboard *keyboard, uint32_t serial, uint32_t time, uint32_t key, uint32_t state);
static void window_keyboard_modifiers(void *data, struct wl_keyboard *keyboard, uint32_t serial, uint32_t depressed, uint32_t latched, uint32_t locked, uint32_t group);
static void window_keyboard_repeat(void *data, struct wl_keyboard *keyboard, int32_t rate, int32_t delay);
static int window_modifier_key(uint32_t key);
static void window_touch_push(struct te_window *window, enum te_touch_type type, uint32_t time, int32_t id, wl_fixed_t x, wl_fixed_t y);
static void window_touch_down(void *data, struct wl_touch *touch, uint32_t serial, uint32_t time, struct wl_surface *surface, int32_t id, wl_fixed_t x, wl_fixed_t y);
static void window_touch_up(void *data, struct wl_touch *touch, uint32_t serial, uint32_t time, int32_t id);
static void window_touch_motion(void *data, struct wl_touch *touch, uint32_t time, int32_t id, wl_fixed_t x, wl_fixed_t y);
static void window_touch_frame(void *data, struct wl_touch *touch);
static void window_touch_cancel(void *data, struct wl_touch *touch);

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

/* The size the compositor gives the window, its request to close, and the largest size it may choose. */
static const struct xdg_toplevel_listener toplevel_listener = {
	window_toplevel_configure, window_toplevel_close, window_toplevel_bounds
};

/* The seat's devices and name. */
static const struct wl_seat_listener seat_listener = {
	window_seat_capabilities, window_seat_name
};

/* The touch screen's events of versions 1 to 5 (shape and orientation, of version 6, are never called). */
static const struct wl_touch_listener touch_listener = {
	window_touch_down, window_touch_up, window_touch_motion, window_touch_frame, window_touch_cancel, NULL, NULL
};

/* The pointer's events of versions 1 to 5 (the later members are never called). */
static const struct wl_pointer_listener pointer_listener = {
	window_pointer_enter, window_pointer_leave, window_pointer_motion, window_pointer_button,
	window_pointer_axis, window_pointer_frame, window_pointer_axis_source, window_pointer_axis_stop,
	window_pointer_axis_discrete, NULL, NULL
};

/* The keyboard's events of versions 1 to 5. */
static const struct wl_keyboard_listener keyboard_listener = {
	window_keyboard_keymap, window_keyboard_enter, window_keyboard_leave,
	window_keyboard_key, window_keyboard_modifiers, window_keyboard_repeat
};

/*
 * Connects to the compositor and makes a toplevel window of a size with
 * a title and an application ID.
 *
 * Returns 0 once the first configure is acknowledged, or -1 with errno set.
 */
int
te_window_open(
	struct te_window *window,
	const char *display,
	uint32_t width,
	uint32_t height,
	const char *title,
	const char *application)
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

	/* The clipboard and the primary selection, through the seat (without them the editor keeps its own). */
	te_clipboard_start(window);
	te_primary_start(window);

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

	/* The title the compositor shows and the application's identity. */
	xdg_toplevel_set_title(window->toplevel, title);
	xdg_toplevel_set_app_id(window->toplevel, application);
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
 * Waits up to a timeout (milliseconds, -1 for ever) for the compositor's
 * events and runs them; they queue input for te_window_take.
 *
 * Returns 0, or -1 when the connection is broken.
 */
int
te_window_dispatch(
	struct te_window *window,
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

	/* Nothing is waited for while input is already queued. */
	if (window->event_count != 0U)
		timeout = 0;

	/* Waits for the compositor. */
	descriptor.fd = wl_display_get_fd(window->display);
	descriptor.events = POLLIN;
	descriptor.revents = 0;
	status = poll(&descriptor, 1, timeout);

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
 * Takes the oldest queued touch input; zero when there is none.
 */
int
te_window_take_touch(
	struct te_window *window,
	struct te_touch_event *event)
{
	/* An empty queue. */
	if (window->touch_count == 0U)
		return 0;

	/* The oldest touch input, and the queue moves on. */
	*event = window->touches[window->touch_first];
	window->touch_first = (window->touch_first + 1U) % TE_WINDOW_TOUCHES;
	window->touch_count--;

	/* Succeeded: one touch input taken. */
	return 1;
}

/*
 * Takes the oldest queued input; zero when there is none.
 */
int
te_window_take(
	struct te_window *window,
	struct te_event *event)
{
	/* An empty queue. */
	if (window->event_count == 0U)
		return 0;

	/* The oldest input, and the queue moves on. */
	*event = window->events[window->event_first];
	window->event_first = (window->event_first + 1U) % TE_WINDOW_EVENTS;
	window->event_count--;

	/* Succeeded: one input taken. */
	return 1;
}

/*
 * Presses the held key again when its repeat is due, and reports in how
 * many milliseconds the next repeat is due (-1 when no key is held).
 */
int
te_window_repeat(
	struct te_window *window,
	uint64_t now)
{
	struct te_event *event;

	/* No key held. */
	if (window->repeat_key == 0U)
		return -1;

	/* Not yet time: the wait until it is. */
	if (now < window->repeat_at)
		return (int)(window->repeat_at - now);

	/* The key once more. */
	event = te_window_push(window, TE_EVENT_KEY);
	if (event != NULL) {
		event->key = window->repeat_key;
		event->pressed = 1;
	}

	/* The next repeat is one interval later. */
	window->repeat_at = now + window->repeat_interval;

	/* Reports the wait until the next repeat. */
	return (int)window->repeat_interval;
}

/*
 * Queues an action of the menus or the titlebar among the window's inputs,
 * so that it is carried out in the order it came.
 */
void
te_window_action(
	struct te_window *window,
	uint32_t action)
{
	struct te_event *event;

	/* The input; a full queue drops it. */
	event = te_window_push(window, TE_EVENT_ACTION);
	if (event == NULL)
		return;
	event->action = action;
}

/*
 * Sets the title the compositor shows.
 */
void
te_window_title(
	struct te_window *window,
	const char *title)
{
	/* The request, sent with the next flush. */
	xdg_toplevel_set_title(window->toplevel, title);
}

/*
 * Destroys the window's objects and disconnects.
 */
void
te_window_close(
	struct te_window *window)
{
	/* The clipboard and the primary selection before the seat they belong to. */
	te_clipboard_close(window);
	te_primary_close(window);
	free(window->clipboard);
	free(window->primary_text);

	/* The devices and the seat. */
	if (window->touch != NULL)
		wl_touch_destroy(window->touch);
	if (window->pointer != NULL)
		wl_pointer_destroy(window->pointer);
	if (window->keyboard != NULL)
		wl_keyboard_destroy(window->keyboard);
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
 * Queues a new input of a kind at the pointer's place with the modifiers
 * held (for the window's events, the menus and the titlebar); NULL when the
 * queue is full.
 */
struct te_event *
te_window_push(
	struct te_window *window,
	enum te_event_type type)
{
	struct te_event *event;
	unsigned slot;

	/* A full queue drops the input (the user is far ahead of the program). */
	if (window->event_count == TE_WINDOW_EVENTS)
		return NULL;

	/* The slot after the last one queued. */
	slot = (window->event_first + window->event_count) % TE_WINDOW_EVENTS;
	window->event_count++;

	/* The input, with what every input carries. */
	event = &window->events[slot];
	memset(event, 0, sizeof(*event));
	event->type = type;
	event->x = window->pointer_x;
	event->y = window->pointer_y;
	event->modifiers = window->modifiers;
	event->time = te_clock();

	/* Reports the queued input for its details. */
	return event;
}

/* Binds the compositor, the shell, the clipboard's and the primary selection's managers, and the first seat. */
static void
window_global(
	void *data,
	struct wl_registry *registry,
	uint32_t name,
	const char *interface,
	uint32_t version)
{
	struct te_window *window;
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

	/* The shell gives the surface its window role; version 4 tells the largest size the window may choose. */
	match = strcmp(interface, "xdg_wm_base");
	if (match == 0 && window->shell == NULL) {
		if (version > 4U)
			version = 4U;
		window->shell = wl_registry_bind(registry, name, &xdg_wm_base_interface, version);
		if (window->shell != NULL)
			(void)xdg_wm_base_add_listener(window->shell, &shell_listener, window);
		return;
	}

	/* The clipboard's manager. */
	match = strcmp(interface, "wl_data_device_manager");
	if (match == 0 && window->data_manager == NULL) {
		te_clipboard_bind(window, registry, name, version);
		return;
	}

	/* The primary selection's manager. */
	match = strcmp(interface, "zwp_primary_selection_device_manager_v1");
	if (match == 0 && window->primary_manager == NULL) {
		te_primary_bind(window, registry, name);
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
	struct te_window *window;

	/* The acknowledgement comes before any image of the new state. */
	window = data;
	xdg_surface_ack_configure(surface, serial);
	window->configured = 1;
}

/* Takes the size the compositor gives; a zero size keeps the window's own, within the bounds. */
static void
window_toplevel_configure(
	void *data,
	struct xdg_toplevel *toplevel,
	int32_t width,
	int32_t height,
	struct wl_array *states)
{
	struct te_window *window;

	/* The states change nothing here. */
	(void)toplevel;
	(void)states;
	window = data;

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
	struct te_window *window;

	/* The main loop ends the program. */
	(void)toplevel;
	window = data;
	window->closed = 1;
}

/* Keeps the largest size the compositor lets the window choose for itself (0: not known). */
static void
window_toplevel_bounds(
	void *data,
	struct xdg_toplevel *toplevel,
	int32_t width,
	int32_t height)
{
	struct te_window *window;

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

/* Takes the seat's pointer, keyboard and touch screen when it has them. */
static void
window_seat_capabilities(
	void *data,
	struct wl_seat *seat,
	uint32_t capabilities)
{
	struct te_window *window;

	/* A pointer, once. */
	window = data;
	if ((capabilities & WL_SEAT_CAPABILITY_POINTER) != 0U && window->pointer == NULL) {
		window->pointer = wl_seat_get_pointer(seat);
		if (window->pointer != NULL)
			(void)wl_pointer_add_listener(window->pointer, &pointer_listener, window);
	}

	/* A keyboard, once. */
	if ((capabilities & WL_SEAT_CAPABILITY_KEYBOARD) != 0U && window->keyboard == NULL) {
		window->keyboard = wl_seat_get_keyboard(seat);
		if (window->keyboard != NULL)
			(void)wl_keyboard_add_listener(window->keyboard, &keyboard_listener, window);
	}

	/* A touch screen, once (ws081-p012). */
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

/* The pointer comes over the window: a motion to where it is. */
static void
window_pointer_enter(
	void *data,
	struct wl_pointer *pointer,
	uint32_t serial,
	struct wl_surface *surface,
	wl_fixed_t x,
	wl_fixed_t y)
{
	struct te_window *window;

	/* The pointer's place, as a motion. */
	(void)pointer;
	(void)surface;
	window = data;
	window->serial = serial;
	window->pointer_x = wl_fixed_to_int(x);
	window->pointer_y = wl_fixed_to_int(y);
	(void)te_window_push(window, TE_EVENT_MOTION);
}

/* The pointer leaves the window. */
static void
window_pointer_leave(
	void *data,
	struct wl_pointer *pointer,
	uint32_t serial,
	struct wl_surface *surface)
{
	struct te_window *window;

	/* The view hears that the pointer left. */
	(void)pointer;
	(void)serial;
	(void)surface;
	window = data;
	(void)te_window_push(window, TE_EVENT_LEAVE);
}

/* The pointer moves over the window. */
static void
window_pointer_motion(
	void *data,
	struct wl_pointer *pointer,
	uint32_t time,
	wl_fixed_t x,
	wl_fixed_t y)
{
	struct te_window *window;

	/* The new place, as a motion. */
	(void)pointer;
	(void)time;
	window = data;
	window->pointer_x = wl_fixed_to_int(x);
	window->pointer_y = wl_fixed_to_int(y);
	(void)te_window_push(window, TE_EVENT_MOTION);
}

/* A pointer button is pressed or let go. */
static void
window_pointer_button(
	void *data,
	struct wl_pointer *pointer,
	uint32_t serial,
	uint32_t time,
	uint32_t button,
	uint32_t state)
{
	struct te_window *window;
	struct te_event *event;

	/* The button as an input; its serial may set a selection. */
	(void)pointer;
	(void)time;
	window = data;
	window->serial = serial;
	event = te_window_push(window, TE_EVENT_BUTTON);
	if (event == NULL)
		return;
	event->button = button;
	event->pressed = 0;
	if (state == WL_POINTER_BUTTON_STATE_PRESSED)
		event->pressed = 1;
}

/* The wheel turns: scrolling in pixels, down and right positive. */
static void
window_pointer_axis(
	void *data,
	struct wl_pointer *pointer,
	uint32_t time,
	uint32_t axis,
	wl_fixed_t value)
{
	struct te_window *window;
	struct te_event *event;

	/* The distance, scaled to the window's pixels, on its axis. */
	(void)pointer;
	(void)time;
	window = data;
	event = te_window_push(window, TE_EVENT_AXIS);
	if (event == NULL)
		return;
	if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL)
		event->scroll = wl_fixed_to_int(value) * WINDOW_SCROLL_SCALE;
	else
		event->scroll_x = wl_fixed_to_int(value) * WINDOW_SCROLL_SCALE;
}

/* A group of pointer events ends; each was queued as it came. */
static void
window_pointer_frame(
	void *data,
	struct wl_pointer *pointer)
{
	/* Nothing to do. */
	(void)data;
	(void)pointer;
}

/* The source of scrolling is not used. */
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

/* The end of scrolling is not used. */
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

/* The wheel's notches are not used (the axis value already says how far). */
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

/* Closes the keymap file: keys arrive as evdev codes and the editor has its own layout (keys.c). */
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

/* Focus arrives: no key is held yet as far as the window is concerned, and the editor hears of it. */
static void
window_keyboard_enter(
	void *data,
	struct wl_keyboard *keyboard,
	uint32_t serial,
	struct wl_surface *surface,
	struct wl_array *keys)
{
	struct te_window *window;
	struct te_event *event;

	/* Keys already held when focus came are not pressed again. */
	(void)keyboard;
	(void)surface;
	(void)keys;
	window = data;
	window->serial = serial;
	window->repeat_key = 0U;

	/* The focus came. */
	event = te_window_push(window, TE_EVENT_FOCUS);
	if (event != NULL)
		event->pressed = 1;
}

/* Focus leaves: nothing repeats any more. */
static void
window_keyboard_leave(
	void *data,
	struct wl_keyboard *keyboard,
	uint32_t serial,
	struct wl_surface *surface)
{
	struct te_window *window;

	/* The held key stops repeating, and modifiers are forgotten. */
	(void)keyboard;
	(void)serial;
	(void)surface;
	window = data;
	window->repeat_key = 0U;
	window->modifiers = 0U;

	/* The focus went. */
	(void)te_window_push(window, TE_EVENT_FOCUS);
}

/* A key is pressed (it repeats while held) or let go. */
static void
window_keyboard_key(
	void *data,
	struct wl_keyboard *keyboard,
	uint32_t serial,
	uint32_t time,
	uint32_t key,
	uint32_t state)
{
	struct te_window *window;
	struct te_event *event;
	int modifier;

	/* The key as an input; its serial may set a selection. */
	(void)keyboard;
	(void)time;
	window = data;
	window->serial = serial;
	event = te_window_push(window, TE_EVENT_KEY);
	if (event != NULL) {
		event->key = key;
		event->pressed = 0;
		if (state == WL_KEYBOARD_KEY_STATE_PRESSED)
			event->pressed = 1;
	}

	/* A release of the repeating key stops it. */
	if (state != WL_KEYBOARD_KEY_STATE_PRESSED) {
		if (key == window->repeat_key)
			window->repeat_key = 0U;
		return;
	}

	/* A key that is not a modifier repeats while held. */
	modifier = window_modifier_key(key);
	if (modifier == 0) {
		window->repeat_key = key;
		window->repeat_at = te_clock() + window->repeat_delay;
	}
}

/* Keeps the modifiers held, in the editor's own bits. */
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
	struct te_window *window;

	/* Only the held modifiers count; zdesktop latches and locks nothing. */
	(void)keyboard;
	(void)serial;
	(void)latched;
	(void)locked;
	(void)group;
	window = data;
	window->modifiers = 0U;
	if ((depressed & WINDOW_WAYLAND_SHIFT) != 0U)
		window->modifiers |= TE_MOD_SHIFT;
	if ((depressed & WINDOW_WAYLAND_CTRL) != 0U)
		window->modifiers |= TE_MOD_CTRL;
	if ((depressed & WINDOW_WAYLAND_ALT) != 0U)
		window->modifiers |= TE_MOD_ALT;
	if ((depressed & WINDOW_WAYLAND_SUPER) != 0U)
		window->modifiers |= TE_MOD_SUPER;
}

/* Takes the compositor's repeat rate and delay, when it gives a rate. */
static void
window_keyboard_repeat(
	void *data,
	struct wl_keyboard *keyboard,
	int32_t rate,
	int32_t delay)
{
	struct te_window *window;

	/* A positive rate is keys per second. */
	(void)keyboard;
	window = data;
	if (rate > 0)
		window->repeat_interval = 1000U / (uint32_t)rate;
	if (delay > 0)
		window->repeat_delay = (uint32_t)delay;
}

/* Tells whether a key is a modifier (which does not repeat). */
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

	/* Every other key repeats. */
	return 0;
}

/* Queues a touch input with the time the window read it; a full queue drops it. */
static void
window_touch_push(
	struct te_window *window,
	enum te_touch_type type,
	uint32_t time,
	int32_t id,
	wl_fixed_t x,
	wl_fixed_t y)
{
	struct te_touch_event *event;
	unsigned slot;

	/* A full queue drops the input (the fingers are far ahead of the program). */
	if (window->touch_count == TE_WINDOW_TOUCHES)
		return;

	/* The slot after the last one queued. */
	slot = (window->touch_first + window->touch_count) % TE_WINDOW_TOUCHES;
	window->touch_count++;

	/* The input. */
	event = &window->touches[slot];
	memset(event, 0, sizeof(*event));
	event->type = type;
	event->id = id;
	event->x = wl_fixed_to_double(x);
	event->y = wl_fixed_to_double(y);
	event->time = time;
	event->arrival = te_touch_clock();
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
	/* Queued; the window has one surface. */
	(void)touch;
	(void)serial;
	(void)surface;
	window_touch_push(data, TE_TOUCH_DOWN, time, id, x, y);
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
	window_touch_push(data, TE_TOUCH_UP, time, id, 0, 0);
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
	window_touch_push(data, TE_TOUCH_MOTION, time, id, x, y);
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
	window_touch_push(data, TE_TOUCH_CANCEL, 0, -1, 0, 0);
}
