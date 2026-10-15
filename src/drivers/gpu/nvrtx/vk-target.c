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
 * no depth; anything else is refused by name.  A framebuffer holds the
 * view of its one attachment.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <kern/kcrt.h>
#include <kern/klog.h>
#include <kern/kmem.h>
#include <uapi/errno.h>
#include <uapi/gpu-op.h>

#include "drivers/gpu/nvrtx/nvrtx-vk-objects.h"

#include "drivers/gpu/i915/render/vulkan-codec.inc"

static int target_create_pass(struct nvrtx_vk_session *session, struct i915_wire_reader *reader, struct i915_wire_writer *reply);
static int target_pass_supported(const VkRenderPassCreateInfo *info);
static int target_create_framebuffer(struct nvrtx_vk_session *session, struct i915_wire_reader *reader, struct i915_wire_writer *reply);
static int target_granularity(struct i915_wire_reader *reader, struct i915_wire_writer *reply);
static int target_framebuffer_destroy(struct nvrtx_vk_session *session, void *payload);

/*
 * Runs a render pass or framebuffer command; *handled stays clear for an
 * opcode of another handler's.
 */
int
nvrtx_vk_target_route(
	struct nvrtx_vk_session *session,
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
		error = nvrtx_vk_destroy_plain(session, reader, I915_VK_OBJ_RENDER_PASS);
		break;
	case GPU_OP_CREATE_FRAMEBUFFER:
		error = target_create_framebuffer(session, reader, reply);
		break;
	case GPU_OP_DESTROY_FRAMEBUFFER:
		error = nvrtx_vk_destroy_plain(session, reader, I915_VK_OBJ_FRAMEBUFFER);
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
	struct nvrtx_vk_session *session,
	struct i915_wire_reader *reader,
	struct i915_wire_writer *reply)
{
	VkRenderPassCreateInfo info;
	const VkSubpassDescription *subpass;
	struct nvrtx_vk_render_pass *pass;
	uint64_t identity;
	uint32_t attachment;
	int supported;
	int error;

	/* The create info behind the device and its presence word. */
	kern_memset(&info, 0, sizeof(info));
	(void)drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u64(reader);
	i915_vkc_dec_VkRenderPassCreateInfo(reader, &session->arena, &info);
	identity = nvrtx_vk_create_tail(reader);
	if (reader->error != 0)
		return EINVAL;

	/* Refuses by name a pass of another shape. */
	error = 0;
	supported = target_pass_supported(&info);
	if (supported == 0) {
		kern_logf("nvrtx: vk: vkCreateRenderPass refused: %u subpasses, %u attachments\n",
			  info.subpassCount,
			  info.attachmentCount);
		error = ENOTSUP;
	}

	/* The pass keeps whether it writes a colour attachment, its format and its load operation. */
	pass = NULL;
	if (error == 0) {
		pass = kern_calloc(1U, sizeof(*pass));
		subpass = &info.pSubpasses[0];
		if (pass != NULL && subpass->colorAttachmentCount != 0U) {
			attachment = subpass->pColorAttachments[0].attachment;
			if (attachment != VK_ATTACHMENT_UNUSED) {
				pass->colour = 1U;
				pass->format = info.pAttachments[attachment].format;
				pass->load_op = info.pAttachments[attachment].loadOp;
			}
		}
	}

	/* Publishes the pass and answers. */
	nvrtx_vk_create_reply(session, reply, I915_VK_OBJ_RENDER_PASS, identity, pass, nvrtx_vk_free_payload, error);

	/* Succeeded: the reply carries the create's result. */
	return 0;
}

/*
 * Reports whether a render pass is one subpass writing at most one colour
 * attachment, in a colour format, and nothing else.
 */
static int
target_pass_supported(
	const VkRenderPassCreateInfo *info)
{
	const VkSubpassDescription *subpass;
	uint32_t attachment;
	bool colour;

	/* One subpass, at most one attachment. */
	if (info->subpassCount != 1U || info->pSubpasses == NULL)
		return 0;
	if (info->attachmentCount > 1U)
		return 0;

	/* No depth, no inputs, no resolves, at most one colour attachment. */
	subpass = &info->pSubpasses[0];
	if (subpass->pDepthStencilAttachment != NULL)
		return 0;
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
		colour = nvrtx_vk_format_colour(info->pAttachments[attachment].format);
		if (!colour)
			return 0;
		if (info->pAttachments[attachment].samples != VK_SAMPLE_COUNT_1_BIT)
			return 0;
	}

	/* Succeeded: the pass has the one supported shape. */
	return 1;
}

/*
 * vkCreateFramebuffer: [device][present][VkFramebufferCreateInfo] and the
 * create's tail -> the create's reply.  The framebuffer holds the view of
 * its one attachment.
 */
static int
target_create_framebuffer(
	struct nvrtx_vk_session *session,
	struct i915_wire_reader *reader,
	struct i915_wire_writer *reply)
{
	VkFramebufferCreateInfo info;
	struct nvrtx_vk_framebuffer *framebuffer;
	struct nvrtx_vk_object *view;
	uint64_t identity;
	uint64_t view_id;
	int error;

	/* The create info behind the device and its presence word. */
	kern_memset(&info, 0, sizeof(info));
	(void)drv_i915_wire_read_u64(reader);
	(void)drv_i915_wire_read_u64(reader);
	i915_vkc_dec_VkFramebufferCreateInfo(reader, &session->arena, &info);
	identity = nvrtx_vk_create_tail(reader);
	if (reader->error != 0)
		return EINVAL;

	/* At most one attachment, a known view, of a size an image may have. */
	error = 0;
	view = NULL;
	if (info.attachmentCount > 1U) {
		error = ENOTSUP;
	} else if (info.width == 0U || info.width > NVRTX_VK_IMAGE_SIDE_MAX) {
		error = EINVAL;
	} else if (info.height == 0U || info.height > NVRTX_VK_IMAGE_SIDE_MAX) {
		error = EINVAL;
	} else if (info.attachmentCount == 1U) {
		/* The decoded handles are the wire's 64-bit identities. */
		kern_memcpy(&view_id, info.pAttachments, sizeof(view_id));
		view = nvrtx_vk_object_find(session, I915_VK_OBJ_IMAGE_VIEW, view_id);
		if (view == NULL)
			error = EINVAL;
	}

	/* The framebuffer holds its view. */
	if (error == 0 && view != NULL)
		error = nvrtx_vk_object_retain(view);
	framebuffer = NULL;
	if (error == 0) {
		framebuffer = kern_calloc(1U, sizeof(*framebuffer));
		if (framebuffer == NULL)
			(void)nvrtx_vk_object_release(view);
	}

	/* Its size and its view. */
	if (framebuffer != NULL) {
		framebuffer->width = info.width;
		framebuffer->height = info.height;
		framebuffer->view = view;
	}

	/* Publishes the framebuffer and answers. */
	nvrtx_vk_create_reply(session, reply, I915_VK_OBJ_FRAMEBUFFER, identity, framebuffer, target_framebuffer_destroy, error);

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

/* Destroys a framebuffer nothing holds: it lets its view go. */
static int
target_framebuffer_destroy(
	struct nvrtx_vk_session *session,
	void *payload)
{
	struct nvrtx_vk_framebuffer *framebuffer;
	int error;

	UNUSED_PARAMETER(session);

	framebuffer = payload;

	/* The view may go with it when nothing else holds the view. */
	error = nvrtx_vk_object_release(framebuffer->view);
	kern_free(framebuffer);

	/* Reports a view whose destruction failed. */
	if (error != 0)
		return error;

	/* Succeeded: the framebuffer is gone. */
	return 0;
}
