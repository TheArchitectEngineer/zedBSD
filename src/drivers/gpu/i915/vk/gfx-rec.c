/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Command pools, command buffers, recording and vkQueueSubmit of the executor's graphics path
 * (see gfx.h).  A recording command arrives as [opcode][0][command buffer][arguments]
 * (commands.c command_record_begin); what it says is kept as one entry of the command buffer's
 * operation list.
 *
 * XXX: EXECUTION MODEL OF THE CONNECTIVITY CHECK.  vkQueueSubmit walks the operation lists in
 * order and finishes every operation before it replies:
 *   - a draw becomes one GPU batch, run to its end (gfx-draw.c);
 *   - attachment clears and buffer <-> image copies are done BY THE CPU on the (linear, CPU-visible)
 *     storage, between those batches.  The order of the list is the order of execution, so the
 *     result is what the GPU commands would give; what is missing is the GPU doing them (BLT /
 *     clear passes) and any overlap between the CPU and the GPU.
 * The fence of the submission is signalled when vkQueueSubmit replies.
 */

#include "vkc.h"
#include "gfx.h"
#include "sync.h"

#include "../internal.h"

#include <kern/klog.h>
#include <kern/kmem.h>

#include <errno.h>

#include "codec-generated.inc"

#define GFX_MAX_OPS 64U

struct gfx_cmdpool;

struct gfx_cmdbuf {
	struct gfx_cmdbuf *next;		/* the pool's buffers */
	struct gfx_cmdpool *pool;
	uint64_t identity;
	uint32_t op_count;
	int overflow;
	struct gfx_op ops[GFX_MAX_OPS];
};

struct gfx_cmdpool {
	struct gfx_cmdbuf *buffers;
};

static uint32_t
gfx_result(int error)
{
	if (error == 0)
		return 0U;
	if (error == ENOMEM)
		return (uint32_t)VK_ERROR_OUT_OF_DEVICE_MEMORY;
	if (error == ETIMEDOUT || error == EIO)
		return (uint32_t)VK_ERROR_DEVICE_LOST;
	return (uint32_t)VK_ERROR_INITIALIZATION_FAILED;
}

/* ---------------- pools and buffers ---------------- */

static int
rec_create_pool(struct i915_vk_session *session, struct i915_vk_reader *reader, struct i915_vk_writer *reply)
{
	VkCommandPoolCreateInfo info;
	struct gfx_cmdpool *pool;
	uint64_t identity;
	int error;

	memset(&info, 0, sizeof(info));
	(void)i915_vk_read_u64(reader);
	(void)i915_vk_read_u64(reader);
	i915_vkc_dec_VkCommandPoolCreateInfo(reader, &session->arena, &info);
	(void)i915_vk_read_u64(reader);
	(void)i915_vk_read_u64(reader);
	identity = i915_vk_read_u64(reader);
	if (reader->error != 0)
		return EINVAL;

	pool = kern_calloc(1U, sizeof(*pool));
	error = pool == NULL ? ENOMEM : i915_vk_obj_insert(session->vk, I915_VK_OBJ_COMMAND_POOL, identity, pool);
	if (error != 0 && pool != NULL)
		kern_free(pool);
	i915_vk_reply_u32(reply, gfx_result(error));
	if (error == 0) {
		i915_vk_reply_u64(reply, 1U);
		i915_vk_reply_u64(reply, identity);
	}
	return 0;
}

static void
rec_free_buffer(struct i915_vk_session *session, struct gfx_cmdbuf *cmdbuf)
{
	struct gfx_cmdbuf **link;

	for (link = &cmdbuf->pool->buffers; *link != NULL; link = &(*link)->next)
		if (*link == cmdbuf) {
			*link = cmdbuf->next;
			break;
		}
	i915_vk_obj_remove(session->vk, I915_VK_OBJ_COMMAND_BUFFER, cmdbuf->identity);
	kern_free(cmdbuf);
}

/* vkDestroyCommandPool: [device][pool][pAllocator]; the pool's buffers go with it. */
static int
rec_destroy_pool(struct i915_vk_session *session, struct i915_vk_reader *reader)
{
	struct gfx_cmdpool *pool;
	uint64_t identity;

	(void)i915_vk_read_u64(reader);
	identity = i915_vk_read_u64(reader);
	(void)i915_vk_read_u64(reader);
	if (reader->error != 0)
		return EINVAL;

	pool = i915_vk_obj_lookup(session->vk, I915_VK_OBJ_COMMAND_POOL, identity);
	if (pool == NULL)
		return 0;
	while (pool->buffers != NULL)
		rec_free_buffer(session, pool->buffers);
	i915_vk_obj_remove(session->vk, I915_VK_OBJ_COMMAND_POOL, identity);
	kern_free(pool);
	return 0;
}

/* vkResetCommandPool: [device][pool][flags] -> [result]. */
static int
rec_reset_pool(struct i915_vk_session *session, struct i915_vk_reader *reader, struct i915_vk_writer *reply)
{
	struct gfx_cmdpool *pool;
	struct gfx_cmdbuf *cmdbuf;
	uint64_t identity;

	(void)i915_vk_read_u64(reader);
	identity = i915_vk_read_u64(reader);
	(void)i915_vk_read_u32(reader);
	if (reader->error != 0)
		return EINVAL;

	pool = i915_vk_obj_lookup(session->vk, I915_VK_OBJ_COMMAND_POOL, identity);
	for (cmdbuf = pool != NULL ? pool->buffers : NULL; cmdbuf != NULL; cmdbuf = cmdbuf->next) {
		cmdbuf->op_count = 0U;
		cmdbuf->overflow = 0;
	}
	i915_vk_reply_u32(reply, pool != NULL ? 0U : gfx_result(EINVAL));
	return 0;
}

/* vkAllocateCommandBuffers: [device][present][VkCommandBufferAllocateInfo][count][identities] -> [result][count][identities]. */
static int
rec_allocate(struct i915_vk_session *session, struct i915_vk_reader *reader, struct i915_vk_writer *reply)
{
	VkCommandBufferAllocateInfo info;
	struct gfx_cmdpool *pool;
	struct gfx_cmdbuf *cmdbuf;
	uint64_t identities[16];
	uint64_t count;
	uint64_t index;
	int error;

	memset(&info, 0, sizeof(info));
	(void)i915_vk_read_u64(reader);
	(void)i915_vk_read_u64(reader);
	i915_vkc_dec_VkCommandBufferAllocateInfo(reader, &session->arena, &info);
	count = i915_vk_read_u64(reader);
	if (reader->error != 0 || count > 16U)
		return EINVAL;
	for (index = 0U; index < count; index++)
		identities[index] = i915_vk_read_u64(reader);
	if (reader->error != 0)
		return EINVAL;

	pool = i915_vk_obj_lookup(session->vk, I915_VK_OBJ_COMMAND_POOL, (uint64_t)(uintptr_t)info.commandPool);
	error = pool == NULL ? EINVAL : 0;
	if (error == 0 && info.level != VK_COMMAND_BUFFER_LEVEL_PRIMARY) {
		kern_logf("i915: vk: XXX secondary command buffers are not implemented\n");
		error = ENOTSUP;
	}
	for (index = 0U; index < count && error == 0; index++) {
		cmdbuf = kern_calloc(1U, sizeof(*cmdbuf));
		if (cmdbuf == NULL) {
			error = ENOMEM;
			break;
		}
		cmdbuf->pool = pool;
		cmdbuf->identity = identities[index];
		error = i915_vk_obj_insert(session->vk, I915_VK_OBJ_COMMAND_BUFFER, identities[index], cmdbuf);
		if (error != 0) {
			kern_free(cmdbuf);
			break;
		}
		cmdbuf->next = pool->buffers;
		pool->buffers = cmdbuf;
	}
	/* XXX: a batch that fails part-way keeps its earlier buffers (happy path only). */

	i915_vk_reply_u32(reply, gfx_result(error));
	if (error == 0) {
		i915_vk_reply_u64(reply, count);
		for (index = 0U; index < count; index++)
			i915_vk_reply_u64(reply, identities[index]);
	}
	return 0;
}

/* vkFreeCommandBuffers: [device][pool][count][count][identities]. */
static int
rec_free(struct i915_vk_session *session, struct i915_vk_reader *reader)
{
	struct gfx_cmdbuf *cmdbuf;
	uint64_t count;
	uint64_t index;

	(void)i915_vk_read_u64(reader);
	(void)i915_vk_read_u64(reader);
	(void)i915_vk_read_u32(reader);
	count = i915_vk_read_u64(reader);
	if (reader->error != 0 || count > 64U)
		return EINVAL;
	for (index = 0U; index < count; index++) {
		cmdbuf = i915_vk_obj_lookup(session->vk, I915_VK_OBJ_COMMAND_BUFFER, i915_vk_read_u64(reader));
		if (reader->error == 0 && cmdbuf != NULL)
			rec_free_buffer(session, cmdbuf);
	}
	return reader->error != 0 ? EINVAL : 0;
}

/* vkBeginCommandBuffer: [command buffer][present][VkCommandBufferBeginInfo] -> [result]. */
static int
rec_begin(struct i915_vk_session *session, struct i915_vk_reader *reader, struct i915_vk_writer *reply)
{
	VkCommandBufferBeginInfo info;
	struct gfx_cmdbuf *cmdbuf;

	memset(&info, 0, sizeof(info));
	cmdbuf = i915_vk_obj_lookup(session->vk, I915_VK_OBJ_COMMAND_BUFFER, i915_vk_read_u64(reader));
	if (i915_vk_read_u64(reader) != 0U)
		i915_vkc_dec_VkCommandBufferBeginInfo(reader, &session->arena, &info);
	if (reader->error != 0)
		return EINVAL;

	if (cmdbuf != NULL) {
		cmdbuf->op_count = 0U;
		cmdbuf->overflow = 0;
	}
	i915_vk_reply_u32(reply, cmdbuf != NULL ? 0U : gfx_result(EINVAL));
	return 0;
}

/* vkEndCommandBuffer: [command buffer] -> [result]. */
static int
rec_end(struct i915_vk_session *session, struct i915_vk_reader *reader, struct i915_vk_writer *reply)
{
	struct gfx_cmdbuf *cmdbuf;

	cmdbuf = i915_vk_obj_lookup(session->vk, I915_VK_OBJ_COMMAND_BUFFER, i915_vk_read_u64(reader));
	if (reader->error != 0)
		return EINVAL;

	if (cmdbuf != NULL && cmdbuf->overflow != 0)
		kern_logf("i915: vk: XXX command buffer holds more than %u operations; the recording is refused\n", GFX_MAX_OPS);
	i915_vk_reply_u32(reply, cmdbuf != NULL && cmdbuf->overflow == 0 ? 0U :
		(uint32_t)VK_ERROR_OUT_OF_HOST_MEMORY);
	return 0;
}

/* ---------------- recording ---------------- */

/* The next free operation of a command buffer, zeroed; NULL (and the overflow latched) when full. */
static struct gfx_op *
rec_op(struct gfx_cmdbuf *cmdbuf, enum gfx_op_kind kind)
{
	static struct gfx_op discard;
	struct gfx_op *op;

	/* A recording for a command buffer that does not exist still has to be decoded to its end. */
	if (cmdbuf == NULL || cmdbuf->op_count >= GFX_MAX_OPS) {
		if (cmdbuf != NULL)
			cmdbuf->overflow = 1;
		memset(&discard, 0, sizeof(discard));
		return &discard;
	}
	op = &cmdbuf->ops[cmdbuf->op_count];
	cmdbuf->op_count++;
	memset(op, 0, sizeof(*op));
	op->kind = kind;
	return op;
}

/* vkCmdPipelineBarrier: decoded to its end and not acted on -- see the execution model above. */
static int
rec_barrier(struct i915_vk_session *session, struct i915_vk_reader *reader)
{
	VkMemoryBarrier memory;
	VkBufferMemoryBarrier buffer;
	VkImageMemoryBarrier image;
	uint64_t count;
	uint64_t index;

	(void)i915_vk_read_u32(reader);
	(void)i915_vk_read_u32(reader);
	(void)i915_vk_read_u32(reader);

	(void)i915_vk_read_u32(reader);
	count = i915_vk_read_u64(reader);
	for (index = 0U; reader->error == 0 && index < count; index++)
		i915_vkc_dec_VkMemoryBarrier(reader, &session->arena, &memory);
	(void)i915_vk_read_u32(reader);
	count = i915_vk_read_u64(reader);
	for (index = 0U; reader->error == 0 && index < count; index++)
		i915_vkc_dec_VkBufferMemoryBarrier(reader, &session->arena, &buffer);
	(void)i915_vk_read_u32(reader);
	count = i915_vk_read_u64(reader);
	for (index = 0U; reader->error == 0 && index < count; index++)
		i915_vkc_dec_VkImageMemoryBarrier(reader, &session->arena, &image);
	return reader->error != 0 ? EINVAL : 0;
}

/* vkCmdCopyBufferToImage: [buffer][image][layout][n][n]{region}; ImageToBuffer: [image][layout][buffer][n][n]{region}. */
static int
rec_copy(struct i915_vk_session *session, struct gfx_cmdbuf *cmdbuf, struct i915_vk_reader *reader, int to_image)
{
	struct gfx_buffer *buffer;
	struct gfx_image *image;
	struct gfx_op *op;
	uint64_t buffer_id;
	uint64_t image_id;
	uint64_t count;
	uint64_t index;

	if (to_image != 0) {
		buffer_id = i915_vk_read_u64(reader);
		image_id = i915_vk_read_u64(reader);
		(void)i915_vk_read_u32(reader);
	} else {
		image_id = i915_vk_read_u64(reader);
		(void)i915_vk_read_u32(reader);
		buffer_id = i915_vk_read_u64(reader);
	}
	(void)i915_vk_read_u32(reader);
	count = i915_vk_read_u64(reader);
	if (reader->error != 0 || count > GFX_MAX_OPS)
		return EINVAL;

	buffer = i915_vk_obj_lookup(session->vk, I915_VK_OBJ_BUFFER, buffer_id);
	image = i915_vk_obj_lookup(session->vk, I915_VK_OBJ_IMAGE, image_id);
	for (index = 0U; index < count; index++) {
		op = rec_op(cmdbuf, to_image != 0 ? GFX_OP_COPY_BUFFER_TO_IMAGE : GFX_OP_COPY_IMAGE_TO_BUFFER);
		op->u.copy.buffer = buffer;
		op->u.copy.image = image;
		i915_vkc_dec_VkBufferImageCopy(reader, &session->arena, &op->u.copy.region);
	}
	return reader->error != 0 ? EINVAL : 0;
}

/*
 * vkCmdBeginRenderPass (commands.c command_encode_render_begin): [present][sType][pNext]
 * [renderPass][framebuffer][VkRect2D][n][n]{clear}[contents]; a clear is [0][2][4]{4 words} for a
 * colour and [1][depth bits][stencil] for a depth / stencil attachment.
 */
static int
rec_begin_pass(struct i915_vk_session *session, struct gfx_cmdbuf *cmdbuf, struct i915_vk_reader *reader)
{
	VkRect2D area;
	struct gfx_op *op;
	uint64_t count;
	uint64_t index;
	uint32_t word;

	op = rec_op(cmdbuf, GFX_OP_BEGIN_PASS);
	if (i915_vk_read_u64(reader) == 0U) {
		(void)i915_vk_read_u32(reader);
		return EINVAL;
	}
	(void)i915_vk_read_u32(reader);
	(void)i915_vk_read_u64(reader);
	op->u.begin.pass = i915_vk_obj_lookup(session->vk, I915_VK_OBJ_RENDER_PASS, i915_vk_read_u64(reader));
	op->u.begin.framebuffer = i915_vk_obj_lookup(session->vk, I915_VK_OBJ_FRAMEBUFFER, i915_vk_read_u64(reader));
	i915_vkc_dec_VkRect2D(reader, &session->arena, &area);	/* XXX: the whole attachment is cleared, not the area */
	(void)i915_vk_read_u32(reader);
	count = i915_vk_read_u64(reader);
	if (reader->error != 0 || count > 64U)
		return EINVAL;

	for (index = 0U; index < count; index++) {
		uint32_t words[4] = { 0U, 0U, 0U, 0U };
		uint32_t is_depth;

		is_depth = i915_vk_read_u32(reader);
		if (is_depth != 0U) {
			words[0] = i915_vk_read_u32(reader);		/* depth, as its bits */
			words[1] = i915_vk_read_u32(reader);		/* stencil */
		} else {
			(void)i915_vk_read_u32(reader);			/* the union's tag */
			if (i915_vk_read_u64(reader) != 4U)
				reader->error = 1;
			for (word = 0U; word < 4U; word++)
				words[word] = i915_vk_read_u32(reader);
		}
		if (index < GFX_MAX_ATTACHMENTS) {
			op->u.begin.clear_is_depth[index] = is_depth;
			memcpy(op->u.begin.clear_words[index], words, sizeof(words));
			op->u.begin.clear_count = (uint32_t)index + 1U;
		}
	}
	(void)i915_vk_read_u32(reader);			/* contents */
	return reader->error != 0 ? EINVAL : 0;
}

static int
rec_bind_vertex(struct i915_vk_session *session, struct gfx_cmdbuf *cmdbuf, struct i915_vk_reader *reader)
{
	struct gfx_op *ops[GFX_MAX_VERTEX_BINDINGS];
	uint64_t count;
	uint64_t index;
	uint32_t first;

	first = i915_vk_read_u32(reader);
	(void)i915_vk_read_u32(reader);
	count = i915_vk_read_u64(reader);
	if (reader->error != 0 || count > GFX_MAX_VERTEX_BINDINGS)
		return EINVAL;
	for (index = 0U; index < count; index++) {
		ops[index] = rec_op(cmdbuf, GFX_OP_BIND_VERTEX_BUFFER);
		ops[index]->u.vertex.binding = first + (uint32_t)index;
		ops[index]->u.vertex.buffer = i915_vk_obj_lookup(session->vk, I915_VK_OBJ_BUFFER, i915_vk_read_u64(reader));
	}
	if (i915_vk_read_u64(reader) != count)
		reader->error = 1;
	for (index = 0U; reader->error == 0 && index < count; index++)
		ops[index]->u.vertex.offset = i915_vk_read_u64(reader);
	return reader->error != 0 ? EINVAL : 0;
}

static int
rec_bind_dsets(struct i915_vk_session *session, struct gfx_cmdbuf *cmdbuf, struct i915_vk_reader *reader)
{
	struct gfx_op *op;
	uint64_t count;
	uint64_t index;
	uint32_t first;

	(void)i915_vk_read_u32(reader);			/* bind point */
	(void)i915_vk_read_u64(reader);			/* layout */
	first = i915_vk_read_u32(reader);
	(void)i915_vk_read_u32(reader);
	count = i915_vk_read_u64(reader);
	if (reader->error != 0 || count > 4U)
		return EINVAL;
	for (index = 0U; index < count; index++) {
		op = rec_op(cmdbuf, GFX_OP_BIND_DESCRIPTOR_SET);
		op->u.descriptor.set = first + (uint32_t)index;
		op->u.descriptor.dset = i915_vk_obj_lookup(session->vk, I915_VK_OBJ_DESCRIPTOR_SET, i915_vk_read_u64(reader));
	}
	(void)i915_vk_read_u32(reader);
	count = i915_vk_read_u64(reader);
	if (reader->error != 0 || count > 64U)
		return EINVAL;
	for (index = 0U; index < count; index++)
		(void)i915_vk_read_u32(reader);		/* dynamic offsets: XXX no dynamic buffers are bound */
	return reader->error != 0 ? EINVAL : 0;
}

static int
rec_push(struct gfx_cmdbuf *cmdbuf, struct i915_vk_reader *reader)
{
	struct gfx_op *op;
	uint64_t count;

	op = rec_op(cmdbuf, GFX_OP_PUSH_CONSTANTS);
	(void)i915_vk_read_u64(reader);			/* layout */
	(void)i915_vk_read_u32(reader);			/* stages: XXX one block shared by every stage */
	op->u.push.offset = i915_vk_read_u32(reader);
	op->u.push.size = i915_vk_read_u32(reader);
	count = i915_vk_read_u64(reader);
	if (reader->error != 0 || count != op->u.push.size || op->u.push.offset > GFX_PUSH_BYTES ||
	    count > GFX_PUSH_BYTES - op->u.push.offset)
		return EINVAL;
	i915_vkc_read_bytes(reader, op->u.push.bytes, (size_t)count);
	return reader->error != 0 ? EINVAL : 0;
}

static int
rec_command(struct i915_vk_session *session, uint32_t opcode, struct i915_vk_reader *reader)
{
	struct gfx_cmdbuf *cmdbuf;
	struct gfx_op *op;

	cmdbuf = i915_vk_obj_lookup(session->vk, I915_VK_OBJ_COMMAND_BUFFER, i915_vk_read_u64(reader));
	if (reader->error != 0)
		return EINVAL;

	switch (opcode) {
	case 93U:	/* vkCmdBindPipeline: [bind point][pipeline] */
		op = rec_op(cmdbuf, GFX_OP_BIND_PIPELINE);
		(void)i915_vk_read_u32(reader);
		op->u.pipeline = i915_vk_obj_lookup(session->vk, I915_VK_OBJ_PIPELINE, i915_vk_read_u64(reader));
		break;
	case 103U:	/* vkCmdBindDescriptorSets */
		return rec_bind_dsets(session, cmdbuf, reader);
	case 105U:	/* vkCmdBindVertexBuffers */
		return rec_bind_vertex(session, cmdbuf, reader);
	case 106U:	/* vkCmdDraw: [vertices][instances][first vertex][first instance] */
		op = rec_op(cmdbuf, GFX_OP_DRAW);
		op->u.draw.vertex_count = i915_vk_read_u32(reader);
		op->u.draw.instance_count = i915_vk_read_u32(reader);
		op->u.draw.first_vertex = i915_vk_read_u32(reader);
		op->u.draw.first_instance = i915_vk_read_u32(reader);
		break;
	case 115U:	/* vkCmdCopyBufferToImage */
		return rec_copy(session, cmdbuf, reader, 1);
	case 116U:	/* vkCmdCopyImageToBuffer */
		return rec_copy(session, cmdbuf, reader, 0);
	case 126U:	/* vkCmdPipelineBarrier */
		return rec_barrier(session, reader);
	case 132U:	/* vkCmdPushConstants */
		return rec_push(cmdbuf, reader);
	case 133U:	/* vkCmdBeginRenderPass */
		return rec_begin_pass(session, cmdbuf, reader);
	case 135U:	/* vkCmdEndRenderPass */
		(void)rec_op(cmdbuf, GFX_OP_END_PASS);
		break;
	default:
		return ENOTSUP;
	}
	return reader->error != 0 ? EINVAL : 0;
}

/* ---------------- execution ---------------- */

/* round(value * 255) of a float given by its bits, clamped to [0, 1]; no floating-point register. */
static uint32_t
gfx_unorm8(uint32_t bits)
{
	uint32_t exponent;
	uint64_t mantissa;
	uint32_t shift;

	if ((bits >> 31) != 0U)
		return 0U;
	exponent = (bits >> 23) & 0xffU;
	if (exponent >= 127U)
		return 255U;			/* >= 1.0, infinities and NaNs */
	if (exponent < 127U - 16U)
		return 0U;
	/* value = 1.m * 2^(exponent - 127); 1.m is the 24-bit integer `mantissa` / 2^23 */
	mantissa = ((uint64_t)(bits & 0x7fffffU) | 0x800000U) * 255U;
	shift = 23U + (127U - exponent);
	return (uint32_t)((mantissa + ((uint64_t)1U << (shift - 1U))) >> shift);
}

static void
exec_clear(const struct gfx_op *op)
{
	struct gfx_framebuffer *framebuffer;
	struct gfx_pass *pass;
	struct gfx_image *image;
	uint32_t *pixels;
	uint64_t count;
	uint64_t pixel;
	uint32_t index;
	uint32_t value;

	pass = op->u.begin.pass;
	framebuffer = op->u.begin.framebuffer;
	if (pass == NULL || framebuffer == NULL)
		return;

	/* XXX: CPU clear -- see the execution model at the top of this file. */
	for (index = 0U; index < pass->attachment_count && index < framebuffer->view_count; index++) {
		if (pass->attachments[index].load_op != VK_ATTACHMENT_LOAD_OP_CLEAR || index >= op->u.begin.clear_count ||
		    framebuffer->views[index] == NULL)
			continue;
		image = framebuffer->views[index]->image;
		pixels = (uint32_t *)(void *)i915_vk_gfx_memory_cpu(image->memory, image->offset, image->bytes);
		if (pixels == NULL) {
			kern_logf("i915: vk: clear of attachment %u skipped: its image has no storage\n", index);
			continue;
		}

		if (op->u.begin.clear_is_depth[index] != 0U) {
			value = op->u.begin.clear_words[index][0];
		} else {
			uint32_t r = gfx_unorm8(op->u.begin.clear_words[index][0]);
			uint32_t g = gfx_unorm8(op->u.begin.clear_words[index][1]);
			uint32_t b = gfx_unorm8(op->u.begin.clear_words[index][2]);
			uint32_t a = gfx_unorm8(op->u.begin.clear_words[index][3]);

			/* bytes in memory order: R G B A, or B G R A */
			value = image->format == VK_FORMAT_B8G8R8A8_UNORM ?
				(b | (g << 8) | (r << 16) | (a << 24)) : (r | (g << 8) | (b << 16) | (a << 24));
		}
		count = image->bytes / 4U;
		for (pixel = 0U; pixel < count; pixel++)
			pixels[pixel] = value;
	}
}

static int
exec_copy(const struct gfx_op *op)
{
	const VkBufferImageCopy *region;
	struct gfx_buffer *buffer;
	struct gfx_image *image;
	uint8_t *image_bytes;
	uint8_t *buffer_bytes;
	uint64_t row_pixels;
	uint64_t buffer_row;
	uint64_t needed;
	uint32_t row;

	buffer = op->u.copy.buffer;
	image = op->u.copy.image;
	region = &op->u.copy.region;
	if (buffer == NULL || image == NULL)
		return EINVAL;

	/* XXX: CPU copy of whole texels of a 4-byte format, level 0, layer 0 -- see the execution model. */
	if (region->imageOffset.x < 0 || region->imageOffset.y < 0 || region->imageExtent.depth != 1U ||
	    (uint64_t)region->imageOffset.x + region->imageExtent.width > image->width ||
	    (uint64_t)region->imageOffset.y + region->imageExtent.height > image->height)
		return EINVAL;
	row_pixels = region->bufferRowLength != 0U ? region->bufferRowLength : region->imageExtent.width;
	if (row_pixels < region->imageExtent.width || region->imageExtent.height == 0U)
		return EINVAL;
	needed = region->bufferOffset + ((uint64_t)(region->imageExtent.height - 1U) * row_pixels +
		region->imageExtent.width) * 4U;
	if (needed > buffer->size)
		return EINVAL;

	image_bytes = i915_vk_gfx_memory_cpu(image->memory, image->offset, image->bytes);
	buffer_bytes = i915_vk_gfx_memory_cpu(buffer->memory, buffer->offset, buffer->size);
	if (image_bytes == NULL || buffer_bytes == NULL) {
		kern_logf("i915: vk: copy skipped: %s has no storage\n", image_bytes == NULL ? "the image" : "the buffer");
		return EINVAL;
	}

	for (row = 0U; row < region->imageExtent.height; row++) {
		uint8_t *texels = image_bytes + (uint64_t)(region->imageOffset.y + (int32_t)row) * image->pitch +
			(uint64_t)region->imageOffset.x * 4U;

		buffer_row = region->bufferOffset + (uint64_t)row * row_pixels * 4U;
		if (op->kind == GFX_OP_COPY_BUFFER_TO_IMAGE)
			memcpy(texels, buffer_bytes + buffer_row, (size_t)region->imageExtent.width * 4U);
		else
			memcpy(buffer_bytes + buffer_row, texels, (size_t)region->imageExtent.width * 4U);
	}
	return 0;
}

static int
exec_cmdbuf(struct i915_vk_session *session, struct gfx_cmdbuf *cmdbuf)
{
	struct gfx_draw_state state;
	const struct gfx_op *op;
	uint32_t index;
	int error;

	memset(&state, 0, sizeof(state));
	error = 0;
	for (index = 0U; index < cmdbuf->op_count && error == 0; index++) {
		op = &cmdbuf->ops[index];
		switch (op->kind) {
		case GFX_OP_COPY_BUFFER_TO_IMAGE:
		case GFX_OP_COPY_IMAGE_TO_BUFFER:
			error = exec_copy(op);
			break;
		case GFX_OP_BEGIN_PASS:
			state.pass = op->u.begin.pass;
			state.framebuffer = op->u.begin.framebuffer;
			exec_clear(op);
			break;
		case GFX_OP_END_PASS:
			state.pass = NULL;
			state.framebuffer = NULL;
			break;
		case GFX_OP_BIND_PIPELINE:
			state.pipeline = op->u.pipeline;
			break;
		case GFX_OP_BIND_VERTEX_BUFFER:
			if (op->u.vertex.binding < GFX_MAX_VERTEX_BINDINGS) {
				state.vertex[op->u.vertex.binding].buffer = op->u.vertex.buffer;
				state.vertex[op->u.vertex.binding].offset = op->u.vertex.offset;
			}
			break;
		case GFX_OP_BIND_DESCRIPTOR_SET:
			if (op->u.descriptor.set < 4U)
				state.dset[op->u.descriptor.set] = op->u.descriptor.dset;
			break;
		case GFX_OP_PUSH_CONSTANTS:
			memcpy(state.push + op->u.push.offset, op->u.push.bytes, op->u.push.size);
			break;
		case GFX_OP_DRAW:
			error = i915_vk_gfx_draw(session, &state, op->u.draw.vertex_count, op->u.draw.instance_count,
				op->u.draw.first_vertex, op->u.draw.first_instance);
			break;
		default:
			error = EINVAL;
			break;
		}
	}
	if (error != 0)
		kern_logf("i915: vk: command buffer stopped at operation %u of %u (kind %u): error %d\n",
			index, cmdbuf->op_count, index != 0U ? (unsigned)cmdbuf->ops[index - 1U].kind : 0U, error);
	return error;
}

/*
 * vkQueueSubmit (queue.c queue_write_submit): [queue][n][n]{[sType][pNext][waits][waits]{id}
 * [stages]{u32}[buffers][buffers]{id}[signals][signals]{id}}[fence] -> [result].
 */
static int
rec_submit(struct i915_vk_session *session, struct i915_vk_reader *reader, struct i915_vk_writer *reply)
{
	struct gfx_cmdbuf *cmdbufs[32];
	struct i915_vk_fence *fence;
	uint64_t submits;
	uint64_t submit;
	uint64_t count;
	uint64_t item;
	uint32_t total;
	uint32_t index;
	int error;

	(void)i915_vk_read_u64(reader);
	(void)i915_vk_read_u32(reader);
	submits = i915_vk_read_u64(reader);
	if (reader->error != 0 || submits > 8U)
		return EINVAL;

	total = 0U;
	for (submit = 0U; submit < submits; submit++) {
		(void)i915_vk_read_u32(reader);
		(void)i915_vk_read_u64(reader);

		/* waits and their stages: XXX nothing to wait for -- every earlier submission has finished */
		(void)i915_vk_read_u32(reader);
		count = i915_vk_read_u64(reader);
		for (item = 0U; reader->error == 0 && item < count; item++)
			(void)i915_vk_read_u64(reader);
		count = i915_vk_read_u64(reader);
		for (item = 0U; reader->error == 0 && item < count; item++)
			(void)i915_vk_read_u32(reader);

		(void)i915_vk_read_u32(reader);
		count = i915_vk_read_u64(reader);
		if (reader->error != 0 || count > 32U)
			return EINVAL;
		for (item = 0U; item < count; item++) {
			struct gfx_cmdbuf *cmdbuf = i915_vk_obj_lookup(session->vk, I915_VK_OBJ_COMMAND_BUFFER,
				i915_vk_read_u64(reader));

			if (cmdbuf != NULL && total < 32U)
				cmdbufs[total++] = cmdbuf;
		}

		(void)i915_vk_read_u32(reader);
		count = i915_vk_read_u64(reader);
		for (item = 0U; reader->error == 0 && item < count; item++)
			(void)i915_vk_read_u64(reader);
	}
	fence = i915_vk_obj_lookup(session->vk, I915_VK_OBJ_FENCE, i915_vk_read_u64(reader));
	if (reader->error != 0)
		return EINVAL;

	error = 0;
	for (index = 0U; index < total && error == 0; index++)
		error = exec_cmdbuf(session, cmdbufs[index]);

	/* Everything the submission asked for has been done (or has failed) by now. */
	if (error == 0 && fence != NULL)
		i915_vk_fence_signal(fence);
	i915_vk_reply_u32(reply, gfx_result(error));
	return 0;
}

/* ---------------- routing ---------------- */

int
i915_vk_gfx_rec_dispatch(struct i915_vk_session *session, uint32_t opcode,
	struct i915_vk_reader *reader, struct i915_vk_writer *reply, int *handled)
{
	int error;

	*handled = 1;
	switch (opcode) {
	case 18U: return rec_submit(session, reader, reply);
	case 85U: return rec_create_pool(session, reader, reply);
	case 86U: return rec_destroy_pool(session, reader);
	case 87U: return rec_reset_pool(session, reader, reply);
	case 88U: return rec_allocate(session, reader, reply);
	case 89U: return rec_free(session, reader);
	case 90U: return rec_begin(session, reader, reply);
	case 91U: return rec_end(session, reader, reply);
	default:
		break;
	}

	/* Every other opcode of the recording range is a vkCmd*. */
	if (opcode < 92U || opcode > 136U) {
		*handled = 0;
		return 0;
	}
	error = rec_command(session, opcode, reader);
	if (error == ENOTSUP) {
		kern_logf("i915: vk: XXX unimplemented opcode %u (recording)\n", opcode);
		reader->error = 1;
	}
	return error;
}
