/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The connection to the desktop: standard Wayland (xdg-shell windows,
 * wl_shm buffers, wl_seat input).
 *
 * Each top-level X window has an xdg toplevel, which the desktop places
 * and decorates.  Its pixels are copied into one of its two wl_shm
 * buffers, the one the desktop has released, and committed with the
 * changed rectangle as damage.  The pointer's events become pointer
 * frames on the root window (the place in the window plus the window's
 * corner there), and the keyboard's evdev codes become X keycodes.
 */

#include "userland/base/zdesktop-x11server/internal.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client.h>
#include <xdg-shell-client-protocol.h>

/* The evdev codes of the pointer's buttons, and the key that toggles caps lock. */
#define WAYLAND_BTN_LEFT	0x110U
#define WAYLAND_BTN_RIGHT	0x111U
#define WAYLAND_BTN_MIDDLE	0x112U
#define WAYLAND_KEY_CAPSLOCK	58U

/* How many evdev key codes are remembered for their releases. */
#define WAYLAND_KEYS		256U

/* The modifier bits of wl_keyboard.modifiers kept (the same bits as X's). */
#define WAYLAND_MODIFIERS	(X11_SHIFT_MASK | X11_CONTROL_MASK | X11_ALT_MASK)

/* The application's identity on the desktop. */
#define WAYLAND_APP_ID		"zdesktop-x11server"

/*
 * One wl_shm buffer of a window, and whether the desktop holds it.
 */
struct wayland_buffer {
	struct wl_buffer *buffer;
	uint32_t *pixels;
	int busy;
};

/*
 * One desktop window: an X window's xdg toplevel, its two buffers in one
 * shared-memory mapping, and its corner on the root window (for the
 * pointer's places).
 */
struct x11_wayland_window {
	/* The connection it belongs to, and the X window it shows. */
	struct x11_wayland *wayland;
	uint32_t id;

	/* The surface and its roles; configured once the desktop has sent the first configure. */
	struct wl_surface *surface;
	struct xdg_surface *role;
	struct xdg_toplevel *toplevel;
	int configured;

	/* The buffers' size, their shared memory, and the buffers. */
	unsigned width;
	unsigned height;
	void *memory;
	size_t memory_size;
	struct wayland_buffer buffers[2];

	/* Where the X window's corner is on the root window. */
	int origin_x;
	int origin_y;

	/* The next window of the connection. */
	struct x11_wayland_window *next;
};

/*
 * The connection: the globals, the windows, the server's callbacks, and
 * the input state that turns Wayland's events into the server's.
 */
struct x11_wayland {
	/* The connection and the globals bound from it. */
	struct wl_display *display;
	struct wl_registry *registry;
	struct wl_compositor *compositor;
	struct wl_shm *shm;
	struct xdg_wm_base *shell;
	struct wl_seat *seat;
	struct wl_pointer *pointer;
	struct wl_keyboard *keyboard;

	/* The pointer's and the keyboard's listeners (only the events of version 1 are filled in). */
	struct wl_pointer_listener pointer_listener;
	struct wl_keyboard_listener keyboard_listener;

	/* The windows open. */
	struct x11_wayland_window *windows;

	/* The server's callbacks. */
	struct x11_wayland_callbacks callbacks;
	void *context;

	/* The window the pointer is in, where it is on the root window, and the buttons held (X's bits). */
	struct x11_wayland_window *pointer_window;
	int pointer_x;
	int pointer_y;
	uint16_t buttons;

	/* The modifiers held and caps lock. */
	uint32_t modifiers;
	int caps_lock;

	/* The X keycode each evdev key was pressed as, so its release says the same. */
	uint8_t keycodes[WAYLAND_KEYS];
};

static int wayland_buffers(struct x11_wayland_window *window);
static void wayland_buffers_free(struct x11_wayland_window *window);
static struct x11_wayland_window *wayland_window_of(struct x11_wayland *wayland, struct wl_surface *surface);
static void wayland_pointer_frame(struct x11_wayland *wayland, uint32_t time, uint8_t button, int pressed, uint16_t bit);
static void wayland_global(void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version);
static void wayland_global_remove(void *data, struct wl_registry *registry, uint32_t name);
static void wayland_ping(void *data, struct xdg_wm_base *shell, uint32_t serial);
static void wayland_configure(void *data, struct xdg_surface *surface, uint32_t serial);
static void wayland_toplevel_configure(void *data, struct xdg_toplevel *toplevel, int32_t width, int32_t height, struct wl_array *states);
static void wayland_toplevel_close(void *data, struct xdg_toplevel *toplevel);
static void wayland_buffer_release(void *data, struct wl_buffer *buffer);
static void wayland_seat_capabilities(void *data, struct wl_seat *seat, uint32_t capabilities);
static void wayland_seat_name(void *data, struct wl_seat *seat, const char *name);
static void wayland_pointer_enter(void *data, struct wl_pointer *pointer, uint32_t serial, struct wl_surface *surface, wl_fixed_t x, wl_fixed_t y);
static void wayland_pointer_leave(void *data, struct wl_pointer *pointer, uint32_t serial, struct wl_surface *surface);
static void wayland_pointer_motion(void *data, struct wl_pointer *pointer, uint32_t time, wl_fixed_t x, wl_fixed_t y);
static void wayland_pointer_button(void *data, struct wl_pointer *pointer, uint32_t serial, uint32_t time, uint32_t button, uint32_t state);
static void wayland_pointer_axis(void *data, struct wl_pointer *pointer, uint32_t time, uint32_t axis, wl_fixed_t value);
static void wayland_keyboard_keymap(void *data, struct wl_keyboard *keyboard, uint32_t format, int32_t fd, uint32_t size);
static void wayland_keyboard_enter(void *data, struct wl_keyboard *keyboard, uint32_t serial, struct wl_surface *surface, struct wl_array *keys);
static void wayland_keyboard_leave(void *data, struct wl_keyboard *keyboard, uint32_t serial, struct wl_surface *surface);
static void wayland_keyboard_key(void *data, struct wl_keyboard *keyboard, uint32_t serial, uint32_t time, uint32_t key, uint32_t state);
static void wayland_keyboard_modifiers(void *data, struct wl_keyboard *keyboard, uint32_t serial, uint32_t depressed, uint32_t latched, uint32_t locked, uint32_t group);

/* The registry's callbacks. */
static const struct wl_registry_listener wayland_registry_listener = {
	wayland_global, wayland_global_remove
};

/* The shell's liveness check. */
static const struct xdg_wm_base_listener wayland_shell_listener = {
	wayland_ping
};

/* A window role's configure. */
static const struct xdg_surface_listener wayland_surface_listener = {
	wayland_configure
};

/* A toplevel's size and close request. */
static const struct xdg_toplevel_listener wayland_toplevel_listener = {
	wayland_toplevel_configure, wayland_toplevel_close
};

/* A buffer given back by the desktop. */
static const struct wl_buffer_listener wayland_buffer_listener = {
	wayland_buffer_release
};

/* The seat's devices. */
static const struct wl_seat_listener wayland_seat_listener = {
	wayland_seat_capabilities, wayland_seat_name
};

/*
 * Connects to the desktop and binds the globals windows need.  Returns 0,
 * or an errno value.
 */
int
x11_wayland_open(
	struct x11_wayland **result,
	const char *display,
	const struct x11_wayland_callbacks *callbacks,
	void *context)
{
	struct x11_wayland *wayland;
	int status;

	/* The connection's state, with the server's callbacks. */
	*result = NULL;
	wayland = calloc(1U, sizeof(*wayland));
	if (wayland == NULL)
		return ENOMEM;
	wayland->callbacks = *callbacks;
	wayland->context = context;

	/* The pointer's events of version 1 (the listener has later members, left empty). */
	wayland->pointer_listener.enter = wayland_pointer_enter;
	wayland->pointer_listener.leave = wayland_pointer_leave;
	wayland->pointer_listener.motion = wayland_pointer_motion;
	wayland->pointer_listener.button = wayland_pointer_button;
	wayland->pointer_listener.axis = wayland_pointer_axis;

	/* The keyboard's events of version 1 (no repeat information). */
	wayland->keyboard_listener.keymap = wayland_keyboard_keymap;
	wayland->keyboard_listener.enter = wayland_keyboard_enter;
	wayland->keyboard_listener.leave = wayland_keyboard_leave;
	wayland->keyboard_listener.key = wayland_keyboard_key;
	wayland->keyboard_listener.modifiers = wayland_keyboard_modifiers;

	/* The connection. */
	wayland->display = wl_display_connect(display);
	if (wayland->display == NULL) {
		x11_wayland_close(wayland);
		return ECONNREFUSED;
	}

	/* The registry, whose globals are bound as they are announced. */
	wayland->registry = wl_display_get_registry(wayland->display);
	if (wayland->registry == NULL) {
		x11_wayland_close(wayland);
		return ENOMEM;
	}

	/* Its globals are bound as they are announced. */
	(void)wl_registry_add_listener(wayland->registry, &wayland_registry_listener, wayland);
	status = wl_display_roundtrip(wayland->display);
	if (status < 0) {
		x11_wayland_close(wayland);
		return EIO;
	}

	/* Windows need a compositor, shared memory and a shell. */
	if (wayland->compositor == NULL || wayland->shm == NULL || wayland->shell == NULL) {
		x11_wayland_close(wayland);
		return EOPNOTSUPP;
	}

	/* Succeeded: windows can be opened. */
	*result = wayland;
	return 0;
}

/*
 * Returns the descriptor the server waits on for the desktop's events.
 */
int
x11_wayland_fd(
	const struct x11_wayland *wayland)
{
	int descriptor;

	/* The connection's socket. */
	descriptor = wl_display_get_fd(wayland->display);

	/* Succeeded: the descriptor. */
	return descriptor;
}

/*
 * Reads the desktop's events when its descriptor is readable, runs those
 * read (the server's callbacks among them), and sends the requests they
 * made.  Returns 0, or -1 when the connection is lost.
 */
int
x11_wayland_dispatch(
	struct x11_wayland *wayland,
	int readable)
{
	int status;

	/* The events that came, read and run. */
	if (readable) {
		status = wl_display_dispatch(wayland->display);
		if (status < 0)
			return -1;
	}

	/* Events already read (by a round trip, say), and the requests made while running them. */
	status = wl_display_dispatch_pending(wayland->display);
	if (status < 0)
		return -1;
	(void)wl_display_flush(wayland->display);

	/* Succeeded: every event so far has run. */
	return 0;
}

/*
 * Closes every window and disconnects.
 */
void
x11_wayland_close(
	struct x11_wayland *wayland)
{
	/* The windows. */
	while (wayland->windows != NULL)
		x11_wayland_window_close(wayland->windows);

	/* The input devices. */
	if (wayland->keyboard != NULL)
		wl_keyboard_destroy(wayland->keyboard);
	if (wayland->pointer != NULL)
		wl_pointer_destroy(wayland->pointer);
	if (wayland->seat != NULL)
		wl_seat_destroy(wayland->seat);

	/* The globals and the connection. */
	if (wayland->shell != NULL)
		xdg_wm_base_destroy(wayland->shell);
	if (wayland->shm != NULL)
		wl_shm_destroy(wayland->shm);
	if (wayland->compositor != NULL)
		wl_compositor_destroy(wayland->compositor);
	if (wayland->registry != NULL)
		wl_registry_destroy(wayland->registry);
	if (wayland->display != NULL)
		wl_display_disconnect(wayland->display);

	/* The state itself. */
	free(wayland);
}

/*
 * Opens a desktop window for an X window: an xdg toplevel with a title,
 * configured, and two buffers of a size.  Returns NULL when it cannot be
 * made.
 */
struct x11_wayland_window *
x11_wayland_window_open(
	struct x11_wayland *wayland,
	uint32_t id,
	const char *title,
	unsigned width,
	unsigned height)
{
	struct x11_wayland_window *window;
	int status;

	/* The window, at the head of the connection's list. */
	window = calloc(1U, sizeof(*window));
	if (window == NULL)
		return NULL;
	window->wayland = wayland;
	window->id = id;
	window->width = width;
	window->height = height;
	window->next = wayland->windows;
	wayland->windows = window;

	/* The surface. */
	window->surface = wl_compositor_create_surface(wayland->compositor);
	if (window->surface == NULL) {
		x11_wayland_window_close(window);
		return NULL;
	}

	/* Its window role, whose configures are heard. */
	window->role = xdg_wm_base_get_xdg_surface(wayland->shell, window->surface);
	if (window->role == NULL) {
		x11_wayland_window_close(window);
		return NULL;
	}

	/* Its configures are heard. */
	(void)xdg_surface_add_listener(window->role, &wayland_surface_listener, window);

	/* The toplevel, whose sizes and close requests are heard. */
	window->toplevel = xdg_surface_get_toplevel(window->role);
	if (window->toplevel == NULL) {
		x11_wayland_window_close(window);
		return NULL;
	}

	/* Its sizes and close requests are heard. */
	(void)xdg_toplevel_add_listener(window->toplevel, &wayland_toplevel_listener, window);

	/* The title, the identity, and the first configure waited for. */
	xdg_toplevel_set_title(window->toplevel, title);
	xdg_toplevel_set_app_id(window->toplevel, WAYLAND_APP_ID);
	wl_surface_commit(window->surface);
	status = wl_display_roundtrip(wayland->display);
	if (status < 0 || !window->configured) {
		x11_wayland_window_close(window);
		return NULL;
	}

	/* The two buffers. */
	status = wayland_buffers(window);
	if (status != 0) {
		x11_wayland_window_close(window);
		return NULL;
	}

	/* Succeeded: the window can show the X window. */
	return window;
}

/*
 * Shows a window's pixels (rows of the window's width): all of them are
 * copied into a buffer the desktop has released, and the changed
 * rectangle is the damage.  Returns 0 when it was committed, or 1 when the
 * desktop holds both buffers (the caller tries again later).
 */
int
x11_wayland_window_present(
	struct x11_wayland_window *window,
	const uint32_t *pixels,
	int x,
	int y,
	int width,
	int height)
{
	struct wayland_buffer *buffer;
	unsigned index;

	/* The first buffer the desktop has given back. */
	buffer = NULL;
	for (index = 0U; index < 2U; index++) {
		if (!window->buffers[index].busy) {
			buffer = &window->buffers[index];
			break;
		}
	}

	/* Both are still held: the frame waits. */
	if (buffer == NULL)
		return 1;

	/* All the pixels, so the buffer is whole whatever it showed before. */
	memcpy(buffer->pixels, pixels, (size_t)window->width * window->height * sizeof(uint32_t));
	buffer->busy = 1;

	/* Attached with the change as the damage, and committed. */
	wl_surface_attach(window->surface, buffer->buffer, 0, 0);
	wl_surface_damage(window->surface, x, y, width, height);
	wl_surface_commit(window->surface);
	(void)wl_display_flush(window->wayland->display);

	/* Succeeded: the frame is the desktop's. */
	return 0;
}

/*
 * Makes a window's buffers again at a new size.  Returns 0, or -1.
 */
int
x11_wayland_window_resize(
	struct x11_wayland_window *window,
	unsigned width,
	unsigned height)
{
	int status;

	/* The same size keeps the buffers. */
	if (width == window->width && height == window->height)
		return 0;

	/* The old buffers go, and new ones of the size come. */
	wayland_buffers_free(window);
	window->width = width;
	window->height = height;
	status = wayland_buffers(window);
	if (status != 0)
		return -1;

	/* Succeeded: the next frame is at the new size. */
	return 0;
}

/*
 * Tells a window where its X window's corner is on the root window.
 */
void
x11_wayland_window_move(
	struct x11_wayland_window *window,
	int x,
	int y)
{
	/* The pointer's places add it. */
	window->origin_x = x;
	window->origin_y = y;
}

/*
 * Gives a window a new title.
 */
void
x11_wayland_window_title(
	struct x11_wayland_window *window,
	const char *title)
{
	/* The desktop shows it on the title bar. */
	xdg_toplevel_set_title(window->toplevel, title);
}

/*
 * Closes a window.
 */
void
x11_wayland_window_close(
	struct x11_wayland_window *window)
{
	struct x11_wayland_window **link;
	struct x11_wayland *wayland;

	/* It leaves the connection's list. */
	wayland = window->wayland;
	for (link = &wayland->windows; *link != NULL; link = &(*link)->next) {
		if (*link == window) {
			*link = window->next;
			break;
		}
	}

	/* The pointer is no longer in it. */
	if (wayland->pointer_window == window)
		wayland->pointer_window = NULL;

	/* Its buffers, its roles and its surface. */
	wayland_buffers_free(window);
	if (window->toplevel != NULL)
		xdg_toplevel_destroy(window->toplevel);
	if (window->role != NULL)
		xdg_surface_destroy(window->role);
	if (window->surface != NULL)
		wl_surface_destroy(window->surface);
	(void)wl_display_flush(wayland->display);

	/* The window itself. */
	free(window);
}

/* Makes a window's two XRGB8888 buffers in one shared-memory pool. */
static int
wayland_buffers(
	struct x11_wayland_window *window)
{
	struct wl_shm_pool *pool;
	char name[64];
	size_t bytes;
	unsigned index;
	int descriptor;
	int error;

	/* Anonymous shared memory for both buffers. */
	bytes = (size_t)window->width * window->height * sizeof(uint32_t);
	window->memory_size = bytes * 2U;
	(void)snprintf(name, sizeof(name), "/zdesktop-x11server-%ld-%lu", (long)getpid(), (unsigned long)window->id);
	descriptor = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
	if (descriptor < 0)
		return -1;
	(void)shm_unlink(name);

	/* Its size. */
	error = ftruncate(descriptor, (off_t)window->memory_size);
	if (error != 0) {
		(void)close(descriptor);
		return -1;
	}

	/* Mapped for the server's writes. */
	window->memory = mmap(NULL, window->memory_size, PROT_READ | PROT_WRITE, MAP_SHARED, descriptor, 0);
	if (window->memory == MAP_FAILED) {
		window->memory = NULL;
		(void)close(descriptor);
		return -1;
	}

	/* The pool the desktop maps (it keeps its own reference to the memory). */
	pool = wl_shm_create_pool(window->wayland->shm, descriptor, (int32_t)window->memory_size);
	(void)close(descriptor);
	if (pool == NULL)
		return -1;

	/* A buffer of each half, whose release is heard. */
	for (index = 0U; index < 2U; index++) {
		window->buffers[index].pixels = (uint32_t *)((unsigned char *)window->memory + bytes * index);
		window->buffers[index].busy = 0;
		window->buffers[index].buffer = wl_shm_pool_create_buffer(pool, (int32_t)(bytes * index), (int32_t)window->width, (int32_t)window->height,
									 (int32_t)window->width * 4, WL_SHM_FORMAT_XRGB8888);
		if (window->buffers[index].buffer == NULL) {
			wl_shm_pool_destroy(pool);
			return -1;
		}

		/* Its release is heard. */
		(void)wl_buffer_add_listener(window->buffers[index].buffer, &wayland_buffer_listener, &window->buffers[index]);
	}

	/* Succeeded: the pool lives on in its buffers. */
	wl_shm_pool_destroy(pool);
	return 0;
}

/* Releases a window's buffers and their memory. */
static void
wayland_buffers_free(
	struct x11_wayland_window *window)
{
	unsigned index;

	/* Each buffer. */
	for (index = 0U; index < 2U; index++) {
		if (window->buffers[index].buffer != NULL)
			wl_buffer_destroy(window->buffers[index].buffer);
		window->buffers[index].buffer = NULL;
		window->buffers[index].busy = 0;
	}

	/* The shared memory under them. */
	if (window->memory != NULL)
		(void)munmap(window->memory, window->memory_size);
	window->memory = NULL;
}

/* Returns the window of a surface, or NULL. */
static struct x11_wayland_window *
wayland_window_of(
	struct x11_wayland *wayland,
	struct wl_surface *surface)
{
	struct x11_wayland_window *window;

	/* One of the connection's. */
	for (window = wayland->windows; window != NULL; window = window->next) {
		if (window->surface == surface)
			return window;
	}

	/* Not one of them. */
	return NULL;
}

/* Gives the server one pointer frame: the pointer's place, and a button's change when there is one. */
static void
wayland_pointer_frame(
	struct x11_wayland *wayland,
	uint32_t time,
	uint8_t button,
	int pressed,
	uint16_t bit)
{
	struct x11_pointer_frame frame;

	/* Where the pointer is on the root window, and the buttons held before. */
	memset(&frame, 0, sizeof(frame));
	frame.x = wayland->pointer_x;
	frame.y = wayland->pointer_y;
	frame.time = time;
	frame.buttons_before = wayland->buttons;

	/* A button's change. */
	if (button != 0U) {
		if (pressed) {
			wayland->buttons = (uint16_t)(wayland->buttons | bit);
		} else {
			wayland->buttons = (uint16_t)(wayland->buttons & ~bit);
		}

		/* The change, in the frame. */
		frame.button = button;
		frame.pressed = pressed;
	}

	/* The server hears it. */
	frame.buttons_after = wayland->buttons;
	wayland->callbacks.pointer(wayland->context, &frame);
}

/* Binds the compositor, shared memory, the shell and the seat. */
static void
wayland_global(
	void *data,
	struct wl_registry *registry,
	uint32_t name,
	const char *interface,
	uint32_t version)
{
	struct x11_wayland *wayland;
	int compositor;
	int shm;
	int shell;
	int seat;

	/* Which of the four this is. */
	wayland = data;
	(void)version;
	compositor = strcmp(interface, "wl_compositor");
	shm = strcmp(interface, "wl_shm");
	shell = strcmp(interface, "xdg_wm_base");
	seat = strcmp(interface, "wl_seat");

	/* Each is bound once, at the version used. */
	if (compositor == 0 && wayland->compositor == NULL) {
		wayland->compositor = wl_registry_bind(registry, name, &wl_compositor_interface, 4U);
	} else if (shm == 0 && wayland->shm == NULL) {
		wayland->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1U);
	} else if (shell == 0 && wayland->shell == NULL) {
		wayland->shell = wl_registry_bind(registry, name, &xdg_wm_base_interface, 1U);
		if (wayland->shell != NULL)
			(void)xdg_wm_base_add_listener(wayland->shell, &wayland_shell_listener, wayland);
	} else if (seat == 0 && wayland->seat == NULL) {
		wayland->seat = wl_registry_bind(registry, name, &wl_seat_interface, 1U);
		if (wayland->seat != NULL)
			(void)wl_seat_add_listener(wayland->seat, &wayland_seat_listener, wayland);
	}
}

/* A global going away does not matter to the windows that are up. */
static void
wayland_global_remove(
	void *data,
	struct wl_registry *registry,
	uint32_t name)
{
	/* Nothing to do. */
	(void)data;
	(void)registry;
	(void)name;
}

/* Answers the desktop's liveness check. */
static void
wayland_ping(
	void *data,
	struct xdg_wm_base *shell,
	uint32_t serial)
{
	/* The same serial back. */
	(void)data;
	xdg_wm_base_pong(shell, serial);
}

/* Acknowledges a configure; the window may take buffers from then on. */
static void
wayland_configure(
	void *data,
	struct xdg_surface *surface,
	uint32_t serial)
{
	struct x11_wayland_window *window;

	/* Acknowledged. */
	window = data;
	xdg_surface_ack_configure(surface, serial);
	window->configured = 1;
}

/* Tells the server the size the desktop gives a window (none keeps the window's own). */
static void
wayland_toplevel_configure(
	void *data,
	struct xdg_toplevel *toplevel,
	int32_t width,
	int32_t height,
	struct wl_array *states)
{
	struct x11_wayland_window *window;

	/* A size that is not given, or the window's own, changes nothing. */
	(void)toplevel;
	(void)states;
	window = data;
	if (width <= 0 || height <= 0)
		return;
	if ((unsigned)width == window->width && (unsigned)height == window->height)
		return;

	/* The server resizes the X window, and then this one follows. */
	window->wayland->callbacks.configure(window->wayland->context, window->id, width, height);
}

/* The desktop asks a window to close: the server decides what that means. */
static void
wayland_toplevel_close(
	void *data,
	struct xdg_toplevel *toplevel)
{
	struct x11_wayland_window *window;

	/* The server hears it. */
	(void)toplevel;
	window = data;
	window->wayland->callbacks.close(window->wayland->context, window->id);
}

/* The desktop has given a buffer back: it can take the next frame. */
static void
wayland_buffer_release(
	void *data,
	struct wl_buffer *buffer)
{
	struct wayland_buffer *released;

	/* Free again. */
	(void)buffer;
	released = data;
	released->busy = 0;
}

/* Takes the seat's pointer and keyboard. */
static void
wayland_seat_capabilities(
	void *data,
	struct wl_seat *seat,
	uint32_t capabilities)
{
	struct x11_wayland *wayland;

	/* The pointer, once. */
	wayland = data;
	if ((capabilities & WL_SEAT_CAPABILITY_POINTER) != 0U && wayland->pointer == NULL) {
		wayland->pointer = wl_seat_get_pointer(seat);
		if (wayland->pointer != NULL)
			(void)wl_pointer_add_listener(wayland->pointer, &wayland->pointer_listener, wayland);
	}

	/* The keyboard, once. */
	if ((capabilities & WL_SEAT_CAPABILITY_KEYBOARD) != 0U && wayland->keyboard == NULL) {
		wayland->keyboard = wl_seat_get_keyboard(seat);
		if (wayland->keyboard != NULL)
			(void)wl_keyboard_add_listener(wayland->keyboard, &wayland->keyboard_listener, wayland);
	}
}

/* The seat's name is not used. */
static void
wayland_seat_name(
	void *data,
	struct wl_seat *seat,
	const char *name)
{
	/* Nothing to do. */
	(void)data;
	(void)seat;
	(void)name;
}

/* The pointer comes into a window: the server hears which, and the pointer moves there. */
static void
wayland_pointer_enter(
	void *data,
	struct wl_pointer *pointer,
	uint32_t serial,
	struct wl_surface *surface,
	wl_fixed_t x,
	wl_fixed_t y)
{
	struct x11_wayland *wayland;
	struct x11_wayland_window *window;

	/* The desktop draws its own pointer over the window. */
	wayland = data;
	wl_pointer_set_cursor(pointer, serial, NULL, 0, 0);

	/* A surface that is not one of the windows is ignored. */
	window = wayland_window_of(wayland, surface);
	wayland->pointer_window = window;
	if (window == NULL)
		return;

	/* The X window comes up, and the pointer is at its place on the root window. */
	wayland->callbacks.enter(wayland->context, window->id, 0);
	wayland->pointer_x = window->origin_x + wl_fixed_to_int(x);
	wayland->pointer_y = window->origin_y + wl_fixed_to_int(y);
	wayland_pointer_frame(wayland, 0U, 0U, 0, 0U);
}

/* The pointer leaves a window. */
static void
wayland_pointer_leave(
	void *data,
	struct wl_pointer *pointer,
	uint32_t serial,
	struct wl_surface *surface)
{
	struct x11_wayland *wayland;

	/* The pointer is in none of the windows. */
	(void)pointer;
	(void)serial;
	(void)surface;
	wayland = data;
	wayland->pointer_window = NULL;
}

/* The pointer moves within a window. */
static void
wayland_pointer_motion(
	void *data,
	struct wl_pointer *pointer,
	uint32_t time,
	wl_fixed_t x,
	wl_fixed_t y)
{
	struct x11_wayland *wayland;
	struct x11_wayland_window *window;

	/* Only within a window. */
	(void)pointer;
	wayland = data;
	window = wayland->pointer_window;
	if (window == NULL)
		return;

	/* Its place on the root window. */
	wayland->pointer_x = window->origin_x + wl_fixed_to_int(x);
	wayland->pointer_y = window->origin_y + wl_fixed_to_int(y);
	wayland_pointer_frame(wayland, time, 0U, 0, 0U);
}

/* A button is pressed or released: X's buttons 1 (left), 2 (middle) and 3 (right). */
static void
wayland_pointer_button(
	void *data,
	struct wl_pointer *pointer,
	uint32_t serial,
	uint32_t time,
	uint32_t button,
	uint32_t state)
{
	struct x11_wayland *wayland;
	int pressed;

	/* The serial is not needed. */
	(void)pointer;
	(void)serial;
	wayland = data;
	pressed = 0;
	if (state == WL_POINTER_BUTTON_STATE_PRESSED)
		pressed = 1;

	/* The button's X number and bit. */
	switch (button) {
	case WAYLAND_BTN_LEFT:
		wayland_pointer_frame(wayland, time, 1U, pressed, 1U << 0);
		break;
	case WAYLAND_BTN_MIDDLE:
		wayland_pointer_frame(wayland, time, 2U, pressed, 1U << 1);
		break;
	case WAYLAND_BTN_RIGHT:
		wayland_pointer_frame(wayland, time, 3U, pressed, 1U << 2);
		break;
	default:
		break;
	}
}

/* Scrolling is not passed on yet. */
static void
wayland_pointer_axis(
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

/* Closes the keymap: keys arrive as evdev codes and keymap.c has the table. */
static void
wayland_keyboard_keymap(
	void *data,
	struct wl_keyboard *keyboard,
	uint32_t format,
	int32_t fd,
	uint32_t size)
{
	/* The descriptor is the client's to close. */
	(void)data;
	(void)keyboard;
	(void)format;
	(void)size;
	if (fd >= 0)
		(void)close(fd);
}

/* Focus comes to a window: the server gives its X window the keyboard. */
static void
wayland_keyboard_enter(
	void *data,
	struct wl_keyboard *keyboard,
	uint32_t serial,
	struct wl_surface *surface,
	struct wl_array *keys)
{
	struct x11_wayland *wayland;
	struct x11_wayland_window *window;

	/* Keys held when focus came are not typed. */
	(void)keyboard;
	(void)serial;
	(void)keys;
	wayland = data;

	/* The X window of the surface gets the focus. */
	window = wayland_window_of(wayland, surface);
	if (window != NULL)
		wayland->callbacks.enter(wayland->context, window->id, 1);
}

/* Focus leaves: the modifiers are forgotten. */
static void
wayland_keyboard_leave(
	void *data,
	struct wl_keyboard *keyboard,
	uint32_t serial,
	struct wl_surface *surface)
{
	struct x11_wayland *wayland;

	/* No modifier is held any more. */
	(void)keyboard;
	(void)serial;
	(void)surface;
	wayland = data;
	wayland->modifiers = 0U;
}

/* A key is pressed or released: the server hears its X keycode with the modifiers. */
static void
wayland_keyboard_key(
	void *data,
	struct wl_keyboard *keyboard,
	uint32_t serial,
	uint32_t time,
	uint32_t key,
	uint32_t state)
{
	struct x11_wayland *wayland;
	uint16_t modifiers;
	uint8_t keycode;
	int shifted;

	/* The serial is not needed; a code past the table is ignored. */
	(void)keyboard;
	(void)serial;
	wayland = data;
	if (key >= WAYLAND_KEYS)
		return;
	modifiers = (uint16_t)(wayland->modifiers & WAYLAND_MODIFIERS);

	/* Caps lock toggles on its press and types nothing. */
	if (key == WAYLAND_KEY_CAPSLOCK) {
		if (state == WL_KEYBOARD_KEY_STATE_PRESSED)
			wayland->caps_lock = !wayland->caps_lock;
		return;
	}

	/* A release says the keycode its press had. */
	if (state != WL_KEYBOARD_KEY_STATE_PRESSED) {
		keycode = wayland->keycodes[key];
		wayland->keycodes[key] = 0U;
		if (keycode != 0U)
			wayland->callbacks.key(wayland->context, keycode, 0, time, modifiers);
		return;
	}

	/* A press: the keycode in the current shift and caps lock states. */
	shifted = 0;
	if ((wayland->modifiers & X11_SHIFT_MASK) != 0U)
		shifted = 1;
	keycode = x11_keymap_keycode((uint16_t)key, shifted, wayland->caps_lock);
	if (keycode == 0U)
		return;

	/* Succeeded: the server hears the press. */
	wayland->keycodes[key] = keycode;
	wayland->callbacks.key(wayland->context, keycode, 1, time, modifiers);
}

/* Keeps the modifiers held (the same bits as X's). */
static void
wayland_keyboard_modifiers(
	void *data,
	struct wl_keyboard *keyboard,
	uint32_t serial,
	uint32_t depressed,
	uint32_t latched,
	uint32_t locked,
	uint32_t group)
{
	struct x11_wayland *wayland;

	/* Only the held ones count. */
	(void)keyboard;
	(void)serial;
	(void)latched;
	(void)locked;
	(void)group;
	wayland = data;
	wayland->modifiers = depressed;
}
