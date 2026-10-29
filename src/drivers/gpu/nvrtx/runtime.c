/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The GPU device's Vulkan transport (nvrtx.vk=1).
 *
 * With the executor bound, the node is a renderer libvulkan opens: it
 * reports the protocol's capset, runs command frames (synchronously, or
 * queued with a completion), and takes supervised jobs whose completion
 * markers follow the frames queued before them.  One worker thread runs
 * the queue in order: a frame through the executor under the node's lock,
 * a marker by completing it.  The executor's submissions do not wait for
 * the GPU, so a marker whose session's newest run has not ended yet is
 * parked, and the completer thread completes the parked markers in order
 * as their runs end, waiting without the node's lock (a run that does not
 * end in time completes its marker with EIO and loses the session's runs).
 * A fault stops new work: frames and markers still queued or parked
 * complete with EIO.
 *
 * queue_lock (a spinlock) guards the queue, the parked markers, the
 * markers, every session's pending and queued counts and the fault flag;
 * the node's lock (a mutex) is taken only outside it, around the executor.
 *
 * A session's first frame gives it a channel of its own on the GPU (made
 * under the node's channel_lock, before the node's lock is taken, as it
 * calls the GSP), or the driver's work channel to share when none can be
 * made; its close frees that channel.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <drivers/gpu/gpu.h>
#include <kern/kcrt.h>
#include <kern/kmem.h>
#include <kern/lock.h>
#include <kern/sched.h>
#include <kern/thread.h>
#include <kern/waitq.h>
#include <uapi/errno.h>
#include <uapi/gpu.h>
#include <uapi/gpu-job.h>

#include "drivers/gpu/nvrtx/nvrtx-node.h"
#include "drivers/gpu/nvrtx/vk/nvrtx-vk.h"

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
static int runtime_stop_begin(void *private_data, void *session, int error);
static int runtime_stop_poll(void *private_data, void *session);
static void runtime_fault(void *private_data, int error);
static void runtime_worker(void *argument);
static void runtime_completer(void *argument);
static bool runtime_park(struct nvrtx_node *node, struct nvrtx_node_entry *entry);
static void runtime_dequeued(struct nvrtx_node *node, struct nvrtx_session *owner);
static void runtime_retire(struct nvrtx_node *node, struct nvrtx_node_entry *entry);
static int runtime_execute(struct nvrtx_node *node, struct nvrtx_session *session, const void *wire, uint32_t bytes);
static void runtime_channel(struct nvrtx_node *node, struct nvrtx_session *session);
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

/* The recovery operations: a session's stop and its poll, and the device's fault. */
static const struct drv_gpu_recovery_ops runtime_recovery = {
	runtime_stop_begin,
	runtime_stop_poll,
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
	bool wanted;

	/* The queue, the parked markers, their lock and the wait queues. */
	spin_init(&node->queue_lock, LOCK_RANK_DEVICE, "nvrtx queue");
	waitq_init(&node->available, "nvrtx queue available");
	waitq_init(&node->retired, "nvrtx queue retired");
	waitq_init(&node->parked_more, "nvrtx queue parked");
	node->parked_head = NULL;
	node->parked_tail = NULL;

	/* The sessions' channels: none made yet, the first number taken first, unless the boot asked for none. */
	(void)mutex_init(&node->channel_lock, LOCK_RANK_DEVICE, "nvrtx channels");
	node->chid_used = 0U;
	node->chid_last = NVRTX_WORK_SESSION_CHANNELS - 1U;
	wanted = nvrtx_stage_channels_wanted();
	node->channels_wanted = 0;
	if (wanted)
		node->channels_wanted = 1;

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
 * Starts the worker and the completer once the node is registered; they
 * run for the node's life.  Without them nothing queued would complete, so
 * a failure faults the renderer (every queued operation then answers EIO).
 */
void
nvrtx_runtime_start(
	struct nvrtx_node *node)
{
	struct thread *worker;
	struct thread *completer;
	unsigned long enabled;
	int error;

	/* The completer of the parked markers, then the worker. */
	error = kthread_create(runtime_completer, node, SCHED_PRIORITY_DEFAULT, &completer);
	if (error == 0) {
		completer->detached = 1;
		thread_start(completer);
		error = kthread_create(runtime_worker, node, SCHED_PRIORITY_DEFAULT, &worker);
	}
	if (error == 0) {
		worker->detached = 1;
		thread_start(worker);
		return;
	}

	/* No worker or no completer: the renderer takes nothing. */
	enabled = spin_lock_irqsave(&node->queue_lock);

	node->faulted = 1;

	spin_unlock_irqrestore(&node->queue_lock, enabled);

	nvrtx_stage_mark("renderer: no renderer worker or completer (%d)", error);
}

/*
 * Stops the renderer for the reboot: nothing new is taken and what is
 * queued completes with EIO, as after a fault, but without a fault's
 * report.  The worker thread runs on, idle, until the reset.
 */
void
nvrtx_runtime_stop(
	struct nvrtx_node *node)
{
	unsigned long enabled;

	/* A node that is no renderer has no queue. */
	if (node->renderer == 0)
		return;

	/* faulted refuses every later frame, marker and reservation; the worker completes the rest with EIO. */
	enabled = spin_lock_irqsave(&node->queue_lock);

	node->faulted = 1;
	waitq_wake_all(&node->available);

	spin_unlock_irqrestore(&node->queue_lock, enabled);

	nvrtx_stage_mark("unload shutdown: the renderer takes no more work");
}

/*
 * Closes a session's channel once its executor session is closed (its
 * runs ended, or were given up): a channel of its own is freed and its
 * number free again; one the resource manager would not free stays the
 * device's, with its number.  The shared work channel stays.
 */
void
nvrtx_runtime_session_close(
	struct nvrtx_node *node,
	struct nvrtx_session *session)
{
	struct nvrtx_work_channel *channel;
	unsigned long enabled;
	uint32_t number;
	int error;

	/* A session that never ran a frame, or one that shares the work channel, has none of its own. */
	channel = session->channel;
	if (channel == NULL || session->shared != 0)
		return;
	number = channel->chid - NVRTX_WORK_SESSION_CHID_FIRST;

	/* The GSP's reports no longer name the session. */
	enabled = spin_lock_irqsave(&node->queue_lock);

	node->channels[number] = NULL;

	spin_unlock_irqrestore(&node->queue_lock, enabled);

	/* Frees the channel, then its number, under the lock of the channels' making. */
	mutex_lock(&node->channel_lock);

	error = nvrtx_work_channel_close(node->gpu, channel);
	if (error == 0)
		node->chid_used &= ~(1U << number);

	mutex_unlock(&node->channel_lock);

	/* A channel that stays keeps its record, as its memory stays. */
	session->channel = NULL;
	if (error != 0)
		return;

	/* Succeeded: the channel is gone. */
	kern_free(channel);
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
 * Runs one frame at once; EAGAIN while frames or markers of the session
 * are queued (a frame may not overtake them; a parked marker does not
 * hold it back).
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
	uint32_t queued;
	int faulted;
	int error;

	node = private_data;
	owner = session;

	/* Whole words, within the command's bound. */
	if (bytes == 0U || bytes > GPU_COMMAND_MAX || (bytes & 3U) != 0U)
		return EINVAL;

	/* Looks at the session's queued work and the fault. */
	enabled = spin_lock_irqsave(&node->queue_lock);

	queued = owner->queued;
	faulted = node->faulted;
	if (owner->stopping != 0)
		faulted = 1;

	spin_unlock_irqrestore(&node->queue_lock, enabled);

	/* A faulted device, or a session being stopped, takes nothing; queued work goes first. */
	if (faulted != 0)
		return EIO;
	if (queued != 0U)
		return EAGAIN;

	/* Runs the frame through the executor. */
	error = runtime_execute(node, owner, wire, bytes);

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

	/* Queues it unless the device faulted or the session is being stopped. */
	enabled = spin_lock_irqsave(&node->queue_lock);

	faulted = node->faulted;
	if (entry->session->stopping != 0)
		faulted = 1;
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

	/* Takes a free marker unless the device faulted or the session is being stopped. */
	enabled = spin_lock_irqsave(&node->queue_lock);

	if (node->faulted != 0 || owner->stopping != 0) {
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
 * Begins the stop of a session (its open is closing, or the core gave up
 * on it): the session takes no new frame, command or reservation from now
 * on.  What it queued still runs to its end; nothing here waits.
 */
static int
runtime_stop_begin(
	void *private_data,
	void *session,
	int error)
{
	struct nvrtx_node *node;
	struct nvrtx_session *owner;
	unsigned long enabled;

	UNUSED_PARAMETER(error);

	node = private_data;
	owner = session;

	/* stopping refuses the session's new work (submit, command, reserve answer EIO). */
	enabled = spin_lock_irqsave(&node->queue_lock);

	owner->stopping = 1;

	spin_unlock_irqrestore(&node->queue_lock, enabled);

	/* Succeeded: the stop is begun. */
	return 0;
}

/*
 * Reports whether a stopping session's work has ended: 0 once nothing it
 * queued is queued or running any more (reservations it never committed
 * may remain: the core withdraws them), EAGAIN until then.
 */
static int
runtime_stop_poll(
	void *private_data,
	void *session)
{
	struct nvrtx_node *node;
	struct nvrtx_session *owner;
	unsigned long enabled;
	uint32_t reserved;
	uint32_t slot;
	uint32_t pending;

	node = private_data;
	owner = session;

	/* The session's pending work less its reservations not yet committed. */
	enabled = spin_lock_irqsave(&node->queue_lock);

	reserved = 0U;
	for (slot = 0U; slot < NVRTX_NODE_MARKERS; slot++) {
		if (node->markers[slot].session == owner && node->markers[slot].state == NVRTX_ENTRY_RESERVED)
			reserved++;
	}
	pending = owner->pending - reserved;

	spin_unlock_irqrestore(&node->queue_lock, enabled);

	/* Work of the session still queued or running. */
	if (pending != 0U)
		return EAGAIN;

	/* Succeeded: the session's work has ended. */
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
	bool first;

	node = private_data;

	/* Sets the flag the queue's users look at; the first fault is shown. */
	enabled = spin_lock_irqsave(&node->queue_lock);

	first = false;
	if (node->faulted == 0)
		first = true;
	node->faulted = 1;
	waitq_wake_all(&node->available);

	spin_unlock_irqrestore(&node->queue_lock, enabled);

	if (first)
		nvrtx_stage_mark("renderer: the renderer faulted (%d); it takes no more work", error);
}

/*
 * Runs the queue in order for the node's life: a frame through the
 * executor, then its completion; a marker's completion, or its parking
 * when its session's newest run has not ended; then the entry is retired
 * and the session's pending count falls.
 */
static void
runtime_worker(
	void *argument)
{
	struct nvrtx_node *node;
	struct nvrtx_node_entry *entry;
	struct nvrtx_session *owner;
	unsigned long enabled;
	uint64_t observed;
	bool parked;
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
		owner = entry->session;
		faulted = node->faulted;

		spin_unlock_irqrestore(&node->queue_lock, enabled);

		/* A frame runs through the executor, unless the device faulted; a marker carries its error. */
		error = entry->error;
		if (entry->kind == RUNTIME_ENTRY_FRAME) {
			error = EIO;
			if (faulted == 0) {
				error = 0;
				if (entry->bytes != 0U)
					error = runtime_execute(node, entry->session, entry->wire, entry->bytes);
			}
		} else if (faulted != 0 && error == 0) {
			error = EIO;
		}

		/*
		 * A marker whose session's newest run has not ended goes to the
		 * completer.  The entry leaves the worker's queue before its
		 * completion (it still counts as pending), a marker once it has
		 * taken its run (runtime_park): the command its waiter sends
		 * next may run at once, and its runs are not the ones the marker
		 * waits for.
		 */
		if (entry->kind == RUNTIME_ENTRY_MARKER && error == 0) {
			parked = runtime_park(node, entry);
			if (parked)
				continue;

			/* The run ended: whether the session's runs are lost. */
			error = nvrtx_vk_node_run_wait(owner, entry->run_fence, entry->run_us);
		} else {
			runtime_dequeued(node, owner);
		}

		/* Delivers the completion, then retires the entry. */
		drv_gpu_complete(entry->completion, error);
		runtime_retire(node, entry);
	}
}

/* Takes an entry the worker ran (a frame, or a marker carrying an error) off its session's count of queued entries. */
static void
runtime_dequeued(
	struct nvrtx_node *node,
	struct nvrtx_session *owner)
{
	unsigned long enabled;

	enabled = spin_lock_irqsave(&node->queue_lock);

	owner->queued--;

	spin_unlock_irqrestore(&node->queue_lock, enabled);
}

/*
 * Completes the parked markers in the order they were parked, for the
 * node's life: each once its session's run has ended, with EIO when it did
 * not end in time or the device faulted.  The waits take no lock.
 */
static void
runtime_completer(
	void *argument)
{
	struct nvrtx_node *node;
	struct nvrtx_node_entry *entry;
	unsigned long enabled;
	uint64_t observed;
	int faulted;
	int error;

	node = argument;

	/* Takes the oldest parked marker, sleeping while none is parked. */
	for (;;) {
		enabled = spin_lock_irqsave(&node->queue_lock);

		while (node->parked_head == NULL) {
			observed = waitq_sequence(&node->parked_more);
			(void)waitq_sleep(&node->parked_more, &node->queue_lock, observed, 0U, 0U);
		}

		/* The head stays parked (counted pending) until it is completed. */
		entry = node->parked_head;
		faulted = node->faulted;

		spin_unlock_irqrestore(&node->queue_lock, enabled);

		/* Waits for its run, unless the device faulted. */
		error = EIO;
		if (faulted == 0)
			error = nvrtx_vk_node_run_wait(entry->session, entry->run_fence, entry->run_us);

		/* Delivers the completion. */
		drv_gpu_complete(entry->completion, error);

		/* Takes it off the parked list, then retires it. */
		enabled = spin_lock_irqsave(&node->queue_lock);

		node->parked_head = entry->next;
		if (node->parked_head == NULL)
			node->parked_tail = NULL;
		entry->next = NULL;

		spin_unlock_irqrestore(&node->queue_lock, enabled);

		runtime_retire(node, entry);
	}
}

/*
 * Parks a marker the worker took when its session's newest run has not
 * ended, or when markers are parked already (markers complete in order).
 * Returns true when parked: the completer completes it.
 */
static bool
runtime_park(
	struct nvrtx_node *node,
	struct nvrtx_node_entry *entry)
{
	unsigned long enabled;
	bool passed;

	/* The session's newest run, as its frames before the marker left it. */
	mutex_lock(&node->lock);

	entry->run_fence = entry->session->run_fence;
	entry->run_us = entry->session->run_us;

	mutex_unlock(&node->lock);

	/* Whether it still runs. */
	passed = nvrtx_vk_node_run_passed(entry->session, entry->run_fence);

	/*
	 * It leaves the worker's queue, with its run taken (before the
	 * completer may retire it); then it parks behind the parked ones when
	 * it must wait, or when others wait already.
	 */
	enabled = spin_lock_irqsave(&node->queue_lock);

	entry->session->queued--;
	if (passed && node->parked_head == NULL) {
		spin_unlock_irqrestore(&node->queue_lock, enabled);
		return false;
	}
	entry->state = NVRTX_ENTRY_PARKED;
	entry->next = NULL;
	if (node->parked_tail == NULL) {
		node->parked_head = entry;
	} else {
		node->parked_tail->next = entry;
	}
	node->parked_tail = entry;
	waitq_wake_one(&node->parked_more);

	spin_unlock_irqrestore(&node->queue_lock, enabled);

	/* Succeeded: the completer has it (counted for nvrtx.vk.perf=1). */
	nvrtx_node_count(node, &node->counts.parked, 1U);
	return true;
}

/*
 * Retires an entry whose completion was delivered: the session's pending
 * count falls, a frame is freed and a marker is free again (capacity).
 */
static void
runtime_retire(
	struct nvrtx_node *node,
	struct nvrtx_node_entry *entry)
{
	unsigned long enabled;
	uint32_t kind;

	/* The session's count, and a marker free again. */
	kind = entry->kind;
	enabled = spin_lock_irqsave(&node->queue_lock);

	entry->session->pending--;
	if (kind == RUNTIME_ENTRY_MARKER) {
		entry->state = NVRTX_ENTRY_FREE;
		entry->session = NULL;
		entry->completion = NULL;
	}

	/* The drains look again. */
	waitq_wake_all(&node->retired);

	spin_unlock_irqrestore(&node->queue_lock, enabled);

	/* A frame's copy goes; a marker's freeing is capacity. */
	if (kind == RUNTIME_ENTRY_FRAME) {
		kern_free(entry);
	} else {
		drv_gpu_capacity_changed(node->device);
	}
}

/*
 * Runs a frame of a session through the executor under the node's lock;
 * with nvrtx.vk.perf=1 the frame, its wait for the lock and its hold of it
 * are counted.  Returns the executor's error.
 */
static int
runtime_execute(
	struct nvrtx_node *node,
	struct nvrtx_session *session,
	const void *wire,
	uint32_t bytes)
{
	uint64_t asked;
	uint64_t taken;
	uint64_t left;
	int error;

	/* The session's channel, before its first frame takes the node's lock. */
	runtime_channel(node, session);

	/* Runs the frame, timing the lock's wait and hold only when the counts are asked for. */
	asked = 0U;
	if (node->perf != 0)
		asked = nvrtx_now_us();

	mutex_lock(&node->lock);

	taken = 0U;
	if (node->perf != 0)
		taken = nvrtx_now_us();
	error = nvrtx_vk_stream_execute(session->vk, wire, bytes, nvrtx_vk_dispatch);

	mutex_unlock(&node->lock);

	/* Counts the frame, its wait and its hold. */
	if (node->perf != 0) {
		left = nvrtx_now_us();
		nvrtx_node_count(node, &node->counts.frames, 1U);
		nvrtx_node_count(node, &node->counts.wait_us, taken - asked);
		nvrtx_node_count(node, &node->counts.held_us, left - taken);
	}

	/* Reports a frame the executor refused or that failed. */
	if (error != 0)
		return error;

	/* Succeeded: the frame ran. */
	return 0;
}

/*
 * Gives a session its channel when it has none yet: a channel of its own
 * under the next free number, or the driver's work channel to share when
 * the boot asked for none, every number is in use or the making failed.
 * The node's lock is not held (the making calls the GSP).
 */
static void
runtime_channel(
	struct nvrtx_node *node,
	struct nvrtx_session *session)
{
	struct nvrtx_work_channel *made;
	unsigned long enabled;
	int number;
	int error;

	/* Looks and makes under the lock of the channels' making. */
	mutex_lock(&node->channel_lock);

	if (session->channel != NULL) {
		mutex_unlock(&node->channel_lock);
		return;
	}

	/* A free number of the sessions', unless every session shares the work channel. */
	number = -1;
	if (node->channels_wanted != 0)
		number = nvrtx_recover_chid_next(node->chid_used, node->chid_last, NVRTX_WORK_SESSION_CHANNELS);

	/* The channel under that number. */
	made = NULL;
	error = ENOSPC;
	if (number >= 0)
		made = kern_calloc(1U, sizeof(*made));
	if (made != NULL)
		error = nvrtx_work_channel_open(node->gpu, made, NVRTX_WORK_SESSION_CHID_FIRST + (uint32_t)number);
	if (error == 0) {
		node->chid_used |= 1U << (uint32_t)number;
		node->chid_last = (uint32_t)number;
	} else if (made != NULL && made->arena_made == 0 && made->region_made == 0 && made->channel_made == 0) {
		/* Nothing of a failed channel stays: its record goes. */
		kern_free(made);
		made = NULL;
	} else if (made != NULL) {
		/* What the resource manager kept of a failed channel stays the device's, with its number. */
		node->chid_used |= 1U << (uint32_t)number;
		node->chid_last = (uint32_t)number;
		made = NULL;
	}

	/* The session's channel: its own, or the shared one. */
	if (error == 0) {
		enabled = spin_lock_irqsave(&node->queue_lock);

		made->state = NVRTX_WORK_CHANNEL_READY;
		node->channels[number] = session;

		spin_unlock_irqrestore(&node->queue_lock, enabled);

		session->channel = made;
	} else {
		session->channel = &node->gpu->work.driver;
		session->shared = 1;
	}

	mutex_unlock(&node->channel_lock);

	/* A session that shares the work channel is told. */
	if (error != 0 && node->channels_wanted != 0)
		nvrtx_stage_mark("renderer: a session shares the work channel (%d)", error);
}

/* Appends an entry to the queue and wakes the worker; the caller holds queue_lock. */
static void
runtime_enqueue(
	struct nvrtx_node *node,
	struct nvrtx_node_entry *entry)
{
	/* At the tail, counted in its session's queue. */
	entry->state = NVRTX_ENTRY_QUEUED;
	entry->session->queued++;
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
