/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The executor's engine: what the Vulkan executor keeps for the whole
 * device.
 *
 * The firmware keeps its objects and the recordings of the command
 * buffers in a heap the driver lends it (GHCI_HEAP) after every start of
 * the command processor.  The frames of every session go through one
 * staging ring and are completed in ring order: the service thread looks
 * at the oldest frame's result, fills in the replies that waited for it,
 * stores the trailer libvulkan polls, gives the staging back and
 * completes the frame's callback.  A reset ends every frame in flight
 * with an error and every session's firmware objects with them.
 */

#include <stddef.h>
#include <stdint.h>

#include <drivers/gpu/gpu.h>
#include <kern/device-io.h>
#include <kern/kcrt.h>
#include <kern/klog.h>
#include <kern/kmem.h>
#include <uapi/errno.h>

#include "../hcr.h"
#include "../packets.h"
#include "../frame.h"
#include "vk.h"
#include "vk-engine.h"

/* The dwords of a control packet before its arguments: the header, the gpu-op, the flags, the result. */
#define CONTROL_HEAD_DWORDS		4U

/* The most argument dwords a control packet carries. */
#define CONTROL_ARGUMENTS_MAX		8U

/* The completions one round of the service collects before it hands them to the core. */
#define SERVICE_BATCH			32U

/* The words of a result record: the VkResult and its aux. */
#define RESULT_WORDS			2U

/* The version the trailer reports: libvulkan's decoder needs Vulkan 1.1. */
#define TRAILER_VERSION			((1U << 22) | (1U << 12))

static int engine_heap(struct ghci_device *device);
static int frame_ready(struct ghci_device *device, struct ghci_vk_frame *frame);
static void frame_finish(struct ghci_device *device, struct ghci_vk_frame *frame, int error);
static void frame_patch(struct ghci_device *device, struct ghci_vk_frame *frame, int32_t frame_status);
static void frame_trailer(struct ghci_vk_frame *frame);
static unsigned retire_jobs(struct ghci_device *device, struct drv_gpu_completion **done, int *errors, unsigned capacity);
static void complete_all(struct drv_gpu_completion **done, const int *errors, unsigned count);
static void store32(uint8_t *bytes, uint32_t offset, uint32_t value);

/*
 * Makes the executor of a device whose command processor has firmware
 * and whose GPU has a shader unit.
 */
int
ghci_vk_engine_start(
	struct ghci_device *device)
{
	struct ghci_vk_engine *engine;
	uint32_t resident;
	int error;

	/* A command processor without firmware decodes no gpu-op: such a GPU has no Vulkan device. */
	if ((device->caps & GHCI_CAPS_FIRMWARE) == 0U)
		return 0;

	/* Nor has a GPU without a shader unit, or one whose programs the driver cannot build. */
	if ((device->caps & GHCI_CAPS_SHADER) == 0U)
		return 0;
	if (device->isa == NULL)
		return 0;

	/* The engine's record. */
	engine = kern_calloc(1U, sizeof(*engine));
	if (engine == NULL)
		return ENOMEM;

	/*
	 * The warps the GPU runs at once, which the firmware sizes the
	 * programs' scratch by, and the registers of a lane, which the
	 * compiler allocates within (0 on a GPU that does not say: 32).
	 */
	resident = ghci_read(device, GHCI_RESIDENT_WARPS);
	engine->warps = GHCI_RESIDENT_WARPS_COUNT(resident);
	engine->registers = GHCI_RESIDENT_WARPS_NREGS(resident);

	/* The heap, the control page and the staging ring: memory of the device's own. */
	mutex_lock(&device->lock);

	error = ghci_memory_alloc(device, NULL, GHCI_USE_DRIVER, GHCI_VK_HEAP_BYTES, &engine->heap);
	if (error == 0)
		error = ghci_memory_alloc(device, NULL, GHCI_USE_DRIVER, GHCI_PAGE_BYTES, &engine->control);
	if (error == 0)
		error = ghci_memory_alloc(device, NULL, GHCI_USE_DRIVER, GHCI_VK_STAGING_BYTES, &engine->staging);

	/* The formats the GPU takes, then the heap the firmware needs before the first session opens. */
	if (error == 0) {
		device->vk = engine;
		ghci_vk_format_load(device);
		error = engine_heap(device);
	}

	/* An executor that cannot start leaves the device without one. */
	if (error != 0) {
		device->vk = NULL;
		if (engine->heap != NULL)
			ghci_memory_free(device, engine->heap);
		if (engine->control != NULL)
			ghci_memory_free(device, engine->control);
		if (engine->staging != NULL)
			ghci_memory_free(device, engine->staging);
	}

	mutex_unlock(&device->lock);

	/* Reports why the device has no executor. */
	if (error != 0) {
		kern_free(engine);
		return error;
	}

	/* Succeeded: the device's sessions may open Vulkan devices. */
	return 0;
}

/*
 * Gives the executor's memory back at the detach.
 */
void
ghci_vk_engine_stop(
	struct ghci_device *device)
{
	struct ghci_vk_engine *engine;

	/* A device without an executor has nothing here. */
	engine = device->vk;
	if (engine == NULL)
		return;

	/* The ring has stopped, so the GPU uses none of it any more. */
	mutex_lock(&device->lock);

	device->vk = NULL;
	ghci_memory_free(device, engine->heap);
	ghci_memory_free(device, engine->control);
	ghci_memory_free(device, engine->staging);

	mutex_unlock(&device->lock);

	/* The record. */
	kern_free(engine);
}

/*
 * Sends one GHCI gpu-op as a class 2 packet and waits for its result.
 */
int
ghci_vk_engine_control(
	struct ghci_device *device,
	struct ghci_session *owner,
	uint32_t opcode,
	const uint32_t *arguments,
	uint32_t count,
	int32_t *status)
{
	uint32_t packet[CONTROL_HEAD_DWORDS + CONTROL_ARGUMENTS_MAX];
	struct ghci_ticket ticket;
	volatile uint32_t *result;
	uint32_t tag;
	uint32_t index;
	int error;

	/* A few words of arguments only. */
	if (count > CONTROL_ARGUMENTS_MAX)
		return EINVAL;

	/* The result record starts pending, so a packet the firmware dropped is seen. */
	result = (volatile uint32_t *)device->vk->control->view;
	result[0] = GHCI_VK_RESULT_PENDING;
	result[1] = 0U;
	kern_io_write_barrier();

	/* The packet: the session's tag (0 for the driver's own), the gpu-op, a result, the arguments. */
	tag = GHCI_TAG_DRIVER;
	if (owner != NULL)
		tag = owner->tag;
	packet[0] = GHCI_PKT_V(tag, CONTROL_HEAD_DWORDS - 1U + count);
	packet[1] = opcode;
	packet[2] = GHCI_V_RESULT;
	packet[3] = device->vk->control->gpu_address;
	for (index = 0U; index < count; index++)
		packet[CONTROL_HEAD_DWORDS + index] = arguments[index];

	/* Into the ring with the fence after it. */
	error = ghci_cp_submit(device, owner, packet, CONTROL_HEAD_DWORDS + count, &ticket);
	if (error != 0)
		return error;

	/* The fence after the packet passes once the firmware is done with it. */
	error = ghci_cp_wait(device, &ticket, GHCI_FENCE_WAIT_MS);
	if (error != 0)
		return error;

	/* The firmware's answer; none at all is the firmware's failure. */
	kern_io_read_barrier();
	if (result[0] == GHCI_VK_RESULT_PENDING)
		return EIO;

	/* Succeeded: the firmware's VkResult. */
	*status = (int32_t)result[0];
	return 0;
}

/*
 * Takes bytes of the staging ring for a frame.
 */
int
ghci_vk_staging_take(
	struct ghci_device *device,
	uint32_t bytes,
	uint32_t *offset,
	uint32_t *consumed)
{
	struct ghci_vk_engine *engine;
	uint32_t need;
	uint32_t skip;
	uint32_t start;

	/* Whole lines of 64 bytes, inside what one ring holds. */
	engine = device->vk;
	need = (bytes + 63U) & ~63U;
	if (need > GHCI_VK_STAGING_BYTES)
		return E2BIG;

	/* An empty ring starts again from its beginning. */
	if (engine->used == 0U)
		engine->head = 0U;

	/* A run that would pass the end starts at the beginning and skips the end. */
	start = engine->head;
	skip = 0U;
	if (need > GHCI_VK_STAGING_BYTES - start) {
		skip = GHCI_VK_STAGING_BYTES - start;
		start = 0U;
	}

	/* Refuses a run the frames in flight still hold. */
	if (need + skip > GHCI_VK_STAGING_BYTES - engine->used)
		return EAGAIN;

	/* The run is the frame's from here. */
	engine->head = start + need;
	if (engine->head == GHCI_VK_STAGING_BYTES)
		engine->head = 0U;
	engine->used += need + skip;

	/* Succeeded: the run and what it used up. */
	*offset = start;
	*consumed = need + skip;
	return 0;
}

/*
 * Puts a submitted frame at the end of the device's list.
 */
void
ghci_vk_frame_queue(
	struct ghci_device *device,
	struct ghci_vk_frame *frame)
{
	struct ghci_vk_engine *engine;

	/* The frame is the newest of the list, and one more of its session's in flight. */
	engine = device->vk;
	frame->next = NULL;
	if (engine->frames_last == NULL) {
		engine->frames = frame;
	} else {
		engine->frames_last->next = frame;
	}

	/* It is the last one now. */
	engine->frames_last = frame;
	frame->session->pending++;
}

/*
 * Reports whether the device's executor takes no new frame.
 */
int
ghci_vk_engine_faulted(
	struct ghci_device *device)
{
	/* Reports the core's fault. */
	return device->vk->faulted;
}

/*
 * Does the executor's part of a round of the service thread.
 */
void
ghci_vk_service(
	struct ghci_device *device)
{
	struct drv_gpu_completion *done[SERVICE_BATCH];
	int errors[SERVICE_BATCH];
	struct ghci_vk_frame *frame;
	unsigned count;
	int ready;

	/* A device without an executor has no frame. */
	if (device->vk == NULL)
		return;

	/* Rounds until every frame whose results are in and every passed marker is completed. */
	for (;;) {
		count = 0U;
		mutex_lock(&device->lock);

		/* The frames in ring order, as long as the oldest is done. */
		while (count < SERVICE_BATCH && device->vk->frames != NULL) {
			frame = device->vk->frames;
			ready = frame_ready(device, frame);
			if (!ready)
				break;

			/* Its replies, its trailer and its callback; then it leaves the list. */
			done[count] = frame->completion;
			errors[count] = 0;
			count++;
			frame_finish(device, frame, 0);
		}

		/* The job markers whose fences passed. */
		count += retire_jobs(device, done + count, errors + count, SERVICE_BATCH - count);

		mutex_unlock(&device->lock);

		/* The core hears of them outside the lock. */
		complete_all(done, errors, count);

		/* A round that did not fill its batch left nothing behind. */
		if (count < SERVICE_BATCH)
			break;
	}
}

/*
 * Ends everything of the firmware's after a reset of the command processor.
 */
void
ghci_vk_reset(
	struct ghci_device *device)
{
	struct drv_gpu_completion *done[SERVICE_BATCH];
	int errors[SERVICE_BATCH];
	struct ghci_vk_engine *engine;
	struct ghci_vk_frame *frame;
	struct ghci_session *owner;
	unsigned count;
	int error;

	/* A device without an executor has nothing of the firmware's. */
	engine = device->vk;
	if (engine == NULL)
		return;

	/* Every frame in flight ends with an error, a batch at a time. */
	for (;;) {
		count = 0U;
		mutex_lock(&device->lock);

		/* The oldest frames, without their replies: the firmware will not write their results. */
		while (count < SERVICE_BATCH && engine->frames != NULL) {
			frame = engine->frames;
			done[count] = frame->completion;
			errors[count] = EIO;
			count++;
			frame_finish(device, frame, EIO);
		}

		mutex_unlock(&device->lock);

		/* The core hears of them outside the lock; a batch that was not full was the last. */
		complete_all(done, errors, count);
		if (count < SERVICE_BATCH)
			break;
	}

	/*
	 * The firmware lost every session's objects: each session is lost
	 * (its later frames fail, its job markers end with an error) and the
	 * core hears of it.  The heap is lent again for the sessions opened
	 * after this.
	 */
	mutex_lock(&device->lock);

	/* Each open of the device. */
	for (owner = device->sessions;
	     owner != NULL;
	     owner = owner->next) {
		/* An open without an executor lost nothing here. */
		if (owner->vk == NULL)
			continue;

		owner->vk->lost = 1;
		ghci_jobs_fail(&owner->vk->jobs);
		if (device->gpu != NULL)
			drv_gpu_report_session_error(device->gpu, owner, EIO);
	}

	engine->heap_ready = 0;
	error = engine_heap(device);
	if (error != 0)
		kern_logf("ghci: %s: the firmware did not take its heap after the reset (%d)\n", device->name, error);

	mutex_unlock(&device->lock);

	/* The failed markers complete with the next round of the service. */
	ghci_service_wake(device, GHCI_WORK_REPLY);
}

/* Lends the firmware its heap: GHCI_HEAP [u64 address][u64 bytes][u32 warps].  The device's lock is held. */
static int
engine_heap(
	struct ghci_device *device)
{
	struct ghci_vk_engine *engine;
	uint32_t arguments[5];
	int32_t status;
	int error;

	/* The heap's place and size, and the warps the GPU runs at once. */
	engine = device->vk;
	arguments[0] = engine->heap->gpu_address;
	arguments[1] = 0U;
	arguments[2] = engine->heap->bytes;
	arguments[3] = 0U;
	arguments[4] = engine->warps;
	error = ghci_vk_engine_control(device, NULL, GPU_OP_GHCI_HEAP, arguments, 5U, &status);
	if (error != 0)
		return error;

	/* A firmware that refuses the heap cannot keep a session's objects. */
	if (status != 0) {
		kern_logf("ghci: %s: the firmware refused its heap (%d)\n", device->name, (int)status);
		return EIO;
	}

	/* Succeeded: sessions may open. */
	engine->heap_ready = 1;
	return 0;
}

/* Reports whether a frame is done: its results are in and the fence it waits for has passed. */
static int
frame_ready(
	struct ghci_device *device,
	struct ghci_vk_frame *frame)
{
	uint32_t status;
	int passed;

	/* A frame with records waits for the firmware's frame result after the records' results. */
	if (frame->records != 0U) {
		status = frame->page[RESULT_WORDS * frame->records];
		if (status == GHCI_VK_RESULT_PENDING)
			return 0;
	}

	/* A frame that waits for the session's work waits for its fence too. */
	if (frame->wait_fence) {
		passed = ghci_cp_done(device, frame->ticket.fence, frame->ticket.generation);
		if (!passed)
			return 0;
	}

	/* Succeeded: the frame may complete. */
	return 1;
}

/*
 * Finishes the oldest frame: fills its replies and stores its trailer
 * (unless it failed), lets its reply blob go, gives its staging back and
 * frees it.  The device's lock is held; the caller completes the callback.
 */
static void
frame_finish(
	struct ghci_device *device,
	struct ghci_vk_frame *frame,
	int error)
{
	struct ghci_vk_engine *engine;
	int32_t frame_status;

	/* The frame leaves the head of the list. */
	engine = device->vk;
	engine->frames = frame->next;
	if (engine->frames == NULL)
		engine->frames_last = NULL;

	/* The results go into the replies, then the trailer tells libvulkan they are complete. */
	if (error == 0) {
		frame_status = 0;
		if (frame->records != 0U) {
			kern_io_read_barrier();
			frame_status = (int32_t)frame->page[RESULT_WORDS * frame->records];
		}

		/* The replies' words, then the trailer. */
		frame_patch(device, frame, frame_status);
		frame_trailer(frame);
	}

	/* The reply blob may go back, and the staging is free again. */
	if (frame->reply_blob != NULL)
		ghci_memory_unhold(device, frame->reply_blob);
	engine->used -= frame->staging_bytes;
	frame->session->pending--;

	/* The record. */
	kern_free(frame->patches);
	kern_free(frame);
}

/*
 * Fills what the frame's replies wait for: each VkResult word from its
 * record's result, and the addresses the firmware gave the events and the
 * query pools it made.  A record the firmware never reached (it stopped
 * the frame earlier) reports the frame's own result.
 */
static void
frame_patch(
	struct ghci_device *device,
	struct ghci_vk_frame *frame,
	int32_t frame_status)
{
	struct ghci_vk_patch *patch;
	struct ghci_vk_object *object;
	enum ghci_vk_kind kind;
	uint32_t status;
	uint32_t aux;
	unsigned index;

	/* Each patch in the order it was made. */
	for (index = 0U; index < frame->patch_count; index++) {
		patch = &frame->patches[index];

		/* A query copy reads the pool's words now that the work before it has finished. */
		if (patch->kind == GHCI_VK_PATCH_QUERY_RESULTS) {
			if (frame->reply_blob != NULL)
				ghci_vk_query_copy(device, frame->session, patch, frame->reply_blob->view);
			continue;
		}

		/* The record's result. */
		status = frame->page[RESULT_WORDS * patch->index];
		aux = frame->page[RESULT_WORDS * patch->index + 1U];

		/* A record the firmware did not decode takes the frame's failure. */
		if (status == GHCI_VK_RESULT_PENDING)
			status = (uint32_t)frame_status;

		/* A VkResult word of a reply. */
		if (patch->kind == GHCI_VK_PATCH_RESULT) {
			if (frame->reply_blob != NULL)
				store32(frame->reply_blob->view, patch->offset, status);
			continue;
		}

		/* An event's or a query pool's words, which the host reads and writes through the heap. */
		kind = GHCI_VK_EVENT;
		if (patch->kind == GHCI_VK_PATCH_QUERY_POOL)
			kind = GHCI_VK_QUERY_POOL;
		object = ghci_vk_object_find(frame->session, kind, patch->identity);
		if (object != NULL && status == 0U) {
			object->as.heap.address = aux;
			object->as.heap.known = 1;
		}
	}
}

/*
 * Stores the trailer: [VkResult 0][u64 1] after the echoed number, then
 * the version, released after every earlier reply so libvulkan sees them
 * all once it sees it.
 */
static void
frame_trailer(
	struct ghci_vk_frame *frame)
{
	uint8_t *view;
	uint32_t *version;

	/* A frame that did not ask for the trailer has none. */
	if (!frame->trailer)
		return;
	if (frame->reply_blob == NULL)
		return;

	/* The result and the presence word. */
	view = frame->reply_blob->view;
	store32(view, frame->trailer_offset, 0U);
	store32(view, frame->trailer_offset + 4U, 1U);
	store32(view, frame->trailer_offset + 8U, 0U);

	/* The version, last (the reply region is word aligned; the GPU is little endian). */
	version = (uint32_t *)(void *)(view + frame->trailer_offset + 12U);
	__atomic_store_n(version, TRAILER_VERSION, __ATOMIC_RELEASE);
}

/* Frees every job marker of every session whose fence passed.  The device's lock is held. */
static unsigned
retire_jobs(
	struct ghci_device *device,
	struct drv_gpu_completion **done,
	int *errors,
	unsigned capacity)
{
	struct ghci_session *owner;
	uint32_t completed;
	unsigned count;
	unsigned taken;

	/* The fence the GPU has completed, read afresh. */
	completed = ghci_cp_completed(device);

	/* Each session's markers, while the caller has room. */
	count = 0U;
	for (owner = device->sessions;
	     owner != NULL;
	     owner = owner->next) {
		/* Stops once the batch is full; the next round takes the rest. */
		if (count == capacity)
			break;

		/* An open without an executor has no markers. */
		if (owner->vk == NULL)
			continue;

		/* Its markers whose fence passed, and its failed ones. */
		taken = ghci_jobs_retire(&owner->vk->jobs, completed, (void **)(done + count), errors + count, capacity - count);
		count += taken;
	}

	/* Reports the completions collected. */
	return count;
}

/* Hands each collected completion to the core, with its error. */
static void
complete_all(
	struct drv_gpu_completion **done,
	const int *errors,
	unsigned count)
{
	unsigned index;

	/* One at a time, in the order they were collected. */
	for (index = 0U; index < count; index++)
		drv_gpu_complete(done[index], errors[index]);
}

/* Stores a 32-bit word into bytes at an offset, the lowest byte first. */
static void
store32(
	uint8_t *bytes,
	uint32_t offset,
	uint32_t value)
{
	/* One byte at a time, whatever the alignment. */
	bytes[offset] = (uint8_t)value;
	bytes[offset + 1U] = (uint8_t)(value >> 8);
	bytes[offset + 2U] = (uint8_t)(value >> 16);
	bytes[offset + 3U] = (uint8_t)(value >> 24);
}
