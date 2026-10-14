/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The Vulkan executor of the nvrtx GPU device (ws037-p007 stage D2).
 *
 * libvulkan sends the Kei GPU command protocol (uapi/gpu-op.h) to a GPU
 * node; the executor decodes it in the kernel and does the work on the
 * GPU.  The wire's readers and writers and the generated record codec are
 * the i915 executor's, used unchanged (as the bcm2711 executor uses them);
 * everything else is the nvrtx driver's own.
 *
 * One executor session belongs to one open of the node.  It holds the
 * session's typed objects by their wire identity (an index of hashed
 * identities over a list), and the scratch one command decodes its records
 * into.  The node's lock serializes everything here.
 */

#ifndef DRIVERS_GPU_NVRTX_VK_H
#define DRIVERS_GPU_NVRTX_VK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "drivers/gpu/i915/render/codec.h"

/* Names a parameter a function does not use (as the driver's other files do). */
#ifndef UNUSED_PARAMETER
#define UNUSED_PARAMETER(parameter) ((void)(parameter))
#endif

/* The buckets of a session's identity index (a power of two). */
#define NVRTX_VK_OBJECT_BUCKETS		1024U

/* The most live objects one session holds. */
#define NVRTX_VK_OBJECT_LIMIT		4096U

/* The scratch one command decodes its records into. */
#define NVRTX_VK_ARENA_BYTES		(256U * 1024U)

struct nvrtx_vk_object;

/*
 * One executor session.
 *
 * owner is the node's session the executor reads blobs through.  objects
 * lists every published identity, newest first, and buckets indexes them
 * by hashed identity; object_count counts the published ones, live_objects
 * the ones whose payload has not been destroyed yet (an unpublished object
 * lives on while something still holds it).  closing is set once the
 * session's close has begun: nothing is published after it.  last_opcode
 * and failed_opcode name the commands the decoder last took and refused.
 */
struct nvrtx_vk_session {
	void *owner;
	struct nvrtx_vk_object *objects;
	struct nvrtx_vk_object *buckets[NVRTX_VK_OBJECT_BUCKETS];
	uint32_t object_count;
	uint32_t live_objects;
	bool closing;
	uint32_t last_opcode;
	uint32_t failed_opcode;
	struct i915_wire_arena arena;
	/* The timelines the session's queues complete jobs on (bit n: timeline n), set by vkGetDeviceQueue2. */
	uint64_t timelines;
};

/*
 * One published wire identity of a kind, and the payload it stands for.
 *
 * next and link chain it into the session's list (link is the pointer that
 * points at it, so it unlinks at once), bucket_next into its bucket.
 * references counts the holders: the registry while published, and every
 * dependent object or submitted work that retained it; the payload is
 * destroyed when the count reaches zero.
 */
struct nvrtx_vk_object {
	struct nvrtx_vk_object *next;
	struct nvrtx_vk_object **link;
	struct nvrtx_vk_object *bucket_next;
	struct nvrtx_vk_session *session;
	enum i915_vk_object_kind kind;
	uint64_t identity;
	uint32_t references;
	bool published;
	void *payload;
	int (*destroy)(struct nvrtx_vk_session *session, void *payload);
};

/* A command's handler: takes the command's body from reader and writes its reply; sets *handled when the opcode is its own. */
typedef int (*nvrtx_vk_route)(struct nvrtx_vk_session *session, uint32_t opcode, uint32_t requested, struct i915_wire_reader *reader, struct i915_wire_writer *reply, int *handled);

/* Runs one decoded command (the transport's opcodes excepted). */
typedef int (*nvrtx_vk_dispatcher)(struct nvrtx_vk_session *session, uint32_t opcode, uint32_t requested, struct i915_wire_reader *reader, struct i915_wire_writer *reply);

int nvrtx_vk_session_open(void *owner, struct nvrtx_vk_session **session);
int nvrtx_vk_session_close(struct nvrtx_vk_session **session);
void *nvrtx_vk_arena_take(struct nvrtx_vk_session *session, size_t bytes);

int nvrtx_vk_object_publish(struct nvrtx_vk_session *session, enum i915_vk_object_kind kind, uint64_t identity, void *payload, int (*destroy)(struct nvrtx_vk_session *, void *), struct nvrtx_vk_object **object);
struct nvrtx_vk_object *nvrtx_vk_object_find(struct nvrtx_vk_session *session, enum i915_vk_object_kind kind, uint64_t identity);
int nvrtx_vk_object_retain(struct nvrtx_vk_object *object);
int nvrtx_vk_object_release(struct nvrtx_vk_object *object);
int nvrtx_vk_object_remove(struct nvrtx_vk_session *session, enum i915_vk_object_kind kind, uint64_t identity);
int nvrtx_vk_objects_close(struct nvrtx_vk_session *session);

int nvrtx_vk_device_route(struct nvrtx_vk_session *session, uint32_t opcode, uint32_t requested, struct i915_wire_reader *reader, struct i915_wire_writer *reply, int *handled);

int nvrtx_vk_stream_execute(struct nvrtx_vk_session *session, const void *wire, uint32_t bytes, nvrtx_vk_dispatcher dispatch);
int nvrtx_vk_dispatch(struct nvrtx_vk_session *session, uint32_t opcode, uint32_t requested, struct i915_wire_reader *reader, struct i915_wire_writer *reply);

/*
 * Finds a blob of the owner's (the node's session) by its resource id and
 * gives its CPU view: the first byte and the length.  Provided by the
 * node (node.c); the transport reads command streams from blobs and writes
 * replies into them.
 */
int nvrtx_vk_blob(void *owner, uint32_t resource_id, uint8_t **cpu, uint64_t *bytes);

/* The owner's GPU: its PCI product, and the bytes of VRAM the CPU's window gives the executor's memory. */
uint32_t nvrtx_vk_device_id(void *owner);
uint64_t nvrtx_vk_heap_bytes(void *owner);

#endif
