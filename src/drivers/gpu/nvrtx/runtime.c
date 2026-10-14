/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The GPU device's Vulkan transport (ws037-p007 stage D2, nvrtx.vk=1).
 *
 * With the executor bound, the node is a renderer libvulkan opens: it
 * reports the protocol's capset, runs command frames (synchronously, or
 * queued with a completion), and takes supervised jobs whose completion
 * markers follow the frames queued before them.  One worker thread runs
 * the queue in order: a frame through the executor under the node's lock,
 * a marker by completing it.  A fault stops new work: frames and markers
 * still queued complete with EIO.
 *
 * queue_lock (a spinlock) guards the queue, the markers, every session's
 * pending count and the fault flag; the node's lock (a mutex) is taken
 * only outside it, around the executor.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <drivers/gpu/gpu.h>
#include <kern/kcrt.h>
#include <kern/kmem.h>
#include <kern/sched.h>
#include <kern/thread.h>
#include <kern/waitq.h>
#include <uapi/errno.h>
#include <uapi/gpu.h>
#include <uapi/gpu-job.h>

#include "drivers/gpu/nvrtx/nvrtx-node.h"
#include "drivers/gpu/nvrtx/nvrtx-vk.h"

/* The protocol's capset: its id, its bytes, the XML version (1.3.269), and the vendor suffix ("SDBZ") with its flags. */
#define RUNTIME_CAPSET_ID		4U
#define RUNTIME_CAPSET_BYTES		168U
#define RUNTIME_CAPSET_WIRE_AT		0U
#define RUNTIME_CAPSET_XML_AT		4U
#define RUNTIME_CAPSET_TIMELINES_AT	152U
#define RUNTIME_CAPSET_MAGIC_AT		160U
#define RUNTIME_CAPSET_FLAGS_AT		164U
#define RUNTIME_WIRE_VERSION		1U
#define RUNTIME_XML_VERSION		((1U << 22) | (3U << 12) | 269U)
#define RUNTIME_VENDOR_MAGIC		0x5a424453U

/* The flags: opaque sharing, strict queues, checked quiescence (the profile libvulkan pairs with). */
#define RUNTIME_VENDOR_FLAGS		7U

/* The timelines a queue may name (1 to 63; 0 is the context's own). */
#define RUNTIME_TIMELINES		64U

/* What a queue entry is. */
#define RUNTIME_ENTRY_FRAME		1U
#define RUNTIME_ENTRY_MARKER		2U

static int runtime_capset(void *private_data, void *session, struct gpu_capset *capset);
static int runtime_command(void *private_data, void *session, const void *wire, uint32_t bytes);
static int runtime_submit(void *private_data, void *session, const void *wire, uint32_t bytes, uint32_t flags, uint32_t timeline, struct drv_gpu_completion *completion);
static void runtime_drain(void *private_data, void *session);
static int runtime_reserve(void *private_data, void *session, uint32_t timeline, struct drv_gpu_completion *completion, void **token);
static int runtime_commit(void *private_data, void *session, void *token, struct drv_gpu_completion *completion);
static int runtime_cancel(void *private_data, void *session, void *token, struct drv_gpu_completion *completion, unsigned fault);
static int runtime_capacity(void *private_data, void *session, uint32_t timeline, unsigned *available);
static void runtime_fault(void *private_data, int error);
static void runtime_worker(void *argument);
static void runtime_enqueue(struct nvrtx_node *node, struct nvrtx_node_entry *entry);
static struct nvrtx_node_entry *runtime_marker(struct nvrtx_node *node, struct nvrtx_session *session, void *token, struct drv_gpu_completion *completion);
static unsigned runtime_free_markers(const struct nvrtx_node *node);
static bool runtime_timeline_owned(struct nvrtx_session *session, uint32_t timeline);

/* The queued-command operations: submit and drain. */
static const struct drv_gpu_command_ops runtime_commands = {
	runtime_submit,
	runtime_drain
};

/* The supervised-job operations. */
static const struct drv_gpu_job_ops runtime_jobs = {
	runtime_reserve,
	runtime_commit,
	runtime_cancel,
	runtime_capacity
};

/* The recovery operations: only the fault (no local stop). */
static const struct drv_gpu_recovery_ops runtime_recovery = {
	NULL,
	NULL,
	runtime_fault,
	NULL,
	NULL
};

/*
 * Binds the renderer's operations to the node before it is registered: the
 * queue, its lock and wait queues, and the capabilities they make good.
 */
void
nvrtx_runtime_bind(
	struct nvrtx_node *node)
{
	/* The queue, its lock and its two wait queues. */
	spin_init(&node->queue_lock, LOCK_RANK_DEVICE, "nvrtx queue");
	waitq_init(&node->available, "nvrtx queue available");
	waitq_init(&node->retired, "nvrtx queue retired");

	/* The operations and the capabilities they make good. */
	node->ops.get_capset = runtime_capset;
	node->ops.command = runtime_command;
	node->ops.commands = &runtime_commands;
	node->ops.jobs = &runtime_jobs;
	node->ops.recovery = &runtime_recovery;
	node->ops.capabilities |= GPU_CAP_CAPSET | GPU_CAP_COMMAND | GPU_CAP_NOTIFICATION | GPU_CAP_JOB | GPU_CAP_JOB_CAPACITY;
	node->renderer = 1;
}

/*
 * Starts the worker once the node is registered; it runs for the node's
 * life.  Without it nothing queued would complete, so a failure faults the
 * renderer (every queued operation then answers EIO).
 */
void
nvrtx_runtime_start(
	struct nvrtx_node *node)
{
	struct thread *worker;
	unsigned long enabled;
	int error;

	/* The worker. */
	error = kthread_create(runtime_worker, node, SCHED_PRIORITY_DEFAULT, &worker);
	if (error == 0) {
		worker->detached = 1;
		thread_start(worker);
		return;
	}

	/* No worker: the renderer takes nothing. */
	enabled = spin_lock_irqsave(&node->queue_lock);

	node->faulted = 1;

	spin_unlock_irqrestore(&node->queue_lock, enabled);

	nvrtx_stage_mark("D2: no renderer worker (%d)", error);
}

/* Reports the protocol's capset: wire 1, XML 1.3.269, timelines, the vendor suffix and its flags. */
static int
runtime_capset(
	void *private_data,
	void *session,
	struct gpu_capset *capset)
{
	UNUSED_PARAMETER(private_data);
	UNUSED_PARAMETER(session);

	/* Only the protocol's capset, version 0, in a reply that holds it. */
	if (capset->capset_id != RUNTIME_CAPSET_ID || capset->capset_version != 0U)
		return ENOTSUP;
	if (capset->capacity < RUNTIME_CAPSET_BYTES || capset->capacity > GPU_CAPSET_MAX)
		return EINVAL;

	/* Every word not named is zero. */
	kern_memset(capset->data, 0, sizeof(capset->data));
	nvrtx_put32(capset->data, RUNTIME_CAPSET_WIRE_AT, RUNTIME_WIRE_VERSION);
	nvrtx_put32(capset->data, RUNTIME_CAPSET_XML_AT, RUNTIME_XML_VERSION);
	nvrtx_put32(capset->data, RUNTIME_CAPSET_TIMELINES_AT, 1U);
	nvrtx_put32(capset->data, RUNTIME_CAPSET_MAGIC_AT, RUNTIME_VENDOR_MAGIC);
	nvrtx_put32(capset->data, RUNTIME_CAPSET_FLAGS_AT, RUNTIME_VENDOR_FLAGS);
	capset->bytes = RUNTIME_CAPSET_BYTES;

	/* Succeeded: the capset is reported. */
	return 0;
}

/*
 * Runs one frame at once; EAGAIN while queued work of the session has not
 * finished (a frame may not overtake it).
 */
static int
runtime_command(
	void *private_data,
	void *session,
	const void *wire,
	uint32_t bytes)
{
	struct nvrtx_node *node;
	struct nvrtx_session *owner;
	unsigned long enabled;
	uint32_t pending;
	int faulted;
	int error;

	node = private_data;
	owner = session;

	/* Whole words, within the command's bound. */
	if (bytes == 0U || bytes > GPU_COMMAND_MAX || (bytes & 3U) != 0U)
		return EINVAL;

	/* Looks at the session's queued work and the fault. */
	enabled = spin_lock_irqsave(&node->queue_lock);

	pending = owner->pending;
	faulted = node->faulted;

	spin_unlock_irqrestore(&node->queue_lock, enabled);

	/* A faulted device takes nothing; queued work goes first. */
	if (faulted != 0)
		return EIO;
	if (pending != 0U)
		return EAGAIN;

	/* Runs the frame through the executor. */
	mutex_lock(&node->lock);

	error = nvrtx_vk_stream_execute(owner->vk, wire, bytes, nvrtx_vk_dispatch);

	mutex_unlock(&node->lock);

	/* Reports a frame that failed. */
	if (error != 0)
		return error;

	/* Succeeded: the frame ran. */
	return 0;
}

/*
 * Queues one frame (a copy) with its completion, which the worker
 * completes once the frame has run.  A frame of no bytes is a marker of the
 * context's fence alone.
 */
static int
runtime_submit(
	void *private_data,
	void *session,
	const void *wire,
	uint32_t bytes,
	uint32_t flags,
	uint32_t timeline,
	struct drv_gpu_completion *completion)
{
	struct nvrtx_node *node;
	struct nvrtx_node_entry *entry;
	unsigned long enabled;
	int faulted;

	node = private_data;

	/* Only the context's fence flag; a timeline only with it. */
	if ((flags & ~GPU_COMMAND_CONTEXT_FENCE) != 0U || timeline >= RUNTIME_TIMELINES)
		return EINVAL;
	if (flags == 0U && timeline != 0U)
		return EINVAL;

	/* Whole words within the bound; an empty frame only as a fence. */
	if (bytes > GPU_COMMAND_MAX || (bytes & 3U) != 0U)
		return EINVAL;
	if (bytes == 0U && flags == 0U)
		return EINVAL;
	if (bytes != 0U && wire == NULL)
		return EINVAL;

	/* Allocates the entry with its copy of the frame. */
	entry = kern_calloc(1U, sizeof(*entry) + bytes);
	if (entry == NULL)
		return ENOMEM;

	entry->kind = RUNTIME_ENTRY_FRAME;
	entry->session = session;
	entry->completion = completion;
	entry->bytes = bytes;
	entry->wire = (uint8_t *)(entry + 1);
	if (bytes != 0U)
		kern_memcpy(entry->wire, wire, bytes);

	/* Queues it unless the device faulted. */
	enabled = spin_lock_irqsave(&node->queue_lock);

	faulted = node->faulted;
	if (faulted == 0) {
		entry->session->pending++;
		runtime_enqueue(node, entry);
	}

	spin_unlock_irqrestore(&node->queue_lock, enabled);

	/* A refused frame stays the caller's: nothing will complete it. */
	if (faulted != 0) {
		kern_free(entry);
		return EIO;
	}

	/* Succeeded: the worker completes the frame. */
	return 0;
}

/* Waits until every queued frame, marker and reservation of the session has ended. */
static void
runtime_drain(
	void *private_data,
	void *session)
{
	struct nvrtx_node *node;
	struct nvrtx_session *owner;
	unsigned long enabled;
	uint64_t observed;

	node = private_data;
	owner = session;

	/* Sleeps on the retirements until the session has nothing pending. */
	enabled = spin_lock_irqsave(&node->queue_lock);

	while (owner->pending != 0U) {
		observed = waitq_sequence(&node->retired);
		(void)waitq_sleep(&node->retired, &node->queue_lock, observed, 0U, 0U);
	}

	spin_unlock_irqrestore(&node->queue_lock, enabled);
}

/*
 * Reserves a marker for a supervised job on a timeline the session's queue
 * owns.  Returns EAGAIN when no marker is free (the producer waits for
 * capacity), EIO once the device faulted.
 */
static int
runtime_reserve(
	void *private_data,
	void *session,
	uint32_t timeline,
	struct drv_gpu_completion *completion,
	void **token)
{
	struct nvrtx_node *node;
	struct nvrtx_session *owner;
	struct nvrtx_node_entry *entry;
	unsigned long enabled;
	uint32_t slot;
	bool owned;

	node = private_data;
	owner = session;
	*token = NULL;

	/* A timeline of the session's queues. */
	if (timeline == 0U || timeline >= RUNTIME_TIMELINES)
		return EINVAL;
	owned = runtime_timeline_owned(owner, timeline);
	if (!owned)
		return EINVAL;

	/* Takes a free marker unless the device faulted. */
	enabled = spin_lock_irqsave(&node->queue_lock);

	if (node->faulted != 0) {
		spin_unlock_irqrestore(&node->queue_lock, enabled);
		return EIO;
	}

	entry = NULL;
	for (slot = 0U; slot < NVRTX_NODE_MARKERS; slot++) {
		if (node->markers[slot].state == NVRTX_ENTRY_FREE) {
			entry = &node->markers[slot];
			break;
		}
	}

	if (entry == NULL) {
		spin_unlock_irqrestore(&node->queue_lock, enabled);
		return EAGAIN;
	}

	/*
	 * The marker is the session's, reserved: the session counts it as
	 * pending until it is cancelled or its completion is delivered.
	 */
	entry->kind = RUNTIME_ENTRY_MARKER;
	entry->session = owner;
	entry->completion = completion;
	entry->error = 0;
	entry->state = NVRTX_ENTRY_RESERVED;
	owner->pending++;

	spin_unlock_irqrestore(&node->queue_lock, enabled);

	/* Succeeded: the token names the marker. */
	*token = entry;
	return 0;
}

/* Queues a reserved marker after the frames queued before it. */
static int
runtime_commit(
	void *private_data,
	void *session,
	void *token,
	struct drv_gpu_completion *completion)
{
	struct nvrtx_node *node;
	struct nvrtx_node_entry *entry;
	unsigned long enabled;

	node = private_data;

	/* Finds the reservation, queues it unless the device faulted. */
	enabled = spin_lock_irqsave(&node->queue_lock);

	entry = runtime_marker(node, session, token, completion);
	if (entry == NULL || entry->state != NVRTX_ENTRY_RESERVED) {
		spin_unlock_irqrestore(&node->queue_lock, enabled);
		return ESTALE;
	}

	if (node->faulted != 0) {
		spin_unlock_irqrestore(&node->queue_lock, enabled);
		return EIO;
	}

	runtime_enqueue(node, entry);

	spin_unlock_irqrestore(&node->queue_lock, enabled);

	/* Succeeded: the worker completes the marker in order. */
	return 0;
}

/*
 * Withdraws a reservation (normal: before it was committed, no completion)
 * or ends an uncertain one with EIO (fault: queued to complete with the
 * error).
 */
static int
runtime_cancel(
	void *private_data,
	void *session,
	void *token,
	struct drv_gpu_completion *completion,
	unsigned fault)
{
	struct nvrtx_node *node;
	struct nvrtx_node_entry *entry;
	unsigned long enabled;

	node = private_data;

	/* Normal or fault only. */
	if (fault > 1U)
		return EINVAL;

	/* Finds the reservation. */
	enabled = spin_lock_irqsave(&node->queue_lock);

	entry = runtime_marker(node, session, token, completion);
	if (entry == NULL) {
		spin_unlock_irqrestore(&node->queue_lock, enabled);
		return ESTALE;
	}

	/* A fault ends a reservation not yet queued with the error, in order. */
	if (fault != 0U) {
		if (entry->state == NVRTX_ENTRY_RESERVED) {
			entry->error = EIO;
			runtime_enqueue(node, entry);
		}

		spin_unlock_irqrestore(&node->queue_lock, enabled);
		return 0;
	}

	/* A normal cancel withdraws only a reservation not yet queued. */
	if (entry->state != NVRTX_ENTRY_RESERVED) {
		spin_unlock_irqrestore(&node->queue_lock, enabled);
		return ESTALE;
	}

	/* The marker is free again; the session no longer waits for it. */
	entry->state = NVRTX_ENTRY_FREE;
	entry->session->pending--;
	entry->session = NULL;
	entry->completion = NULL;
	waitq_wake_all(&node->retired);

	spin_unlock_irqrestore(&node->queue_lock, enabled);

	/* Succeeded: the reservation is withdrawn without a completion (the GPU core tells capacity observers). */
	return 0;
}

/* Reports how many markers a reservation on the timeline could take now. */
static int
runtime_capacity(
	void *private_data,
	void *session,
	uint32_t timeline,
	unsigned *available)
{
	struct nvrtx_node *node;
	unsigned long enabled;
	bool owned;

	node = private_data;
	*available = 0U;

	/* A timeline of the session's queues. */
	if (timeline == 0U || timeline >= RUNTIME_TIMELINES)
		return EINVAL;
	owned = runtime_timeline_owned(session, timeline);
	if (!owned)
		return EINVAL;

	/* The free markers, unless the device faulted. */
	enabled = spin_lock_irqsave(&node->queue_lock);

	if (node->faulted != 0) {
		spin_unlock_irqrestore(&node->queue_lock, enabled);
		return EIO;
	}

	*available = runtime_free_markers(node);

	spin_unlock_irqrestore(&node->queue_lock, enabled);

	/* Succeeded: the count is a snapshot, not a promise. */
	return 0;
}

/*
 * Stops new work after a fault: frames and markers queued afterwards are
 * refused, and the queued ones complete with EIO.  Idempotent.
 */
static void
runtime_fault(
	void *private_data,
	int error)
{
	struct nvrtx_node *node;
	unsigned long enabled;

	UNUSED_PARAMETER(error);

	node = private_data;

	/* Sets the flag the queue's users look at. */
	enabled = spin_lock_irqsave(&node->queue_lock);

	node->faulted = 1;
	waitq_wake_all(&node->available);

	spin_unlock_irqrestore(&node->queue_lock, enabled);
}

/*
 * Runs the queue in order for the node's life: a frame through the
 * executor, then its completion; a marker's completion; then the entry is
 * retired and the session's pending count falls.
 */
static void
runtime_worker(
	void *argument)
{
	struct nvrtx_node *node;
	struct nvrtx_node_entry *entry;
	struct drv_gpu_completion *completion;
	unsigned long enabled;
	uint64_t observed;
	int faulted;
	int error;

	node = argument;

	/* Takes entries one at a time, sleeping while the queue is empty. */
	for (;;) {
		enabled = spin_lock_irqsave(&node->queue_lock);

		while (node->head == NULL) {
			observed = waitq_sequence(&node->available);
			(void)waitq_sleep(&node->available, &node->queue_lock, observed, 0U, 0U);
		}

		entry = node->head;
		node->head = entry->next;
		if (node->head == NULL)
			node->tail = NULL;
		entry->next = NULL;
		entry->state = NVRTX_ENTRY_RUNNING;
		faulted = node->faulted;

		spin_unlock_irqrestore(&node->queue_lock, enabled);

		/* A frame runs through the executor, unless the device faulted; a marker carries its error. */
		error = entry->error;
		if (entry->kind == RUNTIME_ENTRY_FRAME) {
			error = EIO;
			if (faulted == 0) {
				error = 0;
				if (entry->bytes != 0U) {
					mutex_lock(&node->lock);

					error = nvrtx_vk_stream_execute(entry->session->vk, entry->wire, entry->bytes, nvrtx_vk_dispatch);

					mutex_unlock(&node->lock);
				}
			}
		} else if (faulted != 0 && error == 0) {
			error = EIO;
		}

		/* Delivers the completion. */
		completion = entry->completion;
		drv_gpu_complete(completion, error);

		/* Retires the entry: a frame is freed, a marker is free again. */
		enabled = spin_lock_irqsave(&node->queue_lock);

		entry->session->pending--;
		if (entry->kind == RUNTIME_ENTRY_MARKER) {
			entry->state = NVRTX_ENTRY_FREE;
			entry->session = NULL;
			entry->completion = NULL;
		}
		waitq_wake_all(&node->retired);

		spin_unlock_irqrestore(&node->queue_lock, enabled);

		/* A frame's copy goes; a marker's freeing is capacity. */
		if (entry->kind == RUNTIME_ENTRY_FRAME) {
			kern_free(entry);
		} else {
			drv_gpu_capacity_changed(node->device);
		}
	}
}

/* Appends an entry to the queue and wakes the worker; the caller holds queue_lock. */
static void
runtime_enqueue(
	struct nvrtx_node *node,
	struct nvrtx_node_entry *entry)
{
	/* At the tail. */
	entry->state = NVRTX_ENTRY_QUEUED;
	entry->next = NULL;
	if (node->tail == NULL) {
		node->head = entry;
	} else {
		node->tail->next = entry;
	}

	node->tail = entry;

	/* The worker takes it. */
	waitq_wake_one(&node->available);
}

/* Finds the marker a token names for the session and completion, or NULL; the caller holds queue_lock. */
static struct nvrtx_node_entry *
runtime_marker(
	struct nvrtx_node *node,
	struct nvrtx_session *session,
	void *token,
	struct drv_gpu_completion *completion)
{
	struct nvrtx_node_entry *entry;
	uint32_t slot;

	/* Compares the token with each marker before using it. */
	for (slot = 0U; slot < NVRTX_NODE_MARKERS; slot++) {
		entry = &node->markers[slot];
		if (token != (void *)entry)
			continue;
		if (entry->session != session || entry->completion != completion)
			return NULL;
		if (entry->state == NVRTX_ENTRY_FREE)
			return NULL;

		/* The token names this marker of the session's. */
		return entry;
	}

	/* No marker is named. */
	return NULL;
}

/* Counts the free markers; the caller holds queue_lock. */
static unsigned
runtime_free_markers(
	const struct nvrtx_node *node)
{
	unsigned count;
	uint32_t slot;

	/* Each free one. */
	count = 0U;
	for (slot = 0U; slot < NVRTX_NODE_MARKERS; slot++) {
		if (node->markers[slot].state == NVRTX_ENTRY_FREE)
			count++;
	}

	/* Succeeded: the count. */
	return count;
}

/* Reports whether a queue of the session's executor owns the timeline. */
static bool
runtime_timeline_owned(
	struct nvrtx_session *session,
	uint32_t timeline)
{
	uint64_t timelines;

	/* The executor records the timelines its queues were made with. */
	timelines = __atomic_load_n(&session->vk->timelines, __ATOMIC_ACQUIRE);
	if ((timelines & (1ULL << timeline)) == 0U)
		return false;

	/* Succeeded: the timeline is the session's. */
	return true;
}
