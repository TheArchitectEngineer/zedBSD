/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The queue of the Vulkan executor: the submission, the fences, the
 * semaphores and the events, and the completions of a node session (its
 * job markers and its queued frames) that the service thread retires as
 * the GPU passes their tickets.  A retirement counts what it took as the
 * session's completing until each is reported to the core, so that a
 * drain or a stop never says that a session is done while a completion
 * of it is still being reported.
 *
 * One ring is the one queue, so the submissions run in the order they
 * are placed: a semaphore needs no wait of its own, and a fence or a job
 * is done when the ticket of the last submission before it is.  A
 * submission places the packets of its command buffers (vk-execute.c)
 * in the device's GHCI session and a fence after them; one whose command
 * buffers record nothing places the fence alone.  An event is a word of
 * the device's data heap, which the host sets and resets here and the
 * GPU with a fence.  A query pool's slots are in the data heap too, and
 * the host reads its results there.
 */

#include <stddef.h>
#include <stdint.h>

#include <drivers/gpu/gpu.h>
#include <kern/device-io.h>
#include <kern/kcrt.h>
#include <kern/kmem.h>
#include <kern/waitq.h>
#include <uapi/errno.h>
#include <uapi/gpu-op.h>

#include "vk.h"

#include "drivers/gpu/i915/vk/vk-codec.inc"

/* How long one wait of the drain for a session's last ticket lasts before it looks again. */
#define DRAIN_WAIT_MS			100U

/* The bytes and the alignment of an event's word. */
#define EVENT_BYTES			8U

/* The bytes of a query's slot (its result and availability word) and the most queries of a pool. */
#define QUERY_SLOT_BYTES		16U
#define QUERY_POOL_MAX			65536U

static int queue_submit(struct ghci_vk_session *session, uint32_t requested, struct i915_wire_reader *reader, struct i915_wire_writer *reply);
static int submission_check(struct ghci_vk_session *session, const VkSubmitInfo *submits, uint64_t count, struct ghci_vk_device *device, int *recorded);
static int create_sync(struct ghci_vk_session *session, uint32_t opcode, uint32_t requested, struct i915_wire_reader *reader, struct i915_wire_writer *reply);
static int destroy_sync(struct ghci_vk_session *session, enum ghci_vk_kind kind, struct i915_wire_reader *reader);
static int reset_fences(struct ghci_vk_session *session, uint32_t requested, struct i915_wire_reader *reader, struct i915_wire_writer *reply);
static int fence_status(struct ghci_vk_session *session, uint32_t requested, struct i915_wire_reader *reader, struct i915_wire_writer *reply);
static int event_operation(struct ghci_vk_session *session, uint32_t opcode, uint32_t requested, struct i915_wire_reader *reader, struct i915_wire_writer *reply);
static int create_query_pool(struct ghci_vk_session *session, uint32_t requested, struct i915_wire_reader *reader, struct i915_wire_writer *reply);
static int query_results(struct ghci_vk_session *session, uint32_t requested, struct i915_wire_reader *reader, struct i915_wire_writer *reply);
static enum ghci_job_answer judge(void *context, uint64_t seq, uint64_t generation);
static void retire_session(struct ghci_device *device, struct ghci_vk_session *session);
static unsigned retire(struct ghci_vk_session *session, void **done, int *errors, struct ghci_vk_session **owners, unsigned capacity, struct ghci_vk_frame **gone);
static void complete(struct ghci_device *device, void **done, const int *errors, struct ghci_vk_session **owners, unsigned count, struct ghci_vk_frame *gone);

/*
 * Runs a submission, fence, semaphore or event command; *handled stays
 * clear for another group's number.
 */
int
ghci_vk_queue_route(
	struct ghci_vk_session *session,
	uint32_t opcode,
	uint32_t requested,
	struct i915_wire_reader *reader,
	struct i915_wire_writer *reply,
	int *handled)
{
	int error;

	/* Picks the command, or leaves it to another group. */
	*handled = 1;
	switch (opcode) {
	case GPU_OP_QUEUE_SUBMIT:
		error = queue_submit(session, requested, reader, reply);
		break;
	case GPU_OP_CREATE_FENCE:
	case GPU_OP_CREATE_SEMAPHORE:
	case GPU_OP_CREATE_EVENT:
		error = create_sync(session, opcode, requested, reader, reply);
		break;
	case GPU_OP_DESTROY_FENCE:
		error = destroy_sync(session, GHCI_VK_FENCE, reader);
		break;
	case GPU_OP_DESTROY_SEMAPHORE:
		error = destroy_sync(session, GHCI_VK_SEMAPHORE, reader);
		break;
	case GPU_OP_DESTROY_EVENT:
		error = destroy_sync(session, GHCI_VK_EVENT, reader);
		break;
	case GPU_OP_RESET_FENCES:
		error = reset_fences(session, requested, reader, reply);
		break;
	case GPU_OP_GET_FENCE_STATUS:
		error = fence_status(session, requested, reader, reply);
		break;
	case GPU_OP_GET_EVENT_STATUS:
	case GPU_OP_SET_EVENT:
	case GPU_OP_RESET_EVENT:
		error = event_operation(session, opcode, requested, reader, reply);
		break;
	default:
		*handled = 0;
		return 0;
	}

	/* A command that could not be decoded or run. */
	if (error != 0)
		return error;

	/* Succeeded: the command ran. */
	return 0;
}

/*
 * Retires, on the service thread, the completions of every node session
 * whose tickets the GPU passed or a reset ended, and completes them once
 * the interrupt's lock is let go.
 */
void
ghci_vk_service(
	struct ghci_device *device)
{
	struct ghci_vk_session *owners[GHCI_VK_DONE_BATCH];
	struct ghci_vk_session *session;
	struct ghci_vk_frame *gone;
	struct ghci_vk *vk;
	void *done[GHCI_VK_DONE_BATCH];
	int errors[GHCI_VK_DONE_BATCH];
	unsigned long irq;
	unsigned count;

	/* Batches of completions until a batch is not full. */
	vk = device->vk;
	do {
		/* Each session's markers and frames that are done. */
		count = 0U;
		gone = NULL;
		irq = spin_lock_irqsave(&device->irq_lock);

		for (session = vk->sessions; session != NULL && count < GHCI_VK_DONE_BATCH; session = session->next)
			count += retire(session, done + count, errors + count, owners + count, GHCI_VK_DONE_BATCH - count, &gone);

		spin_unlock_irqrestore(&device->irq_lock, irq);

		/* The core hears of each. */
		complete(device, done, errors, owners, count, gone);
	} while (count == GHCI_VK_DONE_BATCH);
}

/*
 * Ends every completion of a node session before it closes: the session
 * retires its own completions as their tickets are answered, waiting a
 * bounded time on its last ticket between looks, and then waits until no
 * completion another thread took is still being reported to the core.  A
 * ticket that never passes is ended by the recovery (a hang, a reset,
 * the loss of the device), so the drain ends.  The caller holds none of
 * the device's locks.
 */
void
ghci_vk_drain(
	struct ghci_device *device,
	struct ghci_vk_session *session)
{
	struct ghci_ticket last;
	struct ghci_vk *vk;
	unsigned long irq;
	uint64_t sequence;
	int pending;
	int submitted;

	/* Looks until nothing of the session is pending or being reported. */
	vk = device->vk;
	for (;;) {
		/* The session's completions that are done, reported at once. */
		retire_session(device, session);

		/* What is left; with nothing, the completions another thread is still reporting. */
		irq = spin_lock_irqsave(&device->irq_lock);

		pending = session->jobs.committed != 0U || session->frame_count != 0U;
		while (!pending && session->completing != 0U) {
			sequence = waitq_sequence(&vk->completing_wake);
			(void)waitq_sleep(&vk->completing_wake, &device->irq_lock, sequence, 0U, 0U);
		}
		last = session->last;
		submitted = session->submitted;

		spin_unlock_irqrestore(&device->irq_lock, irq);

		/* Nothing left: no completion of the session is in flight. */
		if (!pending)
			return;

		/* Waits for the session's last work; a failed marker or frame needs no wait. */
		if (submitted)
			(void)ghci_cp_wait(device, &last, DRAIN_WAIT_MS);
		else
			ghci_sleep_ms(DRAIN_WAIT_MS);
	}
}

/*
 * Ends the completions of a node session that are done now (after an
 * isolation, every one), and waits until none that another thread took
 * is still being reported to the core.  It waits for no ticket.  The
 * caller holds none of the device's locks.
 */
void
ghci_vk_settle(
	struct ghci_device *device,
	struct ghci_vk_session *session)
{
	struct ghci_vk *vk;
	unsigned long irq;
	uint64_t sequence;

	/* What is done, reported at once. */
	vk = device->vk;
	retire_session(device, session);

	/* What the service thread took is reported before the caller goes on. */
	irq = spin_lock_irqsave(&device->irq_lock);

	while (session->completing != 0U) {
		sequence = waitq_sequence(&vk->completing_wake);
		(void)waitq_sleep(&vk->completing_wake, &device->irq_lock, sequence, 0U, 0U);
	}

	spin_unlock_irqrestore(&device->irq_lock, irq);
}

/*
 * Makes the record of a frame's completion before the frame runs, so
 * that a frame that ran always has its completion queued.  timeline is 0
 * (the decoder's notification) or a timeline a queue of the session
 * completes on.  Returns 0 with the record, EINVAL for a timeline no
 * queue of the session owns, or ENOMEM.
 */
int
ghci_vk_frame_new(
	struct ghci_vk_session *session,
	uint32_t timeline,
	struct drv_gpu_completion *completion,
	struct ghci_vk_frame **result)
{
	struct ghci_vk_frame *frame;
	unsigned long irq;
	int known;

	/* Timeline 0, or one of the session's queues. */
	known = 0;
	if (timeline == 0U) {
		known = 1;
	} else if (timeline < GHCI_VK_TIMELINES) {
		irq = spin_lock_irqsave(&session->device->irq_lock);

		if (session->timeline_device[timeline] != NULL)
			known = 1;

		spin_unlock_irqrestore(&session->device->irq_lock, irq);
	}
	if (!known)
		return EINVAL;

	/* The record. */
	frame = kern_calloc(1U, sizeof(*frame));
	if (frame == NULL)
		return ENOMEM;
	frame->completion = completion;
	frame->timeline = timeline;

	/* Succeeded: the caller runs the frame, then queues the record. */
	*result = frame;
	return 0;
}

/*
 * Queues a frame's completion once the frame has run: a frame of
 * timeline 0 completes at the next retirement, since its replies are
 * already written; a context fence of a timeline waits for the work its
 * device placed until now (none when the frame destroyed the device).
 * Returns 0, or EIO for a session the GPU core stopped or isolated, and
 * then frees the record.
 */
int
ghci_vk_frame_queue(
	struct ghci_vk_session *session,
	struct ghci_vk_frame *frame)
{
	struct ghci_vk_frame **position;
	struct ghci_vk_device *device;
	unsigned long irq;
	int stopped;

	/* The ticket it waits for, and the frame last in the session's queue. */
	irq = spin_lock_irqsave(&session->device->irq_lock);

	stopped = session->stopping || session->isolated;
	device = NULL;
	if (frame->timeline != 0U)
		device = session->timeline_device[frame->timeline];
	if (device != NULL && device->submitted)
		frame->ticket = device->last;
	if (!stopped) {
		position = &session->frames;
		while (*position != NULL)
			position = &(*position)->next;
		*position = frame;
		session->frame_count++;
	}

	spin_unlock_irqrestore(&session->device->irq_lock, irq);

	/* A stopped session takes no completion. */
	if (stopped) {
		kern_free(frame);
		return EIO;
	}

	/* Succeeded: the service thread completes the frame. */
	ghci_service_wake(session->device, GHCI_WORK_RECLAIM);
	return 0;
}

/* Records the ticket of a device's last submission, which its jobs, frames and stop wait for. */
void
ghci_vk_submitted(
	struct ghci_vk_device *device,
	const struct ghci_ticket *ticket)
{
	struct ghci_vk_session *session;
	unsigned long irq;

	/* The device's and the session's last work. */
	session = device->session;
	irq = spin_lock_irqsave(&session->device->irq_lock);

	device->last = *ticket;
	device->submitted = 1;
	session->last = *ticket;
	session->submitted = 1;

	spin_unlock_irqrestore(&session->device->irq_lock, irq);
}

/*
 * Reports whether a node session still has work or completions the GPU
 * has not answered: 1 when it has.  The caller holds irq_lock.
 */
int
ghci_vk_session_pending(
	struct ghci_vk_session *session)
{
	enum ghci_ticket_state state;
	struct ghci_vk_device *device;

	/* Completions not retired yet, or retired and not yet reported to the core. */
	if (session->jobs.committed != 0U || session->frame_count != 0U || session->completing != 0U)
		return 1;

	/* A device whose last work the GPU has not finished. */
	for (device = session->devices; device != NULL; device = device->next) {
		if (!device->submitted)
			continue;
		state = ghci_cp_ticket_locked(session->device, &device->last);
		if (state == GHCI_TICKET_PENDING)
			return 1;
	}

	/* Succeeded: nothing pending. */
	return 0;
}

/*
 * Ends every committed marker and queued frame of a node session with an
 * error at the next retirement, and isolates the session from new work.
 * The caller holds irq_lock.
 */
void
ghci_vk_session_fail(
	struct ghci_vk_session *session)
{
	struct ghci_vk_frame *frame;

	/* The markers and the frames fail; nothing new is taken. */
	ghci_jobs_fail(&session->jobs);
	for (frame = session->frames; frame != NULL; frame = frame->next)
		frame->failed = 1;
	session->isolated = 1;
}

/*
 * vkQueueSubmit: [queue][u32 count][u64 count][count x VkSubmitInfo]
 * [fence] -> [result].  The submissions' command buffers run in the
 * device's GHCI session, with a fence after them that the VkFence, the
 * jobs and the frames wait for.
 */
static int
queue_submit(
	struct ghci_vk_session *session,
	uint32_t requested,
	struct i915_wire_reader *reader,
	struct i915_wire_writer *reply)
{
	struct ghci_vk_object *queue;
	struct ghci_vk_object *fence;
	struct ghci_vk_device *device;
	struct ghci_vk_submission submission;
	VkSubmitInfo *submits;
	struct ghci_ticket ticket;
	uint64_t handle;
	int recorded;
	int placed;
	uint64_t count;
	uint64_t index;
	uint64_t signal;
	uint32_t result;
	int error;

	/* The queue, the submissions and the fence. */
	handle = drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u32(reader);
	count = ghci_vk_read_count(reader);
	submits = i915_vkc_array(reader, &session->arena, count, sizeof(*submits));
	for (index = 0U; index < count && reader->error == 0; index++)
		i915_vkc_dec_VkSubmitInfo(reader, &session->arena, &submits[index]);
	signal = drv_i915_wire_read_u64(reader);
	if (reader->error != 0)
		return EINVAL;

	/* A queue of the session, and a fence of its device. */
	queue = ghci_vk_object_find(session, GHCI_VK_QUEUE, handle);
	if (queue == NULL)
		return EINVAL;
	device = queue->device;
	fence = NULL;
	if (signal != 0U) {
		fence = ghci_vk_object_find(session, GHCI_VK_FENCE, signal);
		if (fence == NULL || fence->device != device)
			return EINVAL;
	}

	/* The semaphores and the command buffers named, ready to run. */
	error = submission_check(session, submits, count, device, &recorded);
	if (error != 0)
		return error;

	/* The recorded commands made into packets (none for buffers that record nothing). */
	kern_memset(&submission, 0, sizeof(submission));
	if (recorded) {
		error = ghci_vk_execute(session, device, submits, count, &submission);
		if (error != 0) {
			ghci_vk_submission_end(device, &submission, NULL, 0);
			return error;
		}
	}

	/* The work and its fence in the device's session (the queue holds no lock of the device). */
	result = (uint32_t)VK_SUCCESS;
	error = ghci_cp_submit_placed(session->device, device->vm, submission.words, submission.used, &ticket, &placed);
	if (error != 0)
		result = (uint32_t)VK_ERROR_DEVICE_LOST;

	/*
	 * The ticket the fence, the jobs, the frames and the submission's
	 * memory wait for.  A submission placed in part before a recovery
	 * has a ticket too, which the reset ends: what it reads is kept
	 * until then, and it is the device's last work.  One that placed
	 * nothing gives its memory back at once.
	 */
	if (result == (uint32_t)VK_SUCCESS) {
		ghci_vk_submitted(device, &ticket);
		if (fence != NULL) {
			fence->as.fence.signaled = 0;
			fence->as.fence.submitted = 1;
			fence->as.fence.ticket = ticket;
		}
		ghci_vk_submission_end(device, &submission, &ticket, 1);
	} else if (placed) {
		ghci_vk_submitted(device, &ticket);
		ghci_vk_submission_end(device, &submission, &ticket, 1);
	} else {
		ghci_vk_submission_end(device, &submission, NULL, 0);
	}

	/* The result. */
	ghci_vk_reply_result(reply, requested, result);

	/* Succeeded: the work is in the ring, or the reply says the device is lost. */
	return 0;
}

/*
 * Checks the submissions: every array as long as its count, every
 * semaphore the device's, and every command buffer the device's, primary
 * and ended; *recorded says whether one has commands.  Returns 0, or
 * EINVAL for one the session does not have, one not ready or a count
 * that is not its array's.
 */
static int
submission_check(
	struct ghci_vk_session *session,
	const VkSubmitInfo *submits,
	uint64_t count,
	struct ghci_vk_device *device,
	int *recorded)
{
	struct ghci_vk_object *object;
	uint64_t decoded[4];
	uint64_t index;
	uint32_t at;
	uint64_t handle;

	/* Each submission's semaphores and command buffers. */
	*recorded = 0;
	for (index = 0U; index < count; index++) {
		/* Arrays as long as their counts say (an absent one is not read). */
		decoded[0] = ghci_vk_decoded_count(session, submits[index].pWaitSemaphores);
		decoded[1] = ghci_vk_decoded_count(session, submits[index].pWaitDstStageMask);
		decoded[2] = ghci_vk_decoded_count(session, submits[index].pCommandBuffers);
		decoded[3] = ghci_vk_decoded_count(session, submits[index].pSignalSemaphores);
		if (submits[index].pWaitSemaphores != NULL && decoded[0] != submits[index].waitSemaphoreCount)
			return EINVAL;
		if (submits[index].pWaitDstStageMask != NULL && decoded[1] != submits[index].waitSemaphoreCount)
			return EINVAL;
		if (submits[index].pCommandBuffers != NULL && decoded[2] != submits[index].commandBufferCount)
			return EINVAL;
		if (submits[index].pSignalSemaphores != NULL && decoded[3] != submits[index].signalSemaphoreCount)
			return EINVAL;

		/* The semaphores waited for and signaled. */
		for (at = 0U; at < submits[index].waitSemaphoreCount && submits[index].pWaitSemaphores != NULL; at++) {
			handle = (uint64_t)(uintptr_t)submits[index].pWaitSemaphores[at];
			object = ghci_vk_object_find(session, GHCI_VK_SEMAPHORE, handle);
			if (object == NULL || object->device != device)
				return EINVAL;
		}
		for (at = 0U; at < submits[index].signalSemaphoreCount && submits[index].pSignalSemaphores != NULL; at++) {
			handle = (uint64_t)(uintptr_t)submits[index].pSignalSemaphores[at];
			object = ghci_vk_object_find(session, GHCI_VK_SEMAPHORE, handle);
			if (object == NULL || object->device != device)
				return EINVAL;
		}

		/* The command buffers: primary and ended. */
		if (submits[index].commandBufferCount != 0U && submits[index].pCommandBuffers == NULL)
			return EINVAL;
		for (at = 0U; at < submits[index].commandBufferCount; at++) {
			handle = (uint64_t)(uintptr_t)submits[index].pCommandBuffers[at];
			object = ghci_vk_object_find(session, GHCI_VK_COMMAND_BUFFER, handle);
			if (object == NULL || object->device != device)
				return EINVAL;
			if (object->as.commands.state != GHCI_VK_RECORD_EXECUTABLE || object->as.commands.failed)
				return EINVAL;
			if (object->as.commands.level != (uint32_t)VK_COMMAND_BUFFER_LEVEL_PRIMARY)
				return EINVAL;
			if (object->as.commands.commands != 0U)
				*recorded = 1;
		}
	}

	/* Succeeded: the submissions may run. */
	return 0;
}

/*
 * vkCreateFence, vkCreateSemaphore and vkCreateEvent: the create form
 * with [sType][u64 0][u32 flags].  A fence may start signaled; an event
 * takes a word of the device's executor heap, cleared.
 */
static int
create_sync(
	struct ghci_vk_session *session,
	uint32_t opcode,
	uint32_t requested,
	struct i915_wire_reader *reader,
	struct i915_wire_writer *reply)
{
	VkFenceCreateInfo fence_info;
	VkSemaphoreCreateInfo semaphore_info;
	VkEventCreateInfo event_info;
	struct ghci_vk_object *device;
	struct ghci_vk_object *object;
	enum ghci_vk_kind kind;
	struct ghci_vk_heap_block *block;
	uint64_t handle;
	uint64_t present;
	uint64_t identity;
	uint32_t offset;
	uint32_t result;
	int error;

	/* The device and the create info of the kind. */
	kern_memset(&fence_info, 0, sizeof(fence_info));
	handle = drv_i915_wire_read_u64(reader);
	present = drv_i915_wire_read_u64(reader);
	kind = GHCI_VK_EVENT;
	if (opcode == GPU_OP_CREATE_FENCE) {
		kind = GHCI_VK_FENCE;
		i915_vkc_dec_VkFenceCreateInfo(reader, &session->arena, &fence_info);
	} else if (opcode == GPU_OP_CREATE_SEMAPHORE) {
		kind = GHCI_VK_SEMAPHORE;
		i915_vkc_dec_VkSemaphoreCreateInfo(reader, &session->arena, &semaphore_info);
	} else {
		i915_vkc_dec_VkEventCreateInfo(reader, &session->arena, &event_info);
	}
	identity = ghci_vk_create_tail(reader);
	if (reader->error != 0 || present != 1U)
		return EINVAL;
	device = ghci_vk_object_find(session, GHCI_VK_DEVICE, handle);
	if (device == NULL)
		return EINVAL;

	/* An event's word comes first: a heap without room is the reply's. */
	result = (uint32_t)VK_SUCCESS;
	block = NULL;
	offset = 0U;
	if (kind == GHCI_VK_EVENT) {
		error = ghci_vk_heap_take(session, device->device, GHCI_VK_HEAP_DATA, EVENT_BYTES, EVENT_BYTES, &block, &offset);
		if (error == ENOMEM)
			result = (uint32_t)VK_ERROR_OUT_OF_DEVICE_MEMORY;
		else if (error != 0)
			result = (uint32_t)VK_ERROR_DEVICE_LOST;
	}

	/* The object. */
	if (result == (uint32_t)VK_SUCCESS) {
		error = ghci_vk_object_add(session, kind, identity, device->device, &object);
		if (error != 0) {
			ghci_vk_heap_give(block, offset);
			return error;
		}
		if (kind == GHCI_VK_FENCE && (fence_info.flags & VK_FENCE_CREATE_SIGNALED_BIT) != 0U)
			object->as.fence.signaled = 1;
		if (kind == GHCI_VK_EVENT) {
			object->as.event.block = block;
			object->as.event.offset = offset;
			object->as.event.word = (volatile uint64_t *)(void *)(block->allocation->view + offset);
			*object->as.event.word = 0U;
		}
	}

	/* The result, and the identity when it succeeded. */
	ghci_vk_reply_created(reply, requested, result, identity);

	/* Succeeded: the object exists, or the reply says why not. */
	return 0;
}

/* vkDestroyFence, vkDestroySemaphore and vkDestroyEvent: the destroy form, no reply body. */
static int
destroy_sync(
	struct ghci_vk_session *session,
	enum ghci_vk_kind kind,
	struct i915_wire_reader *reader)
{
	struct ghci_vk_object *object;
	uint64_t identity;
	uint64_t device;

	/* The destroy form. */
	identity = ghci_vk_destroy_form(reader, &device);
	if (reader->error != 0)
		return EINVAL;

	/* The object, when the session has it. */
	object = ghci_vk_object_find(session, kind, identity);
	if (object != NULL)
		ghci_vk_object_destroy(session, object);

	/* Succeeded: the object is gone. */
	return 0;
}

/* vkResetFences: [device][u32 count][u64 count][count x fence] -> [result]. */
static int
reset_fences(
	struct ghci_vk_session *session,
	uint32_t requested,
	struct i915_wire_reader *reader,
	struct i915_wire_writer *reply)
{
	struct ghci_vk_object *object;
	const uint8_t *identities;
	uint64_t identity;
	uint64_t count;
	uint64_t index;

	/* The device and the fences. */
	(void)drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u32(reader);
	count = ghci_vk_read_count(reader);
	identities = drv_i915_wire_read_array(reader, (size_t)count, 8U);
	if (reader->error != 0)
		return EINVAL;

	/* Each fence of the session is unsignaled and waits for no submission. */
	for (index = 0U; index < count; index++) {
		kern_memcpy(&identity, identities + index * 8U, 8U);
		object = ghci_vk_object_find(session, GHCI_VK_FENCE, identity);
		if (object == NULL)
			return EINVAL;
		object->as.fence.signaled = 0;
		object->as.fence.submitted = 0;
	}
	ghci_vk_reply_result(reply, requested, (uint32_t)VK_SUCCESS);

	/* Succeeded: the fences are reset. */
	return 0;
}

/*
 * vkGetFenceStatus: [device][fence] -> [result]: VK_SUCCESS signaled,
 * VK_NOT_READY not yet, VK_ERROR_DEVICE_LOST for work a reset ended.
 */
static int
fence_status(
	struct ghci_vk_session *session,
	uint32_t requested,
	struct i915_wire_reader *reader,
	struct i915_wire_writer *reply)
{
	enum ghci_ticket_state state;
	struct ghci_vk_object *object;
	uint64_t identity;
	uint32_t result;

	/* The device and the fence. */
	(void)drv_i915_wire_read_u64(reader);
	identity = drv_i915_wire_read_u64(reader);
	if (reader->error != 0)
		return EINVAL;
	object = ghci_vk_object_find(session, GHCI_VK_FENCE, identity);
	if (object == NULL)
		return EINVAL;

	/* Signaled on the host, by its submission's ticket, or not yet. */
	result = (uint32_t)VK_NOT_READY;
	if (object->as.fence.signaled) {
		result = (uint32_t)VK_SUCCESS;
	} else if (object->as.fence.submitted) {
		state = ghci_cp_ticket(session->device, &object->as.fence.ticket);
		if (state == GHCI_TICKET_DONE) {
			object->as.fence.signaled = 1;
			result = (uint32_t)VK_SUCCESS;
		} else if (state == GHCI_TICKET_FAILED) {
			result = (uint32_t)VK_ERROR_DEVICE_LOST;
		}
	}
	ghci_vk_reply_result(reply, requested, result);

	/* Succeeded: the status is reported. */
	return 0;
}

/*
 * vkGetEventStatus, vkSetEvent and vkResetEvent: [device][event] ->
 * [result], on the event's word.
 */
static int
event_operation(
	struct ghci_vk_session *session,
	uint32_t opcode,
	uint32_t requested,
	struct i915_wire_reader *reader,
	struct i915_wire_writer *reply)
{
	struct ghci_vk_object *object;
	uint64_t identity;
	uint32_t result;

	/* The device and the event. */
	(void)drv_i915_wire_read_u64(reader);
	identity = drv_i915_wire_read_u64(reader);
	if (reader->error != 0)
		return EINVAL;
	object = ghci_vk_object_find(session, GHCI_VK_EVENT, identity);
	if (object == NULL)
		return EINVAL;

	/* The word read, set or cleared. */
	result = (uint32_t)VK_SUCCESS;
	if (opcode == GPU_OP_SET_EVENT) {
		*object->as.event.word = 1U;
	} else if (opcode == GPU_OP_RESET_EVENT) {
		*object->as.event.word = 0U;
	} else if (*object->as.event.word != 0U) {
		result = (uint32_t)VK_EVENT_SET;
	} else {
		result = (uint32_t)VK_EVENT_RESET;
	}
	ghci_vk_reply_result(reply, requested, result);

	/* Succeeded: the event is answered. */
	return 0;
}

/* Answers a marker's ticket from the device's fence and reset generations (irq_lock held). */
static enum ghci_job_answer
judge(
	void *context,
	uint64_t seq,
	uint64_t generation)
{
	enum ghci_ticket_state state;
	struct ghci_ticket ticket;

	/* The ticket's state. */
	ticket.seq = seq;
	ticket.generation = generation;
	state = ghci_cp_ticket_locked(context, &ticket);

	/* Passed, ended by a reset, or not yet. */
	if (state == GHCI_TICKET_DONE)
		return GHCI_JOB_PASSED;
	if (state == GHCI_TICKET_FAILED)
		return GHCI_JOB_LOST;

	/* Succeeded: not yet. */
	return GHCI_JOB_WAITING;
}

/* Retires and reports, batch by batch, every completion of one session that is done; no lock is held. */
static void
retire_session(
	struct ghci_device *device,
	struct ghci_vk_session *session)
{
	struct ghci_vk_session *owners[GHCI_VK_DONE_BATCH];
	struct ghci_vk_frame *gone;
	void *done[GHCI_VK_DONE_BATCH];
	int errors[GHCI_VK_DONE_BATCH];
	unsigned long irq;
	unsigned count;

	/* Batches until a batch is not full. */
	do {
		gone = NULL;
		irq = spin_lock_irqsave(&device->irq_lock);

		count = retire(session, done, errors, owners, GHCI_VK_DONE_BATCH, &gone);

		spin_unlock_irqrestore(&device->irq_lock, irq);
		complete(device, done, errors, owners, count, gone);
	} while (count == GHCI_VK_DONE_BATCH);
}

/*
 * Takes a session's done markers and frames (irq_lock held): their
 * completions and errors to done and errors and the session to owners
 * (at most capacity), and the frames' records to gone, to be freed once
 * the lock is let go.  What is taken counts as the session's completing
 * until complete() has reported it.  Returns the number taken.
 */
static unsigned
retire(
	struct ghci_vk_session *session,
	void **done,
	int *errors,
	struct ghci_vk_session **owners,
	unsigned capacity,
	struct ghci_vk_frame **gone)
{
	enum ghci_ticket_state state;
	struct ghci_vk_frame **position;
	struct ghci_vk_frame *frame;
	uint64_t waiting;
	uint64_t mark;
	unsigned count;
	unsigned index;

	/* The markers. */
	count = ghci_jobs_retire(&session->jobs, judge, session->device, done, errors, capacity);

	/* The frames, in order within a timeline: one that waits keeps its timeline's later ones waiting. */
	waiting = 0U;
	position = &session->frames;
	while (count < capacity && *position != NULL) {
		frame = *position;
		mark = (uint64_t)1U << frame->timeline;
		state = GHCI_TICKET_DONE;
		if (frame->failed)
			state = GHCI_TICKET_FAILED;
		else if ((waiting & mark) != 0U)
			state = GHCI_TICKET_PENDING;
		else if (frame->ticket.seq != 0U)
			state = ghci_cp_ticket_locked(session->device, &frame->ticket);
		if (state == GHCI_TICKET_PENDING) {
			waiting |= mark;
			position = &frame->next;
			continue;
		}

		/* The frame leaves the queue with its answer. */
		*position = frame->next;
		session->frame_count--;
		done[count] = frame->completion;
		errors[count] = 0;
		if (state == GHCI_TICKET_FAILED)
			errors[count] = EIO;
		count++;
		frame->next = *gone;
		*gone = frame;
	}

	/* What was taken is the session's until it is reported. */
	for (index = 0U; index < count; index++)
		owners[index] = session;
	session->completing += count;

	/* Succeeded: the number taken. */
	return count;
}

/*
 * Completes what a retirement took and frees the frames' records; no lock
 * is held.  Each completion leaves its session's completing once the
 * core has heard of it, and the last one wakes a drain; the session is
 * not touched after that.
 */
static void
complete(
	struct ghci_device *device,
	void **done,
	const int *errors,
	struct ghci_vk_session **owners,
	unsigned count,
	struct ghci_vk_frame *gone)
{
	struct ghci_vk_frame *next;
	struct ghci_vk *vk;
	unsigned long irq;
	unsigned index;

	/* The core hears of each completion, and then its session counts it reported. */
	vk = device->vk;
	for (index = 0U; index < count; index++) {
		drv_gpu_complete(done[index], errors[index]);

		irq = spin_lock_irqsave(&device->irq_lock);

		owners[index]->completing--;
		if (owners[index]->completing == 0U)
			waitq_wake_all(&vk->completing_wake);

		spin_unlock_irqrestore(&device->irq_lock, irq);
	}

	/* The frames' records. */
	while (gone != NULL) {
		next = gone->next;
		kern_free(gone);
		gone = next;
	}
}

/*
 * Runs a query pool command; *handled stays clear for another group's
 * number.
 */
int
ghci_vk_query_route(
	struct ghci_vk_session *session,
	uint32_t opcode,
	uint32_t requested,
	struct i915_wire_reader *reader,
	struct i915_wire_writer *reply,
	int *handled)
{
	int error;

	/* Picks the command, or leaves it to another group. */
	*handled = 1;
	switch (opcode) {
	case GPU_OP_CREATE_QUERY_POOL:
		error = create_query_pool(session, requested, reader, reply);
		break;
	case GPU_OP_DESTROY_QUERY_POOL:
		error = destroy_sync(session, GHCI_VK_QUERY_POOL, reader);
		break;
	case GPU_OP_GET_QUERY_POOL_RESULTS:
		error = query_results(session, requested, reader, reply);
		break;
	default:
		*handled = 0;
		return 0;
	}

	/* A command that could not be decoded or run. */
	if (error != 0)
		return error;

	/* Succeeded: the command ran. */
	return 0;
}

/*
 * vkCreateQueryPool: the create form with VkQueryPoolCreateInfo.  An
 * occlusion or timestamp pool takes 16 bytes of the device's data heap
 * per query, cleared (no query is available); a pipeline statistics pool
 * is answered VK_ERROR_FEATURE_NOT_PRESENT (the feature is not
 * reported), and a pool past QUERY_POOL_MAX queries or the heap
 * VK_ERROR_OUT_OF_DEVICE_MEMORY.
 */
static int
create_query_pool(
	struct ghci_vk_session *session,
	uint32_t requested,
	struct i915_wire_reader *reader,
	struct i915_wire_writer *reply)
{
	VkQueryPoolCreateInfo info;
	struct ghci_vk_object *device;
	struct ghci_vk_object *object;
	struct ghci_vk_heap_block *block;
	uint64_t handle;
	uint64_t present;
	uint64_t identity;
	uint32_t offset;
	uint32_t bytes;
	uint32_t result;
	int error;

	/* The device, the create info and the tail. */
	kern_memset(&info, 0, sizeof(info));
	handle = drv_i915_wire_read_u64(reader);
	present = drv_i915_wire_read_u64(reader);
	if (present == 1U)
		i915_vkc_dec_VkQueryPoolCreateInfo(reader, &session->arena, &info);
	identity = ghci_vk_create_tail(reader);
	if (reader->error != 0 || present != 1U)
		return EINVAL;
	device = ghci_vk_object_find(session, GHCI_VK_DEVICE, handle);
	if (device == NULL)
		return EINVAL;

	/* The types the device counts, and the slots in the data heap. */
	result = (uint32_t)VK_SUCCESS;
	block = NULL;
	offset = 0U;
	bytes = 0U;
	if (info.queryType != VK_QUERY_TYPE_OCCLUSION && info.queryType != VK_QUERY_TYPE_TIMESTAMP)
		result = (uint32_t)VK_ERROR_FEATURE_NOT_PRESENT;
	else if (info.queryCount == 0U || info.queryCount > QUERY_POOL_MAX)
		result = (uint32_t)VK_ERROR_OUT_OF_DEVICE_MEMORY;
	if (result == (uint32_t)VK_SUCCESS) {
		bytes = info.queryCount * QUERY_SLOT_BYTES;
		error = ghci_vk_heap_take(session, device->device, GHCI_VK_HEAP_DATA, bytes, QUERY_SLOT_BYTES, &block, &offset);
		if (error == ENOMEM)
			result = (uint32_t)VK_ERROR_OUT_OF_DEVICE_MEMORY;
		else if (error != 0)
			result = (uint32_t)VK_ERROR_DEVICE_LOST;
	}

	/* The object, its slots cleared. */
	if (result == (uint32_t)VK_SUCCESS) {
		error = ghci_vk_object_add(session, GHCI_VK_QUERY_POOL, identity, device->device, &object);
		if (error != 0) {
			ghci_vk_heap_give(block, offset);
			return error;
		}
		object->as.query_pool.type = info.queryType;
		object->as.query_pool.count = info.queryCount;
		object->as.query_pool.block = block;
		object->as.query_pool.offset = offset;
		object->as.query_pool.va = block->mapping->va + offset;
		object->as.query_pool.bus = block->allocation->bus + offset;
		object->as.query_pool.view = block->allocation->view + offset;
		kern_memset(object->as.query_pool.view, 0, bytes);
	}

	/* The result, and the identity when it succeeded. */
	ghci_vk_reply_created(reply, requested, result, identity);

	/* Succeeded: the pool exists, or the reply says why not. */
	return 0;
}

/*
 * vkGetQueryPoolResults: [device][pool][u32 first][u32 count][u64 bytes]
 * [u64 bytes][u64 stride][u32 flags] -> [result][u64 bytes][the records].
 * Each record is the query's result and its availability word, each
 * 4 bytes (8 with VK_QUERY_RESULT_64_BIT), packed at stride; the result is
 * VK_SUCCESS when every query is available and VK_NOT_READY when one is
 * not (libvulkan waits itself).  A shape that is not that is EINVAL.
 */
static int
query_results(
	struct ghci_vk_session *session,
	uint32_t requested,
	struct i915_wire_reader *reader,
	struct i915_wire_writer *reply)
{
	struct ghci_vk_object *object;
	const uint8_t *slot;
	uint64_t identity;
	uint64_t bytes;
	uint64_t stride;
	uint64_t value;
	uint64_t available;
	uint32_t first;
	uint32_t count;
	uint32_t flags;
	uint32_t width;
	uint32_t index;
	uint32_t result;

	/* The device, the pool, the queries and the shape. */
	(void)drv_i915_wire_read_u64(reader);
	identity = drv_i915_wire_read_u64(reader);
	first = drv_i915_wire_read_u32(reader);
	count = drv_i915_wire_read_u32(reader);
	bytes = drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u64(reader);
	stride = drv_i915_wire_read_u64(reader);
	flags = drv_i915_wire_read_u32(reader);
	if (reader->error != 0)
		return EINVAL;
	object = ghci_vk_object_find(session, GHCI_VK_QUERY_POOL, identity);
	if (object == NULL || first > object->as.query_pool.count || count > object->as.query_pool.count - first)
		return EINVAL;
	width = 4U;
	if ((flags & VK_QUERY_RESULT_64_BIT) != 0U)
		width = 8U;
	if (stride != 2U * width || bytes != (uint64_t)count * stride)
		return EINVAL;

	/* Every query available, or not yet. */
	result = (uint32_t)VK_SUCCESS;
	for (index = 0U; index < count; index++) {
		slot = object->as.query_pool.view + (uint64_t)(first + index) * QUERY_SLOT_BYTES;
		kern_memcpy(&available, slot + 8U, 8U);
		if (available == 0U)
			result = (uint32_t)VK_NOT_READY;
	}
	if (requested == 0U)
		return 0;

	/* The result, then each query's record. */
	drv_i915_wire_reply_u32(reply, result);
	drv_i915_wire_reply_u64(reply, bytes);
	for (index = 0U; index < count; index++) {
		slot = object->as.query_pool.view + (uint64_t)(first + index) * QUERY_SLOT_BYTES;
		kern_memcpy(&available, slot + 8U, 8U);

		/* The GPU writes the result before the availability word: the result is read after it. */
		kern_io_read_barrier();

		kern_memcpy(&value, slot, 8U);
		if (available != 0U)
			available = 1U;
		if (width == 8U) {
			drv_i915_wire_reply_u64(reply, value);
			drv_i915_wire_reply_u64(reply, available);
		} else {
			drv_i915_wire_reply_u32(reply, (uint32_t)value);
			drv_i915_wire_reply_u32(reply, (uint32_t)available);
		}
	}

	/* Succeeded: the records are reported. */
	return 0;
}
