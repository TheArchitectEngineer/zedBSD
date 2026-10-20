/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The PowerVR render node's own state, shared by its operations, its
 * queue and its recovery.
 */

#ifndef KERN_DRIVERS_GPU_POWERVR_RENDER_PRIVATE_H
#define KERN_DRIVERS_GPU_POWERVR_RENDER_PRIVATE_H

#include <stdint.h>

#include <drivers/gpu/gpu.h>
#include <kern/lock.h>
#include <kern/pmem.h>
#include <kern/waitq.h>

#include "drivers/gpu/powervr/powervr-private.h"
#include "drivers/gpu/powervr/fixed-area.h"
#include "drivers/gpu/powervr/gpu-mmu.h"
#include "drivers/gpu/powervr/render.h"

/*
 * The most a session holds, the largest allocation, and how many native
 * commands one session may have in flight.
 */
#define POWERVR_RENDER_RESOURCES	256U
#define POWERVR_RENDER_MAX_BYTES	0x10000000ULL
#define POWERVR_RENDER_PENDING		64U

/*
 * One allocation: a physically contiguous run below the core's DMA limit
 * and its uncached kernel view, the only way the host touches it.
 *
 * References are counted under the render node's buffer guard: each
 * session resource, each export and each display-node lookup holds one.
 * Pages is the run's page list, made by the first image export and kept
 * with the buffer.  Next links a buffer kept after a fault.
 */
struct powervr_buffer {
	struct powervr_buffer *next;
	unsigned references;
	struct kern_pmem run;
	volatile uint8_t *view;
	uint64_t bytes;
	uint64_t *pages;
	uint64_t page_count;
};

/*
 * One session's handle on a buffer, mapped at a device address of the
 * session's memory context.  A blob or an import also has the identifier
 * the renderer names it by, and is listed in its session's blobs through
 * next_blob.
 */
struct powervr_resource {
	struct powervr_session *session;
	struct powervr_buffer *buffer;
	uint64_t address;
	int mappable;
	uint32_t identifier;
	struct powervr_resource *next_blob;
};

/*
 * One export: the buffer it keeps alive, and the image it was exported
 * as, if any.  Live exports are listed under the render node's buffer
 * guard, so a lookup can find them.
 */
struct powervr_shared {
	struct powervr_shared *next;
	struct powervr_buffer *buffer;
	struct gpu_image_descriptor image;
	int has_image;
};

/*
 * One native command in flight: the timeline value that retires it, and
 * whom to tell with which error.  Marker is the job marker slot it frees
 * when it is delivered, or POWERVR_NO_MARKER.
 */
struct powervr_pending {
	uint32_t value;
	uint64_t submitted;
	struct drv_gpu_completion *completion;
	int error;
	unsigned marker;
};

/* No job marker. */
#define POWERVR_NO_MARKER	0xffffffffU

/*
 * One job marker of a session: a reservation libvulkan makes before it
 * submits a supervised job.  Free, reserved (held by the core, counted
 * against the ring), or committed (in the ring until delivered); the
 * completion is the one the reservation was made with.
 */
#define POWERVR_MARKER_FREE		0U
#define POWERVR_MARKER_RESERVED		1U
#define POWERVR_MARKER_COMMITTED	2U

struct powervr_marker {
	unsigned state;
	struct drv_gpu_completion *completion;
};

struct powervr_vk_session;

/*
 * One open of the render node.
 *
 * Mmu and space are the session's memory context, and fixed holds the
 * pages backing the context's fixed area at the start of the general,
 * data sequencer and shading cluster heaps (fixed-area.h), mapped while
 * the tables exist and returned with them; space hands out the general
 * heap after that area.  The firmware records
 * (its memory context, its compute context and state, the context's
 * command buffer and control, and the timeline word the firmware
 * advances) are blocks of the firmware's heap, changed under the core's
 * lock.  Client_write is the host's write offset into the context's
 * buffer.  The pending ring, its count of callbacks not yet delivered,
 * the job markers and their reserved count, and the stop flags are under
 * the render node's queue lock.  Blobs lists the session's blobs and
 * imports by identifier, and vk is the Vulkan executor's session (made
 * by the first command frame); both change only in the session's own
 * ioctls, which the GPU core serializes.
 */
struct powervr_session {
	struct powervr_session *next;
	struct powervr_render *render;
	struct powervr_gpu_mmu mmu;
	struct powervr_gpu_space space;
	struct powervr_buffer *fixed[POWERVR_FIXED_HEAPS];
	struct powervr_object memory_context;
	struct powervr_object context;
	struct powervr_object context_state;
	struct powervr_object client_control;
	struct powervr_object client;
	struct powervr_object timeline;
	uint32_t identity;
	uint32_t client_write;
	uint32_t submitted;
	uint32_t next_resource;
	unsigned resources;
	struct powervr_pending pending[POWERVR_RENDER_PENDING];
	unsigned pending_head;
	unsigned pending_count;
	unsigned outstanding;
	unsigned kicked;
	unsigned cleaned;
	unsigned stopping;
	unsigned stop_sent;
	uint32_t stop_slot;
	uint64_t stop_since;
	unsigned stop_tries;
	unsigned lost;
	unsigned isolated;
	unsigned retained;
	struct powervr_resource *blobs;
	struct powervr_marker markers[POWERVR_RENDER_PENDING];
	unsigned reserved;
	struct powervr_vk_session *vk;
};

/*
 * The render node.
 *
 * Sessions are listed under the core's lock.  Queue_lock guards every
 * session's pending ring and stop flags and the faulted flag, and is
 * taken from the interrupt handler, so it is held only briefly with
 * interrupts off; service wakes the service thread and retired wakes a
 * drain.  Buffer_guard guards buffer references and the export list.
 * Kept holds buffers and sessions that may still be reached by the core
 * after a fault or an isolation, until a reset frees them.  Lost, under
 * the core's lock, says a restart failed and the firmware is gone.
 * Table_memory is where sessions' page tables come from.  Running and
 * service_done, under the queue lock, tell the service thread to end and
 * say it has.
 */
struct powervr_render {
	struct powervr_core *core;
	struct drv_gpu_device *device;
	struct drv_gpu_ops ops;
	struct powervr_gpu_memory table_memory;
	struct spinlock queue_lock;
	struct wait_queue service;
	struct wait_queue retired;
	struct spinlock buffer_guard;
	struct powervr_shared *exports;
	struct powervr_session *sessions;
	struct powervr_buffer *kept_buffers;
	struct powervr_session *kept_sessions;
	unsigned faulted;
	unsigned lost;
	unsigned running;
	unsigned service_done;
	uint32_t next_identity;
};

/* Makes a zeroed buffer of at least bytes bytes. */
int powervr_buffer_create(struct powervr_render *render, uint64_t bytes, struct powervr_buffer **result);

/* Takes another reference on a buffer. */
void powervr_buffer_retain(struct powervr_render *render, struct powervr_buffer *buffer);

/* Drops a reference; the last one frees the buffer, or keeps it after a fault. */
void powervr_buffer_put(struct powervr_render *render, struct powervr_buffer *buffer);

/* Frees a list of buffers kept for a reset, linked through next. */
void powervr_buffers_free(struct powervr_buffer *list);

/* Makes a session's handle on a buffer, mapped at a device address of its own; the handle takes a reference. */
int powervr_resource_make(struct powervr_session *session, struct powervr_buffer *buffer, int mappable, int shareable, struct powervr_resource **result);

/* Undoes a handle: unmaps it and drops its buffer, unless the core may still reach the session. */
void powervr_resource_unmake(struct powervr_session *session, struct powervr_resource *resource);

/* Finds a blob or an import of a session by the identifier the renderer names it by, or NULL. */
struct powervr_resource *powervr_session_blob(struct powervr_session *session, uint32_t identifier);

/*
 * Puts a callback in a session's ring, retired once the timeline reaches
 * value, with an error and the job marker it frees; the caller checked
 * the ring has room.
 */
void powervr_session_pend(struct powervr_render *render, struct powervr_session *session, uint32_t value, struct drv_gpu_completion *completion, int error, unsigned marker);

/*
 * Takes one command frame of the Vulkan transport (or an empty marker
 * frame): runs it at once and puts its callback in the ring after the
 * session's work.
 */
int powervr_render_frame(struct powervr_render *render, struct powervr_session *session, const void *command, uint32_t bytes, uint32_t flags, uint32_t timeline, struct drv_gpu_completion *completion);

/* Binds the Vulkan transport, the capset and the supervised jobs to the node's table. */
void powervr_render_runtime_bind(struct powervr_render *render);

/*
 * Makes the pages of a session's fixed area with its programs written;
 * the core's lock is not held.
 */
int powervr_session_fixed_make(struct powervr_render *render, struct powervr_session *session);

/* Returns the pages of a session's fixed area that nothing maps; the core's lock is not held. */
void powervr_session_fixed_drop(struct powervr_render *render, struct powervr_session *session);

/*
 * Makes a session's memory context, with the fixed area's pages mapped,
 * and its firmware records; the core's lock is held.
 */
int powervr_session_firmware_create(struct powervr_render *render, struct powervr_session *session);

/*
 * Returns a session's firmware records, tables and fixed area's pages;
 * the firmware no longer uses them.
 */
void powervr_session_firmware_destroy(struct powervr_render *render, struct powervr_session *session);

/*
 * Returns a kept session's records and tables once the firmware that knew
 * them is gone, without touching their old places in its heap; the fixed
 * area's pages join the node's kept buffers, which the caller frees next.
 */
void powervr_session_firmware_discard(struct powervr_session *session);

/*
 * Has the firmware clean up a session's context and waits for it,
 * bounded; EBUSY when the context stays busy.
 */
int powervr_session_cleanup(struct powervr_render *render, struct powervr_session *session);

/*
 * Kicks one empty job on a session's context that advances its timeline
 * to value once done, after an optional wait for a sync word; the core's
 * lock is held.
 */
int powervr_session_kick(struct powervr_render *render, struct powervr_session *session, uint32_t value, uint32_t fence_address, uint32_t fence_value);

/*
 * Kicks one compute command of words 32-bit words on a session's context
 * that advances its timeline to value once done; the core's lock is held.
 */
int powervr_session_kick_compute(struct powervr_render *render, struct powervr_session *session, uint32_t value, const uint32_t *command, uint32_t words);

/* Waits, bounded, for a session's timeline to reach a value; ETIMEDOUT when it does not. */
int powervr_session_wait(struct powervr_session *session, uint32_t value, unsigned milliseconds);

/*
 * Runs the driver's own compute work on the core and reports, one line
 * each, whether it wrote what it should, what the processor's cached
 * view sees of it, and how the task issue counts temporaries.
 * Weak: only a test kernel built with CONFIG_POWERVR_COMPUTE_CHECK has
 * it, so the ordinary start does not spend the time.
 */
extern void powervr_compute_check(struct powervr_render *render) __attribute__((weak));

/*
 * Kicks a session's context again without new commands, so the firmware
 * looks again at a fence it waits on; the core's lock is held.
 */
int powervr_session_rekick(struct powervr_render *render, struct powervr_session *session);

/* Reads a session's timeline word as the firmware left it. */
uint32_t powervr_session_timeline(const struct powervr_session *session);

/* Starts the thread that retires native commands and reads the firmware's notices. */
int powervr_service_start(struct powervr_render *render);

/* Ends the service thread and waits until it has. */
void powervr_service_stop(struct powervr_render *render);

/*
 * Ends every pending callback of a session with an error; the work may
 * still be running, nothing is said about that.
 */
void powervr_session_fail_pending(struct powervr_render *render, struct powervr_session *session, int error);

/*
 * Arms the device-wide quarantine: no new kick, every destroy and close
 * keeps what the core may reach.  Idempotent.
 */
void powervr_render_quarantine(struct powervr_render *render, int error);

/* The queue and recovery operations the node's table points at. */
int powervr_render_submit(void *device, void *session, const void *command, uint32_t bytes, uint32_t flags, uint32_t timeline, struct drv_gpu_completion *completion);
void powervr_render_drain(void *device, void *session);
int powervr_render_stop_begin(void *device, void *session, int error);
int powervr_render_stop_poll(void *device, void *session);
void powervr_render_fault(void *device, int error);
int powervr_render_reset(void *device);
int powervr_render_isolate(void *device, void *session);

/*
 * The native command the queue takes before the Venus stream exists: one
 * empty job, retired through the timeline like any other.
 */
#define POWERVR_NATIVE_EMPTY_JOB	0x4e4a4250U
#define POWERVR_NATIVE_BYTES		8U

#endif
