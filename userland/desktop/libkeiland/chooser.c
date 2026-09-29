/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The file chooser (ws092-p003, plan/ws092/phase003/phase.md): the Open
 * and Save As window every application shares.
 *
 * The chooser is a toplevel window of its own, drawn by the library on the
 * CPU (chooser-draw.c, paint.c) into wl_shm buffers, standing on the
 * system's glass when zdesktop has it.  Its globals are bound through a
 * registry of the library's own and live on the application's default
 * queue, so the chooser's input, configures and frames run while the
 * application dispatches that queue; the application needs no timer for
 * it.  The chooser draws at most once a frame (a frame callback) and keeps
 * asking for frames while a finger is down or the list glides.
 *
 * The answer is told through the listener from a wl_display.sync callback
 * after the window is gone, so that the application may destroy the
 * chooser from its callback without destroying an object whose event is
 * being dispatched.
 */

#include "chooser.h"

#include <wayland-client.h>
#include <xdg-shell-client-protocol.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

/* The fonts used unless the application names others. */
#define CHOOSER_FONT		"/usr/share/fonts/keiland.ttf"
#define CHOOSER_FALLBACK_FONT	"/usr/share/fonts/keiland-fallback.ttf"

/* The protocol versions bound. */
#define CHOOSER_COMPOSITOR_VERSION	4U
#define CHOOSER_SEAT_VERSION		5U

/* wl_shm's format of premultiplied 0xAARRGGBB words. */
#define CHOOSER_FORMAT_ARGB8888	0U

/* The left button's evdev code. */
#define CHOOSER_BUTTON_LEFT	0x110U

/* The modifier bits of wl_keyboard.modifiers as zdesktop reports them. */
#define CHOOSER_WAYLAND_SHIFT	0x01U
#define CHOOSER_WAYLAND_CTRL	0x04U
#define CHOOSER_WAYLAND_ALT	0x08U

/* How many pixels one unit of the wheel moves the list (zdesktop sends 15 units a notch). */
#define CHOOSER_WHEEL_SCALE	3.0

/* The most fingers followed at once. */
#define CHOOSER_FINGERS		5

/* The most gestures taken in one step. */
#define CHOOSER_GESTURES	16

/* The buffers a frame is drawn into, one while the compositor holds the other. */
#define CHOOSER_BUFFERS		2

/*
 * One wl_shm buffer: the pixels mapped, their size, and whether the
 * compositor still reads it.
 */
struct chooser_buffer {
	struct wl_buffer *buffer;
	uint32_t *pixels;
	size_t size;
	int width;
	int height;
	int busy;
};

/*
 * One file chooser: the model, the fonts, the Wayland objects of its
 * window and seat, the frames, the fingers, and who is told the answer.
 * It lives from keiland_file_chooser_open to keiland_file_chooser_destroy;
 * the window's objects go when the answer is known (window_gone).
 */
struct keiland_file_chooser {
	struct kl_chooser model;
	struct kl_text text;

	/* The connection and the globals bound for the chooser. */
	struct wl_display *display;
	struct wl_compositor *compositor;
	struct wl_shm *shm;
	struct xdg_wm_base *shell;
	struct wl_seat *seat;
	struct wl_pointer *pointer;
	struct wl_keyboard *keyboard;
	struct wl_touch *touch;

	/* The window. */
	struct wl_surface *surface;
	struct xdg_surface *role;
	struct xdg_toplevel *toplevel;
	struct keiland_glass *glass;
	int configured;
	int pending_width;
	int pending_height;
	int window_gone;

	/* The frames: the buffers, the frame asked for, and whether another is due. */
	struct chooser_buffer buffers[CHOOSER_BUFFERS];
	struct wl_callback *frame;
	int dirty;

	/* The pointer while it is over the window. */
	int pointer_inside;
	int pointer_x;
	int pointer_y;

	/* The fingers on the window, their gestures, the list's scroller, and whether a press caught a glide. */
	int32_t fingers[CHOOSER_FINGERS];
	int finger_count;
	struct keiland_gesture *gesture;
	struct keiland_scroller *scroller;
	int caught;
	int dragging;
	int gliding;
	int touched;
	unsigned tap_generation;

	/* The answer's delivery, and who hears it. */
	struct wl_callback *telling;
	int told;
	const struct keiland_file_chooser_listener *listener;
	void *data;
};

/* What the registry search found: the globals' names and versions (0 for none). */
struct chooser_search {
	uint32_t compositor;
	uint32_t compositor_version;
	uint32_t shm;
	uint32_t shell;
	uint32_t seat;
	uint32_t seat_version;
};

static int chooser_bind(struct keiland_file_chooser *chooser);
static int chooser_window(struct keiland_file_chooser *chooser, struct xdg_toplevel *parent, const char *application);
static void chooser_window_gone(struct keiland_file_chooser *chooser);
static void chooser_redraw(struct keiland_file_chooser *chooser);
static struct chooser_buffer *chooser_buffer_ready(struct keiland_file_chooser *chooser);
static int chooser_buffer_make(struct keiland_file_chooser *chooser, struct chooser_buffer *buffer, int width, int height);
static void chooser_buffer_free(struct chooser_buffer *buffer);
static void chooser_after_input(struct keiland_file_chooser *chooser);
static void chooser_sync_scroller(struct keiland_file_chooser *chooser);
static void chooser_touch_step(struct keiland_file_chooser *chooser, uint64_t now);
static int chooser_finger(struct keiland_file_chooser *chooser, int32_t id);
static uint64_t chooser_now_us(void);
static void chooser_global(void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version);
static void chooser_global_remove(void *data, struct wl_registry *registry, uint32_t name);
static void chooser_shm_format(void *data, struct wl_shm *shm, uint32_t format);
static void chooser_ping(void *data, struct xdg_wm_base *shell, uint32_t serial);
static void chooser_configure(void *data, struct xdg_surface *role, uint32_t serial);
static void chooser_toplevel_configure(void *data, struct xdg_toplevel *toplevel, int32_t width, int32_t height, struct wl_array *states);
static void chooser_toplevel_close(void *data, struct xdg_toplevel *toplevel);
static void chooser_buffer_release(void *data, struct wl_buffer *buffer);
static void chooser_frame_done(void *data, struct wl_callback *callback, uint32_t time);
static void chooser_tell_done(void *data, struct wl_callback *callback, uint32_t time);
static void chooser_seat_capabilities(void *data, struct wl_seat *seat, uint32_t capabilities);
static void chooser_seat_name(void *data, struct wl_seat *seat, const char *name);
static void chooser_pointer_enter(void *data, struct wl_pointer *pointer, uint32_t serial, struct wl_surface *surface, wl_fixed_t x, wl_fixed_t y);
static void chooser_pointer_leave(void *data, struct wl_pointer *pointer, uint32_t serial, struct wl_surface *surface);
static void chooser_pointer_motion(void *data, struct wl_pointer *pointer, uint32_t time, wl_fixed_t x, wl_fixed_t y);
static void chooser_pointer_button(void *data, struct wl_pointer *pointer, uint32_t serial, uint32_t time, uint32_t button, uint32_t state);
static void chooser_pointer_axis(void *data, struct wl_pointer *pointer, uint32_t time, uint32_t axis, wl_fixed_t value);
static void chooser_pointer_frame(void *data, struct wl_pointer *pointer);
static void chooser_pointer_axis_source(void *data, struct wl_pointer *pointer, uint32_t source);
static void chooser_pointer_axis_stop(void *data, struct wl_pointer *pointer, uint32_t time, uint32_t axis);
static void chooser_pointer_axis_discrete(void *data, struct wl_pointer *pointer, uint32_t axis, int32_t discrete);
static void chooser_keyboard_keymap(void *data, struct wl_keyboard *keyboard, uint32_t format, int32_t fd, uint32_t size);
static void chooser_keyboard_enter(void *data, struct wl_keyboard *keyboard, uint32_t serial, struct wl_surface *surface, struct wl_array *keys);
static void chooser_keyboard_leave(void *data, struct wl_keyboard *keyboard, uint32_t serial, struct wl_surface *surface);
static void chooser_keyboard_key(void *data, struct wl_keyboard *keyboard, uint32_t serial, uint32_t time, uint32_t key, uint32_t state);
static void chooser_keyboard_modifiers(void *data, struct wl_keyboard *keyboard, uint32_t serial, uint32_t depressed, uint32_t latched, uint32_t locked, uint32_t group);
static void chooser_keyboard_repeat(void *data, struct wl_keyboard *keyboard, int32_t rate, int32_t delay);
static void chooser_touch_down(void *data, struct wl_touch *touch, uint32_t serial, uint32_t time, struct wl_surface *surface, int32_t id, wl_fixed_t x, wl_fixed_t y);
static void chooser_touch_up(void *data, struct wl_touch *touch, uint32_t serial, uint32_t time, int32_t id);
static void chooser_touch_motion(void *data, struct wl_touch *touch, uint32_t time, int32_t id, wl_fixed_t x, wl_fixed_t y);
static void chooser_touch_frame(void *data, struct wl_touch *touch);
static void chooser_touch_cancel(void *data, struct wl_touch *touch);

/* The registry's callbacks while the globals are looked for. */
static const struct wl_registry_listener chooser_registry_listener = {
	chooser_global, chooser_global_remove
};

/* The formats wl_shm announces (the chooser uses ARGB8888, which every compositor has). */
static const struct wl_shm_listener chooser_shm_listener = {
	chooser_shm_format
};

/* The shell's liveness check. */
static const struct xdg_wm_base_listener chooser_shell_listener = {
	chooser_ping
};

/* The window's configures. */
static const struct xdg_surface_listener chooser_surface_listener = {
	chooser_configure
};

/* The toplevel's size and close (version 1: its bounds are never sent). */
static const struct xdg_toplevel_listener chooser_toplevel_listener = {
	chooser_toplevel_configure, chooser_toplevel_close, NULL
};

/* A buffer given back by the compositor. */
static const struct wl_buffer_listener chooser_buffer_listener = {
	chooser_buffer_release
};

/* The time for the next frame. */
static const struct wl_callback_listener chooser_frame_listener = {
	chooser_frame_done
};

/* The moment to tell the answer. */
static const struct wl_callback_listener chooser_tell_listener = {
	chooser_tell_done
};

/* The seat's devices. */
static const struct wl_seat_listener chooser_seat_listener = {
	chooser_seat_capabilities, chooser_seat_name
};

/* The pointer's events of versions 1 to 5 (the later members are never called). */
static const struct wl_pointer_listener chooser_pointer_listener = {
	chooser_pointer_enter, chooser_pointer_leave, chooser_pointer_motion, chooser_pointer_button,
	chooser_pointer_axis, chooser_pointer_frame, chooser_pointer_axis_source, chooser_pointer_axis_stop,
	chooser_pointer_axis_discrete, NULL, NULL
};

/* The keyboard's events of versions 1 to 5. */
static const struct wl_keyboard_listener chooser_keyboard_listener = {
	chooser_keyboard_keymap, chooser_keyboard_enter, chooser_keyboard_leave,
	chooser_keyboard_key, chooser_keyboard_modifiers, chooser_keyboard_repeat
};

/* The touch screen's events of versions 1 to 5 (shape and orientation are never called). */
static const struct wl_touch_listener chooser_touch_listener = {
	chooser_touch_down, chooser_touch_up, chooser_touch_motion, chooser_touch_frame, chooser_touch_cancel, NULL, NULL
};

/*
 * Opens a file chooser over an application's window.
 */
struct keiland_file_chooser *
keiland_file_chooser_open(
	struct wl_display *display,
	struct xdg_toplevel *parent,
	const struct keiland_file_chooser_options *options,
	const struct keiland_file_chooser_listener *listener,
	void *data)
{
	struct keiland_file_chooser *chooser;
	const char *font;
	const char *fallback;
	int error;

	/* A display and someone to tell the answer. */
	if (display == NULL || listener == NULL || listener->done == NULL) {
		errno = EINVAL;
		return NULL;
	}

	/* The record. */
	chooser = calloc(1, sizeof(*chooser));
	if (chooser == NULL) {
		errno = ENOMEM;
		return NULL;
	}
	chooser->display = display;
	chooser->listener = listener;
	chooser->data = data;

	/* The model, which checks the options and lists the first folder. */
	error = kl_chooser_init(&chooser->model, options);
	if (error != 0) {
		kl_chooser_fini(&chooser->model);
		free(chooser);
		errno = error;
		return NULL;
	}

	/* The fonts, the application's or the system's. */
	font = CHOOSER_FONT;
	if (options->font != NULL)
		font = options->font;
	fallback = CHOOSER_FALLBACK_FONT;
	if (options->fallback_font != NULL)
		fallback = options->fallback_font;
	error = kl_text_open(&chooser->text, font, fallback);
	if (error != 0) {
		kl_chooser_fini(&chooser->model);
		free(chooser);
		errno = error;
		return NULL;
	}

	/* The fingers' gestures and the list's scroller. */
	chooser->gesture = keiland_gesture_create();
	chooser->scroller = keiland_scroller_create();
	if (chooser->gesture == NULL || chooser->scroller == NULL) {
		keiland_file_chooser_destroy(chooser);
		errno = ENOMEM;
		return NULL;
	}

	/* The globals. */
	error = chooser_bind(chooser);
	if (error != 0) {
		keiland_file_chooser_destroy(chooser);
		errno = error;
		return NULL;
	}

	/* The window; its first configure arrives with the application's next dispatch. */
	error = chooser_window(chooser, parent, options->application);
	if (error != 0) {
		keiland_file_chooser_destroy(chooser);
		errno = error;
		return NULL;
	}

	/* Succeeded: the chooser shows itself once configured. */
	return chooser;
}

/*
 * Closes a chooser; one still open closes without telling.
 */
void
keiland_file_chooser_destroy(
	struct keiland_file_chooser *chooser)
{
	/* No chooser, nothing to close. */
	if (chooser == NULL)
		return;

	/* The window, and the answer not told any more. */
	chooser_window_gone(chooser);
	if (chooser->telling != NULL)
		wl_callback_destroy(chooser->telling);

	/* The seat's devices and the globals. */
	if (chooser->touch != NULL)
		wl_touch_destroy(chooser->touch);
	if (chooser->keyboard != NULL)
		wl_keyboard_destroy(chooser->keyboard);
	if (chooser->pointer != NULL)
		wl_pointer_destroy(chooser->pointer);
	if (chooser->seat != NULL)
		wl_seat_destroy(chooser->seat);
	if (chooser->shell != NULL)
		xdg_wm_base_destroy(chooser->shell);
	if (chooser->shm != NULL)
		wl_shm_destroy(chooser->shm);
	if (chooser->compositor != NULL)
		wl_compositor_destroy(chooser->compositor);

	/* The fingers, the fonts and the model. */
	if (chooser->scroller != NULL)
		keiland_scroller_destroy(chooser->scroller);
	if (chooser->gesture != NULL)
		keiland_gesture_destroy(chooser->gesture);
	kl_text_close(&chooser->text);
	kl_chooser_fini(&chooser->model);
	free(chooser);
}

/* Binds the compositor, the shared memory, the shell and the seat through a registry of the library's own. */
static int
chooser_bind(
	struct keiland_file_chooser *chooser)
{
	struct chooser_search search;
	struct wl_event_queue *queue;
	struct wl_display *wrapper;
	struct wl_registry *registry;
	int status;

	/* The search's own queue, and the display as seen from it. */
	queue = wl_display_create_queue(chooser->display);
	if (queue == NULL)
		return ENOMEM;
	wrapper = wl_proxy_create_wrapper(chooser->display);
	if (wrapper == NULL) {
		wl_event_queue_destroy(queue);
		return ENOMEM;
	}
	wl_proxy_set_queue((struct wl_proxy *)wrapper, queue);

	/* The globals, announced to this search alone. */
	memset(&search, 0, sizeof(search));
	registry = wl_display_get_registry(wrapper);
	if (registry == NULL) {
		wl_proxy_wrapper_destroy(wrapper);
		wl_event_queue_destroy(queue);
		return ENOMEM;
	}
	status = wl_registry_add_listener(registry, &chooser_registry_listener, &search);
	if (status == 0)
		(void)wl_display_roundtrip_queue(chooser->display, queue);

	/* Each global the chooser needs, bound and moved to the application's default queue. */
	if (search.compositor != 0U && search.shm != 0U && search.shell != 0U) {
		chooser->compositor = wl_registry_bind(registry, search.compositor, &wl_compositor_interface, search.compositor_version);
		chooser->shm = wl_registry_bind(registry, search.shm, &wl_shm_interface, 1U);
		chooser->shell = wl_registry_bind(registry, search.shell, &xdg_wm_base_interface, 1U);
	}
	if (search.seat != 0U)
		chooser->seat = wl_registry_bind(registry, search.seat, &wl_seat_interface, search.seat_version);

	/* The search's objects go. */
	wl_registry_destroy(registry);
	wl_proxy_wrapper_destroy(wrapper);

	/* Without a compositor, shared memory and a shell there is no window. */
	if (chooser->compositor == NULL || chooser->shm == NULL || chooser->shell == NULL) {
		wl_event_queue_destroy(queue);
		return ENOTSUP;
	}

	/* The bound globals hear their events on the application's queue. */
	wl_proxy_set_queue((struct wl_proxy *)chooser->compositor, NULL);
	wl_proxy_set_queue((struct wl_proxy *)chooser->shm, NULL);
	wl_proxy_set_queue((struct wl_proxy *)chooser->shell, NULL);
	(void)wl_shm_add_listener(chooser->shm, &chooser_shm_listener, chooser);
	(void)xdg_wm_base_add_listener(chooser->shell, &chooser_shell_listener, chooser);
	if (chooser->seat != NULL) {
		wl_proxy_set_queue((struct wl_proxy *)chooser->seat, NULL);
		(void)wl_seat_add_listener(chooser->seat, &chooser_seat_listener, chooser);
	}
	wl_event_queue_destroy(queue);

	/* Succeeded: the globals are the chooser's. */
	return 0;
}

/* Makes the window: its surface, its toplevel with its title, application and parent, and its glass. */
static int
chooser_window(
	struct keiland_file_chooser *chooser,
	struct xdg_toplevel *parent,
	const char *application)
{
	int status;

	/* The surface. */
	chooser->surface = wl_compositor_create_surface(chooser->compositor);
	if (chooser->surface == NULL)
		return ENOMEM;

	/* Its window role. */
	chooser->role = xdg_wm_base_get_xdg_surface(chooser->shell, chooser->surface);
	if (chooser->role == NULL)
		return ENOMEM;
	status = xdg_surface_add_listener(chooser->role, &chooser_surface_listener, chooser);
	if (status != 0)
		return EINVAL;

	/* A toplevel window. */
	chooser->toplevel = xdg_surface_get_toplevel(chooser->role);
	if (chooser->toplevel == NULL)
		return ENOMEM;
	status = xdg_toplevel_add_listener(chooser->toplevel, &chooser_toplevel_listener, chooser);
	if (status != 0)
		return EINVAL;

	/* Its title, its application, its parent and its smallest size. */
	xdg_toplevel_set_title(chooser->toplevel, chooser->model.title);
	if (application != NULL)
		xdg_toplevel_set_app_id(chooser->toplevel, application);
	if (parent != NULL)
		xdg_toplevel_set_parent(chooser->toplevel, parent);
	xdg_toplevel_set_min_size(chooser->toplevel, KL_CHOOSER_MIN_WIDTH, KL_CHOOSER_MIN_HEIGHT);

	/* The glass under its cards, when zdesktop has glass. */
	chooser->glass = keiland_glass_create(chooser->display, chooser->surface);
	chooser->model.glass = 0;
	if (chooser->glass != NULL)
		chooser->model.glass = 1;

	/* The first commit, which asks for the first configure. */
	wl_surface_commit(chooser->surface);

	/* Succeeded: the window waits for its configure. */
	return 0;
}

/* Takes the window away (the answer is known, or the chooser is destroyed). */
static void
chooser_window_gone(
	struct keiland_file_chooser *chooser)
{
	int index;

	/* Once. */
	if (chooser->window_gone)
		return;
	chooser->window_gone = 1;

	/* The frame asked for, the glass, the roles, the surface. */
	if (chooser->frame != NULL) {
		wl_callback_destroy(chooser->frame);
		chooser->frame = NULL;
	}
	if (chooser->glass != NULL) {
		keiland_glass_destroy(chooser->glass);
		chooser->glass = NULL;
	}
	if (chooser->toplevel != NULL) {
		xdg_toplevel_destroy(chooser->toplevel);
		chooser->toplevel = NULL;
	}
	if (chooser->role != NULL) {
		xdg_surface_destroy(chooser->role);
		chooser->role = NULL;
	}
	if (chooser->surface != NULL) {
		wl_surface_destroy(chooser->surface);
		chooser->surface = NULL;
	}

	/* The buffers. */
	for (index = 0; index < CHOOSER_BUFFERS; index++)
		chooser_buffer_free(&chooser->buffers[index]);
}

/*
 * Draws the window now, or once the frame asked for comes (at most one
 * frame a display refresh), or once a buffer comes back.
 */
static void
chooser_redraw(
	struct keiland_file_chooser *chooser)
{
	struct keiland_glass_panel panels[2];
	struct chooser_buffer *buffer;
	struct kl_canvas canvas;
	int count;

	/* A window not shown yet, or gone, draws nothing. */
	chooser->dirty = 1;
	if (chooser->window_gone || !chooser->configured)
		return;

	/* A frame is on its way: this one waits for it. */
	if (chooser->frame != NULL)
		return;

	/* A buffer the compositor does not read (none: wait for one back). */
	buffer = chooser_buffer_ready(chooser);
	if (buffer == NULL)
		return;

	/* The window drawn into it. */
	kl_paint_init(&canvas, buffer->pixels, buffer->width, buffer->height, (size_t)buffer->width);
	kl_chooser_draw(&chooser->model, &chooser->text, &canvas);
	chooser->dirty = 0;

	/* The glass under the cards, for this commit. */
	if (chooser->glass != NULL) {
		count = kl_chooser_glass_panels(&chooser->model, panels, 2U);
		(void)keiland_glass_set_panels(chooser->glass, panels, (size_t)count);
	}

	/* The next frame's callback, and the buffer shown. */
	chooser->frame = wl_surface_frame(chooser->surface);
	if (chooser->frame != NULL)
		(void)wl_callback_add_listener(chooser->frame, &chooser_frame_listener, chooser);
	wl_surface_attach(chooser->surface, buffer->buffer, 0, 0);
	wl_surface_damage_buffer(chooser->surface, 0, 0, buffer->width, buffer->height);
	wl_surface_commit(chooser->surface);
	buffer->busy = 1;
}

/* Finds a buffer the compositor has given back, at the window's size (remade when the size changed). */
static struct chooser_buffer *
chooser_buffer_ready(
	struct keiland_file_chooser *chooser)
{
	struct chooser_buffer *buffer;
	int index;
	int error;

	/* The first free buffer. */
	for (index = 0; index < CHOOSER_BUFFERS; index++) {
		buffer = &chooser->buffers[index];
		if (buffer->busy)
			continue;

		/* Of the size, or remade. */
		if (buffer->buffer == NULL || buffer->width != chooser->model.width || buffer->height != chooser->model.height) {
			chooser_buffer_free(buffer);
			error = chooser_buffer_make(chooser, buffer, chooser->model.width, chooser->model.height);
			if (error != 0)
				return NULL;
		}
		return buffer;
	}

	/* Both are the compositor's still. */
	return NULL;
}

/* Makes a wl_shm buffer of a size in a shared memory object of its own. */
static int
chooser_buffer_make(
	struct keiland_file_chooser *chooser,
	struct chooser_buffer *buffer,
	int width,
	int height)
{
	static unsigned serial;
	struct wl_shm_pool *pool;
	char name[64];
	void *mapped;
	size_t size;
	int descriptor;
	int status;

	/* A shared memory object with a name nobody else uses, unlinked at once. */
	size = (size_t)width * (size_t)height * 4U;
	serial++;
	snprintf(name, sizeof(name), "/keiland-chooser-%ld-%u", (long)getpid(), serial);
	descriptor = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
	if (descriptor < 0)
		return errno;
	(void)shm_unlink(name);

	/* Its size. */
	status = ftruncate(descriptor, (off_t)size);
	if (status != 0) {
		close(descriptor);
		return errno;
	}

	/* Mapped for drawing. */
	mapped = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, descriptor, 0);
	if (mapped == MAP_FAILED) {
		close(descriptor);
		return errno;
	}

	/* The compositor's pool over it, and the buffer from the pool (the pool is not needed after). */
	pool = wl_shm_create_pool(chooser->shm, descriptor, (int32_t)size);
	close(descriptor);
	if (pool == NULL) {
		munmap(mapped, size);
		return ENOMEM;
	}
	buffer->buffer = wl_shm_pool_create_buffer(pool, 0, width, height, width * 4, CHOOSER_FORMAT_ARGB8888);
	wl_shm_pool_destroy(pool);
	if (buffer->buffer == NULL) {
		munmap(mapped, size);
		return ENOMEM;
	}
	(void)wl_buffer_add_listener(buffer->buffer, &chooser_buffer_listener, buffer);

	/* Succeeded: the buffer, free to draw into. */
	buffer->pixels = mapped;
	buffer->size = size;
	buffer->width = width;
	buffer->height = height;
	buffer->busy = 0;
	return 0;
}

/* Frees a buffer and its memory. */
static void
chooser_buffer_free(
	struct chooser_buffer *buffer)
{
	/* The protocol object, then the memory. */
	if (buffer->buffer != NULL)
		wl_buffer_destroy(buffer->buffer);
	if (buffer->pixels != NULL)
		munmap(buffer->pixels, buffer->size);
	memset(buffer, 0, sizeof(*buffer));
}

/*
 * After an input: the window drawn again, and once the answer is known the
 * window taken away and the answer's delivery arranged.
 */
static void
chooser_after_input(
	struct keiland_file_chooser *chooser)
{
	/* A key may have moved the list: the finger's scroller starts from there. */
	if (chooser->model.scroll_moved) {
		chooser->model.scroll_moved = 0;
		chooser_sync_scroller(chooser);
	}

	/* Not answered: the window shows the change. */
	if (!chooser->model.answered) {
		chooser_redraw(chooser);
		return;
	}

	/* Answered: the window goes, and the answer is told after a round trip (not from inside this event). */
	if (chooser->telling != NULL || chooser->told)
		return;
	chooser_window_gone(chooser);
	chooser->telling = wl_display_sync(chooser->display);
	if (chooser->telling != NULL)
		(void)wl_callback_add_listener(chooser->telling, &chooser_tell_listener, chooser);
}

/* Gives the list's scroller its bounds and the model's scroll. */
static void
chooser_sync_scroller(
	struct keiland_file_chooser *chooser)
{
	double maximum;
	double height;

	/* The bounds and the viewport. */
	maximum = kl_chooser_scroll_max(&chooser->model);
	height = (double)chooser->model.layout.list.height;
	if (height < 1.0)
		height = 1.0;
	(void)keiland_scroller_set_bounds(chooser->scroller, 0.0, 0.0, 0.0, maximum, (double)chooser->model.layout.list.width, height);

	/* The position, stopping any glide. */
	keiland_scroller_set_position(chooser->scroller, 0.0, chooser->model.scroll);
	chooser->gliding = 0;
}

/*
 * Takes the fingers' gestures, and moves the list with a drag or a glide.
 *
 * The second tap of a double tap comes as a TAP followed by a
 * DOUBLE_TAP; the pair is carried out as the double tap alone, and not at
 * all when the first tap went into a folder (the second would land on the
 * new folder's items).
 */
static void
chooser_touch_step(
	struct keiland_file_chooser *chooser,
	uint64_t now)
{
	struct keiland_gesture_event events[CHOOSER_GESTURES];
	double dx;
	double dy;
	double x;
	double y;
	int count;
	int index;
	int moving;
	int taken;
	int error;

	/* The gestures found by now. */
	count = 0;
	while (count < CHOOSER_GESTURES) {
		taken = keiland_gesture_next(chooser->gesture, now, &events[count]);
		if (taken == 0)
			break;
		count++;
	}

	/* What each means. */
	for (index = 0; index < count; index++) {
		switch (events[index].kind) {
		case KEILAND_GESTURE_TAP:
			/* A tap that caught a glide only stops it; one followed by its double tap waits for it. */
			if (chooser->caught)
				break;
			if (index + 1 < count && events[index + 1].kind == KEILAND_GESTURE_DOUBLE_TAP)
				break;
			(void)kl_chooser_tap(&chooser->model, (int)events[index].x, (int)events[index].y, 0);
			chooser->tap_generation = chooser->model.generation;
			break;
		case KEILAND_GESTURE_DOUBLE_TAP:
			/* The double tap, unless the first tap showed another folder. */
			if (chooser->caught)
				break;
			if (chooser->tap_generation != chooser->model.generation)
				break;
			(void)kl_chooser_tap(&chooser->model, (int)events[index].x, (int)events[index].y, 1);
			break;
		case KEILAND_GESTURE_DRAG_BEGIN:
			chooser->dragging = 1;
			break;
		case KEILAND_GESTURE_DRAG_END:
			/* The list glides on with the finger's speed. */
			if (chooser->dragging)
				keiland_scroller_release(chooser->scroller, now, events[index].vx, events[index].vy);
			chooser->dragging = 0;
			chooser->gliding = 1;
			break;
		case KEILAND_GESTURE_CANCEL:
			keiland_scroller_cancel(chooser->scroller, now);
			chooser->dragging = 0;
			chooser->gliding = 1;
			break;
		default:
			break;
		}

		/* An answer ends the fingers' work. */
		if (chooser->model.answered)
			return;
	}

	/* The drag follows the fingers. */
	if (chooser->dragging) {
		error = keiland_gesture_drag_offset(chooser->gesture, now, &dx, &dy);
		if (error == 0)
			keiland_scroller_drag(chooser->scroller, dx, dy);
	}

	/* The list where the scroller has it. */
	if (chooser->dragging || chooser->gliding) {
		moving = keiland_scroller_step(chooser->scroller, now, &x, &y);
		kl_chooser_set_scroll(&chooser->model, y);
		if (!moving && !chooser->dragging)
			chooser->gliding = 0;
	}
}

/* Reports the slot of a finger of the window, or -1. */
static int
chooser_finger(
	struct keiland_file_chooser *chooser,
	int32_t id)
{
	int index;

	/* Each finger down on the window. */
	for (index = 0; index < chooser->finger_count; index++) {
		if (chooser->fingers[index] == id)
			return index;
	}

	/* Not one of the window's. */
	return -1;
}

/* Reports the monotonic clock in microseconds. */
static uint64_t
chooser_now_us(void)
{
	struct timespec now;

	/* The clock the gestures and the scroller use. */
	(void)clock_gettime(CLOCK_MONOTONIC, &now);

	/* Reports it in microseconds. */
	return (uint64_t)now.tv_sec * 1000000U + (uint64_t)now.tv_nsec / 1000U;
}

/* Notes the globals the chooser binds: the compositor, shared memory, the shell and the first seat. */
static void
chooser_global(
	void *data,
	struct wl_registry *registry,
	uint32_t name,
	const char *interface,
	uint32_t version)
{
	struct chooser_search *search;
	int match;

	/* The compositor, at most version 4 (damage in buffer pixels). */
	(void)registry;
	search = data;
	match = strcmp(interface, "wl_compositor");
	if (match == 0 && search->compositor == 0U) {
		search->compositor = name;
		search->compositor_version = version;
		if (version > CHOOSER_COMPOSITOR_VERSION)
			search->compositor_version = CHOOSER_COMPOSITOR_VERSION;
		return;
	}

	/* Shared memory. */
	match = strcmp(interface, "wl_shm");
	if (match == 0 && search->shm == 0U) {
		search->shm = name;
		return;
	}

	/* The shell. */
	match = strcmp(interface, "xdg_wm_base");
	if (match == 0 && search->shell == 0U) {
		search->shell = name;
		return;
	}

	/* The first seat, at most version 5. */
	match = strcmp(interface, "wl_seat");
	if (match == 0 && search->seat == 0U) {
		search->seat = name;
		search->seat_version = version;
		if (version > CHOOSER_SEAT_VERSION)
			search->seat_version = CHOOSER_SEAT_VERSION;
	}
}

/* A global going away while the search runs does not matter. */
static void
chooser_global_remove(
	void *data,
	struct wl_registry *registry,
	uint32_t name)
{
	/* Nothing to do. */
	(void)data;
	(void)registry;
	(void)name;
}

/* A format of shared memory; ARGB8888 is always there. */
static void
chooser_shm_format(
	void *data,
	struct wl_shm *shm,
	uint32_t format)
{
	/* Nothing to keep. */
	(void)data;
	(void)shm;
	(void)format;
}

/* Answers the compositor's liveness check. */
static void
chooser_ping(
	void *data,
	struct xdg_wm_base *shell,
	uint32_t serial)
{
	/* The same serial back. */
	(void)data;
	xdg_wm_base_pong(shell, serial);
}

/* Acknowledges a configure and draws the window at its size. */
static void
chooser_configure(
	void *data,
	struct xdg_surface *role,
	uint32_t serial)
{
	struct keiland_file_chooser *chooser;

	/* The acknowledgement, and the size the toplevel's configure gave. */
	chooser = data;
	xdg_surface_ack_configure(role, serial);
	chooser->configured = 1;
	if (chooser->pending_width > 0 && chooser->pending_height > 0)
		kl_chooser_resize(&chooser->model, chooser->pending_width, chooser->pending_height);
	chooser_sync_scroller(chooser);

	/* A frame of the new size at once (a frame asked for before may never come for a hidden window). */
	if (chooser->frame != NULL) {
		wl_callback_destroy(chooser->frame);
		chooser->frame = NULL;
	}
	chooser_redraw(chooser);
}

/* Keeps the size the compositor gives (zero: the chooser's own). */
static void
chooser_toplevel_configure(
	void *data,
	struct xdg_toplevel *toplevel,
	int32_t width,
	int32_t height,
	struct wl_array *states)
{
	struct keiland_file_chooser *chooser;

	/* The size for the configure that follows. */
	(void)toplevel;
	(void)states;
	chooser = data;
	chooser->pending_width = width;
	chooser->pending_height = height;
}

/* The close button: the user cancelled. */
static void
chooser_toplevel_close(
	void *data,
	struct xdg_toplevel *toplevel)
{
	struct keiland_file_chooser *chooser;

	/* Cancelled, told soon. */
	(void)toplevel;
	chooser = data;
	kl_chooser_cancel(&chooser->model);
	chooser_after_input(chooser);
}

/* The compositor gave a buffer back: a frame that waited for it is drawn. */
static void
chooser_buffer_release(
	void *data,
	struct wl_buffer *buffer)
{
	struct chooser_buffer *owned;

	/* Free to draw into. */
	(void)buffer;
	owned = data;
	owned->busy = 0;
}

/* The time for the next frame: the fingers and the glide move on, and a waiting frame is drawn. */
static void
chooser_frame_done(
	void *data,
	struct wl_callback *callback,
	uint32_t time)
{
	struct keiland_file_chooser *chooser;
	int animating;

	/* The callback is spent. */
	(void)time;
	chooser = data;
	wl_callback_destroy(callback);
	chooser->frame = NULL;

	/* The fingers and the glide, which keep the frames coming while they last. */
	animating = 0;
	if (chooser->finger_count > 0 || chooser->dragging || chooser->gliding) {
		chooser_touch_step(chooser, chooser_now_us());
		animating = 1;
	}

	/* A tap may have answered. */
	if (chooser->model.answered) {
		chooser_after_input(chooser);
		return;
	}

	/* A frame when something changed or moves. */
	if (chooser->dirty || animating)
		chooser_redraw(chooser);
}

/* The round trip after the answer: the application is told, last. */
static void
chooser_tell_done(
	void *data,
	struct wl_callback *callback,
	uint32_t time)
{
	struct keiland_file_chooser *chooser;

	/* The callback is spent; the answer is told once. */
	(void)time;
	chooser = data;
	wl_callback_destroy(callback);
	chooser->telling = NULL;
	chooser->told = 1;

	/* The application hears it (and may destroy the chooser; nothing touches it after). */
	chooser->listener->done(chooser->data, chooser, chooser->model.result, chooser->model.answer, chooser->model.filter);
}

/* The seat's devices: the chooser takes a pointer, a keyboard and a touch screen of its own. */
static void
chooser_seat_capabilities(
	void *data,
	struct wl_seat *seat,
	uint32_t capabilities)
{
	struct keiland_file_chooser *chooser;

	/* A pointer. */
	chooser = data;
	if ((capabilities & WL_SEAT_CAPABILITY_POINTER) != 0U && chooser->pointer == NULL) {
		chooser->pointer = wl_seat_get_pointer(seat);
		if (chooser->pointer != NULL)
			(void)wl_pointer_add_listener(chooser->pointer, &chooser_pointer_listener, chooser);
	}

	/* A keyboard. */
	if ((capabilities & WL_SEAT_CAPABILITY_KEYBOARD) != 0U && chooser->keyboard == NULL) {
		chooser->keyboard = wl_seat_get_keyboard(seat);
		if (chooser->keyboard != NULL)
			(void)wl_keyboard_add_listener(chooser->keyboard, &chooser_keyboard_listener, chooser);
	}

	/* A touch screen. */
	if ((capabilities & WL_SEAT_CAPABILITY_TOUCH) != 0U && chooser->touch == NULL) {
		chooser->touch = wl_seat_get_touch(seat);
		if (chooser->touch != NULL)
			(void)wl_touch_add_listener(chooser->touch, &chooser_touch_listener, chooser);
	}
}

/* The seat's name does not matter. */
static void
chooser_seat_name(
	void *data,
	struct wl_seat *seat,
	const char *name)
{
	/* Nothing to keep. */
	(void)data;
	(void)seat;
	(void)name;
}

/* The pointer comes over a surface: followed when it is the chooser's. */
static void
chooser_pointer_enter(
	void *data,
	struct wl_pointer *pointer,
	uint32_t serial,
	struct wl_surface *surface,
	wl_fixed_t x,
	wl_fixed_t y)
{
	struct keiland_file_chooser *chooser;
	int changed;

	/* Another surface of the application is not the chooser's. */
	(void)pointer;
	(void)serial;
	chooser = data;
	chooser->pointer_inside = 0;
	if (surface == NULL || surface != chooser->surface)
		return;

	/* The place, and what is under it lit. */
	chooser->pointer_inside = 1;
	chooser->pointer_x = wl_fixed_to_int(x);
	chooser->pointer_y = wl_fixed_to_int(y);
	changed = kl_chooser_motion(&chooser->model, chooser->pointer_x, chooser->pointer_y);
	if (changed)
		chooser_redraw(chooser);
}

/* The pointer leaves a surface: nothing of the chooser's is lit. */
static void
chooser_pointer_leave(
	void *data,
	struct wl_pointer *pointer,
	uint32_t serial,
	struct wl_surface *surface)
{
	struct keiland_file_chooser *chooser;

	/* Only the chooser's own surface matters. */
	(void)pointer;
	(void)serial;
	chooser = data;
	if (surface == NULL || surface != chooser->surface || !chooser->pointer_inside)
		return;

	/* Nothing lit. */
	chooser->pointer_inside = 0;
	chooser->model.hover_part = KL_PART_NONE;
	chooser->model.hover_index = -1;
	chooser_redraw(chooser);
}

/* The pointer moves over the chooser. */
static void
chooser_pointer_motion(
	void *data,
	struct wl_pointer *pointer,
	uint32_t time,
	wl_fixed_t x,
	wl_fixed_t y)
{
	struct keiland_file_chooser *chooser;
	int changed;

	/* Only over the chooser's surface. */
	(void)pointer;
	(void)time;
	chooser = data;
	if (!chooser->pointer_inside)
		return;

	/* The place, and what is under it lit. */
	chooser->pointer_x = wl_fixed_to_int(x);
	chooser->pointer_y = wl_fixed_to_int(y);
	changed = kl_chooser_motion(&chooser->model, chooser->pointer_x, chooser->pointer_y);
	if (changed)
		chooser_redraw(chooser);
}

/* A button over the chooser: the left one clicks when pressed. */
static void
chooser_pointer_button(
	void *data,
	struct wl_pointer *pointer,
	uint32_t serial,
	uint32_t time,
	uint32_t button,
	uint32_t state)
{
	struct keiland_file_chooser *chooser;

	/* Only a press of the left button over the chooser's surface. */
	(void)pointer;
	(void)serial;
	chooser = data;
	if (!chooser->pointer_inside)
		return;
	if (button != CHOOSER_BUTTON_LEFT || state != WL_POINTER_BUTTON_STATE_PRESSED)
		return;

	/* The click, then its outcome shown (or told). */
	(void)kl_chooser_click(&chooser->model, chooser->pointer_x, chooser->pointer_y, (uint64_t)time);
	chooser_after_input(chooser);
}

/* The wheel over the chooser scrolls its list. */
static void
chooser_pointer_axis(
	void *data,
	struct wl_pointer *pointer,
	uint32_t time,
	uint32_t axis,
	wl_fixed_t value)
{
	struct keiland_file_chooser *chooser;
	int changed;

	/* Only the vertical wheel over the chooser's surface. */
	(void)pointer;
	(void)time;
	chooser = data;
	if (!chooser->pointer_inside || axis != WL_POINTER_AXIS_VERTICAL_SCROLL)
		return;

	/* The list scrolled, and the finger's scroller follows. */
	changed = kl_chooser_wheel(&chooser->model, wl_fixed_to_double(value) * CHOOSER_WHEEL_SCALE);
	if (changed)
		chooser_after_input(chooser);
}

/* A group of pointer events ends; each was carried out as it came. */
static void
chooser_pointer_frame(
	void *data,
	struct wl_pointer *pointer)
{
	/* Nothing waits for the group. */
	(void)data;
	(void)pointer;
}

/* Where scrolling comes from does not matter. */
static void
chooser_pointer_axis_source(
	void *data,
	struct wl_pointer *pointer,
	uint32_t source)
{
	/* Nothing to keep. */
	(void)data;
	(void)pointer;
	(void)source;
}

/* A scroll that stops does not matter. */
static void
chooser_pointer_axis_stop(
	void *data,
	struct wl_pointer *pointer,
	uint32_t time,
	uint32_t axis)
{
	/* Nothing to keep. */
	(void)data;
	(void)pointer;
	(void)time;
	(void)axis;
}

/* The wheel's notches do not matter (its value moves the list). */
static void
chooser_pointer_axis_discrete(
	void *data,
	struct wl_pointer *pointer,
	uint32_t axis,
	int32_t discrete)
{
	/* Nothing to keep. */
	(void)data;
	(void)pointer;
	(void)axis;
	(void)discrete;
}

/* The keymap is not used (zdesktop sends evdev codes; the chooser has the US layout); its descriptor is closed. */
static void
chooser_keyboard_keymap(
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

/* The keyboard comes to a surface: the chooser has it when it is its surface. */
static void
chooser_keyboard_enter(
	void *data,
	struct wl_keyboard *keyboard,
	uint32_t serial,
	struct wl_surface *surface,
	struct wl_array *keys)
{
	struct keiland_file_chooser *chooser;

	/* The chooser's surface or another. */
	(void)keyboard;
	(void)serial;
	(void)keys;
	chooser = data;
	chooser->model.focused = 0;
	if (surface != NULL && surface == chooser->surface)
		chooser->model.focused = 1;
	chooser->model.modifiers = 0;
	chooser_redraw(chooser);
}

/* The keyboard leaves a surface. */
static void
chooser_keyboard_leave(
	void *data,
	struct wl_keyboard *keyboard,
	uint32_t serial,
	struct wl_surface *surface)
{
	struct keiland_file_chooser *chooser;

	/* Leaving the chooser's surface takes its keyboard away. */
	(void)keyboard;
	(void)serial;
	chooser = data;
	if (surface == NULL || surface != chooser->surface)
		return;
	chooser->model.focused = 0;
	chooser->model.modifiers = 0;
	chooser_redraw(chooser);
}

/* A key pressed while the chooser has the keyboard. */
static void
chooser_keyboard_key(
	void *data,
	struct wl_keyboard *keyboard,
	uint32_t serial,
	uint32_t time,
	uint32_t key,
	uint32_t state)
{
	struct keiland_file_chooser *chooser;
	uint32_t character;
	int changed;

	/* Only presses, and only while the chooser has the keyboard. */
	(void)keyboard;
	(void)serial;
	(void)time;
	chooser = data;
	if (!chooser->model.focused || state != WL_KEYBOARD_KEY_STATE_PRESSED)
		return;

	/* The key with what it types. */
	character = kl_chooser_character(key, chooser->model.modifiers);
	changed = kl_chooser_key(&chooser->model, key, character);
	if (changed)
		chooser_after_input(chooser);
}

/* Keeps the modifiers held, in the chooser's own bits. */
static void
chooser_keyboard_modifiers(
	void *data,
	struct wl_keyboard *keyboard,
	uint32_t serial,
	uint32_t depressed,
	uint32_t latched,
	uint32_t locked,
	uint32_t group)
{
	struct keiland_file_chooser *chooser;

	/* Only the held modifiers count; zdesktop latches and locks nothing. */
	(void)keyboard;
	(void)serial;
	(void)latched;
	(void)locked;
	(void)group;
	chooser = data;
	chooser->model.modifiers = 0;
	if ((depressed & CHOOSER_WAYLAND_SHIFT) != 0U)
		chooser->model.modifiers |= KL_MOD_SHIFT;
	if ((depressed & CHOOSER_WAYLAND_CTRL) != 0U)
		chooser->model.modifiers |= KL_MOD_CTRL;
	if ((depressed & CHOOSER_WAYLAND_ALT) != 0U)
		chooser->model.modifiers |= KL_MOD_ALT;
}

/* Key repeat is not used by the chooser. */
static void
chooser_keyboard_repeat(
	void *data,
	struct wl_keyboard *keyboard,
	int32_t rate,
	int32_t delay)
{
	/* Nothing to keep. */
	(void)data;
	(void)keyboard;
	(void)rate;
	(void)delay;
}

/* A finger touches: followed when it is on the chooser's surface. */
static void
chooser_touch_down(
	void *data,
	struct wl_touch *touch,
	uint32_t serial,
	uint32_t time,
	struct wl_surface *surface,
	int32_t id,
	wl_fixed_t x,
	wl_fixed_t y)
{
	struct keiland_file_chooser *chooser;
	uint64_t now;
	int error;

	/* Only a finger on the chooser's surface, and not more than it follows. */
	(void)touch;
	(void)serial;
	chooser = data;
	if (surface == NULL || surface != chooser->surface)
		return;
	if (chooser->finger_count >= CHOOSER_FINGERS)
		return;

	/* The first finger catches a glide (that touch then only stops it). */
	now = chooser_now_us();
	if (chooser->finger_count == 0) {
		chooser->caught = keiland_scroller_press(chooser->scroller, now);
		chooser->gliding = 0;
	}

	/* The finger, to the gestures. */
	error = keiland_gesture_down(chooser->gesture, id, (uint64_t)time * 1000U, now, wl_fixed_to_double(x), wl_fixed_to_double(y));
	if (error != 0)
		return;
	chooser->fingers[chooser->finger_count] = id;
	chooser->finger_count++;
	chooser->touched = 1;

	/* The frames start, so that time goes on for a long press and the drag. */
	chooser_redraw(chooser);
}

/* A finger of the chooser lifts. */
static void
chooser_touch_up(
	void *data,
	struct wl_touch *touch,
	uint32_t serial,
	uint32_t time,
	int32_t id)
{
	struct keiland_file_chooser *chooser;
	int slot;

	/* Only the chooser's fingers. */
	(void)touch;
	(void)serial;
	chooser = data;
	slot = chooser_finger(chooser, id);
	if (slot < 0)
		return;

	/* To the gestures, and out of the list of fingers. */
	(void)keiland_gesture_up(chooser->gesture, id, (uint64_t)time * 1000U);
	chooser->fingers[slot] = chooser->fingers[chooser->finger_count - 1];
	chooser->finger_count--;
	chooser->touched = 1;
	chooser_redraw(chooser);
}

/* A finger of the chooser moves. */
static void
chooser_touch_motion(
	void *data,
	struct wl_touch *touch,
	uint32_t time,
	int32_t id,
	wl_fixed_t x,
	wl_fixed_t y)
{
	struct keiland_file_chooser *chooser;
	int slot;

	/* Only the chooser's fingers. */
	(void)touch;
	chooser = data;
	slot = chooser_finger(chooser, id);
	if (slot < 0)
		return;

	/* To the gestures (the frame moves the list). */
	chooser->touched = 1;
	(void)keiland_gesture_motion(chooser->gesture, id, (uint64_t)time * 1000U, chooser_now_us(), wl_fixed_to_double(x), wl_fixed_to_double(y));
}

/* A group of touch events ends: the gestures are taken at once, so a tap answers without waiting for a frame. */
static void
chooser_touch_frame(
	void *data,
	struct wl_touch *touch)
{
	struct keiland_file_chooser *chooser;

	/* No finger of the chooser in the group, nothing to take. */
	(void)touch;
	chooser = data;
	if (!chooser->touched)
		return;
	chooser->touched = 0;

	/* The gestures so far, and their outcome shown or told. */
	chooser_touch_step(chooser, chooser_now_us());
	chooser_after_input(chooser);
}

/* The compositor took the fingers. */
static void
chooser_touch_cancel(
	void *data,
	struct wl_touch *touch)
{
	struct keiland_file_chooser *chooser;

	/* The fingers forgotten; the list springs back if it was pulled past an end. */
	(void)touch;
	chooser = data;
	if (chooser->finger_count == 0)
		return;
	keiland_gesture_cancel(chooser->gesture);
	chooser->finger_count = 0;
	chooser_touch_step(chooser, chooser_now_us());
	chooser_redraw(chooser);
}
