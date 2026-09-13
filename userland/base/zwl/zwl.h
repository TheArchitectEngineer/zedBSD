/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Shared state for the minimal full-screen Wayland compositor.
 */
#ifndef ZWL_H
#define ZWL_H

#include <uapi/gpu.h>
#include <uapi/gpu-display.h>
#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>

/* Bound each connection's wire, descriptor, object and queued-event storage. */
#define ZWL_WIRE_MAX		65532U
#define ZWL_RIGHTS_MAX		32U
#define ZWL_OBJECT_MAX		4096U
#define ZWL_OUTPUT_MAX		1048576U

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
};

/* The output queue retains unsent bytes through short writes and EAGAIN. */
struct zwl_packet {
	struct zwl_packet *next;
	size_t size;
	size_t sent;
	unsigned char bytes[1];
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
};

uint64_t zwl_milliseconds(void);
int zwl_emit(struct zwl_client *client, uint32_t object, uint32_t opcode, const void *payload, size_t size);
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

#endif
