/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The commands the executor relays as they came (R): the firmware's own
 * objects (semaphores, views, samplers, layouts, descriptor pools and
 * sets, framebuffers, render passes, command pools and buffers), the
 * recorded commands, and the queue's submission.
 *
 * Each command is decoded with the record codec libvulkan's encoder is
 * mirrored by, so a command whose wire is not the shape libvulkan sends
 * is refused, and the decode is what says where the command ends.  The
 * meaning of the objects and of the recordings is the firmware's; the
 * driver keeps only a render pass's attachments (for the shader compiler)
 * and binds the fence a submission signals.
 */

#include <stddef.h>
#include <stdint.h>

#include <kern/kcrt.h>
#include <uapi/errno.h>
#include <uapi/gpu-op.h>

#include "vk.h"
#include "vk-engine.h"

#include "drivers/gpu/i915/vk/vk-codec.inc"

/* The most elements of one array a command carries. */
#define ARRAY_MAX			65536U

/* The words of a clear colour on the wire: [u32 2][u64 4][4 x u32]. */
#define CLEAR_COLOR_TAG			2U
#define CLEAR_COLOR_WORDS		4U

/* The tags of a clear value: a colour, or a depth and a stencil value. */
#define CLEAR_VALUE_COLOR		0U
#define CLEAR_VALUE_DEPTH		1U

static int create_object(struct ghci_vk_build *build, uint32_t opcode, uint32_t flags, struct i915_wire_reader *reader);
static void decode_create_info(struct ghci_vk_build *build, uint32_t opcode, struct i915_wire_reader *reader);
static int keep_render_pass(struct ghci_vk_build *build, uint64_t identity, const VkRenderPassCreateInfo *info);
static int destroy_object(struct ghci_vk_build *build, uint32_t opcode, uint32_t flags, struct i915_wire_reader *reader);
static int relay_result(struct ghci_vk_build *build, uint32_t opcode, uint32_t flags, struct i915_wire_reader *reader, size_t start);
static int relay_result_span(struct ghci_vk_build *build, uint32_t opcode, uint32_t flags, struct i915_wire_reader *reader, size_t start);
static int allocate_many(struct ghci_vk_build *build, uint32_t opcode, uint32_t flags, struct i915_wire_reader *reader);
static int free_many(struct ghci_vk_build *build, uint32_t opcode, uint32_t flags, struct i915_wire_reader *reader);
static int update_sets(struct ghci_vk_build *build, uint32_t flags, struct i915_wire_reader *reader);
static void decode_write(struct i915_wire_reader *reader);
static int begin_buffer(struct ghci_vk_build *build, uint32_t flags, struct i915_wire_reader *reader);
static int queue_submit(struct ghci_vk_build *build, uint32_t flags, struct i915_wire_reader *reader);
static int record(struct ghci_vk_build *build, uint32_t opcode, uint32_t flags, struct i915_wire_reader *reader);
static int record_arguments(struct ghci_vk_build *build, uint32_t opcode, struct i915_wire_reader *reader);
static void decode_barriers(struct ghci_vk_build *build, struct i915_wire_reader *reader);
static void decode_transfer(struct ghci_vk_build *build, uint32_t opcode, struct i915_wire_reader *reader);
static void decode_clear(struct ghci_vk_build *build, uint32_t opcode, struct i915_wire_reader *reader);
static void decode_render_begin(struct i915_wire_reader *reader);
static void decode_color(struct i915_wire_reader *reader);
static void decode_clear_value(struct i915_wire_reader *reader);
static uint64_t read_array(struct i915_wire_reader *reader);
static void skip_u32s(struct i915_wire_reader *reader, uint64_t count);
static void skip_u64s(struct i915_wire_reader *reader, uint64_t count);
static void skip_bytes(struct i915_wire_reader *reader, uint64_t bytes);

/*
 * Runs a relayed command; *handled stays clear for a number no route
 * owns, which the frame refuses.
 */
int
ghci_vk_relay_route(
	struct ghci_vk_build *build,
	uint32_t opcode,
	uint32_t flags,
	struct i915_wire_reader *reader,
	int *handled)
{
	int error;

	/* Picks the command, or leaves it unhandled. */
	*handled = 1;
	switch (opcode) {
	case GPU_OP_CREATE_SEMAPHORE:
	case GPU_OP_CREATE_BUFFER_VIEW:
	case GPU_OP_CREATE_IMAGE_VIEW:
	case GPU_OP_CREATE_PIPELINE_LAYOUT:
	case GPU_OP_CREATE_SAMPLER:
	case GPU_OP_CREATE_DESCRIPTOR_SET_LAYOUT:
	case GPU_OP_CREATE_DESCRIPTOR_POOL:
	case GPU_OP_CREATE_FRAMEBUFFER:
	case GPU_OP_CREATE_RENDER_PASS:
	case GPU_OP_CREATE_COMMAND_POOL:
		error = create_object(build, opcode, flags, reader);
		break;
	case GPU_OP_DESTROY_SEMAPHORE:
	case GPU_OP_DESTROY_BUFFER_VIEW:
	case GPU_OP_DESTROY_IMAGE_VIEW:
	case GPU_OP_DESTROY_PIPELINE_LAYOUT:
	case GPU_OP_DESTROY_SAMPLER:
	case GPU_OP_DESTROY_DESCRIPTOR_SET_LAYOUT:
	case GPU_OP_DESTROY_DESCRIPTOR_POOL:
	case GPU_OP_DESTROY_FRAMEBUFFER:
	case GPU_OP_DESTROY_RENDER_PASS:
	case GPU_OP_DESTROY_COMMAND_POOL:
		error = destroy_object(build, opcode, flags, reader);
		break;
	case GPU_OP_RESET_DESCRIPTOR_POOL:
	case GPU_OP_RESET_COMMAND_POOL:
		error = relay_result(build, opcode, flags, reader, reader->offset);
		break;
	case GPU_OP_ALLOCATE_DESCRIPTOR_SETS:
	case GPU_OP_ALLOCATE_COMMAND_BUFFERS:
		error = allocate_many(build, opcode, flags, reader);
		break;
	case GPU_OP_FREE_DESCRIPTOR_SETS:
	case GPU_OP_FREE_COMMAND_BUFFERS:
		error = free_many(build, opcode, flags, reader);
		break;
	case GPU_OP_UPDATE_DESCRIPTOR_SETS:
		error = update_sets(build, flags, reader);
		break;
	case GPU_OP_BEGIN_COMMAND_BUFFER:
		error = begin_buffer(build, flags, reader);
		break;
	case GPU_OP_END_COMMAND_BUFFER:
	case GPU_OP_RESET_COMMAND_BUFFER:
		error = relay_result(build, opcode, flags, reader, reader->offset);
		break;
	case GPU_OP_QUEUE_SUBMIT:
		error = queue_submit(build, flags, reader);
		break;
	default:
		/* The recorded commands are a range of their own. */
		if (opcode < GPU_OP_CMD_BIND_PIPELINE || opcode > GPU_OP_CMD_EXECUTE_COMMANDS) {
			*handled = 0;
			return 0;
		}

		/* A recorded command. */
		error = record(build, opcode, flags, reader);
		break;
	}

	/* A command that could not be decoded. */
	if (error != 0)
		return error;

	/* Succeeded: the command is relayed. */
	return 0;
}

/*
 * Reads the tail of the create form.
 */
uint64_t
ghci_vk_create_tail(
	struct i915_wire_reader *reader)
{
	uint64_t allocator;
	uint64_t present;
	uint64_t identity;

	/* No allocator, the identity's presence word, then the identity. */
	allocator = drv_i915_wire_read_u64(reader);
	present = drv_i915_wire_read_u64(reader);
	identity = drv_i915_wire_read_u64(reader);

	/* An allocator or an absent identity is not the shape libvulkan sends. */
	if (allocator != 0U || present != 1U)
		reader->error = 1;
	if (identity == 0U)
		reader->error = 1;

	/* Reports the identity (meaningless after the reader's error). */
	return identity;
}

/*
 * Reads the destroy form.
 */
uint64_t
ghci_vk_destroy_form(
	struct i915_wire_reader *reader)
{
	uint64_t identity;
	uint64_t allocator;

	/* The device, the object, the allocator. */
	(void)drv_i915_wire_read_u64(reader);
	identity = drv_i915_wire_read_u64(reader);
	allocator = drv_i915_wire_read_u64(reader);
	if (allocator != 0U)
		reader->error = 1;

	/* Reports the identity (0 for the null handle, which destroys nothing). */
	return identity;
}

/*
 * Reports the VkResult of an executor errno.
 */
uint32_t
ghci_vk_result(
	int error)
{
	/* Memory first, then the rest, which the device cannot recover from. */
	if (error == 0)
		return (uint32_t)VK_SUCCESS;
	if (error == ENOMEM)
		return (uint32_t)VK_ERROR_OUT_OF_HOST_MEMORY;
	if (error == ENOSPC)
		return (uint32_t)VK_ERROR_OUT_OF_DEVICE_MEMORY;
	if (error == ENOTSUP)
		return (uint32_t)VK_ERROR_FEATURE_NOT_PRESENT;

	/* Anything else loses the device. */
	return (uint32_t)VK_ERROR_DEVICE_LOST;
}

/*
 * The create form of a firmware object: [device][u64 1][the create info]
 * [u64 0][u64 1][identity] -> [result][u64 1][identity], relayed.
 */
static int
create_object(
	struct ghci_vk_build *build,
	uint32_t opcode,
	uint32_t flags,
	struct i915_wire_reader *reader)
{
	uint64_t present;
	uint64_t identity;
	uint32_t index;
	size_t start;
	int error;

	/* The device, the create info, the tail. */
	start = reader->offset;
	(void)drv_i915_wire_read_u64(reader);
	present = drv_i915_wire_read_u64(reader);
	if (present != 1U)
		return EINVAL;
	decode_create_info(build, opcode, reader);
	if (reader->error != 0)
		return EINVAL;
	identity = ghci_vk_create_tail(reader);
	if (reader->error != 0)
		return EINVAL;

	/* The firmware makes the object. */
	error = ghci_vk_relay(build, opcode, flags, reader, start, &index);
	if (error != 0)
		return error;

	/* The reply: the firmware's result, the presence word and the identity. */
	if (flags != 0U) {
		error = ghci_vk_reply_result(build, index);
		if (error != 0)
			return error;
		drv_i915_wire_reply_u64(&build->reply, 1U);
		drv_i915_wire_reply_u64(&build->reply, identity);
	}

	/* Succeeded: the object is relayed. */
	return 0;
}

/*
 * Decodes the create info of a firmware object; a render pass is kept
 * for the shader compiler once the tail is read too.
 */
static void
decode_create_info(
	struct ghci_vk_build *build,
	uint32_t opcode,
	struct i915_wire_reader *reader)
{
	VkSemaphoreCreateInfo semaphore;
	VkBufferViewCreateInfo buffer_view;
	VkImageViewCreateInfo image_view;
	VkPipelineLayoutCreateInfo layout;
	VkSamplerCreateInfo sampler;
	VkDescriptorSetLayoutCreateInfo set_layout;
	VkDescriptorPoolCreateInfo pool;
	VkFramebufferCreateInfo framebuffer;
	VkRenderPassCreateInfo *pass;
	VkCommandPoolCreateInfo command_pool;
	struct i915_wire_arena *arena;
	struct i915_wire_reader tail;
	uint64_t identity;
	int error;

	/* The create info of the object's kind. */
	arena = &build->session->arena;
	switch (opcode) {
	case GPU_OP_CREATE_SEMAPHORE:
		i915_vkc_dec_VkSemaphoreCreateInfo(reader, arena, &semaphore);
		break;
	case GPU_OP_CREATE_BUFFER_VIEW:
		i915_vkc_dec_VkBufferViewCreateInfo(reader, arena, &buffer_view);
		break;
	case GPU_OP_CREATE_IMAGE_VIEW:
		i915_vkc_dec_VkImageViewCreateInfo(reader, arena, &image_view);
		break;
	case GPU_OP_CREATE_PIPELINE_LAYOUT:
		i915_vkc_dec_VkPipelineLayoutCreateInfo(reader, arena, &layout);
		break;
	case GPU_OP_CREATE_SAMPLER:
		i915_vkc_dec_VkSamplerCreateInfo(reader, arena, &sampler);
		break;
	case GPU_OP_CREATE_DESCRIPTOR_SET_LAYOUT:
		i915_vkc_dec_VkDescriptorSetLayoutCreateInfo(reader, arena, &set_layout);
		break;
	case GPU_OP_CREATE_DESCRIPTOR_POOL:
		i915_vkc_dec_VkDescriptorPoolCreateInfo(reader, arena, &pool);
		break;
	case GPU_OP_CREATE_FRAMEBUFFER:
		i915_vkc_dec_VkFramebufferCreateInfo(reader, arena, &framebuffer);
		break;
	case GPU_OP_CREATE_RENDER_PASS:
		/* The pass, and its identity from a copy of the reader, to keep it. */
		pass = ghci_vk_arena_take(build->session, sizeof(*pass));
		if (pass == NULL) {
			reader->error = 1;
			break;
		}

		/* The create info, then the identity from a copy of the reader. */
		i915_vkc_dec_VkRenderPassCreateInfo(reader, arena, pass);
		tail = *reader;
		identity = ghci_vk_create_tail(&tail);
		if (tail.error != 0)
			break;
		error = keep_render_pass(build, identity, pass);
		if (error != 0)
			reader->error = 1;
		break;
	default:
		i915_vkc_dec_VkCommandPoolCreateInfo(reader, arena, &command_pool);
		break;
	}
}

/* Keeps a render pass's attachments and each subpass's input attachments for the shader compiler. */
static int
keep_render_pass(
	struct ghci_vk_build *build,
	uint64_t identity,
	const VkRenderPassCreateInfo *info)
{
	struct ghci_vk_render_pass *pass;
	struct ghci_vk_object *object;
	const VkSubpassDescription *subpass;
	uint32_t index;
	uint32_t input;
	int error;

	/* The pass's record. */
	error = ghci_vk_object_add(build->session, GHCI_VK_RENDER_PASS, identity, &object);
	if (error != 0)
		return error;
	pass = &object->as.pass;

	/* The attachments' formats and samples, as many as are kept. */
	pass->attachment_count = info->attachmentCount;
	if (pass->attachment_count > GHCI_VK_PASS_ATTACHMENTS)
		pass->attachment_count = GHCI_VK_PASS_ATTACHMENTS;
	for (index = 0U; index < pass->attachment_count; index++) {
		if (info->pAttachments == NULL)
			break;

		pass->formats[index] = info->pAttachments[index].format;
		pass->samples[index] = info->pAttachments[index].samples;
	}

	/* Each subpass's input attachments by their index, VK_ATTACHMENT_UNUSED for none. */
	pass->subpass_count = info->subpassCount;
	if (pass->subpass_count > GHCI_VK_PASS_SUBPASSES)
		pass->subpass_count = GHCI_VK_PASS_SUBPASSES;
	for (index = 0U; index < pass->subpass_count; index++) {
		if (info->pSubpasses == NULL)
			break;

		/* The subpass's inputs, as many as a program reads. */
		subpass = &info->pSubpasses[index];
		pass->input_count[index] = subpass->inputAttachmentCount;
		if (pass->input_count[index] > GHCI_GSI1_INPUT_ATTACHMENTS)
			pass->input_count[index] = GHCI_GSI1_INPUT_ATTACHMENTS;
		for (input = 0U; input < pass->input_count[index]; input++) {
			pass->inputs[index][input] = VK_ATTACHMENT_UNUSED;
			if (subpass->pInputAttachments != NULL)
				pass->inputs[index][input] = subpass->pInputAttachments[input].attachment;
		}
	}

	/* Succeeded: the pass is kept. */
	return 0;
}

/* The destroy form of a firmware object, relayed; a render pass's record goes too. */
static int
destroy_object(
	struct ghci_vk_build *build,
	uint32_t opcode,
	uint32_t flags,
	struct i915_wire_reader *reader)
{
	struct ghci_vk_object *object;
	uint64_t identity;
	uint32_t index;
	size_t start;
	int error;

	/* The destroy form. */
	start = reader->offset;
	identity = ghci_vk_destroy_form(reader);
	error = ghci_vk_relay(build, opcode, flags, reader, start, &index);
	if (error != 0)
		return error;

	/* The render pass the driver kept. */
	if (opcode == GPU_OP_DESTROY_RENDER_PASS) {
		object = ghci_vk_object_find(build->session, GHCI_VK_RENDER_PASS, identity);
		if (object != NULL)
			ghci_vk_object_remove(build->session, object);
	}

	/* Succeeded: the object is destroyed. */
	return 0;
}

/*
 * Relays a command whose reply is [result] alone: the pools' resets
 * ([device][pool][u32 flags]) and the command buffers' end ([cb]) and
 * reset ([cb][u32 flags]).
 */
static int
relay_result(
	struct ghci_vk_build *build,
	uint32_t opcode,
	uint32_t flags,
	struct i915_wire_reader *reader,
	size_t start)
{
	int error;

	/* The arguments of each command. */
	if (opcode == GPU_OP_RESET_DESCRIPTOR_POOL || opcode == GPU_OP_RESET_COMMAND_POOL) {
		(void)drv_i915_wire_read_u64(reader);
		(void)drv_i915_wire_read_u64(reader);
		(void)drv_i915_wire_read_u32(reader);
	} else if (opcode == GPU_OP_END_COMMAND_BUFFER) {
		(void)drv_i915_wire_read_u64(reader);
	} else {
		(void)drv_i915_wire_read_u64(reader);
		(void)drv_i915_wire_read_u32(reader);
	}

	/* The command as it came, with the firmware's result as its reply. */
	error = relay_result_span(build, opcode, flags, reader, start);
	if (error != 0)
		return error;

	/* Succeeded: the command is relayed. */
	return 0;
}

/* Relays the decoded span from start as one record whose reply is the firmware's result. */
static int
relay_result_span(
	struct ghci_vk_build *build,
	uint32_t opcode,
	uint32_t flags,
	struct i915_wire_reader *reader,
	size_t start)
{
	uint32_t index;
	int error;

	/* The command as it came. */
	error = ghci_vk_relay(build, opcode, flags, reader, start, &index);
	if (error != 0)
		return error;

	/* The firmware's result. */
	if (flags != 0U) {
		error = ghci_vk_reply_result(build, index);
		if (error != 0)
			return error;
	}

	/* Succeeded: the command is relayed. */
	return 0;
}

/*
 * vkAllocateDescriptorSets and vkAllocateCommandBuffers: [device][u64 1]
 * [the allocate info][u64 count][count x identity] -> [result][u64 count]
 * [count x identity], relayed.
 */
static int
allocate_many(
	struct ghci_vk_build *build,
	uint32_t opcode,
	uint32_t flags,
	struct i915_wire_reader *reader)
{
	VkDescriptorSetAllocateInfo sets;
	VkCommandBufferAllocateInfo buffers;
	const uint8_t *identities;
	uint64_t present;
	uint64_t count;
	uint64_t index;
	uint64_t identity;
	uint32_t record_index;
	size_t start;
	int error;

	/* The device and the allocate info. */
	start = reader->offset;
	(void)drv_i915_wire_read_u64(reader);
	present = drv_i915_wire_read_u64(reader);
	if (present != 1U)
		return EINVAL;
	if (opcode == GPU_OP_ALLOCATE_DESCRIPTOR_SETS) {
		i915_vkc_dec_VkDescriptorSetAllocateInfo(reader, &build->session->arena, &sets);
	} else {
		i915_vkc_dec_VkCommandBufferAllocateInfo(reader, &build->session->arena, &buffers);
	}

	/* The identities libvulkan reserved, which the reply echoes. */
	count = read_array(reader);
	identities = drv_i915_wire_read_array(reader, (size_t)count, 8U);
	if (reader->error != 0)
		return EINVAL;

	/* The firmware makes them. */
	error = ghci_vk_relay(build, opcode, flags, reader, start, &record_index);
	if (error != 0)
		return error;

	/* The firmware's result, the count and the identities. */
	if (flags != 0U) {
		error = ghci_vk_reply_result(build, record_index);
		if (error != 0)
			return error;
		drv_i915_wire_reply_u64(&build->reply, count);
		for (index = 0U; index < count; index++) {
			kern_memcpy(&identity, identities + index * 8U, 8U);
			drv_i915_wire_reply_u64(&build->reply, identity);
		}
	}

	/* Succeeded: the objects are relayed. */
	return 0;
}

/*
 * vkFreeDescriptorSets ([device][pool][u32 count][u64 count][ids] ->
 * [result]) and vkFreeCommandBuffers (the same, reply the echo alone).
 */
static int
free_many(
	struct ghci_vk_build *build,
	uint32_t opcode,
	uint32_t flags,
	struct i915_wire_reader *reader)
{
	uint64_t count;
	uint32_t index;
	size_t start;
	int error;

	/* The device, the pool and the identities. */
	start = reader->offset;
	(void)drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u32(reader);
	count = read_array(reader);
	skip_u64s(reader, count);

	/* The command as it came. */
	error = ghci_vk_relay(build, opcode, flags, reader, start, &index);
	if (error != 0)
		return error;

	/* Freeing sets reports the firmware's result. */
	if (flags != 0U && opcode == GPU_OP_FREE_DESCRIPTOR_SETS) {
		error = ghci_vk_reply_result(build, index);
		if (error != 0)
			return error;
	}

	/* Succeeded: the objects are freed. */
	return 0;
}

/*
 * vkUpdateDescriptorSets: [device][u32 writes][u64 writes][writes]
 * [u32 copies][u64 copies][copies], no reply body, relayed.
 */
static int
update_sets(
	struct ghci_vk_build *build,
	uint32_t flags,
	struct i915_wire_reader *reader)
{
	VkCopyDescriptorSet copy;
	uint64_t count;
	uint64_t index;
	uint32_t record_index;
	size_t start;
	int error;

	/* The device and the writes. */
	start = reader->offset;
	(void)drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u32(reader);
	count = read_array(reader);
	for (index = 0U; index < count && reader->error == 0; index++)
		decode_write(reader);

	/* The copies. */
	(void)drv_i915_wire_read_u32(reader);
	count = read_array(reader);
	for (index = 0U; index < count && reader->error == 0; index++)
		i915_vkc_dec_VkCopyDescriptorSet(reader, &build->session->arena, &copy);

	/* The command as it came. */
	error = ghci_vk_relay(build, GPU_OP_UPDATE_DESCRIPTOR_SETS, flags, reader, start, &record_index);
	if (error != 0)
		return error;

	/* Succeeded: the update is relayed. */
	return 0;
}

/*
 * Decodes one VkWriteDescriptorSet as libvulkan writes it: [sType][u64 0]
 * [set][binding][element][count][type], then the images
 * [u64 n][n x {sampler, view, layout}], the buffers
 * [u64 n][n x {buffer, offset, range}] and the texel buffer views
 * [u64 n][n x view].
 */
static void
decode_write(
	struct i915_wire_reader *reader)
{
	uint64_t count;
	uint64_t chain;

	/* The head. */
	(void)drv_i915_wire_read_u32(reader);
	chain = drv_i915_wire_read_u64(reader);
	if (chain != 0U)
		reader->error = 1;
	(void)drv_i915_wire_read_u64(reader);
	skip_u32s(reader, 4U);

	/* The images: a sampler, a view and a layout each. */
	count = drv_i915_wire_read_u64(reader);
	if (count > ARRAY_MAX)
		reader->error = 1;
	skip_bytes(reader, count * 20U);

	/* The buffers: an identity, an offset and a range each. */
	count = drv_i915_wire_read_u64(reader);
	if (count > ARRAY_MAX)
		reader->error = 1;
	skip_u64s(reader, count * 3U);

	/* The texel buffer views. */
	count = drv_i915_wire_read_u64(reader);
	skip_u64s(reader, count);
}

/* vkBeginCommandBuffer: [cb][u64 1][VkCommandBufferBeginInfo] -> [result], relayed. */
static int
begin_buffer(
	struct ghci_vk_build *build,
	uint32_t flags,
	struct i915_wire_reader *reader)
{
	VkCommandBufferBeginInfo info;
	uint64_t present;
	size_t start;
	int error;

	/* The command buffer and the begin info. */
	start = reader->offset;
	(void)drv_i915_wire_read_u64(reader);
	present = drv_i915_wire_read_u64(reader);
	if (present != 1U)
		return EINVAL;
	i915_vkc_dec_VkCommandBufferBeginInfo(reader, &build->session->arena, &info);
	if (reader->error != 0)
		return EINVAL;

	/* Relayed, with the firmware's result as the reply. */
	error = relay_result_span(build, GPU_OP_BEGIN_COMMAND_BUFFER, flags, reader, start);
	if (error != 0)
		return error;

	/* Succeeded: the begin is relayed. */
	return 0;
}

/*
 * vkQueueSubmit: [queue][u32 count][u64 count][count x VkSubmitInfo]
 * [fence] -> [result], relayed.  The device's fence after the frame
 * signals the VkFence.
 */
static int
queue_submit(
	struct ghci_vk_build *build,
	uint32_t flags,
	struct i915_wire_reader *reader)
{
	VkSubmitInfo info;
	uint64_t count;
	uint64_t index;
	uint64_t fence;
	size_t start;
	int error;

	/* The queue, the submissions and the fence. */
	start = reader->offset;
	(void)drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u32(reader);
	count = read_array(reader);
	for (index = 0U; index < count && reader->error == 0; index++)
		i915_vkc_dec_VkSubmitInfo(reader, &build->session->arena, &info);
	fence = drv_i915_wire_read_u64(reader);
	if (reader->error != 0)
		return EINVAL;

	/* Relayed, with the firmware's acceptance as the reply. */
	error = relay_result_span(build, GPU_OP_QUEUE_SUBMIT, flags, reader, start);
	if (error != 0)
		return error;

	/* The fence signals when the frame's work has finished. */
	if (fence != 0U) {
		error = ghci_vk_fence_later(build, fence);
		if (error != 0)
			return error;
	}

	/* Succeeded: the submission is relayed. */
	return 0;
}

/*
 * A recorded command: [cb] and the Vulkan command's arguments, no reply.
 */
static int
record(
	struct ghci_vk_build *build,
	uint32_t opcode,
	uint32_t flags,
	struct i915_wire_reader *reader)
{
	uint32_t index;
	size_t start;
	int error;

	/* A recorded command has no reply. */
	if (flags != 0U)
		return EINVAL;

	/* The command buffer, then the arguments. */
	start = reader->offset;
	(void)drv_i915_wire_read_u64(reader);
	error = record_arguments(build, opcode, reader);
	if (error != 0)
		return error;
	if (reader->error != 0)
		return EINVAL;

	/* The firmware records it. */
	error = ghci_vk_relay(build, opcode, flags, reader, start, &index);
	if (error != 0)
		return error;

	/* Succeeded: the command is relayed. */
	return 0;
}

/*
 * Decodes a recorded command's arguments.  Every recorded command of
 * Vulkan 1.0 is relayed: the firmware says in the result of the
 * recording's end when it cannot do one.
 */
static int
record_arguments(
	struct ghci_vk_build *build,
	uint32_t opcode,
	struct i915_wire_reader *reader)
{
	VkViewport viewport;
	VkRect2D rect;
	uint64_t count;
	uint64_t index;

	/* The arguments of each command. */
	switch (opcode) {
	case GPU_OP_CMD_BIND_PIPELINE:
		/* [bind point][pipeline]. */
		(void)drv_i915_wire_read_u32(reader);
		(void)drv_i915_wire_read_u64(reader);
		break;
	case GPU_OP_CMD_SET_VIEWPORT:
		/* [first][count][u64 count][viewports]. */
		skip_u32s(reader, 2U);
		count = read_array(reader);
		for (index = 0U; index < count && reader->error == 0; index++)
			i915_vkc_dec_VkViewport(reader, &build->session->arena, &viewport);
		break;
	case GPU_OP_CMD_SET_SCISSOR:
		/* [first][count][u64 count][rectangles]. */
		skip_u32s(reader, 2U);
		count = read_array(reader);
		for (index = 0U; index < count && reader->error == 0; index++)
			i915_vkc_dec_VkRect2D(reader, &build->session->arena, &rect);
		break;
	case GPU_OP_CMD_SET_LINE_WIDTH:
		/* [float width]. */
		skip_u32s(reader, 1U);
		break;
	case GPU_OP_CMD_SET_DEPTH_BIAS:
		/* [constant][clamp][slope]. */
		skip_u32s(reader, 3U);
		break;
	case GPU_OP_CMD_SET_BLEND_CONSTANTS:
		/* [u64 4][4 x float]. */
		count = read_array(reader);
		if (count != 4U)
			reader->error = 1;
		skip_u32s(reader, count);
		break;
	case GPU_OP_CMD_SET_DEPTH_BOUNDS:
	case GPU_OP_CMD_SET_STENCIL_COMPARE_MASK:
	case GPU_OP_CMD_SET_STENCIL_WRITE_MASK:
	case GPU_OP_CMD_SET_STENCIL_REFERENCE:
		/* Two words: the bounds, or the faces and their value. */
		skip_u32s(reader, 2U);
		break;
	case GPU_OP_CMD_BIND_DESCRIPTOR_SETS:
		/* [bind point][layout][first][count][u64 count][sets][dynamic count][u64 count][offsets]. */
		(void)drv_i915_wire_read_u32(reader);
		(void)drv_i915_wire_read_u64(reader);
		skip_u32s(reader, 2U);
		count = read_array(reader);
		skip_u64s(reader, count);
		(void)drv_i915_wire_read_u32(reader);
		count = read_array(reader);
		skip_u32s(reader, count);
		break;
	case GPU_OP_CMD_BIND_INDEX_BUFFER:
		/* [buffer][u64 offset][index type]. */
		skip_u64s(reader, 2U);
		(void)drv_i915_wire_read_u32(reader);
		break;
	case GPU_OP_CMD_BIND_VERTEX_BUFFERS:
		/* [first][count][u64 count][buffers][u64 count][offsets]. */
		skip_u32s(reader, 2U);
		count = read_array(reader);
		skip_u64s(reader, count);
		count = read_array(reader);
		skip_u64s(reader, count);
		break;
	case GPU_OP_CMD_DRAW:
		/* [vertices][instances][first vertex][first instance]. */
		skip_u32s(reader, 4U);
		break;
	case GPU_OP_CMD_DRAW_INDEXED:
		/* [indices][instances][first index][vertex offset][first instance]. */
		skip_u32s(reader, 5U);
		break;
	case GPU_OP_CMD_DRAW_INDIRECT:
	case GPU_OP_CMD_DRAW_INDEXED_INDIRECT:
		/* [buffer][u64 offset][draw count][stride]. */
		skip_u64s(reader, 2U);
		skip_u32s(reader, 2U);
		break;
	case GPU_OP_CMD_DISPATCH:
		/* [x][y][z]. */
		skip_u32s(reader, 3U);
		break;
	case GPU_OP_CMD_DISPATCH_INDIRECT:
		/* [buffer][u64 offset]. */
		skip_u64s(reader, 2U);
		break;
	case GPU_OP_CMD_COPY_BUFFER:
	case GPU_OP_CMD_COPY_IMAGE:
	case GPU_OP_CMD_BLIT_IMAGE:
	case GPU_OP_CMD_COPY_BUFFER_TO_IMAGE:
	case GPU_OP_CMD_COPY_IMAGE_TO_BUFFER:
	case GPU_OP_CMD_RESOLVE_IMAGE:
		decode_transfer(build, opcode, reader);
		break;
	case GPU_OP_CMD_UPDATE_BUFFER:
		/* [buffer][u64 offset][u64 size][u64 count][the bytes, padded to words]. */
		skip_u64s(reader, 3U);
		count = read_array(reader);
		skip_bytes(reader, (count + 3U) & ~(uint64_t)3U);
		break;
	case GPU_OP_CMD_FILL_BUFFER:
		/* [buffer][u64 offset][u64 size][data]. */
		skip_u64s(reader, 3U);
		(void)drv_i915_wire_read_u32(reader);
		break;
	case GPU_OP_CMD_CLEAR_COLOR_IMAGE:
	case GPU_OP_CMD_CLEAR_DEPTH_STENCIL_IMAGE:
	case GPU_OP_CMD_CLEAR_ATTACHMENTS:
		decode_clear(build, opcode, reader);
		break;
	case GPU_OP_CMD_SET_EVENT:
	case GPU_OP_CMD_RESET_EVENT:
		/* [event][stage mask]. */
		(void)drv_i915_wire_read_u64(reader);
		(void)drv_i915_wire_read_u32(reader);
		break;
	case GPU_OP_CMD_WAIT_EVENTS:
		/* [count][u64 count][events][source stages][destination stages], then the barriers. */
		(void)drv_i915_wire_read_u32(reader);
		count = read_array(reader);
		skip_u64s(reader, count);
		skip_u32s(reader, 2U);
		decode_barriers(build, reader);
		break;
	case GPU_OP_CMD_PIPELINE_BARRIER:
		/* [source stages][destination stages][dependency flags], then the barriers. */
		skip_u32s(reader, 3U);
		decode_barriers(build, reader);
		break;
	case GPU_OP_CMD_BEGIN_QUERY:
		/* [pool][query][flags]. */
		(void)drv_i915_wire_read_u64(reader);
		skip_u32s(reader, 2U);
		break;
	case GPU_OP_CMD_END_QUERY:
		/* [pool][query]. */
		(void)drv_i915_wire_read_u64(reader);
		(void)drv_i915_wire_read_u32(reader);
		break;
	case GPU_OP_CMD_RESET_QUERY_POOL:
		/* [pool][first][count]. */
		(void)drv_i915_wire_read_u64(reader);
		skip_u32s(reader, 2U);
		break;
	case GPU_OP_CMD_WRITE_TIMESTAMP:
		/* [stage][pool][query]. */
		(void)drv_i915_wire_read_u32(reader);
		(void)drv_i915_wire_read_u64(reader);
		(void)drv_i915_wire_read_u32(reader);
		break;
	case GPU_OP_CMD_COPY_QUERY_POOL_RESULTS:
		/* [pool][first][count][buffer][u64 offset][u64 stride][flags]. */
		(void)drv_i915_wire_read_u64(reader);
		skip_u32s(reader, 2U);
		skip_u64s(reader, 3U);
		(void)drv_i915_wire_read_u32(reader);
		break;
	case GPU_OP_CMD_PUSH_CONSTANTS:
		/* [layout][stages][offset][size][u64 count][the bytes, padded to words]. */
		(void)drv_i915_wire_read_u64(reader);
		skip_u32s(reader, 3U);
		count = read_array(reader);
		skip_bytes(reader, (count + 3U) & ~(uint64_t)3U);
		break;
	case GPU_OP_CMD_BEGIN_RENDER_PASS:
		/* [u64 present][the begin info][contents]. */
		count = drv_i915_wire_read_u64(reader);
		if (count != 0U)
			decode_render_begin(reader);
		(void)drv_i915_wire_read_u32(reader);
		break;
	case GPU_OP_CMD_NEXT_SUBPASS:
		/* [contents]. */
		(void)drv_i915_wire_read_u32(reader);
		break;
	case GPU_OP_CMD_END_RENDER_PASS:
		/* Nothing. */
		break;
	case GPU_OP_CMD_EXECUTE_COMMANDS:
		/* [count][u64 count][command buffers]. */
		(void)drv_i915_wire_read_u32(reader);
		count = read_array(reader);
		skip_u64s(reader, count);
		break;
	default:
		/* A number of the range that Vulkan 1.0 does not have. */
		return ENOTSUP;
	}

	/* Succeeded: the arguments are decoded (the reader says whether they were whole). */
	return 0;
}

/*
 * Decodes the barrier arrays of a pipeline barrier or an event wait:
 * memory, buffer and image barriers, each [u32 count][u64 count][barriers].
 */
static void
decode_barriers(
	struct ghci_vk_build *build,
	struct i915_wire_reader *reader)
{
	VkMemoryBarrier memory;
	VkBufferMemoryBarrier buffer;
	VkImageMemoryBarrier image;
	uint64_t count;
	uint64_t index;

	/* The memory barriers. */
	(void)drv_i915_wire_read_u32(reader);
	count = read_array(reader);
	for (index = 0U; index < count && reader->error == 0; index++)
		i915_vkc_dec_VkMemoryBarrier(reader, &build->session->arena, &memory);

	/* The buffer barriers. */
	(void)drv_i915_wire_read_u32(reader);
	count = read_array(reader);
	for (index = 0U; index < count && reader->error == 0; index++)
		i915_vkc_dec_VkBufferMemoryBarrier(reader, &build->session->arena, &buffer);

	/* The image barriers. */
	(void)drv_i915_wire_read_u32(reader);
	count = read_array(reader);
	for (index = 0U; index < count && reader->error == 0; index++)
		i915_vkc_dec_VkImageMemoryBarrier(reader, &build->session->arena, &image);
}

/*
 * Decodes a copy, a blit or a resolve: the source and the destination
 * (with their layouts where they are images), [count][u64 count] and the
 * regions, and a blit's filter.
 */
static void
decode_transfer(
	struct ghci_vk_build *build,
	uint32_t opcode,
	struct i915_wire_reader *reader)
{
	VkBufferCopy buffer_copy;
	VkImageCopy image_copy;
	VkImageBlit blit;
	VkBufferImageCopy buffer_image;
	VkImageResolve resolve;
	struct i915_wire_arena *arena;
	uint64_t count;
	uint64_t index;

	/* The source and the destination: a buffer is its identity, an image its identity and layout. */
	arena = &build->session->arena;
	switch (opcode) {
	case GPU_OP_CMD_COPY_BUFFER:
		skip_u64s(reader, 2U);
		break;
	case GPU_OP_CMD_COPY_BUFFER_TO_IMAGE:
		skip_u64s(reader, 2U);
		(void)drv_i915_wire_read_u32(reader);
		break;
	case GPU_OP_CMD_COPY_IMAGE_TO_BUFFER:
		(void)drv_i915_wire_read_u64(reader);
		(void)drv_i915_wire_read_u32(reader);
		(void)drv_i915_wire_read_u64(reader);
		break;
	default:
		(void)drv_i915_wire_read_u64(reader);
		(void)drv_i915_wire_read_u32(reader);
		(void)drv_i915_wire_read_u64(reader);
		(void)drv_i915_wire_read_u32(reader);
		break;
	}

	/* The regions of the command's kind. */
	(void)drv_i915_wire_read_u32(reader);
	count = read_array(reader);
	for (index = 0U; index < count && reader->error == 0; index++) {
		/* One region. */
		switch (opcode) {
		case GPU_OP_CMD_COPY_BUFFER:
			i915_vkc_dec_VkBufferCopy(reader, arena, &buffer_copy);
			break;
		case GPU_OP_CMD_COPY_IMAGE:
			i915_vkc_dec_VkImageCopy(reader, arena, &image_copy);
			break;
		case GPU_OP_CMD_BLIT_IMAGE:
			i915_vkc_dec_VkImageBlit(reader, arena, &blit);
			break;
		case GPU_OP_CMD_RESOLVE_IMAGE:
			i915_vkc_dec_VkImageResolve(reader, arena, &resolve);
			break;
		default:
			i915_vkc_dec_VkBufferImageCopy(reader, arena, &buffer_image);
			break;
		}
	}

	/* A blit's filter. */
	if (opcode == GPU_OP_CMD_BLIT_IMAGE)
		(void)drv_i915_wire_read_u32(reader);
}

/*
 * Decodes the clears: an image's colour ([image][layout][u64 present]
 * [colour][count][u64 count][ranges]) or depth and stencil ([image]
 * [layout][u64 present][float][u32][count][u64 count][ranges]), and the
 * attachments' ([count][u64 count][attachments][count][u64 count][rects]).
 */
static void
decode_clear(
	struct ghci_vk_build *build,
	uint32_t opcode,
	struct i915_wire_reader *reader)
{
	VkImageSubresourceRange range;
	VkClearRect rect;
	uint64_t present;
	uint64_t count;
	uint64_t index;

	/* The attachments: each [aspect][attachment][the tagged value], then the rectangles. */
	if (opcode == GPU_OP_CMD_CLEAR_ATTACHMENTS) {
		(void)drv_i915_wire_read_u32(reader);
		count = read_array(reader);
		for (index = 0U; index < count && reader->error == 0; index++) {
			skip_u32s(reader, 2U);
			decode_clear_value(reader);
		}

		/* The rectangles. */
		(void)drv_i915_wire_read_u32(reader);
		count = read_array(reader);
		for (index = 0U; index < count && reader->error == 0; index++)
			i915_vkc_dec_VkClearRect(reader, &build->session->arena, &rect);
		return;
	}

	/* The image, its layout and the value. */
	(void)drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u32(reader);
	present = drv_i915_wire_read_u64(reader);
	if (present != 0U && opcode == GPU_OP_CMD_CLEAR_COLOR_IMAGE) {
		decode_color(reader);
	} else if (present != 0U) {
		skip_u32s(reader, 2U);
	}

	/* The ranges. */
	(void)drv_i915_wire_read_u32(reader);
	count = read_array(reader);
	for (index = 0U; index < count && reader->error == 0; index++)
		i915_vkc_dec_VkImageSubresourceRange(reader, &build->session->arena, &range);
}

/*
 * Decodes a render pass's begin info: [sType][u64 0][render pass]
 * [framebuffer][rectangle][count][u64 count][the tagged clear values].
 */
static void
decode_render_begin(
	struct i915_wire_reader *reader)
{
	uint64_t chain;
	uint64_t count;
	uint64_t index;

	/* The head, the pass, the framebuffer and the area. */
	(void)drv_i915_wire_read_u32(reader);
	chain = drv_i915_wire_read_u64(reader);
	if (chain != 0U)
		reader->error = 1;
	skip_u64s(reader, 2U);
	skip_u32s(reader, 4U);

	/* The clear values. */
	(void)drv_i915_wire_read_u32(reader);
	count = read_array(reader);
	for (index = 0U; index < count && reader->error == 0; index++)
		decode_clear_value(reader);
}

/* Decodes a clear colour: [u32 2][u64 4][4 x u32]. */
static void
decode_color(
	struct i915_wire_reader *reader)
{
	uint32_t tag;
	uint64_t count;

	/* The union's tag and its four words. */
	tag = drv_i915_wire_read_u32(reader);
	count = drv_i915_wire_read_u64(reader);
	if (tag != CLEAR_COLOR_TAG || count != CLEAR_COLOR_WORDS)
		reader->error = 1;
	skip_u32s(reader, CLEAR_COLOR_WORDS);
}

/* Decodes a tagged clear value: [u32 0][colour] or [u32 1][float depth][u32 stencil]. */
static void
decode_clear_value(
	struct i915_wire_reader *reader)
{
	uint32_t tag;

	/* The tag says which. */
	tag = drv_i915_wire_read_u32(reader);
	if (tag == CLEAR_VALUE_COLOR) {
		decode_color(reader);
	} else if (tag == CLEAR_VALUE_DEPTH) {
		skip_u32s(reader, 2U);
	} else {
		reader->error = 1;
	}
}

/* Reads an array's u64 element count, refusing one past the bound. */
static uint64_t
read_array(
	struct i915_wire_reader *reader)
{
	uint64_t count;

	/* The count, and a bound on it before it sizes a walk. */
	count = drv_i915_wire_read_u64(reader);
	if (count > ARRAY_MAX) {
		reader->error = 1;
		return 0U;
	}

	/* Succeeded: the count. */
	return count;
}

/* Skips count 32-bit words. */
static void
skip_u32s(
	struct i915_wire_reader *reader,
	uint64_t count)
{
	/* The words as one run, inside the command. */
	skip_bytes(reader, count * 4U);
}

/* Skips count 64-bit values. */
static void
skip_u64s(
	struct i915_wire_reader *reader,
	uint64_t count)
{
	/* The values as one run, inside the command. */
	skip_bytes(reader, count * 8U);
}

/* Skips a run of bytes, refusing one past the command's end. */
static void
skip_bytes(
	struct i915_wire_reader *reader,
	uint64_t bytes)
{
	/* A prior error moves nothing. */
	if (reader->error != 0)
		return;

	/* A run past the end fails the command. */
	if (bytes > (uint64_t)(reader->size - reader->offset)) {
		reader->error = 1;
		return;
	}

	/* The cursor passes the run. */
	reader->offset += (size_t)bytes;
}
