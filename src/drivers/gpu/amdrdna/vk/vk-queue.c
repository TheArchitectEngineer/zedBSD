/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The executor's submissions, fences and semaphores.
 *
 * vkQueueSubmit runs its command buffers on the GPU in order and waits
 * for them before it answers (vk-execute.c), so a submission's work is
 * done when its reply is written: its fence is signaled then, and the
 * marker libvulkan reserved for it, queued after this frame, completes
 * after the work.  A semaphore orders nothing more than the one queue's
 * order already does: waits and signals are read and kept as nothing.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <kern/kcrt.h>
#include <kern/klog.h>
#include <kern/kmem.h>
#include <uapi/errno.h>
#include <uapi/gpu-op.h>

#include "drivers/gpu/amdrdna/vk/amdrdna-vk-objects.h"

/* The most submits one vkQueueSubmit carries, and the most command buffers and semaphores of one submit. */
#define QUEUE_SUBMITS_MAX		16U
#define QUEUE_ITEMS_MAX			32U

static int queue_submit(struct amdrdna_vk_session *session, struct i915_wire_reader *reader, struct i915_wire_writer *reply);
static int queue_read_submit(struct i915_wire_reader *reader, uint64_t *commands, uint32_t *command_count);
static int queue_read_identities(struct i915_wire_reader *reader, bool stages);
static uint32_t queue_result(int error);
static int queue_create_sync(struct amdrdna_vk_session *session, enum i915_vk_object_kind kind, struct i915_wire_reader *reader, struct i915_wire_writer *reply);
static int queue_reset_fences(struct amdrdna_vk_session *session, struct i915_wire_reader *reader, struct i915_wire_writer *reply);
static int queue_fence_status(struct amdrdna_vk_session *session, struct i915_wire_reader *reader, struct i915_wire_writer *reply);

/*
 * Runs a submission, fence or semaphore command; *handled stays clear for
 * an opcode of another handler's.
 */
int
amdrdna_vk_queue_route(
	struct amdrdna_vk_session *session,
	uint32_t opcode,
	uint32_t requested,
	struct i915_wire_reader *reader,
	struct i915_wire_writer *reply,
	int *handled)
{
	int error;

	UNUSED_PARAMETER(requested);

	/* Picks the command, or leaves it to another handler. */
	*handled = 1;
	switch (opcode) {
	case GPU_OP_QUEUE_SUBMIT:
		error = queue_submit(session, reader, reply);
		break;
	case GPU_OP_CREATE_FENCE:
		error = queue_create_sync(session, I915_VK_OBJ_FENCE, reader, reply);
		break;
	case GPU_OP_DESTROY_FENCE:
		error = amdrdna_vk_destroy_plain(session, reader, I915_VK_OBJ_FENCE);
		break;
	case GPU_OP_RESET_FENCES:
		error = queue_reset_fences(session, reader, reply);
		break;
	case GPU_OP_GET_FENCE_STATUS:
		error = queue_fence_status(session, reader, reply);
		break;
	case GPU_OP_CREATE_SEMAPHORE:
		error = queue_create_sync(session, I915_VK_OBJ_SEMAPHORE, reader, reply);
		break;
	case GPU_OP_DESTROY_SEMAPHORE:
		error = amdrdna_vk_destroy_plain(session, reader, I915_VK_OBJ_SEMAPHORE);
		break;
	default:
		*handled = 0;
		return 0;
	}

	/* Reports a command that could not be decoded. */
	if (error != 0)
		return error;

	/* Succeeded: the command ran and wrote its reply. */
	return 0;
}

/*
 * vkQueueSubmit: [queue][count][count]{submit}[fence] -> [result].  Every
 * submit is read first; then each command buffer runs in order, and the
 * fence is signaled once all of them did.
 */
static int
queue_submit(
	struct amdrdna_vk_session *session,
	struct i915_wire_reader *reader,
	struct i915_wire_writer *reply)
{
	uint64_t commands[QUEUE_SUBMITS_MAX * QUEUE_ITEMS_MAX];
	struct amdrdna_vk_command_buffer *command;
	struct amdrdna_vk_fence *fence;
	uint32_t command_count;
	uint32_t taken;
	uint64_t submits;
	uint64_t index;
	uint64_t fence_id;
	uint32_t result;
	int error;

	/* The queue and how many submits follow. */
	(void)drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u32(reader);
	submits = drv_i915_wire_read_u64(reader);
	if (reader->error != 0 || submits > QUEUE_SUBMITS_MAX)
		return EINVAL;

	/* Each submit's command buffers, in order. */
	command_count = 0U;
	for (index = 0U; index < submits; index++) {
		error = queue_read_submit(reader, commands + command_count, &taken);
		if (error != 0)
			return error;
		command_count += taken;
	}

	/* The fence. */
	fence_id = drv_i915_wire_read_u64(reader);
	if (reader->error != 0)
		return EINVAL;

	/* Runs each command buffer; the first that fails ends the submission. */
	error = 0;
	for (index = 0U; index < command_count; index++) {
		command = amdrdna_vk_payload(session, I915_VK_OBJ_COMMAND_BUFFER, commands[index]);
		if (command == NULL) {
			error = EINVAL;
			break;
		}

		error = amdrdna_vk_execute(session, command);
		if (error != 0) {
			kern_logf("amdrdna: vk: a command buffer did not run (%d)\n", error);
			break;
		}
	}

	/* The work is done: the fence is signaled. */
	fence = amdrdna_vk_payload(session, I915_VK_OBJ_FENCE, fence_id);
	if (error == 0 && fence != NULL)
		fence->signaled = true;

	/* The submission's result. */
	result = queue_result(error);
	drv_i915_wire_reply_u32(reply, result);

	/* Succeeded: the reply carries the result. */
	return 0;
}

/*
 * Reads one submit: [sType][pNext][wait count][count]{semaphore}[count]
 * {stage}[command count][count]{command buffer}[signal count][count]
 * {semaphore}; the command buffers' identities are kept.
 */
static int
queue_read_submit(
	struct i915_wire_reader *reader,
	uint64_t *commands,
	uint32_t *command_count)
{
	uint64_t count;
	uint64_t index;
	int error;

	/* sType and pNext. */
	*command_count = 0U;
	(void)drv_i915_wire_read_u32(reader);
	(void)drv_i915_wire_read_u64(reader);

	/* The waits with their stages. */
	error = queue_read_identities(reader, true);
	if (error != 0)
		return error;

	/* The command buffers. */
	(void)drv_i915_wire_read_u32(reader);
	count = drv_i915_wire_read_u64(reader);
	if (reader->error != 0 || count > QUEUE_ITEMS_MAX)
		return EINVAL;
	for (index = 0U; index < count; index++)
		commands[index] = drv_i915_wire_read_u64(reader);
	*command_count = (uint32_t)count;

	/* The signals. */
	error = queue_read_identities(reader, false);
	if (error != 0)
		return error;

	/* Succeeded: the submit is read. */
	return 0;
}

/* Reads a semaphore list [count][count]{identity}, and with stages its [count]{stage}. */
static int
queue_read_identities(
	struct i915_wire_reader *reader,
	bool stages)
{
	uint64_t count;
	uint64_t index;

	/* The semaphores. */
	(void)drv_i915_wire_read_u32(reader);
	count = drv_i915_wire_read_u64(reader);
	if (reader->error != 0 || count > QUEUE_ITEMS_MAX)
		return EINVAL;
	for (index = 0U; index < count; index++)
		(void)drv_i915_wire_read_u64(reader);

	/* Their stages. */
	if (stages) {
		count = drv_i915_wire_read_u64(reader);
		if (reader->error != 0 || count > QUEUE_ITEMS_MAX)
			return EINVAL;
		for (index = 0U; index < count; index++)
			(void)drv_i915_wire_read_u32(reader);
	}
	if (reader->error != 0)
		return EINVAL;

	/* Succeeded: the list is read. */
	return 0;
}

/* Reports a submission's VkResult: a run that did not finish loses the device. */
static uint32_t
queue_result(
	int error)
{
	uint32_t result;

	/* The GPU that did not finish its work is lost. */
	if (error == EIO || error == ETIMEDOUT)
		return (uint32_t)VK_ERROR_DEVICE_LOST;

	/* Any other error as the executor maps it. */
	result = amdrdna_vk_result(error);

	/* Succeeded: the result. */
	return result;
}

/*
 * vkCreateFence and vkCreateSemaphore: [device][present][sType][pNext]
 * [flags] and the create's tail -> the create's reply.  A fence created
 * signaled starts signaled.
 */
static int
queue_create_sync(
	struct amdrdna_vk_session *session,
	enum i915_vk_object_kind kind,
	struct i915_wire_reader *reader,
	struct i915_wire_writer *reply)
{
	struct amdrdna_vk_fence *fence;
	uint64_t identity;
	uint32_t flags;

	/* The device, the presence word, sType, pNext and the flags, then the tail. */
	(void)drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u32(reader);
	(void)drv_i915_wire_read_u64(reader);
	flags = drv_i915_wire_read_u32(reader);
	identity = amdrdna_vk_create_tail(reader);
	if (reader->error != 0)
		return EINVAL;

	/* The record: a fence's state, a semaphore's nothing but itself. */
	fence = kern_calloc(1U, sizeof(*fence));
	if (fence != NULL && kind == I915_VK_OBJ_FENCE && (flags & VK_FENCE_CREATE_SIGNALED_BIT) != 0U)
		fence->signaled = true;

	/* Publishes it and answers. */
	amdrdna_vk_create_reply(session, reply, kind, identity, fence, amdrdna_vk_free_payload, 0);

	/* Succeeded: the reply carries the create's result. */
	return 0;
}

/* vkResetFences: [device][count][count]{fence} -> [result]. */
static int
queue_reset_fences(
	struct amdrdna_vk_session *session,
	struct i915_wire_reader *reader,
	struct i915_wire_writer *reply)
{
	struct amdrdna_vk_fence *fence;
	uint64_t count;
	uint64_t index;
	uint64_t identity;

	/* How many fences follow. */
	(void)drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u32(reader);
	count = drv_i915_wire_read_u64(reader);
	if (reader->error != 0 || count > QUEUE_ITEMS_MAX * QUEUE_SUBMITS_MAX)
		return EINVAL;

	/* Each known fence is unsignaled. */
	for (index = 0U; index < count; index++) {
		identity = drv_i915_wire_read_u64(reader);
		fence = amdrdna_vk_payload(session, I915_VK_OBJ_FENCE, identity);
		if (fence != NULL)
			fence->signaled = false;
	}
	if (reader->error != 0)
		return EINVAL;

	/* Success. */
	drv_i915_wire_reply_u32(reply, (uint32_t)VK_SUCCESS);

	/* Succeeded: the reply carries the result. */
	return 0;
}

/* vkGetFenceStatus: [device][fence] -> [VK_SUCCESS or VK_NOT_READY]. */
static int
queue_fence_status(
	struct amdrdna_vk_session *session,
	struct i915_wire_reader *reader,
	struct i915_wire_writer *reply)
{
	const struct amdrdna_vk_fence *fence;
	uint64_t identity;
	uint32_t result;

	/* The fence behind the device. */
	(void)drv_i915_wire_read_u64(reader);
	identity = drv_i915_wire_read_u64(reader);
	if (reader->error != 0)
		return EINVAL;

	/* Signaled, or not yet. */
	result = (uint32_t)VK_NOT_READY;
	fence = amdrdna_vk_payload(session, I915_VK_OBJ_FENCE, identity);
	if (fence != NULL && fence->signaled)
		result = (uint32_t)VK_SUCCESS;
	drv_i915_wire_reply_u32(reply, result);

	/* Succeeded: the reply carries the status. */
	return 0;
}
