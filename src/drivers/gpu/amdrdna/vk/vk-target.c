/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The executor's render passes and framebuffers.
 *
 * A render pass is one subpass writing at most one colour attachment and
 * at most one depth attachment (D32_SFLOAT); anything else is refused by
 * name.  A framebuffer holds the views of its attachments.
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

#include "drivers/gpu/i915/render/vulkan-codec.inc"

static int target_create_pass(struct amdrdna_vk_session *session, struct i915_wire_reader *reader, struct i915_wire_writer *reply);
static int target_pass_supported(const VkRenderPassCreateInfo *info);
static int target_create_framebuffer(struct amdrdna_vk_session *session, struct i915_wire_reader *reader, struct i915_wire_writer *reply);
static int target_granularity(struct i915_wire_reader *reader, struct i915_wire_writer *reply);
static int target_framebuffer_destroy(struct amdrdna_vk_session *session, void *payload);

/*
 * Runs a render pass or framebuffer command; *handled stays clear for an
 * opcode of another handler's.
 */
int
amdrdna_vk_target_route(
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
	case GPU_OP_CREATE_RENDER_PASS:
		error = target_create_pass(session, reader, reply);
		break;
	case GPU_OP_DESTROY_RENDER_PASS:
		error = amdrdna_vk_destroy_plain(session, reader, I915_VK_OBJ_RENDER_PASS);
		break;
	case GPU_OP_CREATE_FRAMEBUFFER:
		error = target_create_framebuffer(session, reader, reply);
		break;
	case GPU_OP_DESTROY_FRAMEBUFFER:
		error = amdrdna_vk_destroy_plain(session, reader, I915_VK_OBJ_FRAMEBUFFER);
		break;
	case GPU_OP_GET_RENDER_AREA_GRANULARITY:
		error = target_granularity(reader, reply);
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
 * vkCreateRenderPass: [device][present][VkRenderPassCreateInfo] and the
 * create's tail -> the create's reply.
 */
static int
target_create_pass(
	struct amdrdna_vk_session *session,
	struct i915_wire_reader *reader,
	struct i915_wire_writer *reply)
{
	VkRenderPassCreateInfo info;
	const VkSubpassDescription *subpass;
	struct amdrdna_vk_render_pass *pass;
	uint64_t identity;
	uint32_t attachment;
	int supported;
	int error;

	/* The create info behind the device and its presence word. */
	kern_memset(&info, 0, sizeof(info));
	(void)drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u64(reader);
	i915_vkc_dec_VkRenderPassCreateInfo(reader, &session->arena, &info);
	identity = amdrdna_vk_create_tail(reader);
	if (reader->error != 0)
		return EINVAL;

	/* Refuses by name a pass of another shape. */
	error = 0;
	supported = target_pass_supported(&info);
	if (supported == 0) {
		kern_logf("amdrdna: vk: vkCreateRenderPass refused: %u subpasses, %u attachments\n",
			  info.subpassCount,
			  info.attachmentCount);
		error = ENOTSUP;
	}

	/* The pass keeps whether it writes a colour and a depth attachment, their numbers, formats and load operations. */
	pass = NULL;
	if (error == 0) {
		pass = kern_calloc(1U, sizeof(*pass));
		subpass = &info.pSubpasses[0];
		if (pass != NULL && subpass->colorAttachmentCount != 0U) {
			attachment = subpass->pColorAttachments[0].attachment;
			if (attachment != VK_ATTACHMENT_UNUSED) {
				pass->colour = 1U;
				pass->colour_attachment = attachment;
				pass->format = info.pAttachments[attachment].format;
				pass->load_op = info.pAttachments[attachment].loadOp;
			}
		}
		if (pass != NULL && subpass->pDepthStencilAttachment != NULL) {
			attachment = subpass->pDepthStencilAttachment->attachment;
			if (attachment != VK_ATTACHMENT_UNUSED) {
				pass->depth = 1U;
				pass->depth_attachment = attachment;
				pass->depth_format = info.pAttachments[attachment].format;
				pass->depth_load_op = info.pAttachments[attachment].loadOp;
			}
		}
	}

	/* Publishes the pass and answers. */
	amdrdna_vk_create_reply(session, reply, I915_VK_OBJ_RENDER_PASS, identity, pass, amdrdna_vk_free_payload, error);

	/* Succeeded: the reply carries the create's result. */
	return 0;
}

/*
 * Reports whether a render pass is one subpass writing at most one colour
 * attachment, in a colour format, and at most one depth attachment, in the
 * depth format, and nothing else.
 */
static int
target_pass_supported(
	const VkRenderPassCreateInfo *info)
{
	const VkSubpassDescription *subpass;
	uint32_t attachment;
	bool colour;
	bool depth;

	/* One subpass, at most a colour and a depth attachment. */
	if (info->subpassCount != 1U || info->pSubpasses == NULL)
		return 0;
	if (info->attachmentCount > AMDRDNA_VK_ATTACHMENTS_MAX)
		return 0;

	/* No inputs, no resolves, at most one colour attachment. */
	subpass = &info->pSubpasses[0];
	if (subpass->inputAttachmentCount != 0U)
		return 0;
	if (subpass->pResolveAttachments != NULL)
		return 0;
	if (subpass->colorAttachmentCount > 1U)
		return 0;

	/* The colour attachment, when there is one, is the pass's attachment in a colour format. */
	if (subpass->colorAttachmentCount == 1U) {
		attachment = subpass->pColorAttachments[0].attachment;
		if (attachment == VK_ATTACHMENT_UNUSED)
			return 1;
		if (attachment >= info->attachmentCount || info->pAttachments == NULL)
			return 0;
		colour = amdrdna_vk_format_colour(info->pAttachments[attachment].format);
		if (!colour)
			return 0;
		if (info->pAttachments[attachment].samples != VK_SAMPLE_COUNT_1_BIT)
			return 0;
	}

	/* The depth attachment, when there is one, is the pass's attachment in the depth format. */
	if (subpass->pDepthStencilAttachment != NULL) {
		attachment = subpass->pDepthStencilAttachment->attachment;
		if (attachment == VK_ATTACHMENT_UNUSED)
			return 1;
		if (attachment >= info->attachmentCount || info->pAttachments == NULL)
			return 0;
		depth = amdrdna_vk_format_depth(info->pAttachments[attachment].format);
		if (!depth)
			return 0;
		if (info->pAttachments[attachment].samples != VK_SAMPLE_COUNT_1_BIT)
			return 0;
	}

	/* Succeeded: the pass has a supported shape. */
	return 1;
}

/*
 * vkCreateFramebuffer: [device][present][VkFramebufferCreateInfo] and the
 * create's tail -> the create's reply.  The framebuffer holds the views of
 * its attachments.
 */
static int
target_create_framebuffer(
	struct amdrdna_vk_session *session,
	struct i915_wire_reader *reader,
	struct i915_wire_writer *reply)
{
	VkFramebufferCreateInfo info;
	struct amdrdna_vk_framebuffer *framebuffer;
	struct amdrdna_vk_object *views[AMDRDNA_VK_ATTACHMENTS_MAX];
	uint64_t identity;
	uint64_t view_id;
	uint32_t index;
	uint32_t held;
	int error;

	/* The create info behind the device and its presence word. */
	kern_memset(&info, 0, sizeof(info));
	(void)drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u64(reader);
	i915_vkc_dec_VkFramebufferCreateInfo(reader, &session->arena, &info);
	identity = amdrdna_vk_create_tail(reader);
	if (reader->error != 0)
		return EINVAL;

	/* At most two attachments, known views, of a size an image may have. */
	error = 0;
	kern_memset(views, 0, sizeof(views));
	if (info.attachmentCount > AMDRDNA_VK_ATTACHMENTS_MAX) {
		error = ENOTSUP;
	} else if (info.width == 0U || info.width > AMDRDNA_VK_IMAGE_SIDE_MAX) {
		error = EINVAL;
	} else if (info.height == 0U || info.height > AMDRDNA_VK_IMAGE_SIDE_MAX) {
		error = EINVAL;
	}
	for (index = 0U; error == 0 && index < info.attachmentCount; index++) {
		/* The decoded handles are the wire's 64-bit identities. */
		kern_memcpy(&view_id, (const uint8_t *)info.pAttachments + (size_t)index * sizeof(view_id), sizeof(view_id));
		views[index] = amdrdna_vk_object_find(session, I915_VK_OBJ_IMAGE_VIEW, view_id);
		if (views[index] == NULL)
			error = EINVAL;
	}

	/* The framebuffer holds its views. */
	held = 0U;
	for (index = 0U; error == 0 && index < info.attachmentCount; index++) {
		error = amdrdna_vk_object_retain(views[index]);
		if (error == 0)
			held++;
	}
	framebuffer = NULL;
	if (error == 0)
		framebuffer = kern_calloc(1U, sizeof(*framebuffer));

	/* A framebuffer that could not be made lets the views it took go. */
	if (framebuffer == NULL) {
		for (index = 0U; index < held; index++)
			(void)amdrdna_vk_object_release(views[index]);
	}

	/* Its size and its views. */
	if (framebuffer != NULL) {
		framebuffer->width = info.width;
		framebuffer->height = info.height;
		framebuffer->count = info.attachmentCount;
		kern_memcpy(framebuffer->views, views, sizeof(views));
	}

	/* Publishes the framebuffer and answers. */
	amdrdna_vk_create_reply(session, reply, I915_VK_OBJ_FRAMEBUFFER, identity, framebuffer, target_framebuffer_destroy, error);

	/* Succeeded: the reply carries the create's result. */
	return 0;
}

/*
 * vkGetRenderAreaGranularity: [device][render pass][present] ->
 * [present][VkExtent2D].  Any pixel can start a render area.
 */
static int
target_granularity(
	struct i915_wire_reader *reader,
	struct i915_wire_writer *reply)
{
	VkExtent2D extent;

	/* The device, the pass and the output's presence word. */
	(void)drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u64(reader);
	if (reader->error != 0)
		return EINVAL;

	/* One pixel by one. */
	extent.width = 1U;
	extent.height = 1U;
	drv_i915_wire_reply_u64(reply, 1U);
	i915_vkc_enc_VkExtent2D(reply, &extent);

	/* Succeeded: the reply carries the granularity. */
	return 0;
}

/* Destroys a framebuffer nothing holds: it lets its views go. */
static int
target_framebuffer_destroy(
	struct amdrdna_vk_session *session,
	void *payload)
{
	struct amdrdna_vk_framebuffer *framebuffer;
	uint32_t index;
	int released;
	int error;

	UNUSED_PARAMETER(session);

	framebuffer = payload;

	/* Each view may go with it when nothing else holds the view; the first failure is reported. */
	error = 0;
	for (index = 0U; index < framebuffer->count; index++) {
		released = amdrdna_vk_object_release(framebuffer->views[index]);
		if (released != 0 && error == 0)
			error = released;
	}
	kern_free(framebuffer);

	/* Reports a view whose destruction failed. */
	if (error != 0)
		return error;

	/* Succeeded: the framebuffer is gone. */
	return 0;
}
