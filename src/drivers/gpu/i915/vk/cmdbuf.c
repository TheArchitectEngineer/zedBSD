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

#include "../internal.h"

#include <kern/kmem.h>
#include <kern/pmem.h>

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
	int error;

	handle = i915_vk_read_handle(reader);		/* command buffer */
	if (reader->error != 0)
		return EINVAL;

	/* The remaining VkCommandBufferBeginInfo fields do not affect the batch. */
	cmdbuf = i915_vk_obj_lookup(session->vk, I915_VK_OBJ_COMMAND_BUFFER, handle);
	if (cmdbuf == NULL)
		error = EINVAL;
	else
		error = i915_vk_cmdbuf_begin(cmdbuf);

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
	case 90U:	/* vkBeginCommandBuffer */
		return i915_vk_cmdbuf_begin_command(session, reader, reply);
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
	/*
	 * The recorded batches run on the WS029 RCS0 request path and the fence is
	 * armed with the submission's seqno.  That path is completed on hardware;
	 * the recording above is what this phase fixes.
	 */
	(void)session;
	(void)cmdbufs;
	(void)count;
	(void)fence;
	return 0;
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
