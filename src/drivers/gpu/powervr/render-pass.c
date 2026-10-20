/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The executor's render passes, framebuffers and render jobs.  See
 * render-pass.h.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <libc/vulkan/vulkan_core.h>
#include <uapi/errno.h>

#include "drivers/gpu/powervr/image-state.h"
#include "drivers/gpu/powervr/render-pass.h"
#include "drivers/gpu/powervr/render-program.h"

/*
 * The default depth clear value, 1.0.
 *
 * Values referenced:
 * Mesa 26.2.5 src/imagination/vulkan/pvr_arch_cmd_buffer.c
 * (pvr_sub_cmd_gfx_job_init: default_ds_clear_value depth 1.0f).
 */
#define DEPTH_ONE	0x3f800000U

static int pass_attachment(const VkAttachmentDescription *description, struct powervr_pass_attachment *attachment);
static int target_surface(const struct powervr_view_shape *view, struct powervr_render_surface *surface);
static bool area_covers(const struct powervr_render_area *area, const VkClearRect *rect);

/*
 * Keeps what a VkRenderPass of the supported shape needs: one subpass,
 * at most four colour attachments and one depth attachment, formats the
 * executor takes, one sample, no input, resolve or preserved attachment.
 *
 * Any other shape is refused with ENOBUFS, which the reply turns into
 * VK_ERROR_OUT_OF_HOST_MEMORY (one of the failures vkCreateRenderPass
 * may report).
 *
 * Procedure referenced:
 * Mesa 26.2.5 src/imagination/vulkan/pvr_pass.c (pvr_CreateRenderPass2:
 * the attachments' formats and operations, the subpasses' colour and
 * depth references).
 */
int
powervr_pass_build(
	const VkRenderPassCreateInfo *info,
	struct powervr_pass *pass)
{
	const VkSubpassDescription *subpass;
	uint32_t reference;
	uint32_t index;
	int error;

	/* Refuses missing input or result. */
	if (info == NULL || pass == NULL)
		return EINVAL;

	/* One subpass of at most four colour attachments, and no more attachments than a pass holds. */
	if (info->subpassCount != 1 || info->pSubpasses == NULL)
		return ENOBUFS;
	if (info->attachmentCount > POWERVR_PASS_ATTACHMENTS)
		return ENOBUFS;
	subpass = &info->pSubpasses[0];
	if (subpass->pipelineBindPoint != VK_PIPELINE_BIND_POINT_GRAPHICS || subpass->colorAttachmentCount > POWERVR_PASS_COLORS)
		return ENOBUFS;
	if (subpass->inputAttachmentCount != 0 || subpass->pResolveAttachments != NULL || subpass->preserveAttachmentCount != 0)
		return ENOBUFS;

	/* Each attachment's format and operations. */
	pass->attachment_count = info->attachmentCount;
	for (index = 0; index < info->attachmentCount; index++) {
		error = pass_attachment(&info->pAttachments[index], &pass->attachments[index]);
		if (error != 0)
			return error;
	}

	/* The colour references, each a colour attachment of the pass or unused. */
	pass->color_count = subpass->colorAttachmentCount;
	for (index = 0; index < subpass->colorAttachmentCount; index++) {
		reference = subpass->pColorAttachments[index].attachment;
		if (reference != VK_ATTACHMENT_UNUSED && reference >= info->attachmentCount)
			return EINVAL;
		pass->colors[index] = reference == VK_ATTACHMENT_UNUSED ? POWERVR_PASS_UNUSED : reference;
	}

	/* The depth reference, a depth attachment of the pass or none. */
	pass->depth = POWERVR_PASS_UNUSED;
	if (subpass->pDepthStencilAttachment != NULL && subpass->pDepthStencilAttachment->attachment != VK_ATTACHMENT_UNUSED) {
		reference = subpass->pDepthStencilAttachment->attachment;
		if (reference >= info->attachmentCount)
			return EINVAL;
		pass->depth = reference;
	}

	/* Succeeded: the pass is kept. */
	return 0;
}

/*
 * Starts a render job: the pass's attachments in the framebuffer, the
 * render area and the clear values (indexed by attachment).
 *
 * The area must lie in the framebuffer (EINVAL).  A colour attachment
 * takes a partial area (the pixel back end clips to it); so does a depth
 * attachment, whose store writes whole tiles: the job marks it partial
 * and the depth is loaded before it is stored, as Mesa does
 * (render-depth.c).  A depth view of a level
 * other than 0 is not taken yet (ENOTSUP).  A cleared attachment without
 * its clear value is refused (EINVAL).
 *
 * Procedure referenced:
 * Mesa 26.2.5 src/imagination/vulkan/pvr_arch_cmd_buffer.c
 * (pvr_cmd_buffer_begin_render_pass and pvr_setup_pbe_state: each
 * colour attachment's image view level and layer as the surface).
 */
int
powervr_render_job_begin(
	const struct powervr_pass *pass,
	const struct powervr_pass_framebuffer *framebuffer,
	const struct powervr_render_area *area,
	const struct powervr_pass_clear *clears,
	uint32_t clear_count,
	struct powervr_render_job *job)
{
	const struct powervr_pass_attachment *attachment;
	const struct powervr_view_shape *view;
	struct powervr_render_target *target;
	uint32_t reference;
	uint32_t index;
	int error;

	/* Refuses missing input or result. */
	if (pass == NULL || framebuffer == NULL || area == NULL || job == NULL || (clear_count != 0 && clears == NULL))
		return EINVAL;

	/* The area: not empty, inside the framebuffer. */
	if (area->width == 0 || area->height == 0)
		return EINVAL;
	if (area->x >= framebuffer->width || area->width > framebuffer->width - area->x)
		return EINVAL;
	if (area->y >= framebuffer->height || area->height > framebuffer->height - area->y)
		return EINVAL;

	/* The framebuffer must hold the pass's attachments. */
	if (framebuffer->view_count < pass->attachment_count)
		return EINVAL;

	/* An empty job over the area. */
	*job = (struct powervr_render_job){ 0 };
	job->area = *area;
	job->width = framebuffer->width;
	job->height = framebuffer->height;

	/* The colour targets in the subpass's order. */
	job->color_count = pass->color_count;
	for (index = 0; index < pass->color_count; index++) {
		target = &job->colors[index];
		reference = pass->colors[index];
		target->load_op = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		target->store_op = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		if (reference == POWERVR_PASS_UNUSED)
			continue;
		attachment = &pass->attachments[reference];
		view = &framebuffer->views[reference].shape;
		if (view->format == NULL || view->format->vk_format != attachment->format || (view->format->kind & POWERVR_FORMAT_COLOR) == 0)
			return EINVAL;
		error = target_surface(view, &target->surface);
		if (error != 0)
			return error;
		target->view = *view;
		target->load_op = attachment->load_op;
		target->store_op = attachment->store_op;
		if (attachment->load_op == VK_ATTACHMENT_LOAD_OP_CLEAR) {
			if (reference >= clear_count || clears[reference].is_depth)
				return EINVAL;
			target->clear[0] = clears[reference].words[0];
			target->clear[1] = clears[reference].words[1];
			target->clear[2] = clears[reference].words[2];
			target->clear[3] = clears[reference].words[3];
		}
	}

	/* The depth target, when the subpass has one. */
	if (pass->depth != POWERVR_PASS_UNUSED) {
		attachment = &pass->attachments[pass->depth];
		view = &framebuffer->views[pass->depth].shape;
		if (view->format == NULL || view->format->vk_format != attachment->format || (view->format->kind & POWERVR_FORMAT_DEPTH) == 0)
			return EINVAL;
		if (view->layout == NULL || view->base_level != 0)
			return ENOTSUP;
		job->depth.present = true;
		job->depth.format = view->format;
		job->depth.address = view->address + (uint64_t)view->base_layer * view->layout->layer_bytes;
		job->depth.width = view->width;
		job->depth.height = view->height;
		job->depth.physical_width = view->layout->padded_width;
		job->depth.physical_height = view->layout->padded_height;
		job->depth.twiddled = !view->linear;
		job->depth.layer_bytes = view->layout->layer_bytes;
		job->depth.load_op = attachment->load_op;
		job->depth.store_op = attachment->store_op;
		job->depth.clear_depth = DEPTH_ONE;
		if (attachment->load_op == VK_ATTACHMENT_LOAD_OP_CLEAR) {
			if (pass->depth >= clear_count || !clears[pass->depth].is_depth)
				return EINVAL;
			job->depth.clear_depth = clears[pass->depth].words[0];
		}

		/* An area short of the depth view (as Mesa's pvr_is_render_area_tile_aligned): the depth outside it is kept by a load before the store. */
		job->depth.partial = false;
		if (area->x != 0 || area->y != 0 || area->width != view->width || area->height != view->height)
			job->depth.partial = true;
	}

	/* Succeeded: the job is started. */
	return 0;
}

/*
 * Applies vkCmdClearAttachments to a job.  A clear of the whole render
 * area of layer 0 before any draw is the attachment's load clear; any
 * other clear (part of the area, after draws) is not taken (ENOTSUP).  A colour index past the subpass's or an
 * unused attachment is refused (EINVAL).
 */
int
powervr_render_job_clear(
	struct powervr_render_job *job,
	const struct powervr_pass_clear_attachment *attachments,
	uint32_t count,
	const VkClearRect *rects,
	uint32_t rect_count)
{
	struct powervr_render_target *target;
	uint32_t index;
	uint32_t rect;
	bool whole;
	bool covers;

	/* Refuses missing input. */
	if (job == NULL || (count != 0 && attachments == NULL) || (rect_count != 0 && rects == NULL))
		return EINVAL;

	/* Only before any draw, over the whole area of layer 0. */
	whole = rect_count != 0 && job->draw_count == 0;
	for (rect = 0; rect < rect_count && whole; rect++) {
		covers = area_covers(&job->area, &rects[rect]);
		if (!covers)
			whole = false;
	}
	if (!whole)
		return ENOTSUP;

	/* Each attachment becomes a load clear with its value. */
	for (index = 0; index < count; index++) {
		if ((attachments[index].aspects & VK_IMAGE_ASPECT_COLOR_BIT) != 0) {
			if (attachments[index].color_index >= job->color_count || attachments[index].value.is_depth)
				return EINVAL;
			target = &job->colors[attachments[index].color_index];
			if (target->surface.format == NULL)
				return EINVAL;
			target->load_op = VK_ATTACHMENT_LOAD_OP_CLEAR;
			target->clear[0] = attachments[index].value.words[0];
			target->clear[1] = attachments[index].value.words[1];
			target->clear[2] = attachments[index].value.words[2];
			target->clear[3] = attachments[index].value.words[3];
		} else if ((attachments[index].aspects & VK_IMAGE_ASPECT_DEPTH_BIT) != 0) {
			if (!job->depth.present || !attachments[index].value.is_depth)
				return EINVAL;
			job->depth.load_op = VK_ATTACHMENT_LOAD_OP_CLEAR;
			job->depth.clear_depth = attachments[index].value.words[0];
		} else {
			return ENOTSUP;
		}
	}

	/* Succeeded: the clears are the job's load clears. */
	return 0;
}

/*
 * Continues a render job whose draws filled its room in a next job of the
 * same area and targets: the full job stores every target, and the next
 * one loads what the full one stored, then stores as the pass asked.  The
 * next job starts without draws.  A depth target is carried the same way,
 * so the depth test goes on across the two jobs.
 */
int
powervr_render_job_continue(
	struct powervr_render_job *job,
	struct powervr_render_job *next)
{
	uint32_t index;

	/* Refuses missing input and a job that is not full. */
	if (job == NULL || next == NULL || job == next || job->draw_count != POWERVR_RENDER_DRAWS)
		return EINVAL;

	/* The next job: the same area and targets, no draws. */
	__builtin_memcpy(next, job, offsetof(struct powervr_render_job, draws));
	next->draw_count = 0;
	next->continued = true;

	/* Each colour target: stored by the full job, loaded by the next one. */
	for (index = 0; index < job->color_count; index++) {
		job->colors[index].store_op = VK_ATTACHMENT_STORE_OP_STORE;
		next->colors[index].load_op = VK_ATTACHMENT_LOAD_OP_LOAD;
	}

	/* The depth target likewise. */
	if (job->depth.present) {
		job->depth.store_op = VK_ATTACHMENT_STORE_OP_STORE;
		next->depth.load_op = VK_ATTACHMENT_LOAD_OP_LOAD;
	}

	/* Succeeded: the next job goes on where the full one stops. */
	return 0;
}

/* Keeps one attachment of a pass: a format the executor takes, one sample, the core operations. */
static int
pass_attachment(
	const VkAttachmentDescription *description,
	struct powervr_pass_attachment *attachment)
{
	const struct powervr_format *format;

	/* A format the executor renders to, one sample. */
	format = powervr_format_find(description->format);
	if (format == NULL || description->samples != VK_SAMPLE_COUNT_1_BIT)
		return ENOBUFS;
	if ((format->kind & (POWERVR_FORMAT_COLOR | POWERVR_FORMAT_DEPTH)) == 0)
		return ENOBUFS;

	/* The load and store operations of Vulkan 1.0. */
	if (description->loadOp > VK_ATTACHMENT_LOAD_OP_DONT_CARE || description->storeOp > VK_ATTACHMENT_STORE_OP_DONT_CARE)
		return ENOBUFS;

	/* The format and the operations. */
	attachment->format = description->format;
	attachment->load_op = description->loadOp;
	attachment->store_op = description->storeOp;
	attachment->stencil_load_op = description->stencilLoadOp;
	attachment->stencil_store_op = description->stencilStoreOp;

	/* Succeeded: the attachment is kept. */
	return 0;
}

/*
 * The surface the pixel back end stores a colour view to: the view's
 * level and layer of its image, its level's edges, the linear row or the
 * twiddled padded edge of that level.
 */
static int
target_surface(
	const struct powervr_view_shape *view,
	struct powervr_render_surface *surface)
{
	uint32_t level;

	/* Refuses a view without a layout or past the level table. */
	if (view->layout == NULL || view->base_level >= POWERVR_IMAGE_LEVELS_MAX)
		return EINVAL;

	/* The level's place and edges (at least one texel). */
	level = view->base_level;
	surface->format = view->format;
	surface->address = view->address + (uint64_t)view->base_layer * view->layout->layer_bytes + view->layout->level_offset[level];
	surface->width = view->width >> level;
	if (surface->width == 0)
		surface->width = 1;
	surface->height = view->height >> level;
	if (surface->height == 0)
		surface->height = 1;
	surface->linear = view->linear;
	surface->row_pitch = view->layout->row_pitch;
	surface->padded_width = view->layout->padded_width >> level;
	if (surface->padded_width == 0)
		surface->padded_width = 1;

	/* Succeeded: the surface. */
	return 0;
}

/* Says whether a clear rectangle covers the whole area on layer 0 alone. */
static bool
area_covers(
	const struct powervr_render_area *area,
	const VkClearRect *rect)
{
	/* Layer 0 only. */
	if (rect->baseArrayLayer != 0 || rect->layerCount != 1)
		return false;

	/* The rectangle holds the area. */
	if (rect->rect.offset.x > (int32_t)area->x || rect->rect.offset.y > (int32_t)area->y)
		return false;
	if ((int64_t)rect->rect.offset.x + rect->rect.extent.width < (int64_t)area->x + area->width)
		return false;
	if ((int64_t)rect->rect.offset.y + rect->rect.extent.height < (int64_t)area->y + area->height)
		return false;

	/* It covers it. */
	return true;
}
