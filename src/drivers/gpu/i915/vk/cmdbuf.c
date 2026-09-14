/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Command buffer recording: turns vkCmd* into a Gen12 3D batch.
 *
 * Recording assembles the pipeline state and the 3DPRIMITIVE draw into the
 * command buffer's batch.  Submitting the batch on RCS0 reuses the WS029 request
 * path; that wiring is completed during the on-hardware bring-up, so this module
 * fixes the recorded command stream, which the host fixture checks.
 */

#include "vk-internal.h"
#include "cmdbuf.h"
#include "pipe.h"
#include "cmd.h"
#include "sync.h"

#include "../internal.h"

#include <kern/kmem.h>
#include <kern/pmem.h>
#include <kern/device-io.h>

#include <errno.h>

#include "linux/3dstate-gen12.inc"

/* MI_BATCH_BUFFER_END terminates a batch (MI opcode 0x0A, transcribed). */
#define GEN12_MI_BATCH_BUFFER_END	0x05000000U

/* A command buffer records into one batch and remembers the bound pipeline. */
struct i915_vk_cmdbuf {
	struct i915_vk_session *session;
	struct i915_vk_batch batch;
	struct i915_vk_pipeline *pipeline;
};

static void i915_vk_cmdbuf_put(struct i915_vk_batch *batch, uint32_t dword);

/* A command pool owns the session its command buffers allocate against. */
struct i915_vk_cmdpool {
	struct i915_vk_session *session;
};

/* Every command buffer records into a batch buffer of this size. */
#define I915_VK_CMDBUF_BATCH_BYTES	65536U

/* The most command buffers one vkAllocateCommandBuffers request creates. */
#define I915_VK_CMDBUF_ALLOC_MAX	64U

/* Maps a driver errno to the VkResult a command reply carries. */
static uint32_t
i915_vk_cmdbuf_result(
	int error)
{
	if (error == 0)
		return 0U;			/* VK_SUCCESS */
	if (error == ENOMEM)
		return (uint32_t)(-2);		/* VK_ERROR_OUT_OF_DEVICE_MEMORY */
	return (uint32_t)(-3);			/* VK_ERROR_INITIALIZATION_FAILED */
}

/* Allocates one command buffer with a GEM-backed, session-bound batch buffer. */
static int
i915_vk_cmdbuf_object_create(
	struct i915_vk_session *session,
	struct i915_vk_cmdbuf **out)
{
	struct i915_device *device;
	struct i915_vk_cmdbuf *cmdbuf;
	int error;

	*out = NULL;
	device = session->vk->i915;

	cmdbuf = kern_calloc(1U, sizeof(*cmdbuf));
	if (cmdbuf == NULL)
		return ENOMEM;
	cmdbuf->session = session;

	/* The batch buffer is a GEM object bound into the session address space. */
	mutex_lock(&device->mutex);
	error = drv_i915_gem_create(device, I915_VK_CMDBUF_BATCH_BYTES, &cmdbuf->batch.object);
	if (error != 0) {
		mutex_unlock(&device->mutex);
		kern_free(cmdbuf);
		return error;
	}
	error = drv_i915_gem_bind_vm(session->gpu->vm, cmdbuf->batch.object);
	if (error != 0) {
		drv_i915_gem_destroy(device, cmdbuf->batch.object);
		mutex_unlock(&device->mutex);
		kern_free(cmdbuf);
		return error;
	}
	mutex_unlock(&device->mutex);

	/* Managed RAM is direct-mapped, so recording writes through the kernel view. */
	cmdbuf->batch.map = kern_pmem_to_kernel(cmdbuf->batch.object->run.paddr);
	cmdbuf->batch.capacity = I915_VK_CMDBUF_BATCH_BYTES / 4U;
	cmdbuf->batch.cursor = 0U;
	cmdbuf->batch.error = 0;

	*out = cmdbuf;
	return 0;
}

/* Releases a command buffer and its batch buffer. */
static void
i915_vk_cmdbuf_object_destroy(
	struct i915_vk_cmdbuf *cmdbuf)
{
	struct i915_device *device;

	device = cmdbuf->session->vk->i915;
	mutex_lock(&device->mutex);
	if (cmdbuf->batch.object != NULL)
		drv_i915_gem_destroy(device, cmdbuf->batch.object);
	mutex_unlock(&device->mutex);
	kern_free(cmdbuf);
}

/*
 * vkCreateCommandPool: [device][pCreateInfo present] VkCommandPoolCreateInfo
 * [pAllocator present][pPool present][pool wire_id].  VkCommandPoolCreateInfo is
 * [sType][pNext present][flags][queueFamilyIndex].  The reply is result then,
 * on success, present and identity.
 */
static int
i915_vk_cmdbuf_create_pool(
	struct i915_vk_session *session,
	struct i915_vk_reader *reader,
	struct i915_vk_writer *reply)
{
	struct i915_vk_cmdpool *pool;
	i915_vk_handle handle;
	uint32_t stype;
	int error;

	(void)i915_vk_read_handle(reader);		/* device */
	(void)i915_vk_read_u64(reader);			/* pCreateInfo present */
	stype = i915_vk_read_u32(reader);		/* VkStructureType */
	(void)i915_vk_read_u64(reader);			/* pNext present */
	(void)i915_vk_read_u32(reader);			/* flags */
	(void)i915_vk_read_u32(reader);			/* queueFamilyIndex */
	(void)i915_vk_read_u64(reader);			/* pAllocator present */
	(void)i915_vk_read_u64(reader);			/* pPool present */
	handle = i915_vk_read_handle(reader);
	if (reader->error != 0)
		return EINVAL;

	/* VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO is the only accepted head. */
	if (stype != 39U)
		return EINVAL;

	pool = kern_calloc(1U, sizeof(*pool));
	if (pool == NULL) {
		error = ENOMEM;
	} else {
		pool->session = session;
		error = i915_vk_obj_insert(session->vk, I915_VK_OBJ_COMMAND_POOL, handle, pool);
		if (error != 0)
			kern_free(pool);
	}

	i915_vk_reply_u32(reply, i915_vk_cmdbuf_result(error));
	if (error == 0) {
		i915_vk_reply_u64(reply, 1U);
		i915_vk_reply_u64(reply, handle);
	}
	return 0;
}

/*
 * vkDestroyCommandPool: [device][pool][pAllocator present].  It returns void, so
 * the reply is the echoed opcode alone.  Command buffers are freed explicitly.
 */
static int
i915_vk_cmdbuf_destroy_pool(
	struct i915_vk_session *session,
	struct i915_vk_reader *reader,
	struct i915_vk_writer *reply)
{
	struct i915_vk_cmdpool *pool;
	i915_vk_handle handle;

	(void)i915_vk_read_handle(reader);		/* device */
	handle = i915_vk_read_handle(reader);
	(void)i915_vk_read_u64(reader);			/* pAllocator present */
	if (reader->error != 0)
		return EINVAL;

	pool = i915_vk_obj_lookup(session->vk, I915_VK_OBJ_COMMAND_POOL, handle);
	if (pool != NULL) {
		i915_vk_obj_remove(session->vk, I915_VK_OBJ_COMMAND_POOL, handle);
		kern_free(pool);
	}

	(void)reply;
	return 0;
}

/*
 * vkResetCommandPool: [device][pool][flags].  The reply is the VkResult alone;
 * a baseline pool has no cross-buffer state to release.
 */
static int
i915_vk_cmdbuf_reset_pool(
	struct i915_vk_session *session,
	struct i915_vk_reader *reader,
	struct i915_vk_writer *reply)
{
	(void)i915_vk_read_handle(reader);		/* device */
	(void)i915_vk_read_handle(reader);		/* pool */
	(void)i915_vk_read_u32(reader);			/* flags */
	if (reader->error != 0)
		return EINVAL;

	(void)session;
	i915_vk_reply_u32(reply, 0U);			/* VK_SUCCESS */
	return 0;
}

/*
 * vkAllocateCommandBuffers: [device][pAllocateInfo present]
 * VkCommandBufferAllocateInfo [count][count x buffer wire_id].
 * VkCommandBufferAllocateInfo is [sType][pNext present][commandPool][level][count].
 * The reply is result, the returned count, then each buffer identity.
 */
static int
i915_vk_cmdbuf_allocate(
	struct i915_vk_session *session,
	struct i915_vk_reader *reader,
	struct i915_vk_writer *reply)
{
	struct i915_vk_cmdbuf *created[I915_VK_CMDBUF_ALLOC_MAX];
	i915_vk_handle handles[I915_VK_CMDBUF_ALLOC_MAX];
	uint64_t count;
	uint64_t index;
	uint32_t stype;
	int error;

	(void)i915_vk_read_handle(reader);		/* device */
	(void)i915_vk_read_u64(reader);			/* pAllocateInfo present */
	stype = i915_vk_read_u32(reader);		/* VkStructureType */
	(void)i915_vk_read_u64(reader);			/* pNext present */
	(void)i915_vk_read_handle(reader);		/* commandPool */
	(void)i915_vk_read_u32(reader);			/* level */
	(void)i915_vk_read_u32(reader);			/* commandBufferCount */
	count = i915_vk_read_u64(reader);		/* payload count */
	if (reader->error != 0 || count > I915_VK_CMDBUF_ALLOC_MAX)
		return EINVAL;
	for (index = 0U; index < count; index++)
		handles[index] = i915_vk_read_handle(reader);
	if (reader->error != 0)
		return EINVAL;

	/* VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO is the only accepted head. */
	if (stype != 40U)
		return EINVAL;

	/* Every requested buffer is created; any failure rolls the batch back. */
	error = 0;
	for (index = 0U; index < count; index++) {
		error = i915_vk_cmdbuf_object_create(session, &created[index]);
		if (error != 0)
			break;
		error = i915_vk_obj_insert(session->vk, I915_VK_OBJ_COMMAND_BUFFER, handles[index], created[index]);
		if (error != 0) {
			i915_vk_cmdbuf_object_destroy(created[index]);
			break;
		}
	}
	if (error != 0) {
		uint64_t undo;

		for (undo = 0U; undo < index; undo++) {
			i915_vk_obj_remove(session->vk, I915_VK_OBJ_COMMAND_BUFFER, handles[undo]);
			i915_vk_cmdbuf_object_destroy(created[undo]);
		}
		i915_vk_reply_u32(reply, i915_vk_cmdbuf_result(error));
		return 0;
	}

	/* The reply echoes the requested count and each allocated identity. */
	i915_vk_reply_u32(reply, 0U);			/* VK_SUCCESS */
	i915_vk_reply_u64(reply, count);
	for (index = 0U; index < count; index++)
		i915_vk_reply_u64(reply, handles[index]);
	return 0;
}

/*
 * vkFreeCommandBuffers: [device][pool][count32][count][count x buffer].
 * It returns void, so the reply is the echoed opcode alone.
 */
static int
i915_vk_cmdbuf_free(
	struct i915_vk_session *session,
	struct i915_vk_reader *reader,
	struct i915_vk_writer *reply)
{
	struct i915_vk_cmdbuf *cmdbuf;
	uint64_t count;
	uint64_t index;
	i915_vk_handle handle;

	(void)i915_vk_read_handle(reader);		/* device */
	(void)i915_vk_read_handle(reader);		/* pool */
	(void)i915_vk_read_u32(reader);			/* commandBufferCount */
	count = i915_vk_read_u64(reader);		/* payload count */
	if (reader->error != 0 || count > I915_VK_CMDBUF_ALLOC_MAX)
		return EINVAL;

	for (index = 0U; index < count; index++) {
		handle = i915_vk_read_handle(reader);
		cmdbuf = i915_vk_obj_lookup(session->vk, I915_VK_OBJ_COMMAND_BUFFER, handle);
		if (cmdbuf != NULL) {
			i915_vk_obj_remove(session->vk, I915_VK_OBJ_COMMAND_BUFFER, handle);
			i915_vk_cmdbuf_object_destroy(cmdbuf);
		}
	}
	if (reader->error != 0)
		return EINVAL;

	(void)reply;
	return 0;
}

/*
 * vkBeginCommandBuffer: [command buffer][pBeginInfo present] VkCommandBufferBeginInfo.
 * The recording state is emptied; the reply is the VkResult alone.
 */
static int
i915_vk_cmdbuf_begin_command(
	struct i915_vk_session *session,
	struct i915_vk_reader *reader,
	struct i915_vk_writer *reply)
{
	struct i915_vk_cmdbuf *cmdbuf;
	i915_vk_handle handle;
	uint64_t inheritance;
	int error;

	handle = i915_vk_read_handle(reader);		/* command buffer */
	(void)i915_vk_read_u64(reader);			/* pBeginInfo present */
	(void)i915_vk_read_u32(reader);			/* sType */
	(void)i915_vk_read_u64(reader);			/* pNext present */
	(void)i915_vk_read_u32(reader);			/* flags */
	inheritance = i915_vk_read_u64(reader);		/* pInheritanceInfo count */
	if (reader->error != 0 || inheritance != 0U)	/* primary command buffers only */
		return EINVAL;

	cmdbuf = i915_vk_obj_lookup(session->vk, I915_VK_OBJ_COMMAND_BUFFER, handle);
	if (cmdbuf == NULL)
		error = EINVAL;
	else
		error = i915_vk_cmdbuf_begin(cmdbuf);

	i915_vk_reply_u32(reply, i915_vk_cmdbuf_result(error));
	return 0;
}

/* The most vertex-buffer bindings one record decodes. */
#define I915_VK_CMDBUF_BIND_MAX		64U

/*
 * vkEndCommandBuffer: [command buffer].  Terminates the batch and, as the one
 * reply-bearing record in a recording stream, returns the VkResult.
 */
static int
i915_vk_cmdbuf_end_command(
	struct i915_vk_session *session,
	struct i915_vk_reader *reader,
	struct i915_vk_writer *reply)
{
	struct i915_vk_cmdbuf *cmdbuf;
	i915_vk_handle handle;
	int error;

	handle = i915_vk_read_handle(reader);		/* command buffer */
	if (reader->error != 0)
		return EINVAL;

	cmdbuf = i915_vk_obj_lookup(session->vk, I915_VK_OBJ_COMMAND_BUFFER, handle);
	if (cmdbuf == NULL)
		error = EINVAL;
	else
		error = i915_vk_cmdbuf_end(cmdbuf);

	i915_vk_reply_u32(reply, i915_vk_cmdbuf_result(error));
	return 0;
}

/*
 * vkCmdBindPipeline: [command buffer][bindPoint][pipeline].  A recording command
 * carries no reply; it records the pipeline the following draws use.
 */
static int
i915_vk_cmdbuf_record_bind_pipeline(
	struct i915_vk_session *session,
	struct i915_vk_reader *reader,
	struct i915_vk_writer *reply)
{
	struct i915_vk_cmdbuf *cmdbuf;
	struct i915_vk_pipeline *pipeline;
	i915_vk_handle cmd_handle;
	i915_vk_handle pipeline_handle;

	cmd_handle = i915_vk_read_handle(reader);
	(void)i915_vk_read_u32(reader);			/* pipelineBindPoint */
	pipeline_handle = i915_vk_read_handle(reader);
	if (reader->error != 0)
		return EINVAL;

	cmdbuf = i915_vk_obj_lookup(session->vk, I915_VK_OBJ_COMMAND_BUFFER, cmd_handle);
	pipeline = i915_vk_obj_lookup(session->vk, I915_VK_OBJ_PIPELINE, pipeline_handle);
	if (cmdbuf != NULL && pipeline != NULL)
		(void)i915_vk_cmd_bind_pipeline(cmdbuf, pipeline);

	(void)reply;
	return 0;
}

/*
 * vkCmdBindVertexBuffers: [command buffer][firstBinding][bindingCount]
 * [count][count x buffer][count][count x offset].  The fetch state lands later;
 * the record is consumed to keep the stream aligned.
 */
static int
i915_vk_cmdbuf_record_bind_vertex(
	struct i915_vk_session *session,
	struct i915_vk_reader *reader,
	struct i915_vk_writer *reply)
{
	struct i915_vk_cmdbuf *cmdbuf;
	uint64_t count;
	uint64_t index;
	i915_vk_handle handle;

	handle = i915_vk_read_handle(reader);
	(void)i915_vk_read_u32(reader);			/* firstBinding */
	(void)i915_vk_read_u32(reader);			/* bindingCount */
	count = i915_vk_read_u64(reader);		/* buffer count */
	if (reader->error != 0 || count > I915_VK_CMDBUF_BIND_MAX)
		return EINVAL;
	for (index = 0U; index < count; index++)
		(void)i915_vk_read_handle(reader);	/* pBuffers */
	count = i915_vk_read_u64(reader);		/* offset count */
	if (reader->error != 0 || count > I915_VK_CMDBUF_BIND_MAX)
		return EINVAL;
	for (index = 0U; index < count; index++)
		(void)i915_vk_read_u64(reader);		/* pOffsets */
	if (reader->error != 0)
		return EINVAL;

	cmdbuf = i915_vk_obj_lookup(session->vk, I915_VK_OBJ_COMMAND_BUFFER, handle);
	if (cmdbuf != NULL)
		(void)i915_vk_cmd_bind_vertex_buffers(cmdbuf, 0U, 0U, NULL, NULL);

	(void)reply;
	return 0;
}

/*
 * vkCmdDraw: [command buffer][vertexCount][instanceCount][firstVertex]
 * [firstInstance].  Records the bound pipeline state and one 3DPRIMITIVE.
 */
static int
i915_vk_cmdbuf_record_draw(
	struct i915_vk_session *session,
	struct i915_vk_reader *reader,
	struct i915_vk_writer *reply)
{
	struct i915_vk_cmdbuf *cmdbuf;
	i915_vk_handle handle;
	uint32_t vertex_count;
	uint32_t instance_count;
	uint32_t first_vertex;
	uint32_t first_instance;

	handle = i915_vk_read_handle(reader);
	vertex_count = i915_vk_read_u32(reader);
	instance_count = i915_vk_read_u32(reader);
	first_vertex = i915_vk_read_u32(reader);
	first_instance = i915_vk_read_u32(reader);
	if (reader->error != 0)
		return EINVAL;

	cmdbuf = i915_vk_obj_lookup(session->vk, I915_VK_OBJ_COMMAND_BUFFER, handle);
	if (cmdbuf != NULL)
		(void)i915_vk_cmd_draw(cmdbuf, vertex_count, instance_count, first_vertex, first_instance);

	(void)reply;
	return 0;
}

/*
 * vkCmdEndRenderPass: [command buffer].  A recording command with no reply.
 */
static int
i915_vk_cmdbuf_record_end_render_pass(
	struct i915_vk_session *session,
	struct i915_vk_reader *reader,
	struct i915_vk_writer *reply)
{
	struct i915_vk_cmdbuf *cmdbuf;
	i915_vk_handle handle;

	handle = i915_vk_read_handle(reader);
	if (reader->error != 0)
		return EINVAL;

	cmdbuf = i915_vk_obj_lookup(session->vk, I915_VK_OBJ_COMMAND_BUFFER, handle);
	if (cmdbuf != NULL)
		(void)i915_vk_cmd_end_render_pass(cmdbuf);

	(void)reply;
	return 0;
}

/*
 * vkQueueSubmit: [queue][submitCount][count][count x VkSubmitInfo][fence].
 * Each VkSubmitInfo is [sType][pNext][waits][cmdCount][count][cmds][signals].
 * The recorded batches run on RCS0 and the fence is armed to the last breadcrumb.
 */
static int
i915_vk_cmdbuf_queue_submit(
	struct i915_vk_session *session,
	struct i915_vk_reader *reader,
	struct i915_vk_writer *reply)
{
	struct i915_vk_cmdbuf *cmdbufs[I915_VK_CMDBUF_ALLOC_MAX];
	struct i915_vk_cmdbuf *cmdbuf;
	struct i915_vk_fence *fence;
	uint64_t submit_count;
	uint64_t submit;
	uint64_t element;
	uint64_t buffers;
	i915_vk_handle fence_handle;
	i915_vk_handle handle;
	uint32_t waits;
	uint32_t signals;
	uint32_t total;
	int error;

	(void)i915_vk_read_handle(reader);		/* queue */
	(void)i915_vk_read_u32(reader);			/* submitCount */
	submit_count = i915_vk_read_u64(reader);
	if (reader->error != 0 || submit_count > 8U)
		return EINVAL;

	total = 0U;
	for (submit = 0U; submit < submit_count; submit++) {
		(void)i915_vk_read_u32(reader);		/* sType */
		(void)i915_vk_read_u64(reader);		/* pNext present */

		/* Wait semaphores: count, then handles, then per-wait stage masks. */
		waits = i915_vk_read_u32(reader);
		(void)i915_vk_read_u64(reader);
		for (element = 0U; element < waits; element++)
			(void)i915_vk_read_u64(reader);
		(void)i915_vk_read_u64(reader);
		for (element = 0U; element < waits; element++)
			(void)i915_vk_read_u32(reader);

		/* Command buffers: each names a recorded batch to run. */
		(void)i915_vk_read_u32(reader);		/* commandBufferCount */
		buffers = i915_vk_read_u64(reader);
		if (reader->error != 0 || buffers > I915_VK_CMDBUF_ALLOC_MAX)
			return EINVAL;
		for (element = 0U; element < buffers; element++) {
			handle = i915_vk_read_handle(reader);
			cmdbuf = i915_vk_obj_lookup(session->vk, I915_VK_OBJ_COMMAND_BUFFER, handle);
			if (cmdbuf != NULL && total < I915_VK_CMDBUF_ALLOC_MAX)
				cmdbufs[total++] = cmdbuf;
		}

		/* Signal semaphores: single-queue serial order needs no hardware wait. */
		signals = i915_vk_read_u32(reader);
		(void)i915_vk_read_u64(reader);
		for (element = 0U; element < signals; element++)
			(void)i915_vk_read_u64(reader);
	}

	fence_handle = i915_vk_read_handle(reader);	/* fence */
	if (reader->error != 0)
		return EINVAL;
	fence = i915_vk_obj_lookup(session->vk, I915_VK_OBJ_FENCE, fence_handle);

	error = i915_vk_queue_submit(session, cmdbufs, total, fence);
	i915_vk_reply_u32(reply, i915_vk_cmdbuf_result(error));
	return 0;
}

/* Routes command pool/buffer, vkCmd* and queue submit opcodes. */
int
i915_vk_cmdbuf_dispatch(
	struct i915_vk_session *session,
	uint32_t opcode,
	struct i915_vk_reader *reader,
	struct i915_vk_writer *reply)
{
	/*
	 * The command-buffer object lifetime and vkBeginCommandBuffer are top-level
	 * commands (p011 increment B).  The vkCmd* recording and vkEndCommandBuffer
	 * arrive through vkExecuteCommandStreamsMESA at submit time and land in the
	 * command-stream increment.
	 */
	switch (opcode) {
	case 85U:	/* vkCreateCommandPool */
		return i915_vk_cmdbuf_create_pool(session, reader, reply);
	case 86U:	/* vkDestroyCommandPool */
		return i915_vk_cmdbuf_destroy_pool(session, reader, reply);
	case 87U:	/* vkResetCommandPool */
		return i915_vk_cmdbuf_reset_pool(session, reader, reply);
	case 88U:	/* vkAllocateCommandBuffers */
		return i915_vk_cmdbuf_allocate(session, reader, reply);
	case 89U:	/* vkFreeCommandBuffers */
		return i915_vk_cmdbuf_free(session, reader, reply);
	case 18U:	/* vkQueueSubmit */
		return i915_vk_cmdbuf_queue_submit(session, reader, reply);
	case 90U:	/* vkBeginCommandBuffer */
		return i915_vk_cmdbuf_begin_command(session, reader, reply);
	case 91U:	/* vkEndCommandBuffer */
		return i915_vk_cmdbuf_end_command(session, reader, reply);
	case 93U:	/* vkCmdBindPipeline */
		return i915_vk_cmdbuf_record_bind_pipeline(session, reader, reply);
	case 105U:	/* vkCmdBindVertexBuffers */
		return i915_vk_cmdbuf_record_bind_vertex(session, reader, reply);
	case 106U:	/* vkCmdDraw */
		return i915_vk_cmdbuf_record_draw(session, reader, reply);
	case 135U:	/* vkCmdEndRenderPass */
		return i915_vk_cmdbuf_record_end_render_pass(session, reader, reply);
	default:
		return EINVAL;
	}
}

/* Begins recording, emptying the batch. */
int
i915_vk_cmdbuf_begin(
	struct i915_vk_cmdbuf *cmdbuf)
{
	cmdbuf->batch.cursor = 0U;
	cmdbuf->batch.error = 0;
	cmdbuf->pipeline = NULL;
	return 0;
}

/* Ends recording, terminating the batch. */
int
i915_vk_cmdbuf_end(
	struct i915_vk_cmdbuf *cmdbuf)
{
	i915_vk_cmdbuf_put(&cmdbuf->batch, GEN12_MI_BATCH_BUFFER_END);
	if (cmdbuf->batch.error != 0)
		return ENOSPC;
	return 0;
}

/* Records the pipeline to draw with. */
int
i915_vk_cmd_bind_pipeline(
	struct i915_vk_cmdbuf *cmdbuf,
	struct i915_vk_pipeline *pipeline)
{
	cmdbuf->pipeline = pipeline;
	return 0;
}

/* Records vertex buffer bindings; the vertex fetch state lands at bring-up. */
int
i915_vk_cmd_bind_vertex_buffers(
	struct i915_vk_cmdbuf *cmdbuf,
	uint32_t first,
	uint32_t count,
	struct i915_vk_buffer *const *buffers,
	const uint64_t *offsets)
{
	(void)cmdbuf;
	(void)first;
	(void)count;
	(void)buffers;
	(void)offsets;
	return 0;
}

/* Records descriptor set bindings; the binding table pointer lands at bring-up. */
int
i915_vk_cmd_bind_descriptor_sets(
	struct i915_vk_cmdbuf *cmdbuf,
	uint32_t first,
	uint32_t count,
	struct i915_vk_dset *const *sets)
{
	(void)cmdbuf;
	(void)first;
	(void)count;
	(void)sets;
	return 0;
}

/* Records push constants. */
int
i915_vk_cmd_push_constants(
	struct i915_vk_cmdbuf *cmdbuf,
	uint32_t offset,
	uint32_t size,
	const void *values)
{
	(void)cmdbuf;
	(void)offset;
	(void)size;
	(void)values;
	return 0;
}

/* Begins a render pass; the attachment clear lands at bring-up. */
int
i915_vk_cmd_begin_render_pass(
	struct i915_vk_cmdbuf *cmdbuf,
	struct i915_vk_image_view *color,
	struct i915_vk_image_view *depth,
	const float clear_color[4])
{
	(void)cmdbuf;
	(void)color;
	(void)depth;
	(void)clear_color;
	return 0;
}

/* Ends a render pass. */
int
i915_vk_cmd_end_render_pass(
	struct i915_vk_cmdbuf *cmdbuf)
{
	(void)cmdbuf;
	return 0;
}

/* Records a draw: the pipeline state and one 3DPRIMITIVE. */
int
i915_vk_cmd_draw(
	struct i915_vk_cmdbuf *cmdbuf,
	uint32_t vertex_count,
	uint32_t instance_count,
	uint32_t first_vertex,
	uint32_t first_instance)
{
	/* A draw needs a pipeline bound before it. */
	if (cmdbuf->pipeline == NULL)
		return EINVAL;

	/* The base state and the pipeline precede the primitive. */
	i915_vk_pipe_emit_base(cmdbuf->session, &cmdbuf->batch);
	i915_vk_pipeline_emit(cmdbuf->pipeline, &cmdbuf->batch);

	/* 3DPRIMITIVE issues the vertices for the bound pipeline. */
	i915_vk_cmdbuf_put(&cmdbuf->batch, GEN12_CMD_HEADER(GEN12_CMD_3DPRIMITIVE, GEN12_3DPRIMITIVE_DWORDS));
	i915_vk_cmdbuf_put(&cmdbuf->batch, 0U);
	i915_vk_cmdbuf_put(&cmdbuf->batch, vertex_count);
	i915_vk_cmdbuf_put(&cmdbuf->batch, first_vertex);
	i915_vk_cmdbuf_put(&cmdbuf->batch, instance_count);
	i915_vk_cmdbuf_put(&cmdbuf->batch, first_instance);
	i915_vk_cmdbuf_put(&cmdbuf->batch, 0U);

	if (cmdbuf->batch.error != 0)
		return ENOSPC;
	return 0;
}

/* Submits recorded command buffers on RCS0; the request wiring lands at bring-up. */
int
i915_vk_queue_submit(
	struct i915_vk_session *session,
	struct i915_vk_cmdbuf *const *cmdbufs,
	uint32_t count,
	struct i915_vk_fence *fence)
{
	struct i915_device *device;
	struct i915_session *gpu;
	struct i915_engine *engine;
	struct i915_request *request;
	unsigned long irq;
	uint32_t index;
	uint32_t last_seqno;
	int error;

	/* Nothing to run still counts as an accepted, immediately complete submit. */
	if (count == 0U)
		return 0;

	device = session->vk->i915;
	gpu = session->gpu;
	engine = &device->engines[I915_ENGINE_RCS0];

	/* Publish every recorded batch dword before the engine can parse it. */
	kern_io_write_barrier();

	request = NULL;
	last_seqno = 0U;
	error = 0;
	irq = spin_lock_irqsave(&device->irq_lock);

	/* Each command buffer runs its own batch in the session's RCS0 context. */
	for (index = 0U; index < count; index++) {
		error = drv_i915_request_alloc(engine, gpu, NULL, &request);
		if (error != 0)
			break;
		request->context = &gpu->contexts[engine->index];
		request->batch = cmdbufs[index]->batch.object;
		request->batch_va = cmdbufs[index]->batch.object->va;
		drv_i915_request_queue(engine, request);
	}

	/* A fence is armed to the last accepted breadcrumb so waits observe completion. */
	if (error == 0) {
		drv_i915_request_kick(engine);

		/* The seqno is assigned when kick emits the request into the ring. */
		last_seqno = request != NULL ? request->seqno : 0U;
		if (fence != NULL)
			(void)i915_vk_fence_arm(fence, I915_ENGINE_RCS0, last_seqno);
	}

	spin_unlock_irqrestore(&device->irq_lock, irq);
	return error;
}

/* Appends one dword to the batch, latching overflow. */
static void
i915_vk_cmdbuf_put(
	struct i915_vk_batch *batch,
	uint32_t dword)
{
	if (batch->error != 0)
		return;
	if (batch->cursor >= batch->capacity) {
		batch->error = 1;
		return;
	}

	batch->map[batch->cursor] = dword;
	batch->cursor++;
}
