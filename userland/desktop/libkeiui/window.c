/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The window of the library (ws090-p004, Text Editor's window.c moved
 * here, itself PDF Viewer's): an xdg-shell toplevel of its own connection,
 * and its seat's pointer, keyboard and touch screen, whose input becomes
 * kui_window_event values in a queue the application takes.  The last
 * input's serial is kept for the clipboard and the primary selection
 * (clipboard.c, primary.c), whose managers are bound here, and for the
 * context menus.
 *
 * zdesktop does not repeat keys, so a key held past the repeat delay is
 * pressed again on each interval -- only by kui_window_repeat, which the
 * application calls after a dispatch, so that a release read in the same
 * dispatch stops it first (BUG-111).
 */

#include "window.h"

#include <errno.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* The seat version the window understands (frames and discrete axes). */
#define WINDOW_SEAT_VERSION	5U

/* The repeat's delay and interval when the compositor gives none, in milliseconds. */
#define WINDOW_REPEAT_DELAY	400U
#define WINDOW_REPEAT_INTERVAL	40U

/* How many pixels one unit of scrolling moves (zdesktop sends 15 units a wheel notch). */
#define WINDOW_SCROLL_SCALE	4.0

/* The oldest a wl_touch time may be and still be taken (older is another clock), in milliseconds. */
#define WINDOW_TOUCH_BEHIND	2000U

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
static void window_touch_push(struct kui_window *window, unsigned kind, uint32_t time, int32_t id, wl_fixed_t x, wl_fixed_t y);
static void window_touch_down(void *data, struct wl_touch *touch, uint32_t serial, uint32_t time, struct wl_surface *surface, int32_t id, wl_fixed_t x, wl_fixed_t y);
static void window_touch_up(void *data, struct wl_touch *touch, uint32_t serial, uint32_t time, int32_t id);
static void window_touch_motion(void *data, struct wl_touch *touch, uint32_t time, int32_t id, wl_fixed_t x, wl_fixed_t y);
static void window_touch_frame(void *data, struct wl_touch *touch);
static void window_touch_cancel(void *data, struct wl_touch *touch);
static int window_touch_foreign(struct kui_window *window, int32_t id, int forget);
static struct kui_window_event *window_push(struct kui_window *window, unsigned kind);
static int window_setup(struct kui_window *window, const struct kui_window_options *options);

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
 * Connects to the compositor and makes a toplevel window, configured and
 * ready to be drawn into (the presenter made, except for
 * KUI_PRESENT_NONE).
 *
 * Returns NULL with errno set: EINVAL (no options, an unknown way of
 * showing), a connection's error, EOPNOTSUPP (no compositor or shell, or
 * no shared memory for KUI_PRESENT_SHM), EPROTO (no configure), ENOMEM,
 * EIO (Vulkan refused).
 */
struct kui_window *
kui_window_open(
	const struct kui_window_options *options)
{
	struct kui_window *window;
	int error;

	/* Only the three ways of showing. */
	if (options == NULL || options->present > KUI_PRESENT_NONE) {
		errno = EINVAL;
		return NULL;
	}

	/* The record. */
	window = calloc(1, sizeof(*window));
	if (window == NULL) {
		errno = ENOMEM;
		return NULL;
	}

	/* The connection, the globals, the surface and its first configure. */
	error = window_setup(window, options);
	if (error != 0) {
		kui_window_close(window);
		errno = error;
		return NULL;
	}

	/* Succeeded: the window can be drawn into. */
	return window;
}

/*
 * Destroys the window's objects and disconnects.
 */
void
kui_window_close(
	struct kui_window *window)
{
	/* No window, nothing to close. */
	if (window == NULL)
		return;

	/* The presenter before the surface it shows on. */
	if (window->present == KUI_PRESENT_VULKAN)
		keiui_present_close(&window->vulkan);
	keiui_shm_close(window);

	/* The clipboard and the primary selection before the seat they belong to. */
	keiui_clipboard_close(window);
	keiui_primary_close(window);
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
	if (window->shm != NULL)
		wl_shm_destroy(window->shm);
	if (window->compositor != NULL)
		wl_compositor_destroy(window->compositor);
	if (window->registry != NULL)
		wl_registry_destroy(window->registry);

	/* The connection last, then the record. */
	if (window->display != NULL)
		wl_display_disconnect(window->display);
	free(window);
}

/*
 * Waits up to a timeout (milliseconds, -1 for ever) for the compositor's
 * events and runs them; they queue input for kui_window_take.
 *
 * Returns 0, or -1 when the connection is broken.
 */
int
kui_window_dispatch(
	struct kui_window *window,
	int timeout_ms)
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
		timeout_ms = 0;

	/* Waits for the compositor. */
	descriptor.fd = wl_display_get_fd(window->display);
	descriptor.events = POLLIN;
	descriptor.revents = 0;
	status = poll(&descriptor, 1, timeout_ms);

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
 * Takes the oldest queued input.  Returns 1 with it in *event, 0 when
 * there is none.
 */
int
kui_window_take(
	struct kui_window *window,
	struct kui_window_event *event)
{
	/* An empty queue. */
	if (window->event_count == 0U)
		return 0;

	/* The oldest input, and the queue moves on. */
	*event = window->events[window->event_first];
	window->event_first = (window->event_first + 1U) % KEIUI_WINDOW_EVENTS;
	window->event_count--;

	/* Succeeded: one input taken. */
	return 1;
}

/*
 * Presses the held key again when its repeat is due (the application
 * calls it after a dispatch), and reports in how many milliseconds the
 * next repeat is due (-1 when no key is held).
 */
int
kui_window_repeat(
	struct kui_window *window,
	uint64_t now_us)
{
	struct kui_window_event *event;
	uint64_t now;

	/* No key held. */
	if (window->repeat_key == 0U)
		return -1;

	/* Not yet time: the wait until it is. */
	now = now_us / 1000U;
	if (now < window->repeat_at)
		return (int)(window->repeat_at - now);

	/* The key once more. */
	event = window_push(window, KUI_WINDOW_KEY);
	if (event != NULL) {
		event->code = window->repeat_key;
		event->pressed = 1;
		event->repeated = 1;
	}

	/* The next repeat is one interval later. */
	window->repeat_at = now + window->repeat_interval;

	/* Reports the wait until the next repeat. */
	return (int)window->repeat_interval;
}

/*
 * Reports in how many milliseconds the held key's repeat is due (0: now,
 * -1 when no key is held), without pressing it: the loop waits that long
 * and reads the compositor's events (a release among them) before the
 * repeat is pressed (BUG-111).
 */
int
kui_window_repeat_wait(
	const struct kui_window *window,
	uint64_t now_us)
{
	uint64_t now;

	/* No key held. */
	if (window->repeat_key == 0U)
		return -1;

	/* Due already. */
	now = now_us / 1000U;
	if (now >= window->repeat_at)
		return 0;

	/* Reports the wait until it is due. */
	return (int)(window->repeat_at - now);
}

/*
 * Sets the title the compositor shows.
 */
void
kui_window_set_title(
	struct kui_window *window,
	const char *title)
{
	/* The request, sent with the next flush. */
	xdg_toplevel_set_title(window->toplevel, title);
}

/*
 * Reports the size the compositor gives the window (the one a frame is
 * drawn at after kui_window_present_resize).
 */
void
kui_window_size(
	const struct kui_window *window,
	uint32_t *width,
	uint32_t *height)
{
	/* The configured size. */
	*width = window->width;
	*height = window->height;
}

/*
 * Makes the presenter fit the window's size and reports the size a frame
 * is drawn at.  Returns 0, or EIO when Vulkan refused.
 */
int
kui_window_present_resize(
	struct kui_window *window,
	uint32_t *width,
	uint32_t *height)
{
	VkResult result;

	/* Vulkan: a swapchain of the size, whose extent may differ. */
	if (window->present == KUI_PRESENT_VULKAN) {
		result = keiui_present_resize(&window->vulkan, window->width, window->height);
		if (result != VK_SUCCESS)
			return EIO;
		*width = window->vulkan.extent.width;
		*height = window->vulkan.extent.height;
		return 0;
	}

	/* Shared memory, or the application's own drawing: the window's size. */
	*width = window->width;
	*height = window->height;
	window->shm_width = window->width;
	window->shm_height = window->height;

	/* Succeeded: frames are drawn at that size. */
	return 0;
}

/*
 * Shows a frame (premultiplied 0xAARRGGBB words, stride words a row) of
 * the size kui_window_present_resize reported.  Returns 0, EAGAIN when it
 * could not be shown now (the swapchain is out of date: resize and draw
 * again; no shared-memory buffer is free: draw again later), EINVAL for
 * KUI_PRESENT_NONE, or EIO.
 */
int
kui_window_present(
	struct kui_window *window,
	const uint32_t *pixels,
	size_t stride)
{
	VkResult result;
	int error;

	/* Shared memory. */
	if (window->present == KUI_PRESENT_SHM) {
		error = keiui_shm_present(window, pixels, stride);
		if (error != 0)
			return error;
		return 0;
	}

	/* The application draws its own frames. */
	if (window->present != KUI_PRESENT_VULKAN)
		return EINVAL;

	/* Vulkan: out of date asks for a resize and another frame. */
	result = keiui_present_frame(&window->vulkan, pixels, stride);
	if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
		return EAGAIN;
	if (result != VK_SUCCESS)
		return EIO;

	/* Succeeded: the frame is shown. */
	return 0;
}

/*
 * Tells whether the frames are blended by their alpha (a see-through
 * swapchain, or shared memory), which zdesktop's glass needs.
 */
int
kui_window_see_through(
	const struct kui_window *window)
{
	/* Shared memory is ARGB, blended by its alpha. */
	if (window->present == KUI_PRESENT_SHM)
		return 1;

	/* Vulkan: when the swapchain is premultiplied. */
	if (window->present == KUI_PRESENT_VULKAN && window->vulkan.premultiplied)
		return 1;

	/* Opaque. */
	return 0;
}

/*
 * Reports the window's connection (for libkeiland's menus, titlebar and glass).
 */
struct wl_display *
kui_window_display(
	const struct kui_window *window)
{
	/* The connection. */
	return window->display;
}

/*
 * Reports the window's surface.
 */
struct wl_surface *
kui_window_surface(
	const struct kui_window *window)
{
	/* The surface. */
	return window->surface;
}

/*
 * Reports the window's toplevel.
 */
struct xdg_toplevel *
kui_window_toplevel(
	const struct kui_window *window)
{
	/* The toplevel. */
	return window->toplevel;
}

/*
 * Reports the serial of the window's last input.
 */
uint32_t
kui_window_serial(
	const struct kui_window *window)
{
	/* The serial. */
	return window->serial;
}

/*
 * Reports the serial of the last press of a button or a finger on the
 * window (a context menu opens for a press).
 */
uint32_t
kui_window_press_serial(
	const struct kui_window *window)
{
	/* The press's serial. */
	return window->press_serial;
}

/*
 * Reports the monotonic clock in microseconds (the clock of the library's
 * times).
 */
uint64_t
kui_clock_us(void)
{
	struct timespec now;

	/* The monotonic clock. */
	(void)clock_gettime(CLOCK_MONOTONIC, &now);

	/* Reports it in microseconds. */
	return (uint64_t)now.tv_sec * 1000000U + (uint64_t)now.tv_nsec / 1000U;
}

/*
 * Reports the monotonic clock in milliseconds.
 */
uint64_t
keiui_clock_ms(void)
{
	uint64_t now;

	/* The microseconds' clock. */
	now = kui_clock_us();

	/* Reports it in milliseconds. */
	return now / 1000U;
}

/* Connects, binds the globals, makes the surface and its toplevel, waits for the first configure and makes the presenter; 0 or an errno value. */
static int
window_setup(
	struct kui_window *window,
	const struct kui_window_options *options)
{
	VkResult result;
	int status;

	/* The size the window asks for until the compositor gives one, and the key repeat's defaults. */
	window->width = options->width;
	window->height = options->height;
	window->preferred_width = options->width;
	window->preferred_height = options->height;
	window->repeat_delay = WINDOW_REPEAT_DELAY;
	window->repeat_interval = WINDOW_REPEAT_INTERVAL;
	window->present = options->present;

	/* The connection. */
	window->display = wl_display_connect(options->display);
	if (window->display == NULL)
		return errno;

	/* The globals: the compositor, shared memory, the shell, the seat and the selections' managers. */
	window->registry = wl_display_get_registry(window->display);
	if (window->registry == NULL)
		return ENOMEM;
	status = wl_registry_add_listener(window->registry, &registry_listener, window);
	if (status != 0)
		return EINVAL;
	status = wl_display_roundtrip(window->display);
	if (status < 0)
		return EPROTO;

	/* The clipboard and the primary selection, through the seat (without them the window keeps its own copies). */
	keiui_clipboard_start(window);
	keiui_primary_start(window);

	/* A window needs a compositor and a shell, and shared memory to show frames through it. */
	if (window->compositor == NULL || window->shell == NULL)
		return EOPNOTSUPP;
	if (window->present == KUI_PRESENT_SHM && window->shm == NULL)
		return EOPNOTSUPP;

	/* The surface, as an xdg surface. */
	window->surface = wl_compositor_create_surface(window->compositor);
	if (window->surface == NULL)
		return ENOMEM;
	window->role = xdg_wm_base_get_xdg_surface(window->shell, window->surface);
	if (window->role == NULL)
		return ENOMEM;
	status = xdg_surface_add_listener(window->role, &surface_listener, window);
	if (status != 0)
		return EINVAL;

	/* A toplevel window with its size and close request heard. */
	window->toplevel = xdg_surface_get_toplevel(window->role);
	if (window->toplevel == NULL)
		return ENOMEM;
	status = xdg_toplevel_add_listener(window->toplevel, &toplevel_listener, window);
	if (status != 0)
		return EINVAL;

	/* The title the compositor shows and the application's identity. */
	if (options->title != NULL)
		xdg_toplevel_set_title(window->toplevel, options->title);
	if (options->application != NULL)
		xdg_toplevel_set_app_id(window->toplevel, options->application);
	wl_surface_commit(window->surface);

	/* The first configure (and the seat's devices) before anything is drawn. */
	status = wl_display_roundtrip(window->display);
	if (status < 0)
		return EPROTO;
	if (window->configured == 0)
		return EPROTO;

	/* The size given is not news to the application. */
	window->event_count = 0;

	/* The Vulkan presenter over the surface. */
	if (window->present == KUI_PRESENT_VULKAN) {
		result = keiui_present_open(&window->vulkan, window);
		if (result != VK_SUCCESS)
			return EIO;
	}

	/* Succeeded: the window is configured. */
	window->shm_width = window->width;
	window->shm_height = window->height;
	return 0;
}

/*
 * Queues a new input of a kind at the pointer's place with the modifiers
 * held; NULL when the queue is full (the input is dropped).
 */
static struct kui_window_event *
window_push(
	struct kui_window *window,
	unsigned kind)
{
	struct kui_window_event *event;
	unsigned slot;

	/* A full queue drops the input (the user is far ahead of the program). */
	if (window->event_count == KEIUI_WINDOW_EVENTS)
		return NULL;

	/* The slot after the last one queued. */
	slot = (window->event_first + window->event_count) % KEIUI_WINDOW_EVENTS;
	window->event_count++;

	/* The input, with what every input carries. */
	event = &window->events[slot];
	memset(event, 0, sizeof(*event));
	event->kind = kind;
	event->x = window->pointer_x;
	event->y = window->pointer_y;
	event->modifiers = window->modifiers;
	event->serial = window->serial;
	event->arrival_us = kui_clock_us();
	event->time_us = event->arrival_us;

	/* Reports the queued input for its details. */
	return event;
}

/* Binds the compositor, shared memory, the shell, the clipboard's and the primary selection's managers, and the first seat. */
static void
window_global(
	void *data,
	struct wl_registry *registry,
	uint32_t name,
	const char *interface,
	uint32_t version)
{
	struct kui_window *window;
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

	/* Shared memory, for frames shown through it. */
	match = strcmp(interface, "wl_shm");
	if (match == 0 && window->shm == NULL) {
		window->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1U);
		return;
	}

	/* The clipboard's manager. */
	match = strcmp(interface, "wl_data_device_manager");
	if (match == 0 && window->data_manager == NULL) {
		keiui_clipboard_bind(window, registry, name, version);
		return;
	}

	/* The primary selection's manager. */
	match = strcmp(interface, "zwp_primary_selection_device_manager_v1");
	if (match == 0 && window->primary_manager == NULL) {
		keiui_primary_bind(window, registry, name);
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
	struct kui_window *window;

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
	struct kui_window *window;
	int resized;

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
	resized = 0;
	if (width > 0 && (uint32_t)width != window->width) {
		window->width = (uint32_t)width;
		resized = 1;
	}

	/* And so does a new height. */
	if (height > 0 && (uint32_t)height != window->height) {
		window->height = (uint32_t)height;
		resized = 1;
	}

	/* The application hears of a new size. */
	if (resized)
		(void)window_push(window, KUI_WINDOW_RESIZE);
}

/* The compositor asks the window to close (its close button). */
static void
window_toplevel_close(
	void *data,
	struct xdg_toplevel *toplevel)
{
	struct kui_window *window;

	/* The application decides (it may ask about unsaved work first). */
	(void)toplevel;
	window = data;
	(void)window_push(window, KUI_WINDOW_CLOSE);
}

/* Keeps the largest size the compositor lets the window choose for itself (0: not known). */
static void
window_toplevel_bounds(
	void *data,
	struct xdg_toplevel *toplevel,
	int32_t width,
	int32_t height)
{
	struct kui_window *window;

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
	struct kui_window *window;

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
	struct kui_window *window;

	/* Another surface of the program (the file chooser's) is not the window's. */
	(void)pointer;
	window = data;
	window->pointer_ours = 0;
	if (surface == NULL || surface != window->surface)
		return;
	window->pointer_ours = 1;

	/* The pointer's place, as a motion. */
	window->serial = serial;
	window->pointer_x = wl_fixed_to_double(x);
	window->pointer_y = wl_fixed_to_double(y);
	(void)window_push(window, KUI_WINDOW_MOTION);
}

/* The pointer leaves the window. */
static void
window_pointer_leave(
	void *data,
	struct wl_pointer *pointer,
	uint32_t serial,
	struct wl_surface *surface)
{
	struct kui_window *window;

	/* Only leaving the window's own surface matters. */
	(void)pointer;
	(void)serial;
	window = data;
	if (surface == NULL || surface != window->surface)
		return;
	window->pointer_ours = 0;

	/* The view hears that the pointer left. */
	(void)window_push(window, KUI_WINDOW_LEAVE);
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
	struct kui_window *window;

	/* Only over the window's own surface. */
	(void)pointer;
	(void)time;
	window = data;
	if (!window->pointer_ours)
		return;

	/* The new place, as a motion. */
	window->pointer_x = wl_fixed_to_double(x);
	window->pointer_y = wl_fixed_to_double(y);
	(void)window_push(window, KUI_WINDOW_MOTION);
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
	struct kui_window *window;
	struct kui_window_event *event;

	/* Only over the window's own surface. */
	(void)pointer;
	(void)time;
	window = data;
	if (!window->pointer_ours)
		return;

	/* The button as an input; its serial may set a selection. */
	window->serial = serial;
	if (state == WL_POINTER_BUTTON_STATE_PRESSED)
		window->press_serial = serial;
	event = window_push(window, KUI_WINDOW_BUTTON);
	if (event == NULL)
		return;
	event->code = button;
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
	struct kui_window *window;
	struct kui_window_event *event;

	/* Only over the window's own surface. */
	(void)pointer;
	(void)time;
	window = data;
	if (!window->pointer_ours)
		return;

	/* The distance, scaled to the window's pixels, on its axis. */
	event = window_push(window, KUI_WINDOW_AXIS);
	if (event == NULL)
		return;
	if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL)
		event->dy = (double)wl_fixed_to_int(value) * WINDOW_SCROLL_SCALE;
	else
		event->dx = (double)wl_fixed_to_int(value) * WINDOW_SCROLL_SCALE;
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

/* Closes the keymap file: keys arrive as evdev codes and the library has its own layout (input.c). */
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

/* Focus arrives: no key is held yet as far as the window is concerned, and the application hears of it. */
static void
window_keyboard_enter(
	void *data,
	struct wl_keyboard *keyboard,
	uint32_t serial,
	struct wl_surface *surface,
	struct wl_array *keys)
{
	struct kui_window *window;
	struct kui_window_event *event;

	/* Keys already held when focus came are not pressed again. */
	(void)keyboard;
	(void)keys;
	window = data;
	window->repeat_key = 0U;

	/* Another surface of the program (the file chooser's) has the keys, not the window. */
	window->keyboard_ours = 0;
	if (surface == NULL || surface != window->surface)
		return;
	window->keyboard_ours = 1;
	window->serial = serial;

	/* The focus came. */
	event = window_push(window, KUI_WINDOW_FOCUS);
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
	struct kui_window *window;

	/* Only leaving the window's own surface matters. */
	(void)keyboard;
	(void)serial;
	window = data;
	if (surface == NULL || surface != window->surface)
		return;
	window->keyboard_ours = 0;

	/* The held key stops repeating, and modifiers are forgotten. */
	window->repeat_key = 0U;
	window->modifiers = 0U;

	/* The focus went. */
	(void)window_push(window, KUI_WINDOW_FOCUS);
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
	struct kui_window *window;
	struct kui_window_event *event;
	int modifier;

	/* Only while the window's own surface has the keys. */
	(void)keyboard;
	(void)time;
	window = data;
	if (!window->keyboard_ours)
		return;

	/* The key as an input; its serial may set a selection. */
	window->serial = serial;
	event = window_push(window, KUI_WINDOW_KEY);
	if (event != NULL) {
		event->code = key;
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
		window->repeat_at = keiui_clock_ms() + window->repeat_delay;
	}
}

/* Keeps the modifiers held, in the window's own bits. */
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
	struct kui_window *window;

	/* Only the held modifiers count; zdesktop latches and locks nothing. */
	(void)keyboard;
	(void)serial;
	(void)latched;
	(void)locked;
	(void)group;
	window = data;
	window->modifiers = 0U;
	if ((depressed & WINDOW_WAYLAND_SHIFT) != 0U)
		window->modifiers |= KUI_MOD_SHIFT;
	if ((depressed & WINDOW_WAYLAND_CTRL) != 0U)
		window->modifiers |= KUI_MOD_CTRL;
	if ((depressed & WINDOW_WAYLAND_ALT) != 0U)
		window->modifiers |= KUI_MOD_ALT;
	if ((depressed & WINDOW_WAYLAND_SUPER) != 0U)
		window->modifiers |= KUI_MOD_SUPER;
}

/* Takes the compositor's repeat rate and delay, when it gives a rate. */
static void
window_keyboard_repeat(
	void *data,
	struct wl_keyboard *keyboard,
	int32_t rate,
	int32_t delay)
{
	struct kui_window *window;

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

/* Queues a touch input with its time turned into the monotonic clock's microseconds. */
static void
window_touch_push(
	struct kui_window *window,
	unsigned kind,
	uint32_t time,
	int32_t id,
	wl_fixed_t x,
	wl_fixed_t y)
{
	struct kui_window_event *event;
	uint64_t arrival_ms;
	uint32_t behind;

	/* The input; a full queue drops it. */
	event = window_push(window, kind);
	if (event == NULL)
		return;
	event->id = id;
	event->x = wl_fixed_to_double(x);
	event->y = wl_fixed_to_double(y);

	/* The event's time: the compositor's milliseconds (the low 32 bits) behind the reading, else the reading's time. */
	arrival_ms = event->arrival_us / 1000U;
	behind = (uint32_t)arrival_ms - time;
	event->time_us = event->arrival_us;
	if (kind != KUI_WINDOW_TOUCH_CANCEL && behind <= WINDOW_TOUCH_BEHIND)
		event->time_us = (arrival_ms - behind) * 1000U;
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
	struct kui_window *window;

	/* A finger on another surface of the program (the file chooser's) is remembered and left alone. */
	(void)touch;
	window = data;
	if (surface == NULL || surface != window->surface) {
		if (window->foreign_count < KEIUI_WINDOW_FOREIGN) {
			window->foreign[window->foreign_count] = id;
			window->foreign_count++;
		}

		/* The chooser follows it itself. */
		return;
	}

	/* Queued, its serial kept for a long press's context menu. */
	window->serial = serial;
	window->press_serial = serial;
	window_touch_push(window, KUI_WINDOW_TOUCH_DOWN, time, id, x, y);
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
	int foreign;

	/* A finger of another surface is forgotten, not queued. */
	(void)touch;
	(void)serial;
	foreign = window_touch_foreign(data, id, 1);
	if (foreign)
		return;

	/* Queued, with no place. */
	window_touch_push(data, KUI_WINDOW_TOUCH_UP, time, id, 0, 0);
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
	int foreign;

	/* A finger of another surface is not the window's. */
	(void)touch;
	foreign = window_touch_foreign(data, id, 0);
	if (foreign)
		return;

	/* Queued. */
	window_touch_push(data, KUI_WINDOW_TOUCH_MOTION, time, id, x, y);
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
	struct kui_window *window;

	/* Every finger goes, the other surfaces' too; the window's are cancelled. */
	(void)touch;
	window = data;
	window->foreign_count = 0;
	window_touch_push(data, KUI_WINDOW_TOUCH_CANCEL, 0, -1, 0, 0);
}

/* Tells whether a finger is down on another surface of the program, and forgets it when asked (its lift). */
static int
window_touch_foreign(
	struct kui_window *window,
	int32_t id,
	int forget)
{
	unsigned index;

	/* Each finger of the other surfaces. */
	for (index = 0; index < window->foreign_count; index++) {
		if (window->foreign[index] != id)
			continue;

		/* Found: forgotten at its lift, the last one taking its slot. */
		if (forget) {
			window->foreign_count--;
			window->foreign[index] = window->foreign[window->foreign_count];
		}

		/* One of another surface's. */
		return 1;
	}

	/* One of the window's own. */
	return 0;
}
