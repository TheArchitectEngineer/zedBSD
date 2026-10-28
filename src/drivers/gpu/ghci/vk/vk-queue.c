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
 * the GPU passes their tickets.
 *
 * One ring is the one queue, so the submissions run in the order they
 * are placed: a semaphore needs no wait of its own, and a fence or a job
 * is done when the ticket of the last submission before it is.  A
 * submission places the executed command buffers in the device's GHCI
 * session and a fence after them; one whose command buffers record
 * nothing places the fence alone.  An event is a word of the device's
 * executor heap, which the host sets and resets here.
 */

#include <stddef.h>
#include <stdint.h>

#include <drivers/gpu/gpu.h>
#include <kern/kcrt.h>
#include <kern/kmem.h>
#include <uapi/errno.h>
#include <uapi/gpu-op.h>

#include "vk.h"

#include "drivers/gpu/i915/vk/vk-codec.inc"

/* How long one wait of the drain for a session's last ticket lasts before it looks again. */
#define DRAIN_WAIT_MS			100U

/* The bytes and the alignment of an event's word. */
#define EVENT_BYTES			8U

static int queue_submit(struct ghci_vk_session *session, uint32_t requested, struct i915_wire_reader *reader, struct i915_wire_writer *reply);
static int submission_check(struct ghci_vk_session *session, const VkSubmitInfo *submits, uint64_t count, struct ghci_vk_device *device);
static int create_sync(struct ghci_vk_session *session, uint32_t opcode, uint32_t requested, struct i915_wire_reader *reader, struct i915_wire_writer *reply);
static int destroy_sync(struct ghci_vk_session *session, enum ghci_vk_kind kind, struct i915_wire_reader *reader);
static int reset_fences(struct ghci_vk_session *session, uint32_t requested, struct i915_wire_reader *reader, struct i915_wire_writer *reply);
static int fence_status(struct ghci_vk_session *session, uint32_t requested, struct i915_wire_reader *reader, struct i915_wire_writer *reply);
static int event_operation(struct ghci_vk_session *session, uint32_t opcode, uint32_t requested, struct i915_wire_reader *reader, struct i915_wire_writer *reply);
static enum ghci_job_answer judge(void *context, uint64_t seq, uint64_t generation);
static unsigned retire(struct ghci_vk_session *session, void **done, int *errors, unsigned capacity, struct ghci_vk_frame **gone);
static void complete(void **done, const int *errors, unsigned count, struct ghci_vk_frame *gone);

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
			count += retire(session, done + count, errors + count, GHCI_VK_DONE_BATCH - count, &gone);

		spin_unlock_irqrestore(&device->irq_lock, irq);

		/* The core hears of each. */
		complete(done, errors, count, gone);
	} while (count == GHCI_VK_DONE_BATCH);
}

/*
 * Ends every completion of a node session before it closes: the session
 * retires its own completions as their tickets are answered, waiting a
 * bounded time on its last ticket between looks.  A ticket that never
 * passes is ended by the recovery (a hang, a reset, the loss of the
 * device), so the drain ends.  The caller holds none of the device's
 * locks.
 */
void
ghci_vk_drain(
	struct ghci_device *device,
	struct ghci_vk_session *session)
{
	struct ghci_vk_frame *gone;
	struct ghci_ticket last;
	void *done[GHCI_VK_DONE_BATCH];
	int errors[GHCI_VK_DONE_BATCH];
	unsigned long irq;
	unsigned count;
	int pending;
	int submitted;

	/* Looks until nothing of the session is pending. */
	for (;;) {
		/* The session's completions that are done, and what is left. */
		gone = NULL;
		irq = spin_lock_irqsave(&device->irq_lock);

		count = retire(session, done, errors, GHCI_VK_DONE_BATCH, &gone);
		pending = session->jobs.committed != 0U || session->frame_count != 0U;
		last = session->last;
		submitted = session->submitted;

		spin_unlock_irqrestore(&device->irq_lock, irq);
		complete(done, errors, count, gone);

		/* Nothing left, or more done than a batch: look again at once. */
		if (!pending)
			return;
		if (count == GHCI_VK_DONE_BATCH)
			continue;

		/* Waits for the session's last work; a failed marker or frame needs no wait. */
		if (submitted)
			(void)ghci_cp_wait(device, &last, DRAIN_WAIT_MS);
		else
			ghci_sleep_ms(DRAIN_WAIT_MS);
	}
}

/*
 * Queues a frame's completion behind the work the session placed until
 * now (the work of the timeline's device for a context fence of a
 * timeline).  Returns 0, ENOMEM, or EIO for a session the GPU core
 * stopped or isolated.
 */
int
ghci_vk_frame_queue(
	struct ghci_vk_session *session,
	uint32_t timeline,
	struct drv_gpu_completion *completion)
{
	struct ghci_vk_frame *frame;
	struct ghci_vk_frame **position;
	struct ghci_vk_device *device;
	unsigned long irq;
	int stopped;

	/* The record. */
	frame = kern_calloc(1U, sizeof(*frame));
	if (frame == NULL)
		return ENOMEM;
	frame->completion = completion;

	/* The ticket it waits for, and the frame last in the session's queue. */
	irq = spin_lock_irqsave(&session->device->irq_lock);

	stopped = session->stopping || session->isolated;
	device = NULL;
	if (timeline < GHCI_VK_TIMELINES)
		device = session->timeline_device[timeline];
	if (device != NULL && device->submitted)
		frame->ticket = device->last;
	else if (device == NULL && session->submitted)
		frame->ticket = session->last;
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

	/* Completions not retired yet. */
	if (session->jobs.committed != 0U || session->frame_count != 0U)
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
	VkSubmitInfo *submits;
	struct ghci_ticket ticket;
	uint64_t handle;
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
	error = submission_check(session, submits, count, device);
	if (error != 0)
		return error;

	/* The work and its fence in the device's session (the queue holds no lock of the device). */
	result = (uint32_t)VK_SUCCESS;
	error = ghci_cp_submit(session->device, device->vm, NULL, 0U, &ticket);
	if (error != 0)
		result = (uint32_t)VK_ERROR_DEVICE_LOST;

	/* The ticket the fence, the jobs and the frames wait for. */
	if (result == (uint32_t)VK_SUCCESS) {
		ghci_vk_submitted(device, &ticket);
		if (fence != NULL) {
			fence->as.fence.signaled = 0;
			fence->as.fence.submitted = 1;
			fence->as.fence.ticket = ticket;
		}
	}

	/* The result. */
	ghci_vk_reply_result(reply, requested, result);

	/* Succeeded: the work is in the ring, or the reply says the device is lost. */
	return 0;
}

/*
 * Checks the submissions: every semaphore is the device's, and every
 * command buffer the device's and ended.  Returns 0, EINVAL for one the
 * session does not have or one not ready, or ENOTSUP for a command
 * buffer with recorded commands, which the executor does not execute
 * yet.
 */
static int
submission_check(
	struct ghci_vk_session *session,
	const VkSubmitInfo *submits,
	uint64_t count,
	struct ghci_vk_device *device)
{
	struct ghci_vk_object *object;
	uint64_t index;
	uint32_t at;
	uint64_t handle;

	/* Each submission's semaphores and command buffers. */
	for (index = 0U; index < count; index++) {
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

		/* The command buffers: ended, and (for now) empty. */
		for (at = 0U; at < submits[index].commandBufferCount && submits[index].pCommandBuffers != NULL; at++) {
			handle = (uint64_t)(uintptr_t)submits[index].pCommandBuffers[at];
			object = ghci_vk_object_find(session, GHCI_VK_COMMAND_BUFFER, handle);
			if (object == NULL || object->device != device)
				return EINVAL;
			if (object->as.commands.state != GHCI_VK_RECORD_EXECUTABLE)
				return EINVAL;
			if (object->as.commands.commands != 0U)
				return ENOTSUP;
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
		error = ghci_vk_heap_take(session, device->device, EVENT_BYTES, EVENT_BYTES, &block, &offset);
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

/*
 * Takes a session's done markers and frames (irq_lock held): their
 * completions and errors to done and errors (at most capacity), and the
 * frames' records to gone, to be freed once the lock is let go.  Returns
 * the number taken.
 */
static unsigned
retire(
	struct ghci_vk_session *session,
	void **done,
	int *errors,
	unsigned capacity,
	struct ghci_vk_frame **gone)
{
	enum ghci_ticket_state state;
	struct ghci_vk_frame *frame;
	unsigned count;

	/* The markers. */
	count = ghci_jobs_retire(&session->jobs, judge, session->device, done, errors, capacity);

	/* The frames, in order: one that waits keeps the later ones waiting. */
	while (count < capacity && session->frames != NULL) {
		frame = session->frames;
		state = GHCI_TICKET_DONE;
		if (frame->failed)
			state = GHCI_TICKET_FAILED;
		else if (frame->ticket.seq != 0U)
			state = ghci_cp_ticket_locked(session->device, &frame->ticket);
		if (state == GHCI_TICKET_PENDING)
			break;

		/* The frame leaves the queue with its answer. */
		session->frames = frame->next;
		session->frame_count--;
		done[count] = frame->completion;
		errors[count] = 0;
		if (state == GHCI_TICKET_FAILED)
			errors[count] = EIO;
		count++;
		frame->next = *gone;
		*gone = frame;
	}

	/* Succeeded: the number taken. */
	return count;
}

/* Completes what a retirement took and frees the frames' records; no lock is held. */
static void
complete(
	void **done,
	const int *errors,
	unsigned count,
	struct ghci_vk_frame *gone)
{
	struct ghci_vk_frame *next;
	unsigned index;

	/* The core hears of each completion. */
	for (index = 0U; index < count; index++)
		drv_gpu_complete(done[index], errors[index]);

	/* The frames' records. */
	while (gone != NULL) {
		next = gone->next;
		kern_free(gone);
		gone = next;
	}
}
