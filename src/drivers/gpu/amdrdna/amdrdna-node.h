/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The GPU device of the amdrdna driver (stage A5s on).
 *
 * Once the command processor runs, the GPU is registered with the GPU core
 * as a device whose sessions each have an address space of their own:
 * ordinary storage in RAM, blobs in the VRAM the CPU reaches through its
 * window, transfers and CPU mappings of both, and the sharing of blobs
 * between the device's opens.  node.c holds the sessions, the resources
 * and the sharing; runtime.c the queue of command frames and supervised
 * jobs, the stop of a session and the device's fault; vk-node.c what the
 * Vulkan executor reaches the node through (its memory in the session's
 * address space and the runs of its indirect buffers); present.c the
 * display taken over from the firmware, when the boot asks for it (stage
 * A8).  The device is a renderer libvulkan opens only once an executor is
 * bound.
 */

#ifndef DRIVERS_GPU_AMDRDNA_AMDRDNA_NODE_H
#define DRIVERS_GPU_AMDRDNA_AMDRDNA_NODE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <drivers/gpu/gpu.h>
#include <kern/lock.h>
#include <kern/pmem.h>
#include <kern/waitq.h>

#include "drivers/gpu/amdrdna/amdrdna-private.h"

/* The markers of supervised jobs the renderer holds at once. */
#define AMDRDNA_NODE_MARKERS		64U

/* Where a queue entry is: free (a marker), reserved, queued, being run. */
#define AMDRDNA_ENTRY_FREE		0U
#define AMDRDNA_ENTRY_RESERVED		1U
#define AMDRDNA_ENTRY_QUEUED		2U
#define AMDRDNA_ENTRY_RUNNING		3U

/* The two kinds of resource: ordinary storage in RAM, a blob in the VRAM. */
#define AMDRDNA_RESOURCE_STORAGE	1U
#define AMDRDNA_RESOURCE_BLOB		2U

struct amdrdna_node;

/*
 * One resource of a session.
 *
 * Storage is a contiguous run of RAM the CPU reads and writes directly.  A
 * blob holds a reference to a backing in the VRAM; an import is a blob
 * naming another open's backing.  A blob has a protocol number and a
 * place in its session's list of blobs (session NULL: not listed).
 */
struct amdrdna_resource {
	uint32_t kind;
	uint64_t bytes;
	struct kern_pmem ram;
	uint8_t *cpu;
	struct amdrdna_backing *backing;
	uint32_t resource_id;
	struct amdrdna_session *session;
	struct amdrdna_resource *next_blob;
};

/*
 * One open of the GPU device, or one of the driver's own checks.
 *
 * space is its GPU address space, blobs lists its blobs by protocol
 * number, and vk is its executor session once an executor is bound.
 * pending counts its queued frames and its reserved or queued markers and
 * stopping refuses its new work (both under the node's queue_lock);
 * isolated says the core isolated it: what it held stays the device's.
 */
struct amdrdna_session {
	struct amdrdna_node *node;
	struct amdrdna_space *space;
	struct amdrdna_resource *blobs;
	void *vk;
	uint32_t pending;
	int stopping;
	int isolated;
};

/*
 * One entry of the renderer's queue: a frame (a copy of its wire, freed
 * once run) or a marker of a supervised job (one of the node's, reused).
 * error is the result a marker completes with.
 */
struct amdrdna_node_entry {
	struct amdrdna_node_entry *next;
	uint32_t kind;
	uint32_t state;
	struct amdrdna_session *session;
	struct drv_gpu_completion *completion;
	int error;
	uint32_t bytes;
	uint8_t *wire;
};

/* Runs one command frame of a session through the bound executor. */
typedef int (*amdrdna_node_execute)(struct amdrdna_session *session, const void *wire, uint32_t bytes);

/* Reports the timelines (bit n: timeline n) the queues of a session's executor complete jobs on. */
typedef uint64_t (*amdrdna_node_timelines)(struct amdrdna_session *session);

/*
 * The registered GPU device.
 *
 * Made once by stage A5s and kept until power-off.  gpu is the core's
 * handle once registered (NULL before).  lock serializes the sessions'
 * blob lists, the protocol numbers and everything the executor does; it
 * is taken before an address space's lock.  execute and timelines are
 * the executor's frame runner and its record of its queues' timelines
 * once one is bound (renderer set), NULL before.
 *
 * The renderer (runtime.c): queue_lock (IRQ-safe) guards the queue from
 * head to tail, the markers, every session's pending count and stopping,
 * and faulted; available wakes the worker, retired the drains.  faulted
 * is set by the core's fault or the stop: nothing new is taken and what
 * is queued completes with EIO.
 *
 * The counts the executor adds with relaxed atomics: transfers the CPU
 * did through the window, draws and quads, command streams read.
 *
 * The display (present.c, stage A8 on, when display is set): the lease of
 * one open at a time and the frames it presents.  present_lock puts the
 * presentations, the lease's end and a blob's going off the screen in one
 * order; display_lock (taken after present_lock, never with lock) guards
 * the lease (owner, lease, next_lease, sequence, shown and its time), the
 * frame the next copied presentation goes to, the cursor's state, the
 * power state, and every write to the display's registers.  broken says a
 * flip did not begin (nothing more is flipped); stopped that the machine
 * is going down (nothing more touches the display).  The counts are
 * presentations and leases.
 */
struct amdrdna_node {
	struct amdrdna_device *device;
	struct drv_gpu_device *gpu;
	struct drv_gpu_ops ops;
	struct mutex lock;
	uint32_t next_resource_id;
	amdrdna_node_execute execute;
	amdrdna_node_timelines timelines;
	int renderer;
	struct spinlock queue_lock;
	struct wait_queue available;
	struct wait_queue retired;
	struct amdrdna_node_entry *head;
	struct amdrdna_node_entry *tail;
	struct amdrdna_node_entry markers[AMDRDNA_NODE_MARKERS];
	int faulted;
	uint64_t copies;
	uint64_t copy_bytes;
	uint64_t draws;
	uint64_t quads;
	uint64_t streams;
	uint64_t stream_bytes;
	int display;
	struct mutex present_lock;
	struct mutex display_lock;
	void *owner;
	uint64_t lease;
	uint64_t next_lease;
	uint64_t sequence;
	uint64_t shown_time_ns;
	struct amdrdna_resource *shown;
	unsigned next_frame;
	int broken;
	int stopped;
	int powered_off;
	int cursor_has_image;
	int cursor_visible;
	int32_t cursor_x;
	int32_t cursor_y;
	uint64_t presents;
	uint64_t leases;
};

/* node.c */
int amdrdna_node_create(struct amdrdna_device *device);
int amdrdna_node_register(struct amdrdna_device *device);
int amdrdna_node_session_open(struct amdrdna_node *node, struct amdrdna_session **result);
void amdrdna_node_session_close(struct amdrdna_node *node, struct amdrdna_session *session);
uint32_t amdrdna_node_resource_id(struct amdrdna_node *node);
void amdrdna_node_blob_listed(struct amdrdna_node *node, struct amdrdna_session *session, struct amdrdna_resource *resource, uint32_t resource_id);

/* present.c: the display's operations (stage A8 on). */
void amdrdna_present_session_close(struct amdrdna_node *node, void *session);
void amdrdna_present_resource_gone(struct amdrdna_node *node, struct amdrdna_resource *resource);
void amdrdna_present_stop(struct amdrdna_node *node);

/* runtime.c */
void amdrdna_runtime_prepare(struct amdrdna_node *node);
void amdrdna_runtime_bind(struct amdrdna_node *node, amdrdna_node_execute execute, amdrdna_node_timelines timelines);
void amdrdna_runtime_start(struct amdrdna_node *node);
void amdrdna_runtime_stop(struct amdrdna_node *node);

/* vk-node.c: the executor's binding to the node (with amdrdna.vk=1). */
void amdrdna_vk_node_bind(struct amdrdna_node *node);
int amdrdna_vk_node_open(struct amdrdna_session *session);
int amdrdna_vk_node_close(struct amdrdna_session *session);
int amdrdna_vk_node_blob_attach(struct amdrdna_session *session, uint64_t blob_id, struct amdrdna_backing *backing, uint64_t bytes);

/*
 * vk-node.c: what the Vulkan executor reaches the node through.  owner is
 * the node's session; the executor holds the node's lock around every
 * call.  The executor's own header declares the same functions.
 */
int amdrdna_vk_blob(void *owner, uint32_t resource_id, uint8_t **cpu, uint64_t *bytes);
uint32_t amdrdna_vk_device_id(void *owner);
uint64_t amdrdna_vk_heap_bytes(void *owner, bool cpu);
void amdrdna_vk_memory_detach(void *owner, void *backing, uint64_t va, uint64_t bytes);
int amdrdna_vk_memory_import(void *owner, uint32_t resource_id, uint64_t bytes, uint8_t **cpu, uint64_t *va, void **backing);
int amdrdna_vk_memory_create(void *owner, uint64_t bytes, uint64_t *va, void **backing);
int amdrdna_vk_gpu_alloc(void *owner, uint64_t bytes, void **token, uint8_t **cpu, uint64_t *va);
void amdrdna_vk_gpu_free(void *owner, void *token);
int amdrdna_vk_gpu_run(void *owner, uint64_t va, uint32_t bytes);
void amdrdna_vk_gpu_copied(void *owner, uint64_t bytes);
void amdrdna_vk_gpu_drawn(void *owner, uint32_t draws, uint32_t quads);
void amdrdna_vk_blob_read(void *owner, uint8_t *to, const uint8_t *from, size_t length);

#endif
