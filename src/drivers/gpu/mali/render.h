/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The Mali render node: its sessions, their memory and address spaces,
 * and the stream every session's work runs on.
 *
 * Every session has an address space of its own, its translation tables
 * kept by the driver; the render node's group translates through one
 * slot, and a session's tables are bound to that slot while its work
 * runs.  Work runs one submission at a time under the device's lock: the
 * executor builds a command stream in the session's stage memory, the
 * node's ring calls it and adds one to the node's sync object, and the
 * submission returns once the sync object says the work ended.  The ring
 * and the sync object are mapped at the same addresses in every
 * session's space.
 */

#ifndef KERN_DRIVERS_GPU_MALI_RENDER_H
#define KERN_DRIVERS_GPU_MALI_RENDER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <drivers/gpu/gpu.h>
#include <kern/lock.h>

#include "drivers/gpu/mali/mali-private.h"

struct mali_vk_session;

/*
 * The fixed places of every session's space: the node's ring, its sync
 * page, and the part the session's own objects are placed in.
 */
#define MALI_RENDER_RING_ADDRESS	0x00100000ULL
#define MALI_RENDER_SYNC_ADDRESS	0x00200000ULL
#define MALI_RENDER_SPACE_START		0x01000000ULL
#define MALI_RENDER_SPACE_END		0x8000000000ULL

/* The alignment of a session's objects in its space. */
#define MALI_RENDER_SPACE_ALIGN		0x10000ULL

/* The most free runs a session's space keeps track of. */
#define MALI_RENDER_SPACE_RUNS		512U

/* The most resources of one session, and the largest one. */
#define MALI_RENDER_RESOURCES		4096U
#define MALI_RENDER_MAX_BYTES		(256ULL << 20)

/* The job markers a session reserves at once. */
#define MALI_RENDER_MARKERS		64U

/* The bytes of a stage chunk at least, and the most chunks one submission takes. */
#define MALI_RENDER_STAGE_BYTES		(1ULL << 20)
#define MALI_RENDER_STAGE_CHUNKS	16U

/* How long one submission may run before the node stops it (5 s). */
#define MALI_RENDER_SUBMIT_MS		5000U

/*
 * One buffer object: a run of memory that one or more resources map.
 *
 * references counts its holders (each resource, each export); the run
 * is freed with the last.  pages lists the run's page addresses for a
 * display's import, made on the first image export.
 */
struct mali_bo {
	struct mali_buffer buffer;
	uint32_t references;
	uint64_t *pages;
	uint64_t page_count;
};

/*
 * A session's handle on a buffer object, mapped at an address of the
 * session's space.  mappable says the client may map it; identifier is
 * the blob number the session named it by (zero for a plain resource).
 */
struct mali_resource {
	struct mali_resource *next;
	struct mali_session *session;
	struct mali_bo *bo;
	uint64_t gpu_address;
	uint32_t identifier;
	bool mappable;
};

/*
 * An export of a buffer object: it holds the object while the core
 * keeps the export, independently of the session that made it.
 */
struct mali_shared {
	struct mali_shared *next;
	struct mali_bo *bo;
	struct gpu_image_descriptor image;
	bool has_image;
};

/*
 * The free runs of a session's space, sorted by address and merged with
 * their neighbours.
 */
struct mali_space_runs {
	uint64_t start[MALI_RENDER_SPACE_RUNS];
	uint64_t bytes[MALI_RENDER_SPACE_RUNS];
	uint32_t count;
};

/* A job marker's state: free, reserved for a completion, or committed and not yet delivered. */
#define MALI_MARKER_FREE		0U
#define MALI_MARKER_RESERVED		1U

/* One job marker: its state and the completion it holds while reserved. */
struct mali_marker {
	unsigned state;
	struct drv_gpu_completion *completion;
};

/*
 * One open of the render node.
 *
 * space is the session's address space (bound to the node's slot while
 * its work runs), runs its free addresses, resources its handles.
 * The stage chunks are the memory a submission's command streams and
 * descriptors are written to: the first is kept, the others go when the
 * submission ends; stage_used is what the last chunk has handed out.  lost says a
 * submission of the session did not end cleanly: the session takes no
 * more work.  The fields are the session's ioctl's (the core admits one
 * at a time), except the markers and the flags, which the node's lock
 * guards.
 */
struct mali_session {
	struct mali_session *next;
	struct mali_render *render;
	struct mali_address_space space;
	struct mali_space_runs runs;
	struct mali_resource *resources;
	uint32_t resource_count;
	uint32_t next_identifier;
	struct mali_bo *stage[MALI_RENDER_STAGE_CHUNKS];
	uint64_t stage_address[MALI_RENDER_STAGE_CHUNKS];
	uint32_t stage_count;
	uint64_t stage_used;
	struct mali_vk_session *vk;
	struct mali_marker markers[MALI_RENDER_MARKERS];
	unsigned reserved;
	bool lost;
	bool isolated;
	bool stopping;
};

/*
 * The board's render node.
 *
 * queue is the node's stream (group slot 1, slot 2's space); idle is the
 * device's own tables bound to the slot when no session's are, holding
 * only the ring and the sync page.  bound is the session whose tables
 * the slot translates through now.  submitted is the last value the
 * sync object was told to reach.  guard protects the session list, the
 * exports and every buffer object's count; the device's lock serializes
 * the GPU's use.
 */
struct mali_render {
	struct mali_device *device;
	struct drv_gpu_ops ops;
	struct drv_gpu_device *node;
	struct mali_queue queue;
	struct mali_address_space idle;
	struct mali_bo *ring;
	struct mali_bo *sync;
	struct mali_session *bound;
	struct mali_session *sessions;
	struct mali_shared *exports;
	uint64_t submitted;
	struct spinlock guard;
	bool faulted;
	unsigned submissions;
	unsigned failures;
};

/* The buffer objects and resources of render.c. */
int mali_bo_create(struct mali_render *render, uint64_t bytes, struct mali_bo **result);
void mali_bo_retain(struct mali_render *render, struct mali_bo *bo);
void mali_bo_put(struct mali_render *render, struct mali_bo *bo);
int mali_resource_make(struct mali_session *session, struct mali_bo *bo, bool mappable, struct mali_resource **result);
void mali_resource_unmake(struct mali_session *session, struct mali_resource *resource);
struct mali_resource *mali_session_blob(struct mali_session *session, uint32_t identifier);

/* The session's stage memory of render.c: bytes for one submission, with their GPU address. */
int mali_stage_take(struct mali_session *session, uint64_t bytes, uint64_t align, volatile uint8_t **view, uint64_t *gpu_address);
void mali_stage_reset(struct mali_session *session);

/* The submissions and the recovery of sched.c. */
int mali_sched_start(struct mali_render *render);
int mali_sched_run(struct mali_session *session, uint64_t stream_address, uint32_t stream_bytes);
void mali_sched_forget(struct mali_render *render, struct mali_session *session);
int mali_sched_restart(struct mali_render *render);
void mali_sched_bind_ops(struct mali_render *render);

/* The executor of vk/ (vk-stream.c, vk-object.c). */
int mali_vk_open(struct mali_session *owner, struct mali_vk_session **result);
void mali_vk_close(struct mali_vk_session *session);
int mali_vk_execute(struct mali_vk_session *session, const void *wire, uint32_t bytes);
int mali_vk_memory_bo(struct mali_vk_session *session, uint64_t identity, uint64_t bytes, struct mali_bo **bo);
bool mali_vk_timeline_owned(const struct mali_vk_session *session, uint32_t timeline);
uint32_t mali_vk_failed_opcode(const struct mali_vk_session *session);

#endif
