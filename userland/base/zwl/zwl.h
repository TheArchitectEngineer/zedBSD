/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Shared state for the minimal full-screen Wayland compositor.
 *
 * zwl shows one full-screen surface at a time.  The WS014/WS029 scope had
 * no input; WS031 p013 extends it with one seat ("seat0", wl_seat v5):
 *
 * - Every /dev/input/eventN reporting REL_X+REL_Y or ABS_X+ABS_Y is a
 *   pointer and every one reporting KEY_A and KEY_Z is a keyboard.  Nodes are
 *   scanned at start-up and again every ZWL_INPUT_SCAN_MS; a node that fails
 *   a read is closed.  Capabilities follow the open nodes.
 * - Focus is the surface currently on the display; its client's pointer and
 *   keyboard objects get enter when it is shown and leave when it is replaced,
 *   unmapped or destroyed.  Clients without seat objects get nothing.
 * - Pointer positions are integer surface pixels (0..width-1, 0..height-1) sent
 *   as wl_fixed.  An absolute device's range is mapped linearly onto the
 *   surface; relative motion is added and clamped; the start is the centre.
 * - Buttons are Linux BTN_* codes.  A wheel notch is axis value 15.0 (negative
 *   is up/left) with axis_source wheel and axis_discrete +-1 for v5 pointers.
 *   Each evdev report ends with wl_pointer.frame for v5 pointers.
 * - The keyboard sends keymap format no_keymap with a /dev/null descriptor of
 *   size 0, evdev key codes, depressed modifiers (shift 0x1, ctrl 0x4,
 *   alt 0x8, meta 0x40), and repeat_info rate 0 (no client repeat).
 *   Kernel autorepeat events are not forwarded.
 * - wl_pointer.set_cursor is accepted and ignored; nothing draws a cursor and
 *   a cursor surface cannot be committed.  wl_touch is not offered.
 */
#ifndef ZWL_H
#define ZWL_H

#include <uapi/gpu.h>
#include <uapi/gpu-display.h>
#include <uapi/input.h>
#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>

/* Bound each connection's wire, descriptor, object and queued-event storage. */
#define ZWL_WIRE_MAX		65532U
#define ZWL_RIGHTS_MAX		32U
#define ZWL_OBJECT_MAX		4096U
#define ZWL_OUTPUT_MAX		1048576U

/* Bound the evdev nodes the seat reads and the events one report may carry. */
#define ZWL_INPUT_MAX		16U
#define ZWL_INPUT_FRAME_MAX	64U
#define ZWL_INPUT_PATH_MAX	64U

/* Rescan period for evdev nodes that appear after start-up, in milliseconds. */
#define ZWL_INPUT_SCAN_MS	2000U

struct zwl_server;
struct zwl_client;
struct zwl_object;

/* Each live protocol identity has one immutable interface and negotiated version. */
enum zwl_kind {
	ZWL_DISPLAY,
	ZWL_REGISTRY,
	ZWL_COMPOSITOR,
	ZWL_SURFACE,
	ZWL_REGION,
	ZWL_CALLBACK,
	ZWL_BUFFER,
	ZWL_OUTPUT,
	ZWL_WM,
	ZWL_XDG_SURFACE,
	ZWL_TOPLEVEL,
	ZWL_FACTORY,
	ZWL_SEAT,
	ZWL_POINTER,
	ZWL_KEYBOARD,
};

/*
 * The output queue retains unsent bytes through short writes and EAGAIN.
 *
 * A packet may own one descriptor, which travels as SCM_RIGHTS with the
 * packet's first byte and is closed once that byte has been sent; -1 means
 * the event carries no descriptor.
 */
struct zwl_packet {
	struct zwl_packet *next;
	size_t size;
	size_t sent;
	int descriptor;
	unsigned char bytes[1];
};

/*
 * One open evdev node the seat reads.
 *
 * A slot is in use while live is set; the server's fixed table keeps a slot's
 * address stable while the event loop holds it in a poll snapshot.  Events
 * are gathered into frame[] until SYN_REPORT, then applied together.
 */
struct zwl_input_device {
	int fd;
	unsigned live;
	unsigned pointer;
	unsigned keyboard;
	unsigned absolute;
	unsigned discarding;
	int32_t abs_x_minimum;
	int32_t abs_x_maximum;
	int32_t abs_y_minimum;
	int32_t abs_y_maximum;
	int32_t abs_x;
	int32_t abs_y;
	unsigned frame_count;
	struct input_event frame[ZWL_INPUT_FRAME_MAX];
	char path[ZWL_INPUT_PATH_MAX];
};

/*
 * One client-owned protocol object; destroyed buffers remain until all pending,
 * current and scanout holds are gone. Surface state is double-buffered.
 */
struct zwl_object {
	struct zwl_object *next;
	struct zwl_client *client;
	uint32_t id;
	enum zwl_kind kind;
	uint32_t version;
	unsigned dead;
	unsigned holds;
	unsigned busy;
	struct gpu_resource_import image;
	struct zwl_object *surface;
	struct zwl_object *role;
	struct zwl_object *top;
	struct zwl_object *pending;
	struct zwl_object *queued;
	struct zwl_object *current;
	struct zwl_object *callbacks;
	struct zwl_object *committed_callbacks;
	struct zwl_object *callback_next;
	unsigned attached;
	unsigned ready;
	unsigned configured;
	unsigned acknowledged;
	uint32_t configure_serial;
	uint64_t commit_order;
};

/* One stream has independent byte and fd FIFOs, plus its own protocol namespace. */
struct zwl_client {
	struct zwl_client *next;
	struct zwl_server *server;
	int fd;
	uint64_t number;
	unsigned fatal;
	uint64_t fatal_time;
	struct zwl_object *objects;
	unsigned object_count;
	unsigned char input[ZWL_WIRE_MAX];
	size_t input_size;
	int rights[ZWL_RIGHTS_MAX];
	unsigned right_count;
	struct zwl_packet *output_head;
	struct zwl_packet *output_tail;
	size_t output_bytes;
};

/* The compositor alone owns the GPU context and the currently scanned-out image. */
struct zwl_server {
	int listener;
	int gpu;
	const char *gpu_path;
	char socket_path[108];
	dev_t socket_device;
	ino_t socket_inode;
	unsigned socket_owned;
	struct zwl_client *clients;
	struct zwl_object *front;
	struct zwl_object *front_surface;
	struct gpu_display_info display;
	uint64_t lease;
	uint64_t frame;
	uint64_t commit_order;
	uint64_t client_serial;
	uint32_t serial;
	uint32_t width;
	uint32_t height;
	uint32_t refresh;
	uint64_t timeout_ms;
	uint64_t max_frames;
	unsigned failed;
	struct zwl_input_device inputs[ZWL_INPUT_MAX];
	uint64_t input_scan_time;
	uint64_t input_events;
	uint64_t seat_events;
	unsigned capabilities;
	struct zwl_object *focus;
	int32_t pointer_x;
	int32_t pointer_y;
	unsigned modifier_keys;
	uint32_t modifiers;
};

uint64_t zwl_milliseconds(void);
int zwl_emit(struct zwl_client *client, uint32_t object, uint32_t opcode, const void *payload, size_t size);
int zwl_emit_fd(struct zwl_client *client, uint32_t object, uint32_t opcode, const void *payload, size_t size, int descriptor);
void zwl_packet_free(struct zwl_packet *packet);
int zwl_flush(struct zwl_client *client);
int zwl_read(struct zwl_client *client);
int zwl_dispatch(struct zwl_client *client, uint32_t id, uint32_t opcode, const unsigned char *payload, size_t size);
int zwl_error(struct zwl_client *client, uint32_t object, const char *reason);
int zwl_take_fd(struct zwl_client *client);
void zwl_delete_id(struct zwl_client *client, uint32_t id);
void zwl_client_destroy(struct zwl_client *client);
struct zwl_object *zwl_find(struct zwl_client *client, uint32_t id);
struct zwl_object *zwl_create(struct zwl_client *client, uint32_t id, enum zwl_kind kind, uint32_t version);
void zwl_object_destroy(struct zwl_object *object);
void zwl_buffer_get(struct zwl_object *buffer);
void zwl_buffer_put(struct zwl_object *buffer);
void zwl_callbacks_done(struct zwl_object **callbacks);
int zwl_gpu_open(struct zwl_server *server);
int zwl_gpu_import(struct zwl_object *buffer, int descriptor, const struct gpu_image_descriptor *image);
int zwl_present(struct zwl_object *surface);
int zwl_unscan(struct zwl_server *server);
void zwl_schedule(struct zwl_server *server);
uint32_t zwl_next_serial(struct zwl_server *server);
int zwl_seat_bind(struct zwl_object *seat);
int zwl_seat_request(struct zwl_object *object, uint32_t opcode, const unsigned char *bytes, size_t size);
void zwl_seat_focus(struct zwl_server *server);
void zwl_seat_surface_gone(struct zwl_object *surface);
void zwl_seat_capabilities(struct zwl_server *server);
void zwl_seat_motion(struct zwl_server *server, uint32_t time);
void zwl_seat_button(struct zwl_server *server, uint32_t time, uint32_t button, uint32_t state);
void zwl_seat_axis(struct zwl_server *server, uint32_t time, int32_t vertical, int32_t horizontal);
void zwl_seat_frame(struct zwl_server *server);
void zwl_seat_key(struct zwl_server *server, uint32_t time, uint32_t key, uint32_t state);
void zwl_seat_modifiers(struct zwl_server *server);
void zwl_input_scan(struct zwl_server *server);
int zwl_input_attach(struct zwl_server *server, int descriptor, const char *path, unsigned pointer, unsigned keyboard, const struct input_absinfo *x, const struct input_absinfo *y);
void zwl_input_read(struct zwl_server *server, struct zwl_input_device *device);
void zwl_input_close(struct zwl_server *server, struct zwl_input_device *device);
void zwl_input_cleanup(struct zwl_server *server);

#endif
