/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Command pools, command buffers, recording and vkQueueSubmit (see
 * command.h).
 *
 * Every command is decoded exactly as libvulkan encodes it: the records
 * through the generated codec, the framing around them as read from the
 * library's own senders.  A recording is decoded to its end even when the
 * command buffer it names does not exist, so the stream stays in step.
 */

#include "command.h"
#include "codec.h"
#include "fence.h"
#include "gfx.h"
#include "internal.h"
#include "object.h"

#include <kern/klog.h>
#include <kern/kmem.h>

#include <vulkan/vulkan_core.h>

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "../data/vulkan-codec.inc"

/* How many operations one command buffer records. */
#define I915_GFX_MAX_OPS	64U

/* How many command buffers one vkAllocateCommandBuffers creates, and one vkQueueSubmit runs. */
#define I915_GFX_MAX_ALLOCATE	16U
#define I915_GFX_MAX_SUBMITTED	32U

/* How many submissions one vkQueueSubmit carries. */
#define I915_GFX_MAX_SUBMITS	8U

/* How many regions one image copy, clear or blit records. */
#define I915_GFX_MAX_REGIONS	16U

/* How many descriptor sets one command buffer binds. */
#define I915_GFX_MAX_SETS	4U

struct i915_gfx_cmdpool;
struct i915_vk_fence;

/*
 * One VkCommandBuffer: its recorded operation list.
 *
 * It belongs to its pool from vkAllocateCommandBuffers to vkFreeCommandBuffers
 * or the pool's destruction, and is published in the object table under its
 * identity for that whole time.  Begin and a pool reset empty the list.
 */
struct i915_gfx_cmdbuf {
	/* The next buffer of the same pool. */
	struct i915_gfx_cmdbuf *next;

	/* The pool the buffer was allocated from. */
	struct i915_gfx_cmdpool *pool;

	/* The wire identity the buffer is published under. */
	uint64_t identity;

	/* How many operations are recorded. */
	uint32_t op_count;

	/* Nonzero once an operation did not fit: vkEndCommandBuffer then fails. */
	int overflow;

	/* The recorded operations, in recording order. */
	struct i915_gfx_op ops[I915_GFX_MAX_OPS];
};

/*
 * One VkCommandPool: the buffers allocated from it.
 *
 * It is published in the object table from its creation to its destruction,
 * which frees every buffer still on the list.
 */
struct i915_gfx_cmdpool {
	/* The pool's buffers, newest first. */
	struct i915_gfx_cmdbuf *buffers;
};

/*
 * The operation a recording writes into when it has nowhere else to go.
 *
 * A recording for a command buffer that does not exist, or one that is full,
 * still has to be decoded to its end; its operation is written here and
 * discarded.  It is cleared each time it is handed out and read by nobody.
 * XXX: one operation is shared by every session; it is only ever written.
 */
static struct i915_gfx_op i915_command_discard_op;

static uint32_t i915_command_result(int error);
static int i915_command_pool_create(struct i915_render_session *session, struct i915_wire_reader *reader, struct i915_wire_writer *reply);
static void i915_command_buffer_release(struct i915_render_session *session, struct i915_gfx_cmdbuf *cmdbuf);
static int i915_command_pool_destroy(struct i915_render_session *session, struct i915_wire_reader *reader);
static int i915_command_pool_reset(struct i915_render_session *session, struct i915_wire_reader *reader, struct i915_wire_writer *reply);
static int i915_command_buffers_allocate(struct i915_render_session *session, struct i915_wire_reader *reader, struct i915_wire_writer *reply);
static int i915_command_buffers_free(struct i915_render_session *session, struct i915_wire_reader *reader);
static int i915_command_buffer_begin(struct i915_render_session *session, struct i915_wire_reader *reader, struct i915_wire_writer *reply);
static int i915_command_buffer_end(struct i915_render_session *session, struct i915_wire_reader *reader, struct i915_wire_writer *reply);
static struct i915_gfx_op *i915_command_op(struct i915_gfx_cmdbuf *cmdbuf, enum i915_gfx_op_kind kind);
static int i915_record_image_copy(struct i915_render_session *session, struct i915_gfx_cmdbuf *cmdbuf, struct i915_wire_reader *reader, int blit);
static int i915_record_clear_image(struct i915_render_session *session, struct i915_gfx_cmdbuf *cmdbuf, struct i915_wire_reader *reader);
static int i915_record_barrier(struct i915_render_session *session, struct i915_wire_reader *reader);
static int i915_record_buffer_image_copy(struct i915_render_session *session, struct i915_gfx_cmdbuf *cmdbuf, struct i915_wire_reader *reader, int to_image);
static int i915_record_begin_pass(struct i915_render_session *session, struct i915_gfx_cmdbuf *cmdbuf, struct i915_wire_reader *reader);
static int i915_record_bind_vertex(struct i915_render_session *session, struct i915_gfx_cmdbuf *cmdbuf, struct i915_wire_reader *reader);
static int i915_record_bind_descriptor_sets(struct i915_render_session *session, struct i915_gfx_cmdbuf *cmdbuf, struct i915_wire_reader *reader);
static int i915_record_push_constants(struct i915_gfx_cmdbuf *cmdbuf, struct i915_wire_reader *reader);
static int i915_record_command(struct i915_render_session *session, uint32_t opcode, struct i915_wire_reader *reader);
static int i915_image_surface(const struct i915_gfx_image *image, struct i915_gfx_surface *surface);
static int i915_execute_clear(struct i915_render_session *session, const struct i915_gfx_op *op);
static int i915_execute_buffer_image_copy(struct i915_render_session *session, const struct i915_gfx_op *op);
static int i915_execute_clear_image(struct i915_render_session *session, const struct i915_gfx_op *op);
static int i915_execute_image_copy(struct i915_render_session *session, const struct i915_gfx_op *op);
static int i915_execute_image_blit(struct i915_render_session *session, const struct i915_gfx_op *op);
static int i915_command_buffer_execute(struct i915_render_session *session, struct i915_gfx_cmdbuf *cmdbuf);
static int i915_queue_submit(struct i915_render_session *session, struct i915_wire_reader *reader, struct i915_wire_writer *reply);

/*
 * Routes a command pool, command buffer, recording or queue submission
 * command.
 *
 * `handled` is cleared for an opcode the module does not own; every opcode
 * from 92 to 136 is a vkCmd* and is owned here.  A recording opcode the
 * module does not implement is refused with ENOTSUP and fails the stream.
 */
int
drv_i915_gfx_rec_dispatch(
	struct i915_render_session *session,
	uint32_t opcode,
	struct i915_wire_reader *reader,
	struct i915_wire_writer *reply,
	int *handled)
{
	int routed;
	int error;

	/* Routes the commands that are not recordings. */
	*handled = 1;
	routed = 1;
	error = 0;
	switch (opcode) {
	case 18U:
		/* vkQueueSubmit */
		error = i915_queue_submit(session, reader, reply);
		break;
	case 85U:
		/* vkCreateCommandPool */
		error = i915_command_pool_create(session, reader, reply);
		break;
	case 86U:
		/* vkDestroyCommandPool */
		error = i915_command_pool_destroy(session, reader);
		break;
	case 87U:
		/* vkResetCommandPool */
		error = i915_command_pool_reset(session, reader, reply);
		break;
	case 88U:
		/* vkAllocateCommandBuffers */
		error = i915_command_buffers_allocate(session, reader, reply);
		break;
	case 89U:
		/* vkFreeCommandBuffers */
		error = i915_command_buffers_free(session, reader);
		break;
	case 90U:
		/* vkBeginCommandBuffer */
		error = i915_command_buffer_begin(session, reader, reply);
		break;
	case 91U:
		/* vkEndCommandBuffer */
		error = i915_command_buffer_end(session, reader, reply);
		break;
	default:
		/* A recording, or an opcode of another module. */
		routed = 0;
		break;
	}

	/* Reports the outcome of a command that is not a recording. */
	if (routed != 0) {
		/* Reports why the command was refused. */
		if (error != 0)
			return error;

		/* Succeeded: the command was decoded and executed. */
		return 0;
	}

	/* Leaves an opcode outside the recording range to its own module. */
	if (opcode < 92U || opcode > 136U) {
		*handled = 0;
		return 0;
	}

	/* Records the vkCmd*; an unimplemented one fails the stream. */
	error = i915_record_command(session, opcode, reader);
	if (error == ENOTSUP) {
		kern_logf("i915: vk: XXX unimplemented opcode %u (recording)\n", opcode);
		reader->error = 1;
	}

	/* Reports why the recording was refused. */
	if (error != 0)
		return error;

	/* Succeeded: the operation is recorded. */
	return 0;
}

/*
 * Translates an errno into the VkResult a command buffer or submission
 * reply carries.
 *
 * A GPU that timed out or failed is a lost device; out of memory keeps its
 * meaning; anything else is an initialization failure.
 */
static uint32_t
i915_command_result(
	int error)
{
	/* Success is VK_SUCCESS. */
	if (error == 0)
		return 0U;

	/* An allocation that failed is out of device memory. */
	if (error == ENOMEM)
		return (uint32_t)VK_ERROR_OUT_OF_DEVICE_MEMORY;

	/* A GPU run that timed out or failed lost the device. */
	if (error == ETIMEDOUT || error == EIO)
		return (uint32_t)VK_ERROR_DEVICE_LOST;

	/* Anything else is reported as an initialization failure. */
	return (uint32_t)VK_ERROR_INITIALIZATION_FAILED;
}

/*
 * vkCreateCommandPool: [device][present][VkCommandPoolCreateInfo]
 * [pAllocator][present][identity] -> [result][present][identity].
 */
static int
i915_command_pool_create(
	struct i915_render_session *session,
	struct i915_wire_reader *reader,
	struct i915_wire_writer *reply)
{
	VkCommandPoolCreateInfo info;
	struct i915_gfx_cmdpool *pool;
	uint64_t identity;
	uint32_t result;
	int error;

	/* Decodes the create info and the identity; the flags are not acted on. */
	memset(&info, 0, sizeof(info));
	(void)drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u64(reader);
	i915_vkc_dec_VkCommandPoolCreateInfo(reader, &session->arena, &info);
	(void)drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u64(reader);
	identity = drv_i915_wire_read_u64(reader);
	if (reader->error != 0)
		return EINVAL;

	/* Allocates the pool and publishes it under its identity. */
	pool = kern_calloc(1U, sizeof(*pool));
	if (pool == NULL) {
		error = ENOMEM;
	} else {
		error = drv_i915_object_insert(session->vk, I915_VK_OBJ_COMMAND_POOL, identity, pool);
		if (error != 0)
			kern_free(pool);
	}

	/* Replies with the result, and the identity on success. */
	result = i915_command_result(error);
	drv_i915_wire_reply_u32(reply, result);
	if (error == 0) {
		drv_i915_wire_reply_u64(reply, 1U);
		drv_i915_wire_reply_u64(reply, identity);
	}

	/* Succeeded: the command was decoded; the reply carries the create's result. */
	return 0;
}

/* Unlinks a command buffer from its pool, withdraws its identity and frees it. */
static void
i915_command_buffer_release(
	struct i915_render_session *session,
	struct i915_gfx_cmdbuf *cmdbuf)
{
	struct i915_gfx_cmdbuf **link;

	/* Finds the buffer's link in its pool and takes the buffer out of the list. */
	for (link = &cmdbuf->pool->buffers;
	     *link != NULL;
	     link = &(*link)->next) {
		if (*link == cmdbuf) {
			*link = cmdbuf->next;
			break;
		}
	}

	/* Withdraws the identity, then frees the buffer. */
	drv_i915_object_remove(session->vk, I915_VK_OBJ_COMMAND_BUFFER, cmdbuf->identity);
	kern_free(cmdbuf);
}

/* vkDestroyCommandPool: [device][pool][pAllocator]; the pool's buffers go with it. */
static int
i915_command_pool_destroy(
	struct i915_render_session *session,
	struct i915_wire_reader *reader)
{
	struct i915_gfx_cmdpool *pool;
	uint64_t identity;

	/* Decodes the pool's identity. */
	(void)drv_i915_wire_read_u64(reader);
	identity = drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u64(reader);
	if (reader->error != 0)
		return EINVAL;

	/* An unknown pool has nothing to destroy. */
	pool = drv_i915_object_lookup(session->vk, I915_VK_OBJ_COMMAND_POOL, identity);
	if (pool == NULL)
		return 0;

	/* Frees every buffer still allocated from the pool. */
	while (pool->buffers != NULL)
		i915_command_buffer_release(session, pool->buffers);

	/* Withdraws the pool's identity and frees it. */
	drv_i915_object_remove(session->vk, I915_VK_OBJ_COMMAND_POOL, identity);
	kern_free(pool);

	/* Succeeded: the pool and its buffers are gone. */
	return 0;
}

/* vkResetCommandPool: [device][pool][flags] -> [result]; every buffer of the pool is emptied. */
static int
i915_command_pool_reset(
	struct i915_render_session *session,
	struct i915_wire_reader *reader,
	struct i915_wire_writer *reply)
{
	struct i915_gfx_cmdpool *pool;
	struct i915_gfx_cmdbuf *cmdbuf;
	uint64_t identity;
	uint32_t result;

	/* Decodes the pool's identity; the flags are not acted on. */
	(void)drv_i915_wire_read_u64(reader);
	identity = drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u32(reader);
	if (reader->error != 0)
		return EINVAL;

	/* Empties the recording of every buffer of the pool. */
	pool = drv_i915_object_lookup(session->vk, I915_VK_OBJ_COMMAND_POOL, identity);
	cmdbuf = NULL;
	if (pool != NULL)
		cmdbuf = pool->buffers;
	for (;
	     cmdbuf != NULL;
	     cmdbuf = cmdbuf->next) {
		cmdbuf->op_count = 0U;
		cmdbuf->overflow = 0;
	}

	/* Replies success for a known pool and failure for an unknown one. */
	result = 0U;
	if (pool == NULL)
		result = i915_command_result(EINVAL);
	drv_i915_wire_reply_u32(reply, result);

	/* Succeeded: the command was decoded; the reply carries the reset's result. */
	return 0;
}

/*
 * vkAllocateCommandBuffers: [device][present][VkCommandBufferAllocateInfo]
 * [count][identities] -> [result][count][identities].
 *
 * Only primary command buffers are implemented.
 * XXX: an allocation that fails part-way keeps its earlier buffers.
 */
static int
i915_command_buffers_allocate(
	struct i915_render_session *session,
	struct i915_wire_reader *reader,
	struct i915_wire_writer *reply)
{
	VkCommandBufferAllocateInfo info;
	struct i915_gfx_cmdpool *pool;
	struct i915_gfx_cmdbuf *cmdbuf;
	uint64_t identities[I915_GFX_MAX_ALLOCATE];
	uint64_t count;
	uint64_t index;
	uint32_t result;
	int error;

	/* Decodes the allocate info and the number of buffers, at most sixteen. */
	memset(&info, 0, sizeof(info));
	(void)drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u64(reader);
	i915_vkc_dec_VkCommandBufferAllocateInfo(reader, &session->arena, &info);
	count = drv_i915_wire_read_u64(reader);
	if (reader->error != 0 || count > I915_GFX_MAX_ALLOCATE)
		return EINVAL;

	/* Decodes the identity of every buffer. */
	for (index = 0U; index < count; index++)
		identities[index] = drv_i915_wire_read_u64(reader);
	if (reader->error != 0)
		return EINVAL;

	/* Finds the pool, and refuses secondary command buffers. */
	pool = drv_i915_object_lookup(session->vk, I915_VK_OBJ_COMMAND_POOL, (uint64_t)(uintptr_t)info.commandPool);
	error = 0;
	if (pool == NULL) {
		error = EINVAL;
	} else if (info.level != VK_COMMAND_BUFFER_LEVEL_PRIMARY) {
		kern_logf("i915: vk: XXX secondary command buffers are not implemented\n");
		error = ENOTSUP;
	}

	/* Allocates, publishes and links every buffer, stopping at the first failure. */
	for (index = 0U; index < count && error == 0; index++) {
		/* Allocates the buffer. */
		cmdbuf = kern_calloc(1U, sizeof(*cmdbuf));
		if (cmdbuf == NULL) {
			error = ENOMEM;
			break;
		}

		/* Publishes it under its identity. */
		cmdbuf->pool = pool;
		cmdbuf->identity = identities[index];
		error = drv_i915_object_insert(session->vk, I915_VK_OBJ_COMMAND_BUFFER, identities[index], cmdbuf);
		if (error != 0) {
			kern_free(cmdbuf);
			break;
		}

		/* Links it into the pool. */
		cmdbuf->next = pool->buffers;
		pool->buffers = cmdbuf;
	}

	/* Replies with the result, and the identities on success. */
	result = i915_command_result(error);
	drv_i915_wire_reply_u32(reply, result);
	if (error == 0) {
		drv_i915_wire_reply_u64(reply, count);
		for (index = 0U; index < count; index++)
			drv_i915_wire_reply_u64(reply, identities[index]);
	}

	/* Succeeded: the command was decoded; the reply carries the allocation's result. */
	return 0;
}

/* vkFreeCommandBuffers: [device][pool][present][count][identities]. */
static int
i915_command_buffers_free(
	struct i915_render_session *session,
	struct i915_wire_reader *reader)
{
	struct i915_gfx_cmdbuf *cmdbuf;
	uint64_t identity;
	uint64_t count;
	uint64_t index;

	/* Decodes the number of buffers, at most sixty-four. */
	(void)drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u32(reader);
	count = drv_i915_wire_read_u64(reader);
	if (reader->error != 0 || count > 64U)
		return EINVAL;

	/* Frees every named buffer that exists. */
	for (index = 0U; index < count; index++) {
		identity = drv_i915_wire_read_u64(reader);
		cmdbuf = drv_i915_object_lookup(session->vk, I915_VK_OBJ_COMMAND_BUFFER, identity);
		if (reader->error == 0 && cmdbuf != NULL)
			i915_command_buffer_release(session, cmdbuf);
	}

	/* Refuses a stream that ended inside the identities. */
	if (reader->error != 0)
		return EINVAL;

	/* Succeeded: the named buffers are freed. */
	return 0;
}

/* vkBeginCommandBuffer: [command buffer][present][VkCommandBufferBeginInfo] -> [result]. */
static int
i915_command_buffer_begin(
	struct i915_render_session *session,
	struct i915_wire_reader *reader,
	struct i915_wire_writer *reply)
{
	VkCommandBufferBeginInfo info;
	struct i915_gfx_cmdbuf *cmdbuf;
	uint64_t identity;
	uint64_t present;
	uint32_t result;

	/* Decodes the buffer and the begin info; the begin info is not acted on. */
	memset(&info, 0, sizeof(info));
	identity = drv_i915_wire_read_u64(reader);
	cmdbuf = drv_i915_object_lookup(session->vk, I915_VK_OBJ_COMMAND_BUFFER, identity);
	present = drv_i915_wire_read_u64(reader);
	if (present != 0U)
		i915_vkc_dec_VkCommandBufferBeginInfo(reader, &session->arena, &info);
	if (reader->error != 0)
		return EINVAL;

	/* Starts the recording afresh. */
	if (cmdbuf != NULL) {
		cmdbuf->op_count = 0U;
		cmdbuf->overflow = 0;
	}

	/* Replies success for a known buffer and failure for an unknown one. */
	result = 0U;
	if (cmdbuf == NULL)
		result = i915_command_result(EINVAL);
	drv_i915_wire_reply_u32(reply, result);

	/* Succeeded: the command was decoded; the reply carries the begin's result. */
	return 0;
}

/* vkEndCommandBuffer: [command buffer] -> [result]; a recording that overflowed fails. */
static int
i915_command_buffer_end(
	struct i915_render_session *session,
	struct i915_wire_reader *reader,
	struct i915_wire_writer *reply)
{
	struct i915_gfx_cmdbuf *cmdbuf;
	uint64_t identity;
	uint32_t result;

	/* Decodes the buffer. */
	identity = drv_i915_wire_read_u64(reader);
	cmdbuf = drv_i915_object_lookup(session->vk, I915_VK_OBJ_COMMAND_BUFFER, identity);
	if (reader->error != 0)
		return EINVAL;

	/* Says why a recording that did not fit is refused. */
	if (cmdbuf != NULL && cmdbuf->overflow != 0)
		kern_logf("i915: vk: XXX command buffer holds more than %u operations; the recording is refused\n", I915_GFX_MAX_OPS);

	/* Replies success only for a known buffer whose recording fit. */
	result = (uint32_t)VK_ERROR_OUT_OF_HOST_MEMORY;
	if (cmdbuf != NULL && cmdbuf->overflow == 0)
		result = 0U;
	drv_i915_wire_reply_u32(reply, result);

	/* Succeeded: the command was decoded; the reply carries the end's result. */
	return 0;
}

/*
 * Returns the next free operation of a command buffer, zeroed and of the
 * given kind.
 *
 * A buffer that does not exist, or that is full, gets the discarded
 * operation instead; a full buffer is marked overflowed so that its end
 * fails.
 */
static struct i915_gfx_op *
i915_command_op(
	struct i915_gfx_cmdbuf *cmdbuf,
	enum i915_gfx_op_kind kind)
{
	struct i915_gfx_op *op;

	/* Hands a recording with nowhere to go the discarded operation. */
	if (cmdbuf == NULL || cmdbuf->op_count >= I915_GFX_MAX_OPS) {
		/* A full buffer remembers that its recording did not fit. */
		if (cmdbuf != NULL)
			cmdbuf->overflow = 1;
		memset(&i915_command_discard_op, 0, sizeof(i915_command_discard_op));
		return &i915_command_discard_op;
	}

	/* Takes the next operation of the list. */
	op = &cmdbuf->ops[cmdbuf->op_count];
	cmdbuf->op_count++;

	/* Starts it empty, of the given kind. */
	memset(op, 0, sizeof(*op));
	op->kind = kind;

	/* Succeeded: the operation is the buffer's newest. */
	return op;
}

/*
 * vkCmdCopyImage: [src][layout][dst][layout][present][count]{VkImageCopy};
 * vkCmdBlitImage: the same with VkImageBlit, followed by [filter].
 *
 * Every region is one operation.
 */
static int
i915_record_image_copy(
	struct i915_render_session *session,
	struct i915_gfx_cmdbuf *cmdbuf,
	struct i915_wire_reader *reader,
	int blit)
{
	struct i915_gfx_image *src;
	struct i915_gfx_image *dst;
	struct i915_gfx_op *op;
	struct i915_gfx_op *ops[I915_GFX_MAX_REGIONS];
	uint64_t identity;
	uint64_t count;
	uint64_t index;
	uint32_t filter;
	enum i915_gfx_op_kind kind;

	/* Decodes the two images and the number of regions, at most sixteen. */
	identity = drv_i915_wire_read_u64(reader);
	src = drv_i915_object_lookup(session->vk, I915_VK_OBJ_IMAGE, identity);
	(void)drv_i915_wire_read_u32(reader);
	identity = drv_i915_wire_read_u64(reader);
	dst = drv_i915_object_lookup(session->vk, I915_VK_OBJ_IMAGE, identity);
	(void)drv_i915_wire_read_u32(reader);
	(void)drv_i915_wire_read_u32(reader);
	count = drv_i915_wire_read_u64(reader);
	if (reader->error != 0 || count > I915_GFX_MAX_REGIONS)
		return EINVAL;

	/* A blit and a copy record different operations. */
	kind = I915_GFX_OP_COPY_IMAGE;
	if (blit)
		kind = I915_GFX_OP_BLIT_IMAGE;

	/* Records one operation for each region. */
	for (index = 0U; index < count; index++) {
		op = i915_command_op(cmdbuf, kind);
		ops[index] = op;
		if (blit) {
			op->u.blit.src = src;
			op->u.blit.dst = dst;
			i915_vkc_dec_VkImageBlit(reader, &session->arena, &op->u.blit.region);
		} else {
			op->u.image_copy.src = src;
			op->u.image_copy.dst = dst;
			i915_vkc_dec_VkImageCopy(reader, &session->arena, &op->u.image_copy.region);
		}
	}

	/* Gives every region of a blit the filter that follows them. */
	if (blit) {
		filter = drv_i915_wire_read_u32(reader);
		for (index = 0U; index < count; index++)
			ops[index]->u.blit.filter = filter;
	}

	/* Refuses a stream that ended inside the regions. */
	if (reader->error != 0)
		return EINVAL;

	/* Succeeded: the regions are recorded. */
	return 0;
}

/*
 * vkCmdClearColorImage: [image][layout][present]{[tag][4][4 words]}
 * [present][count]{VkImageSubresourceRange}.
 *
 * XXX: one level and one layer: the whole image is cleared, whatever the
 * ranges say.
 */
static int
i915_record_clear_image(
	struct i915_render_session *session,
	struct i915_gfx_cmdbuf *cmdbuf,
	struct i915_wire_reader *reader)
{
	VkImageSubresourceRange range;
	struct i915_gfx_op *op;
	uint64_t identity;
	uint64_t present;
	uint64_t words;
	uint64_t count;
	uint64_t index;

	/* Records the image. */
	op = i915_command_op(cmdbuf, I915_GFX_OP_CLEAR_IMAGE);
	identity = drv_i915_wire_read_u64(reader);
	op->u.clear_image.image = drv_i915_object_lookup(session->vk, I915_VK_OBJ_IMAGE, identity);
	(void)drv_i915_wire_read_u32(reader);

	/* Records the four words of the clear colour: the union's tag, then an array of four. */
	present = drv_i915_wire_read_u64(reader);
	if (present != 0U) {
		(void)drv_i915_wire_read_u32(reader);
		words = drv_i915_wire_read_u64(reader);
		if (words != 4U)
			reader->error = 1;
		for (index = 0U; index < 4U; index++)
			op->u.clear_image.words[index] = drv_i915_wire_read_u32(reader);
	}

	/* Decodes the number of ranges, at most sixteen. */
	(void)drv_i915_wire_read_u32(reader);
	count = drv_i915_wire_read_u64(reader);
	if (reader->error != 0 || count > I915_GFX_MAX_REGIONS)
		return EINVAL;

	/* Decodes the ranges to their end without keeping them. */
	for (index = 0U; index < count; index++)
		i915_vkc_dec_VkImageSubresourceRange(reader, &session->arena, &range);

	/* Refuses a stream that ended inside the ranges. */
	if (reader->error != 0)
		return EINVAL;

	/* Succeeded: the clear is recorded. */
	return 0;
}

/*
 * vkCmdPipelineBarrier: decoded to its end and not acted on.
 *
 * Every operation finishes on the GPU before the next starts (command.h),
 * so there is nothing for a barrier to order.
 */
static int
i915_record_barrier(
	struct i915_render_session *session,
	struct i915_wire_reader *reader)
{
	VkMemoryBarrier memory;
	VkBufferMemoryBarrier buffer;
	VkImageMemoryBarrier image;
	uint64_t count;
	uint64_t index;

	/* Decodes the stage masks and the dependency flags. */
	(void)drv_i915_wire_read_u32(reader);
	(void)drv_i915_wire_read_u32(reader);
	(void)drv_i915_wire_read_u32(reader);

	/* Decodes the memory barriers. */
	(void)drv_i915_wire_read_u32(reader);
	count = drv_i915_wire_read_u64(reader);
	for (index = 0U; reader->error == 0 && index < count; index++)
		i915_vkc_dec_VkMemoryBarrier(reader, &session->arena, &memory);

	/* Decodes the buffer barriers. */
	(void)drv_i915_wire_read_u32(reader);
	count = drv_i915_wire_read_u64(reader);
	for (index = 0U; reader->error == 0 && index < count; index++)
		i915_vkc_dec_VkBufferMemoryBarrier(reader, &session->arena, &buffer);

	/* Decodes the image barriers. */
	(void)drv_i915_wire_read_u32(reader);
	count = drv_i915_wire_read_u64(reader);
	for (index = 0U; reader->error == 0 && index < count; index++)
		i915_vkc_dec_VkImageMemoryBarrier(reader, &session->arena, &image);

	/* Refuses a stream that ended inside the barriers. */
	if (reader->error != 0)
		return EINVAL;

	/* Succeeded: the barrier is decoded. */
	return 0;
}

/*
 * vkCmdCopyBufferToImage: [buffer][image][layout][present][count]{region};
 * vkCmdCopyImageToBuffer: [image][layout][buffer][present][count]{region}.
 *
 * Every region is one operation.
 */
static int
i915_record_buffer_image_copy(
	struct i915_render_session *session,
	struct i915_gfx_cmdbuf *cmdbuf,
	struct i915_wire_reader *reader,
	int to_image)
{
	struct i915_gfx_buffer *buffer;
	struct i915_gfx_image *image;
	struct i915_gfx_op *op;
	uint64_t buffer_id;
	uint64_t image_id;
	uint64_t count;
	uint64_t index;
	enum i915_gfx_op_kind kind;

	/* Decodes the buffer and the image in the order of the direction. */
	if (to_image != 0) {
		buffer_id = drv_i915_wire_read_u64(reader);
		image_id = drv_i915_wire_read_u64(reader);
		(void)drv_i915_wire_read_u32(reader);
	} else {
		image_id = drv_i915_wire_read_u64(reader);
		(void)drv_i915_wire_read_u32(reader);
		buffer_id = drv_i915_wire_read_u64(reader);
	}

	/* Decodes the number of regions, at most as many as a buffer records. */
	(void)drv_i915_wire_read_u32(reader);
	count = drv_i915_wire_read_u64(reader);
	if (reader->error != 0 || count > I915_GFX_MAX_OPS)
		return EINVAL;

	/* Resolves the two objects. */
	buffer = drv_i915_object_lookup(session->vk, I915_VK_OBJ_BUFFER, buffer_id);
	image = drv_i915_object_lookup(session->vk, I915_VK_OBJ_IMAGE, image_id);

	/* The direction decides the operation. */
	kind = I915_GFX_OP_COPY_IMAGE_TO_BUFFER;
	if (to_image != 0)
		kind = I915_GFX_OP_COPY_BUFFER_TO_IMAGE;

	/* Records one operation for each region. */
	for (index = 0U; index < count; index++) {
		op = i915_command_op(cmdbuf, kind);
		op->u.copy.buffer = buffer;
		op->u.copy.image = image;
		i915_vkc_dec_VkBufferImageCopy(reader, &session->arena, &op->u.copy.region);
	}

	/* Refuses a stream that ended inside the regions. */
	if (reader->error != 0)
		return EINVAL;

	/* Succeeded: the regions are recorded. */
	return 0;
}

/*
 * vkCmdBeginRenderPass (libvulkan commands.c command_encode_render_begin):
 * [present][sType][pNext][renderPass][framebuffer][VkRect2D][present][count]
 * {clear}[contents].  A clear is [0][tag][4][4 words] for a colour and
 * [1][depth bits][stencil] for a depth / stencil attachment.
 *
 * XXX: the whole attachment is cleared, not the render area.
 */
static int
i915_record_begin_pass(
	struct i915_render_session *session,
	struct i915_gfx_cmdbuf *cmdbuf,
	struct i915_wire_reader *reader)
{
	VkRect2D area;
	struct i915_gfx_op *op;
	uint64_t identity;
	uint64_t present;
	uint64_t length;
	uint64_t count;
	uint64_t index;
	uint32_t words[4];
	uint32_t is_depth;
	uint32_t word;

	/* Refuses a begin without its begin info. */
	op = i915_command_op(cmdbuf, I915_GFX_OP_BEGIN_PASS);
	present = drv_i915_wire_read_u64(reader);
	if (present == 0U) {
		(void)drv_i915_wire_read_u32(reader);
		return EINVAL;
	}

	/* Records the render pass and the framebuffer; the render area is decoded and not kept. */
	(void)drv_i915_wire_read_u32(reader);
	(void)drv_i915_wire_read_u64(reader);
	identity = drv_i915_wire_read_u64(reader);
	op->u.begin.pass = drv_i915_object_lookup(session->vk, I915_VK_OBJ_RENDER_PASS, identity);
	identity = drv_i915_wire_read_u64(reader);
	op->u.begin.framebuffer = drv_i915_object_lookup(session->vk, I915_VK_OBJ_FRAMEBUFFER, identity);
	i915_vkc_dec_VkRect2D(reader, &session->arena, &area);

	/* Decodes the number of clear values, at most sixty-four. */
	(void)drv_i915_wire_read_u32(reader);
	count = drv_i915_wire_read_u64(reader);
	if (reader->error != 0 || count > 64U)
		return EINVAL;

	/* Records the clear value of every attachment the operation has room for. */
	for (index = 0U; index < count; index++) {
		/* Decodes one clear value: a depth and stencil pair, or four colour words. */
		memset(words, 0, sizeof(words));
		is_depth = drv_i915_wire_read_u32(reader);
		if (is_depth != 0U) {
			words[0] = drv_i915_wire_read_u32(reader);
			words[1] = drv_i915_wire_read_u32(reader);
		} else {
			(void)drv_i915_wire_read_u32(reader);
			length = drv_i915_wire_read_u64(reader);
			if (length != 4U)
				reader->error = 1;
			for (word = 0U; word < 4U; word++)
				words[word] = drv_i915_wire_read_u32(reader);
		}

		/* Keeps the value of an attachment the pass can have. */
		if (index < I915_GFX_MAX_ATTACHMENTS) {
			op->u.begin.clear_is_depth[index] = is_depth;
			memcpy(op->u.begin.clear_words[index], words, sizeof(words));
			op->u.begin.clear_count = (uint32_t)index + 1U;
		}
	}

	/* Decodes the subpass contents. */
	(void)drv_i915_wire_read_u32(reader);

	/* Refuses a stream that ended inside the begin. */
	if (reader->error != 0)
		return EINVAL;

	/* Succeeded: the begin and its clears are recorded. */
	return 0;
}

/*
 * vkCmdBindVertexBuffers: [first][present][count]{buffer}[count]{offset}.
 *
 * Every binding is one operation.
 */
static int
i915_record_bind_vertex(
	struct i915_render_session *session,
	struct i915_gfx_cmdbuf *cmdbuf,
	struct i915_wire_reader *reader)
{
	struct i915_gfx_op *ops[I915_GFX_MAX_VERTEX_BINDINGS];
	uint64_t identity;
	uint64_t offsets;
	uint64_t count;
	uint64_t index;
	uint32_t first;

	/* Decodes the first binding and the number of buffers. */
	first = drv_i915_wire_read_u32(reader);
	(void)drv_i915_wire_read_u32(reader);
	count = drv_i915_wire_read_u64(reader);
	if (reader->error != 0 || count > I915_GFX_MAX_VERTEX_BINDINGS)
		return EINVAL;

	/* Records the buffer of every binding. */
	for (index = 0U; index < count; index++) {
		ops[index] = i915_command_op(cmdbuf, I915_GFX_OP_BIND_VERTEX_BUFFER);
		ops[index]->u.vertex.binding = first + (uint32_t)index;
		identity = drv_i915_wire_read_u64(reader);
		ops[index]->u.vertex.buffer = drv_i915_object_lookup(session->vk, I915_VK_OBJ_BUFFER, identity);
	}

	/* Refuses an offset array of another length. */
	offsets = drv_i915_wire_read_u64(reader);
	if (offsets != count)
		reader->error = 1;

	/* Records the offset of every binding. */
	for (index = 0U; reader->error == 0 && index < count; index++)
		ops[index]->u.vertex.offset = drv_i915_wire_read_u64(reader);

	/* Refuses a stream that ended inside the bindings. */
	if (reader->error != 0)
		return EINVAL;

	/* Succeeded: the bindings are recorded. */
	return 0;
}

/*
 * vkCmdBindDescriptorSets: [bind point][layout][first][present][count]{set}
 * [present][count]{dynamic offset}.
 *
 * Every set is one operation.
 * XXX: no dynamic buffers are bound; the dynamic offsets are decoded and
 * not kept.
 */
static int
i915_record_bind_descriptor_sets(
	struct i915_render_session *session,
	struct i915_gfx_cmdbuf *cmdbuf,
	struct i915_wire_reader *reader)
{
	struct i915_gfx_op *op;
	uint64_t identity;
	uint64_t count;
	uint64_t index;
	uint32_t first;

	/* Decodes the bind point, the layout, the first set and the number of sets, at most four. */
	(void)drv_i915_wire_read_u32(reader);
	(void)drv_i915_wire_read_u64(reader);
	first = drv_i915_wire_read_u32(reader);
	(void)drv_i915_wire_read_u32(reader);
	count = drv_i915_wire_read_u64(reader);
	if (reader->error != 0 || count > I915_GFX_MAX_SETS)
		return EINVAL;

	/* Records every set. */
	for (index = 0U; index < count; index++) {
		op = i915_command_op(cmdbuf, I915_GFX_OP_BIND_DESCRIPTOR_SET);
		op->u.descriptor.set = first + (uint32_t)index;
		identity = drv_i915_wire_read_u64(reader);
		op->u.descriptor.dset = drv_i915_object_lookup(session->vk, I915_VK_OBJ_DESCRIPTOR_SET, identity);
	}

	/* Decodes the number of dynamic offsets, at most sixty-four. */
	(void)drv_i915_wire_read_u32(reader);
	count = drv_i915_wire_read_u64(reader);
	if (reader->error != 0 || count > 64U)
		return EINVAL;

	/* Decodes the dynamic offsets without keeping them. */
	for (index = 0U; index < count; index++)
		(void)drv_i915_wire_read_u32(reader);

	/* Refuses a stream that ended inside the offsets. */
	if (reader->error != 0)
		return EINVAL;

	/* Succeeded: the sets are recorded. */
	return 0;
}

/*
 * vkCmdPushConstants: [layout][stages][offset][size][count]{bytes}.
 *
 * XXX: one block is shared by every stage; the stages are not kept.
 */
static int
i915_record_push_constants(
	struct i915_gfx_cmdbuf *cmdbuf,
	struct i915_wire_reader *reader)
{
	struct i915_gfx_op *op;
	uint64_t count;

	/* Decodes the range of the update. */
	op = i915_command_op(cmdbuf, I915_GFX_OP_PUSH_CONSTANTS);
	(void)drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u32(reader);
	op->u.push.offset = drv_i915_wire_read_u32(reader);
	op->u.push.size = drv_i915_wire_read_u32(reader);
	count = drv_i915_wire_read_u64(reader);

	/* Refuses a stream that ended inside the range. */
	if (reader->error != 0)
		return EINVAL;

	/* Refuses bytes that do not match the declared size. */
	if (count != op->u.push.size)
		return EINVAL;

	/* Refuses a range that starts past the push constant block. */
	if (op->u.push.offset > I915_GFX_PUSH_BYTES)
		return EINVAL;

	/* Refuses a range that runs past the end of the block. */
	if (count > I915_GFX_PUSH_BYTES - op->u.push.offset)
		return EINVAL;

	/* Records the bytes. */
	i915_vkc_read_bytes(reader, op->u.push.bytes, (size_t)count);
	if (reader->error != 0)
		return EINVAL;

	/* Succeeded: the update is recorded. */
	return 0;
}

/*
 * Records one vkCmd* into the command buffer it names.
 *
 * Returns ENOTSUP for a recording the module does not implement.
 */
static int
i915_record_command(
	struct i915_render_session *session,
	uint32_t opcode,
	struct i915_wire_reader *reader)
{
	struct i915_gfx_cmdbuf *cmdbuf;
	struct i915_gfx_op *op;
	uint64_t identity;
	int error;

	/* Decodes the command buffer the recording names. */
	identity = drv_i915_wire_read_u64(reader);
	cmdbuf = drv_i915_object_lookup(session->vk, I915_VK_OBJ_COMMAND_BUFFER, identity);
	if (reader->error != 0)
		return EINVAL;

	/* Records the operation the opcode names. */
	switch (opcode) {
	case 93U:
		/* vkCmdBindPipeline: [bind point][pipeline]. */
		op = i915_command_op(cmdbuf, I915_GFX_OP_BIND_PIPELINE);
		(void)drv_i915_wire_read_u32(reader);
		identity = drv_i915_wire_read_u64(reader);
		op->u.pipeline = drv_i915_object_lookup(session->vk, I915_VK_OBJ_PIPELINE, identity);
		break;
	case 103U:
		/* vkCmdBindDescriptorSets */
		error = i915_record_bind_descriptor_sets(session, cmdbuf, reader);
		return error;
	case 105U:
		/* vkCmdBindVertexBuffers */
		error = i915_record_bind_vertex(session, cmdbuf, reader);
		return error;
	case 106U:
		/* vkCmdDraw: [vertices][instances][first vertex][first instance]. */
		op = i915_command_op(cmdbuf, I915_GFX_OP_DRAW);
		op->u.draw.vertex_count = drv_i915_wire_read_u32(reader);
		op->u.draw.instance_count = drv_i915_wire_read_u32(reader);
		op->u.draw.first_vertex = drv_i915_wire_read_u32(reader);
		op->u.draw.first_instance = drv_i915_wire_read_u32(reader);
		break;
	case 113U:
		/* vkCmdCopyImage */
		error = i915_record_image_copy(session, cmdbuf, reader, 0);
		return error;
	case 114U:
		/* vkCmdBlitImage */
		error = i915_record_image_copy(session, cmdbuf, reader, 1);
		return error;
	case 119U:
		/* vkCmdClearColorImage */
		error = i915_record_clear_image(session, cmdbuf, reader);
		return error;
	case 115U:
		/* vkCmdCopyBufferToImage */
		error = i915_record_buffer_image_copy(session, cmdbuf, reader, 1);
		return error;
	case 116U:
		/* vkCmdCopyImageToBuffer */
		error = i915_record_buffer_image_copy(session, cmdbuf, reader, 0);
		return error;
	case 126U:
		/* vkCmdPipelineBarrier */
		error = i915_record_barrier(session, reader);
		return error;
	case 132U:
		/* vkCmdPushConstants */
		error = i915_record_push_constants(cmdbuf, reader);
		return error;
	case 133U:
		/* vkCmdBeginRenderPass */
		error = i915_record_begin_pass(session, cmdbuf, reader);
		return error;
	case 135U:
		/* vkCmdEndRenderPass */
		(void)i915_command_op(cmdbuf, I915_GFX_OP_END_PASS);
		break;
	default:
		return ENOTSUP;
	}

	/* Refuses a stream that ended inside the recording. */
	if (reader->error != 0)
		return EINVAL;

	/* Succeeded: the operation is recorded. */
	return 0;
}

/* Describes an image as the linear surface it is, in its own format; EINVAL when it has no storage. */
static int
i915_image_surface(
	const struct i915_gfx_image *image,
	struct i915_gfx_surface *surface)
{
	/* Takes the image's address, extent, pitch and format. */
	surface->va = drv_i915_gfx_memory_va(image->memory, image->offset);
	surface->width = image->width;
	surface->height = image->height;
	surface->pitch = image->pitch;
	surface->format = image->format;

	/* Refuses an image that is not bound to storage. */
	if (surface->va == 0U)
		return EINVAL;

	/* Succeeded: the surface describes the image. */
	return 0;
}

/*
 * Runs the clears of a render pass's begin (loadOp CLEAR), one GPU fill for
 * each attachment.
 *
 * A depth clear writes the depth value's bits through an R32_FLOAT view of
 * the whole allocation: every word gets the same value, so the depth
 * buffer's tiling does not matter.
 * XXX: the stencil value is not written, and the whole attachment is
 * cleared, not the render area.
 */
static int
i915_execute_clear(
	struct i915_render_session *session,
	const struct i915_gfx_op *op)
{
	struct i915_gfx_framebuffer *framebuffer;
	struct i915_gfx_pass *pass;
	struct i915_gfx_image *image;
	struct i915_gfx_surface surface;
	struct i915_gfx_rect rect;
	uint32_t index;
	uint32_t words[4];
	int error;

	/* A begin without a pass or a framebuffer clears nothing. */
	pass = op->u.begin.pass;
	framebuffer = op->u.begin.framebuffer;
	if (pass == NULL || framebuffer == NULL)
		return 0;

	/* Fills every attachment that loads with a clear. */
	for (index = 0U; index < pass->attachment_count && index < framebuffer->view_count; index++) {
		/* Skips an attachment that does not load with a clear. */
		if (pass->attachments[index].load_op != VK_ATTACHMENT_LOAD_OP_CLEAR)
			continue;

		/* Skips an attachment the begin gave no clear value. */
		if (index >= op->u.begin.clear_count)
			continue;

		/* Skips an attachment the framebuffer has no view for. */
		if (framebuffer->views[index] == NULL)
			continue;

		/* Describes the attachment's image. */
		image = framebuffer->views[index]->image;
		error = i915_image_surface(image, &surface);
		if (error != 0)
			return EINVAL;

		/* Takes the value: the depth bits through the R32_FLOAT view, or the four colour words. */
		memset(words, 0, sizeof(words));
		if (op->u.begin.clear_is_depth[index] != 0U) {
			surface.format = VK_FORMAT_R32_SFLOAT;
			surface.width = image->pitch / 4U;
			surface.height = (uint32_t)(image->bytes / image->pitch);
			words[0] = op->u.begin.clear_words[index][0];
		} else {
			memcpy(words, op->u.begin.clear_words[index], sizeof(words));
		}

		/* Fills the whole surface. */
		rect.x = 0;
		rect.y = 0;
		rect.w = surface.width;
		rect.h = surface.height;
		error = drv_i915_gfx_rect(session, &surface, &rect, NULL, NULL, words, 0);
		if (error != 0)
			return error;
	}

	/* Succeeded: every attachment that loads with a clear is cleared. */
	return 0;
}

/*
 * Runs a vkCmdCopyBufferToImage or vkCmdCopyImageToBuffer region as one GPU
 * copy.
 *
 * The buffer region is a linear surface of the image's format with the
 * region's row length.  A copy to or from a depth image is not implemented.
 */
static int
i915_execute_buffer_image_copy(
	struct i915_render_session *session,
	const struct i915_gfx_op *op)
{
	const VkBufferImageCopy *region;
	struct i915_gfx_buffer *buffer;
	struct i915_gfx_image *image;
	struct i915_gfx_surface image_surface;
	struct i915_gfx_surface buffer_surface;
	struct i915_gfx_rect image_rect;
	struct i915_gfx_rect buffer_rect;
	uint64_t row_pixels;
	uint64_t needed;
	int error;

	/* Refuses a copy whose buffer or image does not exist. */
	buffer = op->u.copy.buffer;
	image = op->u.copy.image;
	region = &op->u.copy.region;
	if (buffer == NULL || image == NULL)
		return EINVAL;

	/* Describes the image. */
	error = i915_image_surface(image, &image_surface);
	if (error != 0)
		return EINVAL;

	/* Refuses a depth image. */
	if (image->format == VK_FORMAT_D32_SFLOAT) {
		kern_logf("i915: vk: XXX unimplemented path: a copy to or from a depth image\n");
		return ENOTSUP;
	}

	/* A zero row length means rows as long as the region. */
	row_pixels = region->imageExtent.width;
	if (region->bufferRowLength != 0U)
		row_pixels = region->bufferRowLength;

	/* Refuses a region that is not one non-empty 2D rectangle. */
	if (region->imageExtent.depth != 1U)
		return EINVAL;
	if (region->imageExtent.width == 0U || region->imageExtent.height == 0U)
		return EINVAL;

	/* Refuses rows shorter than the region. */
	if (row_pixels < region->imageExtent.width)
		return EINVAL;

	/* Refuses a region that runs past the end of the buffer. */
	needed = region->bufferOffset +
	    ((uint64_t)(region->imageExtent.height - 1U) * row_pixels + region->imageExtent.width) * 4U;
	if (needed > buffer->size)
		return EINVAL;

	/* Describes the buffer region as a linear surface of the image's format. */
	buffer_surface.va = drv_i915_gfx_memory_va(buffer->memory, buffer->offset + region->bufferOffset);
	buffer_surface.width = region->imageExtent.width;
	buffer_surface.height = region->imageExtent.height;
	buffer_surface.pitch = (uint32_t)(row_pixels * 4U);
	buffer_surface.format = image->format;
	if (buffer_surface.va == 0U)
		return EINVAL;

	/* The whole buffer surface is the region. */
	buffer_rect.x = 0;
	buffer_rect.y = 0;
	buffer_rect.w = region->imageExtent.width;
	buffer_rect.h = region->imageExtent.height;

	/* The image rectangle is the region at its offset. */
	image_rect.x = region->imageOffset.x;
	image_rect.y = region->imageOffset.y;
	image_rect.w = region->imageExtent.width;
	image_rect.h = region->imageExtent.height;

	/* Copies in the direction the operation names. */
	if (op->kind == I915_GFX_OP_COPY_BUFFER_TO_IMAGE) {
		error = drv_i915_gfx_rect(session, &image_surface, &image_rect, &buffer_surface, &buffer_rect, NULL, 0);
	} else {
		error = drv_i915_gfx_rect(session, &buffer_surface, &buffer_rect, &image_surface, &image_rect, NULL, 0);
	}

	/* Reports why the copy failed. */
	if (error != 0)
		return error;

	/* Succeeded: the region is copied. */
	return 0;
}

/* Runs a vkCmdClearColorImage as one GPU fill of the whole image. */
static int
i915_execute_clear_image(
	struct i915_render_session *session,
	const struct i915_gfx_op *op)
{
	struct i915_gfx_surface surface;
	struct i915_gfx_rect rect;
	int error;

	/* Refuses a clear whose image does not exist. */
	if (op->u.clear_image.image == NULL)
		return EINVAL;

	/* Describes the image. */
	error = i915_image_surface(op->u.clear_image.image, &surface);
	if (error != 0)
		return EINVAL;

	/* Fills the whole image with the clear words. */
	rect.x = 0;
	rect.y = 0;
	rect.w = surface.width;
	rect.h = surface.height;
	error = drv_i915_gfx_rect(session, &surface, &rect, NULL, NULL, op->u.clear_image.words, 0);
	if (error != 0)
		return error;

	/* Succeeded: the image is cleared. */
	return 0;
}

/* Runs a vkCmdCopyImage region as one GPU copy. */
static int
i915_execute_image_copy(
	struct i915_render_session *session,
	const struct i915_gfx_op *op)
{
	const VkImageCopy *region;
	struct i915_gfx_surface src_surface;
	struct i915_gfx_surface dst_surface;
	struct i915_gfx_rect src_rect;
	struct i915_gfx_rect dst_rect;
	int error;

	/* Refuses a copy whose images do not exist. */
	region = &op->u.image_copy.region;
	if (op->u.image_copy.src == NULL || op->u.image_copy.dst == NULL)
		return EINVAL;

	/* Describes the source image. */
	error = i915_image_surface(op->u.image_copy.src, &src_surface);
	if (error != 0)
		return EINVAL;

	/* Describes the destination image. */
	error = i915_image_surface(op->u.image_copy.dst, &dst_surface);
	if (error != 0)
		return EINVAL;

	/* The source rectangle is the extent at the source offset. */
	src_rect.x = region->srcOffset.x;
	src_rect.y = region->srcOffset.y;
	src_rect.w = region->extent.width;
	src_rect.h = region->extent.height;

	/* The destination rectangle is the same extent at the destination offset. */
	dst_rect.x = region->dstOffset.x;
	dst_rect.y = region->dstOffset.y;
	dst_rect.w = region->extent.width;
	dst_rect.h = region->extent.height;

	/* Copies the region. */
	error = drv_i915_gfx_rect(session, &dst_surface, &dst_rect, &src_surface, &src_rect, NULL, 0);
	if (error != 0)
		return error;

	/* Succeeded: the region is copied. */
	return 0;
}

/*
 * Runs a vkCmdBlitImage region as one scaled GPU copy.
 *
 * XXX: mirrored blits (an offset pair given in decreasing order) are
 * refused.
 */
static int
i915_execute_image_blit(
	struct i915_render_session *session,
	const struct i915_gfx_op *op)
{
	const VkImageBlit *region;
	struct i915_gfx_surface src_surface;
	struct i915_gfx_surface dst_surface;
	struct i915_gfx_rect src_rect;
	struct i915_gfx_rect dst_rect;
	int32_t x0;
	int32_t y0;
	int32_t x1;
	int32_t y1;
	int linear;
	int error;

	/* Refuses a blit whose images do not exist. */
	region = &op->u.blit.region;
	if (op->u.blit.src == NULL || op->u.blit.dst == NULL)
		return EINVAL;

	/* Describes the source image. */
	error = i915_image_surface(op->u.blit.src, &src_surface);
	if (error != 0)
		return EINVAL;

	/* Describes the destination image. */
	error = i915_image_surface(op->u.blit.dst, &dst_surface);
	if (error != 0)
		return EINVAL;

	/* Refuses a mirrored or empty source rectangle. */
	x0 = region->srcOffsets[0].x;
	y0 = region->srcOffsets[0].y;
	x1 = region->srcOffsets[1].x;
	y1 = region->srcOffsets[1].y;
	if (x1 <= x0 || y1 <= y0)
		return ENOTSUP;

	/* Takes the source rectangle between its two corners. */
	src_rect.x = x0;
	src_rect.y = y0;
	src_rect.w = (uint32_t)(x1 - x0);
	src_rect.h = (uint32_t)(y1 - y0);

	/* Refuses a mirrored or empty destination rectangle. */
	x0 = region->dstOffsets[0].x;
	y0 = region->dstOffsets[0].y;
	x1 = region->dstOffsets[1].x;
	y1 = region->dstOffsets[1].y;
	if (x1 <= x0 || y1 <= y0)
		return ENOTSUP;

	/* Takes the destination rectangle between its two corners. */
	dst_rect.x = x0;
	dst_rect.y = y0;
	dst_rect.w = (uint32_t)(x1 - x0);
	dst_rect.h = (uint32_t)(y1 - y0);

	/* A linear filter samples bilinearly; any other filter samples the nearest texel. */
	linear = 0;
	if (op->u.blit.filter == VK_FILTER_LINEAR)
		linear = 1;

	/* Copies the region, scaled. */
	error = drv_i915_gfx_rect(session, &dst_surface, &dst_rect, &src_surface, &src_rect, NULL, linear);
	if (error != 0)
		return error;

	/* Succeeded: the region is blitted. */
	return 0;
}

/*
 * Runs the operation list of one command buffer in order.
 *
 * Binds are tracked in a draw state that lives for this execution; clears,
 * copies and draws each run to their end on the GPU.  The first failure
 * stops the list and is logged with where it stopped.
 */
static int
i915_command_buffer_execute(
	struct i915_render_session *session,
	struct i915_gfx_cmdbuf *cmdbuf)
{
	struct i915_gfx_draw_state state;
	const struct i915_gfx_op *op;
	uint32_t index;
	unsigned kind;
	int error;

	/* Starts with nothing bound. */
	memset(&state, 0, sizeof(state));
	error = 0;

	/* Runs the operations until one fails. */
	for (index = 0U; index < cmdbuf->op_count && error == 0; index++) {
		op = &cmdbuf->ops[index];

		/* Runs or tracks the operation by its kind. */
		switch (op->kind) {
		case I915_GFX_OP_COPY_BUFFER_TO_IMAGE:
		case I915_GFX_OP_COPY_IMAGE_TO_BUFFER:
			error = i915_execute_buffer_image_copy(session, op);
			break;
		case I915_GFX_OP_BEGIN_PASS:
			state.pass = op->u.begin.pass;
			state.framebuffer = op->u.begin.framebuffer;
			error = i915_execute_clear(session, op);
			break;
		case I915_GFX_OP_COPY_IMAGE:
			error = i915_execute_image_copy(session, op);
			break;
		case I915_GFX_OP_BLIT_IMAGE:
			error = i915_execute_image_blit(session, op);
			break;
		case I915_GFX_OP_CLEAR_IMAGE:
			error = i915_execute_clear_image(session, op);
			break;
		case I915_GFX_OP_END_PASS:
			state.pass = NULL;
			state.framebuffer = NULL;
			break;
		case I915_GFX_OP_BIND_PIPELINE:
			state.pipeline = op->u.pipeline;
			break;
		case I915_GFX_OP_BIND_VERTEX_BUFFER:
			/* A binding past the tracked ones is ignored. */
			if (op->u.vertex.binding < I915_GFX_MAX_VERTEX_BINDINGS) {
				state.vertex[op->u.vertex.binding].buffer = op->u.vertex.buffer;
				state.vertex[op->u.vertex.binding].offset = op->u.vertex.offset;
			}

			break;
		case I915_GFX_OP_BIND_DESCRIPTOR_SET:
			/* A set past the tracked ones is ignored. */
			if (op->u.descriptor.set < I915_GFX_MAX_SETS)
				state.dset[op->u.descriptor.set] = op->u.descriptor.dset;
			break;
		case I915_GFX_OP_PUSH_CONSTANTS:
			memcpy(state.push + op->u.push.offset, op->u.push.bytes, op->u.push.size);
			break;
		case I915_GFX_OP_DRAW:
			error = drv_i915_gfx_draw(session,
						  &state,
						  op->u.draw.vertex_count,
						  op->u.draw.instance_count,
						  op->u.draw.first_vertex,
						  op->u.draw.first_instance);
			break;
		default:
			error = EINVAL;
			break;
		}
	}

	/*
	 * Says where the list stopped.  The index has already moved past the
	 * failed operation, so the kind logged is the failed one's.
	 */
	if (error != 0) {
		kind = 0U;
		if (index != 0U)
			kind = (unsigned)cmdbuf->ops[index - 1U].kind;
		kern_logf("i915: vk: command buffer stopped at operation %u of %u (kind %u): error %d\n",
			  index,
			  cmdbuf->op_count,
			  kind,
			  error);
		return error;
	}

	/* Succeeded: every operation has run to its end. */
	return 0;
}

/*
 * vkQueueSubmit (libvulkan queue.c queue_write_submit): [queue][present]
 * [count]{[sType][pNext][present][count]{wait}[count]{stage}[present]
 * [count]{buffer}[present][count]{signal}}[fence] -> [result].
 *
 * The command buffers run in submission order, each to its end, before the
 * reply; the fence is signalled once all of them succeeded.
 * XXX: the waits and signals are decoded and not acted on: every earlier
 * submission has finished, so there is nothing to wait for.
 */
static int
i915_queue_submit(
	struct i915_render_session *session,
	struct i915_wire_reader *reader,
	struct i915_wire_writer *reply)
{
	struct i915_gfx_cmdbuf *cmdbufs[I915_GFX_MAX_SUBMITTED];
	struct i915_gfx_cmdbuf *cmdbuf;
	struct i915_vk_fence *fence;
	uint64_t identity;
	uint64_t submits;
	uint64_t submit;
	uint64_t count;
	uint64_t item;
	uint32_t total;
	uint32_t index;
	uint32_t result;
	int error;

	/* Decodes the number of submissions, at most eight. */
	(void)drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u32(reader);
	submits = drv_i915_wire_read_u64(reader);
	if (reader->error != 0 || submits > I915_GFX_MAX_SUBMITS)
		return EINVAL;

	/* Collects the command buffers of every submission, in order. */
	total = 0U;
	for (submit = 0U; submit < submits; submit++) {
		/* Decodes the submission's sType and pNext. */
		(void)drv_i915_wire_read_u32(reader);
		(void)drv_i915_wire_read_u64(reader);

		/* Decodes the wait semaphores. */
		(void)drv_i915_wire_read_u32(reader);
		count = drv_i915_wire_read_u64(reader);
		for (item = 0U; reader->error == 0 && item < count; item++)
			(void)drv_i915_wire_read_u64(reader);

		/* Decodes their stages. */
		count = drv_i915_wire_read_u64(reader);
		for (item = 0U; reader->error == 0 && item < count; item++)
			(void)drv_i915_wire_read_u32(reader);

		/* Decodes the number of command buffers, at most thirty-two. */
		(void)drv_i915_wire_read_u32(reader);
		count = drv_i915_wire_read_u64(reader);
		if (reader->error != 0 || count > I915_GFX_MAX_SUBMITTED)
			return EINVAL;

		/* Keeps every known command buffer while there is room. */
		for (item = 0U; item < count; item++) {
			identity = drv_i915_wire_read_u64(reader);
			cmdbuf = drv_i915_object_lookup(session->vk, I915_VK_OBJ_COMMAND_BUFFER, identity);
			if (cmdbuf != NULL && total < I915_GFX_MAX_SUBMITTED) {
				cmdbufs[total] = cmdbuf;
				total++;
			}
		}

		/* Decodes the signal semaphores. */
		(void)drv_i915_wire_read_u32(reader);
		count = drv_i915_wire_read_u64(reader);
		for (item = 0U; reader->error == 0 && item < count; item++)
			(void)drv_i915_wire_read_u64(reader);
	}

	/* Decodes the fence. */
	identity = drv_i915_wire_read_u64(reader);
	fence = drv_i915_object_lookup(session->vk, I915_VK_OBJ_FENCE, identity);
	if (reader->error != 0)
		return EINVAL;

	/* Runs the command buffers in order until one fails. */
	error = 0;
	for (index = 0U; index < total && error == 0; index++)
		error = i915_command_buffer_execute(session, cmdbufs[index]);

	/* Signals the fence: everything the submission asked for has been done by now. */
	if (error == 0 && fence != NULL) {
		drv_i915_fence_signal(fence);
	}

	/* Replies with the submission's result. */
	result = i915_command_result(error);
	drv_i915_wire_reply_u32(reply, result);

	/* Succeeded: the command was decoded; the reply carries the submission's result. */
	return 0;
}
