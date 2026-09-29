/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws094-p002: a desktop surface client for zdesktop's keiland_desktop_v1,
 * with protocol code of its own (as wayland-scanner would make it) over
 * libwayland's marshalling.
 *
 * It takes the desktop surface with the token (KEILAND_DESKTOP_TOKEN, or
 * --token=), and draws a wl_shm image of the size configured: clear, with
 * three 64x64 squares (red, green, blue) down the right edge, as icons
 * would be, and a yellow one at the top-left corner; each left press adds a white 16x16 mark where it was, and a
 * right press a black one.  Every event is one line on standard output:
 * DESKPROBE <what> ...  (configure, frame, enter, leave, motion, button,
 * key, touch, dnd, failed, done).
 *
 *   desktop-probe [--token=TOKEN] [--timeout-s=N]
 */

#include <wayland-client.h>

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

/* Marks a parameter a listener must take but does not use. */
#define UNUSED_PARAMETER(name)	((void)(name))

/* The squares: their side, the margin to the right edge and between them, and the press marks' side. */
#define PROBE_ICON		64
#define PROBE_MARGIN		24
#define PROBE_MARK		16

/* The most press marks kept. */
#define PROBE_MARKS		32

/* The left and right buttons (evdev). */
#define PROBE_BUTTON_LEFT	0x110U
#define PROBE_BUTTON_RIGHT	0x111U

extern const struct wl_interface probe_desktop_manager_interface;
extern const struct wl_interface probe_desktop_surface_interface;

/* get_desktop_surface: the new desktop surface, the surface and the token. */
static const struct wl_interface *manager_get_types[] = {
	&probe_desktop_surface_interface,
	&wl_surface_interface,
	NULL,
};

/* The arguments of messages that name no interface (at most five). */
static const struct wl_interface *probe_plain_types[] = {
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
};

/* The requests of keiland_desktop_manager_v1. */
static const struct wl_message manager_requests[] = {
	{ "destroy", "", NULL },
	{ "get_desktop_surface", "nos", manager_get_types },
};

/* keiland_desktop_manager_v1, as the probe describes it. */
const struct wl_interface probe_desktop_manager_interface = {
	"keiland_desktop_manager_v1", 1, 2, manager_requests,
	0, NULL
};

/* The requests of keiland_desktop_surface_v1. */
static const struct wl_message surface_requests[] = {
	{ "destroy", "", NULL },
	{ "ack_configure", "u", probe_plain_types },
};

/* The events of keiland_desktop_surface_v1. */
static const struct wl_message surface_events[] = {
	{ "configure", "uiiii", probe_plain_types },
};

/* keiland_desktop_surface_v1, as the probe describes it. */
const struct wl_interface probe_desktop_surface_interface = {
	"keiland_desktop_surface_v1", 1, 2, surface_requests,
	1, surface_events
};

/* The configure event's listener (as wayland-scanner would declare it). */
struct probe_desktop_surface_listener {
	void (*configure)(void *data, struct wl_proxy *desktop, uint32_t serial, int32_t x, int32_t y, int32_t width, int32_t height);
};

/*
 * One press mark: where it is on the surface and its colour.
 */
struct probe_mark {
	int32_t x;
	int32_t y;
	uint32_t color;
};

/*
 * The probe's connection, globals, surface and image, for its whole run.
 */
struct probe {
	struct wl_display *display;
	struct wl_registry *registry;
	struct wl_compositor *compositor;
	struct wl_shm *shm;
	struct wl_seat *seat;
	struct wl_pointer *pointer;
	struct wl_keyboard *keyboard;
	struct wl_touch *touch;
	struct wl_data_device_manager *data_manager;
	struct wl_data_device *data_device;
	struct wl_proxy *manager;
	struct wl_proxy *desktop;
	struct wl_surface *surface;
	const char *token;
	int32_t width;
	int32_t height;
	int32_t pointer_x;
	int32_t pointer_y;
	struct probe_mark marks[PROBE_MARKS];
	unsigned mark_count;
	int dirty;
};

/*
 * The probe, for the whole run (the listeners reach it through their data).
 */
static struct probe probe;

static void registry_global(void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version);
static void registry_remove(void *data, struct wl_registry *registry, uint32_t name);
static void desktop_configure(void *data, struct wl_proxy *desktop, uint32_t serial, int32_t x, int32_t y, int32_t width, int32_t height);
static void frame_done(void *data, struct wl_callback *callback, uint32_t time);
static void seat_capabilities(void *data, struct wl_seat *seat, uint32_t capabilities);
static void seat_name(void *data, struct wl_seat *seat, const char *name);
static void pointer_enter(void *data, struct wl_pointer *pointer, uint32_t serial, struct wl_surface *surface, wl_fixed_t x, wl_fixed_t y);
static void pointer_leave(void *data, struct wl_pointer *pointer, uint32_t serial, struct wl_surface *surface);
static void pointer_motion(void *data, struct wl_pointer *pointer, uint32_t time, wl_fixed_t x, wl_fixed_t y);
static void pointer_button(void *data, struct wl_pointer *pointer, uint32_t serial, uint32_t time, uint32_t button, uint32_t state);
static void pointer_axis(void *data, struct wl_pointer *pointer, uint32_t time, uint32_t axis, wl_fixed_t value);
static void keyboard_keymap(void *data, struct wl_keyboard *keyboard, uint32_t format, int32_t fd, uint32_t size);
static void keyboard_enter(void *data, struct wl_keyboard *keyboard, uint32_t serial, struct wl_surface *surface, struct wl_array *keys);
static void keyboard_leave(void *data, struct wl_keyboard *keyboard, uint32_t serial, struct wl_surface *surface);
static void keyboard_key(void *data, struct wl_keyboard *keyboard, uint32_t serial, uint32_t time, uint32_t key, uint32_t state);
static void keyboard_modifiers(void *data, struct wl_keyboard *keyboard, uint32_t serial, uint32_t depressed, uint32_t latched, uint32_t locked, uint32_t group);
static void touch_down(void *data, struct wl_touch *touch, uint32_t serial, uint32_t time, struct wl_surface *surface, int32_t id, wl_fixed_t x, wl_fixed_t y);
static void touch_up(void *data, struct wl_touch *touch, uint32_t serial, uint32_t time, int32_t id);
static void touch_motion(void *data, struct wl_touch *touch, uint32_t time, int32_t id, wl_fixed_t x, wl_fixed_t y);
static void touch_frame(void *data, struct wl_touch *touch);
static void touch_cancel(void *data, struct wl_touch *touch);
static void data_offer(void *data, struct wl_data_device *device, struct wl_data_offer *offer);
static void data_enter(void *data, struct wl_data_device *device, uint32_t serial, struct wl_surface *surface, wl_fixed_t x, wl_fixed_t y, struct wl_data_offer *offer);
static void data_leave(void *data, struct wl_data_device *device);
static void data_motion(void *data, struct wl_data_device *device, uint32_t time, wl_fixed_t x, wl_fixed_t y);
static void data_drop(void *data, struct wl_data_device *device);
static void data_selection(void *data, struct wl_data_device *device, struct wl_data_offer *offer);
static int draw(void);
static void fill(uint32_t *pixels, int32_t stride, int32_t x, int32_t y, int32_t side, uint32_t color);
static int parse(int argc, char **argv, unsigned *timeout);

/* The registry's listener. */
static const struct wl_registry_listener registry_listener = {
	registry_global,
	registry_remove
};

/* The desktop surface's listener. */
static const struct probe_desktop_surface_listener desktop_listener = {
	desktop_configure
};

/* The frame callback's listener. */
static const struct wl_callback_listener frame_listener = {
	frame_done
};

/* The seat's listener. */
static const struct wl_seat_listener seat_listener = {
	seat_capabilities,
	seat_name
};

/* The pointer's listener (version 1 events only). */
static const struct wl_pointer_listener pointer_listener = {
	pointer_enter,
	pointer_leave,
	pointer_motion,
	pointer_button,
	pointer_axis,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL
};

/* The keyboard's listener (without repeat_info). */
static const struct wl_keyboard_listener keyboard_listener = {
	keyboard_keymap,
	keyboard_enter,
	keyboard_leave,
	keyboard_key,
	keyboard_modifiers,
	NULL
};

/* The touch screen's listener (without shape and orientation). */
static const struct wl_touch_listener touch_listener = {
	touch_down,
	touch_up,
	touch_motion,
	touch_frame,
	touch_cancel,
	NULL,
	NULL
};

/* The data device's listener. */
static const struct wl_data_device_listener data_listener = {
	data_offer,
	data_enter,
	data_leave,
	data_motion,
	data_drop,
	data_selection
};

/* Runs the probe until the timeout or a failure. */
int
main(
	int argc,
	char **argv)
{
	struct pollfd descriptor;
	const char *inherited;
	char token[128];
	unsigned timeout;
	time_t started;
	int status;
	int error;

	/* The options; the token from the environment unless given (copied: unsetenv frees the environment's string). */
	memset(&probe, 0, sizeof(probe));
	inherited = getenv("KEILAND_DESKTOP_TOKEN");
	if (inherited != NULL) {
		snprintf(token, sizeof(token), "%s", inherited);
		probe.token = token;
	}
	error = parse(argc, argv, &timeout);
	if (error != 0) {
		fprintf(stderr, "usage: desktop-probe [--token=TOKEN] [--timeout-s=N]\n");
		return 2;
	}

	/* The token is not passed on to anything the probe might start. */
	(void)unsetenv("KEILAND_DESKTOP_TOKEN");
	if (probe.token == NULL)
		probe.token = "";

	/* The connection. */
	probe.display = wl_display_connect(NULL);
	if (probe.display == NULL) {
		printf("DESKPROBE failed what=connect errno=%d\n", errno);
		return 1;
	}

	/* The globals. */
	probe.registry = wl_display_get_registry(probe.display);
	(void)wl_registry_add_listener(probe.registry, &registry_listener, NULL);
	status = wl_display_roundtrip(probe.display);
	if (status < 0 || probe.compositor == NULL || probe.shm == NULL || probe.manager == NULL) {
		printf("DESKPROBE failed what=globals\n");
		return 1;
	}

	/* The seat's devices. */
	status = wl_display_roundtrip(probe.display);
	if (status < 0) {
		printf("DESKPROBE failed what=seat\n");
		return 1;
	}

	/* The drag and drop device, when the compositor has one. */
	if (probe.data_manager != NULL && probe.seat != NULL) {
		probe.data_device = wl_data_device_manager_get_data_device(probe.data_manager, probe.seat);
		(void)wl_data_device_add_listener(probe.data_device, &data_listener, NULL);
	}

	/* The surface, and the desktop's role with the token. */
	probe.surface = wl_compositor_create_surface(probe.compositor);
	probe.desktop = wl_proxy_marshal_constructor(probe.manager, 1U, &probe_desktop_surface_interface, NULL, probe.surface, probe.token);
	(void)wl_proxy_add_listener(probe.desktop, (void (**)(void))&desktop_listener, NULL);

	/* The events until the timeout; a failed connection ends the run. */
	started = time(NULL);
	for (;;) {
		/* The image again when it changed. */
		if (probe.dirty) {
			error = draw();
			if (error != 0) {
				printf("DESKPROBE failed what=draw errno=%d\n", error);
				return 1;
			}
		}

		/* Anything queued, sent. */
		status = wl_display_dispatch_pending(probe.display);
		if (status < 0)
			break;
		(void)wl_display_flush(probe.display);

		/* The timeout. */
		if (timeout != 0U && time(NULL) - started >= (time_t)timeout) {
			printf("DESKPROBE done reason=timeout\n");
			fflush(stdout);
			return 0;
		}

		/* The next events, or a moment's wait. */
		descriptor.fd = wl_display_get_fd(probe.display);
		descriptor.events = POLLIN;
		descriptor.revents = 0;
		status = poll(&descriptor, 1, 200);
		if (status > 0) {
			status = wl_display_dispatch(probe.display);
			if (status < 0)
				break;
		}
	}

	/* The connection failed: the compositor's error, if any. */
	error = wl_display_get_error(probe.display);
	printf("DESKPROBE failed what=connection error=%d\n", error);
	fflush(stdout);
	return 1;
}

/* Binds the globals the probe uses. */
static void
registry_global(
	void *data,
	struct wl_registry *registry,
	uint32_t name,
	const char *interface,
	uint32_t version)
{
	int match;

	UNUSED_PARAMETER(data);
	UNUSED_PARAMETER(version);

	/* wl_compositor. */
	match = strcmp(interface, "wl_compositor");
	if (match == 0) {
		probe.compositor = wl_registry_bind(registry, name, &wl_compositor_interface, 4);
		return;
	}

	/* wl_shm. */
	match = strcmp(interface, "wl_shm");
	if (match == 0) {
		probe.shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
		return;
	}

	/* wl_seat, with its listener. */
	match = strcmp(interface, "wl_seat");
	if (match == 0) {
		probe.seat = wl_registry_bind(registry, name, &wl_seat_interface, 5);
		(void)wl_seat_add_listener(probe.seat, &seat_listener, NULL);
		return;
	}

	/* wl_data_device_manager. */
	match = strcmp(interface, "wl_data_device_manager");
	if (match == 0) {
		probe.data_manager = wl_registry_bind(registry, name, &wl_data_device_manager_interface, 3);
		return;
	}

	/* keiland_desktop_manager_v1. */
	match = strcmp(interface, "keiland_desktop_manager_v1");
	if (match == 0)
		probe.manager = wl_registry_bind(registry, name, &probe_desktop_manager_interface, 1);
}

/* A global going away does not matter to the probe. */
static void
registry_remove(
	void *data,
	struct wl_registry *registry,
	uint32_t name)
{
	UNUSED_PARAMETER(data);
	UNUSED_PARAMETER(registry);
	UNUSED_PARAMETER(name);
}

/* Takes the desktop's place: acknowledged, and the image drawn at its size. */
static void
desktop_configure(
	void *data,
	struct wl_proxy *desktop,
	uint32_t serial,
	int32_t x,
	int32_t y,
	int32_t width,
	int32_t height)
{
	UNUSED_PARAMETER(data);

	/* The log line, the acknowledgement and the size. */
	printf("DESKPROBE configure serial=%u x=%d y=%d width=%d height=%d\n", serial, x, y, width, height);
	fflush(stdout);
	wl_proxy_marshal(desktop, 1U, serial);
	probe.width = width;
	probe.height = height;
	probe.dirty = 1;
}

/* A frame showed the image. */
static void
frame_done(
	void *data,
	struct wl_callback *callback,
	uint32_t time)
{
	UNUSED_PARAMETER(data);
	UNUSED_PARAMETER(time);

	/* The log line; the callback is done with. */
	printf("DESKPROBE frame\n");
	fflush(stdout);
	wl_callback_destroy(callback);
}

/* Takes the seat's pointer, keyboard and touch screen. */
static void
seat_capabilities(
	void *data,
	struct wl_seat *seat,
	uint32_t capabilities)
{
	UNUSED_PARAMETER(data);

	/* The pointer. */
	if ((capabilities & WL_SEAT_CAPABILITY_POINTER) != 0U && probe.pointer == NULL) {
		probe.pointer = wl_seat_get_pointer(seat);
		(void)wl_pointer_add_listener(probe.pointer, &pointer_listener, NULL);
	}

	/* The keyboard. */
	if ((capabilities & WL_SEAT_CAPABILITY_KEYBOARD) != 0U && probe.keyboard == NULL) {
		probe.keyboard = wl_seat_get_keyboard(seat);
		(void)wl_keyboard_add_listener(probe.keyboard, &keyboard_listener, NULL);
	}

	/* The touch screen. */
	if ((capabilities & WL_SEAT_CAPABILITY_TOUCH) != 0U && probe.touch == NULL) {
		probe.touch = wl_seat_get_touch(seat);
		(void)wl_touch_add_listener(probe.touch, &touch_listener, NULL);
	}
}

/* The seat's name is not used. */
static void
seat_name(
	void *data,
	struct wl_seat *seat,
	const char *name)
{
	UNUSED_PARAMETER(data);
	UNUSED_PARAMETER(seat);
	UNUSED_PARAMETER(name);
}

/* The pointer came onto the desktop. */
static void
pointer_enter(
	void *data,
	struct wl_pointer *pointer,
	uint32_t serial,
	struct wl_surface *surface,
	wl_fixed_t x,
	wl_fixed_t y)
{
	UNUSED_PARAMETER(data);
	UNUSED_PARAMETER(pointer);
	UNUSED_PARAMETER(serial);
	UNUSED_PARAMETER(surface);

	/* The place, kept for the presses. */
	probe.pointer_x = wl_fixed_to_int(x);
	probe.pointer_y = wl_fixed_to_int(y);
	printf("DESKPROBE enter x=%d y=%d\n", probe.pointer_x, probe.pointer_y);
	fflush(stdout);
}

/* The pointer left the desktop. */
static void
pointer_leave(
	void *data,
	struct wl_pointer *pointer,
	uint32_t serial,
	struct wl_surface *surface)
{
	UNUSED_PARAMETER(data);
	UNUSED_PARAMETER(pointer);
	UNUSED_PARAMETER(serial);
	UNUSED_PARAMETER(surface);

	/* The log line. */
	printf("DESKPROBE leave\n");
	fflush(stdout);
}

/* The pointer moved over the desktop (kept, not logged: there are many). */
static void
pointer_motion(
	void *data,
	struct wl_pointer *pointer,
	uint32_t time,
	wl_fixed_t x,
	wl_fixed_t y)
{
	UNUSED_PARAMETER(data);
	UNUSED_PARAMETER(pointer);
	UNUSED_PARAMETER(time);

	/* The place, kept for the presses. */
	probe.pointer_x = wl_fixed_to_int(x);
	probe.pointer_y = wl_fixed_to_int(y);
}

/* A button on the desktop: logged, and a press marks where it was. */
static void
pointer_button(
	void *data,
	struct wl_pointer *pointer,
	uint32_t serial,
	uint32_t time,
	uint32_t button,
	uint32_t state)
{
	uint32_t color;

	UNUSED_PARAMETER(data);
	UNUSED_PARAMETER(pointer);
	UNUSED_PARAMETER(serial);
	UNUSED_PARAMETER(time);

	/* The log line. */
	printf("DESKPROBE button x=%d y=%d button=%u state=%u\n", probe.pointer_x, probe.pointer_y, button, state);
	fflush(stdout);

	/* Only a press marks. */
	if (state == 0U || probe.mark_count == PROBE_MARKS)
		return;

	/* White for the left button, black for the right. */
	color = 0xffffffffU;
	if (button == PROBE_BUTTON_RIGHT)
		color = 0xff000000U;

	/* The mark, drawn with the next image. */
	probe.marks[probe.mark_count].x = probe.pointer_x;
	probe.marks[probe.mark_count].y = probe.pointer_y;
	probe.marks[probe.mark_count].color = color;
	probe.mark_count++;
	probe.dirty = 1;
}

/* The wheel is logged. */
static void
pointer_axis(
	void *data,
	struct wl_pointer *pointer,
	uint32_t time,
	uint32_t axis,
	wl_fixed_t value)
{
	UNUSED_PARAMETER(data);
	UNUSED_PARAMETER(pointer);
	UNUSED_PARAMETER(time);

	/* The log line. */
	printf("DESKPROBE axis axis=%u value=%d\n", axis, wl_fixed_to_int(value));
	fflush(stdout);
}

/* The keymap's descriptor is closed (keys are logged as codes). */
static void
keyboard_keymap(
	void *data,
	struct wl_keyboard *keyboard,
	uint32_t format,
	int32_t fd,
	uint32_t size)
{
	UNUSED_PARAMETER(data);
	UNUSED_PARAMETER(keyboard);
	UNUSED_PARAMETER(format);
	UNUSED_PARAMETER(size);

	/* The descriptor is the probe's to close. */
	if (fd >= 0)
		close(fd);
}

/* The keyboard came to the desktop. */
static void
keyboard_enter(
	void *data,
	struct wl_keyboard *keyboard,
	uint32_t serial,
	struct wl_surface *surface,
	struct wl_array *keys)
{
	UNUSED_PARAMETER(data);
	UNUSED_PARAMETER(keyboard);
	UNUSED_PARAMETER(serial);
	UNUSED_PARAMETER(surface);
	UNUSED_PARAMETER(keys);

	/* The log line. */
	printf("DESKPROBE focus in\n");
	fflush(stdout);
}

/* The keyboard left the desktop. */
static void
keyboard_leave(
	void *data,
	struct wl_keyboard *keyboard,
	uint32_t serial,
	struct wl_surface *surface)
{
	UNUSED_PARAMETER(data);
	UNUSED_PARAMETER(keyboard);
	UNUSED_PARAMETER(serial);
	UNUSED_PARAMETER(surface);

	/* The log line. */
	printf("DESKPROBE focus out\n");
	fflush(stdout);
}

/* A key on the desktop is logged. */
static void
keyboard_key(
	void *data,
	struct wl_keyboard *keyboard,
	uint32_t serial,
	uint32_t time,
	uint32_t key,
	uint32_t state)
{
	UNUSED_PARAMETER(data);
	UNUSED_PARAMETER(keyboard);
	UNUSED_PARAMETER(serial);
	UNUSED_PARAMETER(time);

	/* The log line. */
	printf("DESKPROBE key key=%u state=%u\n", key, state);
	fflush(stdout);
}

/* The modifiers are not used. */
static void
keyboard_modifiers(
	void *data,
	struct wl_keyboard *keyboard,
	uint32_t serial,
	uint32_t depressed,
	uint32_t latched,
	uint32_t locked,
	uint32_t group)
{
	UNUSED_PARAMETER(data);
	UNUSED_PARAMETER(keyboard);
	UNUSED_PARAMETER(serial);
	UNUSED_PARAMETER(depressed);
	UNUSED_PARAMETER(latched);
	UNUSED_PARAMETER(locked);
	UNUSED_PARAMETER(group);
}

/* A finger touched the desktop. */
static void
touch_down(
	void *data,
	struct wl_touch *touch,
	uint32_t serial,
	uint32_t time,
	struct wl_surface *surface,
	int32_t id,
	wl_fixed_t x,
	wl_fixed_t y)
{
	UNUSED_PARAMETER(data);
	UNUSED_PARAMETER(touch);
	UNUSED_PARAMETER(serial);
	UNUSED_PARAMETER(time);
	UNUSED_PARAMETER(surface);

	/* The log line. */
	printf("DESKPROBE touch down id=%d x=%d y=%d\n", id, wl_fixed_to_int(x), wl_fixed_to_int(y));
	fflush(stdout);
}

/* A finger lifted. */
static void
touch_up(
	void *data,
	struct wl_touch *touch,
	uint32_t serial,
	uint32_t time,
	int32_t id)
{
	UNUSED_PARAMETER(data);
	UNUSED_PARAMETER(touch);
	UNUSED_PARAMETER(serial);
	UNUSED_PARAMETER(time);

	/* The log line. */
	printf("DESKPROBE touch up id=%d\n", id);
	fflush(stdout);
}

/* A finger moved (not logged). */
static void
touch_motion(
	void *data,
	struct wl_touch *touch,
	uint32_t time,
	int32_t id,
	wl_fixed_t x,
	wl_fixed_t y)
{
	UNUSED_PARAMETER(data);
	UNUSED_PARAMETER(touch);
	UNUSED_PARAMETER(time);
	UNUSED_PARAMETER(id);
	UNUSED_PARAMETER(x);
	UNUSED_PARAMETER(y);
}

/* The end of a group of touch events. */
static void
touch_frame(
	void *data,
	struct wl_touch *touch)
{
	UNUSED_PARAMETER(data);
	UNUSED_PARAMETER(touch);
}

/* The compositor took the fingers. */
static void
touch_cancel(
	void *data,
	struct wl_touch *touch)
{
	UNUSED_PARAMETER(data);
	UNUSED_PARAMETER(touch);

	/* The log line. */
	printf("DESKPROBE touch cancel\n");
	fflush(stdout);
}

/* A drag's offer is announced (its types are not read). */
static void
data_offer(
	void *data,
	struct wl_data_device *device,
	struct wl_data_offer *offer)
{
	UNUSED_PARAMETER(data);
	UNUSED_PARAMETER(device);
	UNUSED_PARAMETER(offer);
}

/* A drag came over the desktop. */
static void
data_enter(
	void *data,
	struct wl_data_device *device,
	uint32_t serial,
	struct wl_surface *surface,
	wl_fixed_t x,
	wl_fixed_t y,
	struct wl_data_offer *offer)
{
	UNUSED_PARAMETER(data);
	UNUSED_PARAMETER(device);
	UNUSED_PARAMETER(serial);
	UNUSED_PARAMETER(surface);
	UNUSED_PARAMETER(offer);

	/* The log line. */
	printf("DESKPROBE dnd enter x=%d y=%d\n", wl_fixed_to_int(x), wl_fixed_to_int(y));
	fflush(stdout);
}

/* The drag left the desktop. */
static void
data_leave(
	void *data,
	struct wl_data_device *device)
{
	UNUSED_PARAMETER(data);
	UNUSED_PARAMETER(device);

	/* The log line. */
	printf("DESKPROBE dnd leave\n");
	fflush(stdout);
}

/* The drag moved over the desktop (not logged). */
static void
data_motion(
	void *data,
	struct wl_data_device *device,
	uint32_t time,
	wl_fixed_t x,
	wl_fixed_t y)
{
	UNUSED_PARAMETER(data);
	UNUSED_PARAMETER(device);
	UNUSED_PARAMETER(time);
	UNUSED_PARAMETER(x);
	UNUSED_PARAMETER(y);
}

/* The drag was dropped on the desktop. */
static void
data_drop(
	void *data,
	struct wl_data_device *device)
{
	UNUSED_PARAMETER(data);
	UNUSED_PARAMETER(device);

	/* The log line. */
	printf("DESKPROBE dnd drop\n");
	fflush(stdout);
}

/* The clipboard's offer is not used. */
static void
data_selection(
	void *data,
	struct wl_data_device *device,
	struct wl_data_offer *offer)
{
	UNUSED_PARAMETER(data);
	UNUSED_PARAMETER(device);
	UNUSED_PARAMETER(offer);
}

/* Draws the image at the configured size into a new wl_shm buffer and commits it with a frame callback. */
static int
draw(void)
{
	struct wl_shm_pool *pool;
	struct wl_buffer *buffer;
	struct wl_callback *callback;
	uint32_t *pixels;
	char name[64];
	size_t size;
	unsigned index;
	int descriptor;
	int32_t stride;
	int32_t left;
	int error;

	/* Nothing to draw before the configure. */
	probe.dirty = 0;
	if (probe.width <= 0 || probe.height <= 0)
		return 0;

	/* The shared memory of the image. */
	stride = probe.width * 4;
	size = (size_t)stride * (size_t)probe.height;
	snprintf(name, sizeof(name), "/desktop-probe-%ld-%u", (long)getpid(), probe.mark_count);
	descriptor = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
	if (descriptor < 0)
		return errno;

	/* The memory's name is not needed once it is open. */
	(void)shm_unlink(name);
	error = ftruncate(descriptor, (off_t)size);
	if (error != 0) {
		error = errno;
		close(descriptor);
		return error;
	}

	/* Mapped for the drawing. */
	pixels = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, descriptor, 0);
	if (pixels == MAP_FAILED) {
		error = errno;
		close(descriptor);
		return error;
	}

	/* Clear, then the three squares down the right edge, then the press marks. */
	memset(pixels, 0, size);
	left = probe.width - PROBE_MARGIN - PROBE_ICON;
	fill(pixels, probe.width, left, PROBE_MARGIN, PROBE_ICON, 0xffd94040U);
	fill(pixels, probe.width, left, PROBE_MARGIN * 2 + PROBE_ICON, PROBE_ICON, 0xff40a060U);
	fill(pixels, probe.width, left, PROBE_MARGIN * 3 + PROBE_ICON * 2, PROBE_ICON, 0xff3060d0U);

	/* A yellow square at the top-left corner too, which App Home's shrunk layer still shows. */
	fill(pixels, probe.width, PROBE_MARGIN, PROBE_MARGIN, PROBE_ICON, 0xffe0b020U);
	for (index = 0; index < probe.mark_count; index++)
		fill(pixels, probe.width, probe.marks[index].x - PROBE_MARK / 2, probe.marks[index].y - PROBE_MARK / 2, PROBE_MARK, probe.marks[index].color);
	munmap(pixels, size);

	/* The buffer, from a pool over the memory. */
	pool = wl_shm_create_pool(probe.shm, descriptor, (int32_t)size);
	buffer = wl_shm_pool_create_buffer(pool, 0, probe.width, probe.height, stride, WL_SHM_FORMAT_ARGB8888);
	wl_shm_pool_destroy(pool);
	close(descriptor);

	/* Attached with a frame callback, committed. */
	wl_surface_attach(probe.surface, buffer, 0, 0);
	wl_surface_damage(probe.surface, 0, 0, probe.width, probe.height);
	callback = wl_surface_frame(probe.surface);
	(void)wl_callback_add_listener(callback, &frame_listener, NULL);
	wl_surface_commit(probe.surface);
	printf("DESKPROBE commit width=%d height=%d marks=%u\n", probe.width, probe.height, probe.mark_count);
	fflush(stdout);

	/* Succeeded: the image is on its way. */
	return 0;
}

/* Fills a square of the image with a colour, clipped to it. */
static void
fill(
	uint32_t *pixels,
	int32_t stride,
	int32_t x,
	int32_t y,
	int32_t side,
	uint32_t color)
{
	int32_t row;
	int32_t column;

	/* Each pixel of the square inside the image. */
	for (row = y; row < y + side; row++) {
		/* A row outside the image is left out. */
		if (row < 0 || row >= probe.height)
			continue;

		/* The row's pixels inside the image. */
		for (column = x; column < x + side; column++) {
			if (column >= 0 && column < probe.width)
				pixels[(size_t)row * (size_t)stride + (size_t)column] = color;
		}
	}
}

/* Reads the options: --token=TOKEN and --timeout-s=N. */
static int
parse(
	int argc,
	char **argv,
	unsigned *timeout)
{
	int index;
	int match;

	/* No timeout unless given. */
	*timeout = 0U;
	for (index = 1; index < argc; index++) {
		/* The token. */
		match = strncmp(argv[index], "--token=", 8);
		if (match == 0) {
			probe.token = argv[index] + 8;
			continue;
		}

		/* The timeout. */
		match = strncmp(argv[index], "--timeout-s=", 12);
		if (match == 0) {
			*timeout = (unsigned)strtoul(argv[index] + 12, NULL, 10);
			continue;
		}

		/* Anything else is refused. */
		return EINVAL;
	}

	/* Succeeded: the options are read. */
	return 0;
}
