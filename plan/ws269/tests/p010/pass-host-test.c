/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Host test of ws269-p010g (render-pass.c): the render pass shapes taken
 * and refused (two subpasses with ENOBUFS, which the reply makes
 * VK_ERROR_OUT_OF_HOST_MEMORY), the render job of a pass instance (its
 * targets' surfaces, operations and clear values), the partial render
 * area (a colour attachment takes it, a stored depth refuses it with
 * EPERM), and vkCmdClearAttachments as load clears.
 */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "drivers/gpu/powervr/render-pass.h"

static unsigned passed;
static unsigned failed;

static void
check(
	int ok,
	const char *name)
{
	if (ok) {
		passed++;
	} else {
		failed++;
		printf("FAIL %s\n", name);
	}
}

static VkAttachmentDescription attachments[5];
static VkAttachmentReference colors[5];
static VkAttachmentReference depth;
static VkSubpassDescription subpasses[2];
static VkRenderPassCreateInfo info;
static struct powervr_image_layout layouts[2];

/* A pass of one R8G8B8A8 colour attachment cleared and stored, and a D32 depth cleared with store_op. */
static void
pass_info(
	uint32_t depth_store)
{
	memset(attachments, 0, sizeof(attachments));
	memset(subpasses, 0, sizeof(subpasses));
	memset(&info, 0, sizeof(info));
	attachments[0].format = VK_FORMAT_R8G8B8A8_UNORM;
	attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
	attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	attachments[1].format = VK_FORMAT_D32_SFLOAT;
	attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
	attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	attachments[1].storeOp = depth_store;
	colors[0].attachment = 0;
	depth.attachment = 1;
	subpasses[0].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpasses[0].colorAttachmentCount = 1;
	subpasses[0].pColorAttachments = colors;
	subpasses[0].pDepthStencilAttachment = &depth;
	info.attachmentCount = 2;
	info.pAttachments = attachments;
	info.subpassCount = 1;
	info.pSubpasses = subpasses;
}

/* A 64 x 64 framebuffer of a linear colour image (level 1 of 2, layer 1) and an optimal depth image. */
static void
framebuffer_of(
	struct powervr_pass_framebuffer *framebuffer)
{
	memset(framebuffer, 0, sizeof(*framebuffer));
	memset(layouts, 0, sizeof(layouts));
	layouts[0].row_pitch = 512;
	layouts[0].padded_width = 128;
	layouts[0].level_offset[1] = 0x10000;
	layouts[0].layer_bytes = 0x20000;
	layouts[1].padded_width = 64;
	layouts[1].layer_bytes = 0x4000;
	framebuffer->width = 64;
	framebuffer->height = 64;
	framebuffer->layers = 1;
	framebuffer->view_count = 2;
	framebuffer->views[0].shape.format = powervr_format_find(VK_FORMAT_R8G8B8A8_UNORM);
	framebuffer->views[0].shape.layout = &layouts[0];
	framebuffer->views[0].shape.address = 0x100000;
	framebuffer->views[0].shape.width = 128;
	framebuffer->views[0].shape.height = 128;
	framebuffer->views[0].shape.image_levels = 2;
	framebuffer->views[0].shape.base_level = 1;
	framebuffer->views[0].shape.level_count = 1;
	framebuffer->views[0].shape.base_layer = 1;
	framebuffer->views[0].shape.layer_count = 1;
	framebuffer->views[0].shape.linear = 1;
	framebuffer->views[1].shape.format = powervr_format_find(VK_FORMAT_D32_SFLOAT);
	framebuffer->views[1].shape.layout = &layouts[1];
	framebuffer->views[1].shape.address = 0x200000;
	framebuffer->views[1].shape.width = 64;
	framebuffer->views[1].shape.height = 64;
	framebuffer->views[1].shape.image_levels = 1;
	framebuffer->views[1].shape.level_count = 1;
	framebuffer->views[1].shape.layer_count = 1;
}

int
main(void)
{
	struct powervr_pass pass;
	struct powervr_pass_framebuffer framebuffer;
	struct powervr_render_area area;
	struct powervr_pass_clear clears[2];
	struct powervr_pass_clear_attachment clear;
	struct powervr_render_job job;
	VkClearRect rect;
	int ok;

	/* The supported shape, and the refused ones. */
	pass_info(VK_ATTACHMENT_STORE_OP_DONT_CARE);
	ok = powervr_pass_build(&info, &pass) == 0;
	check(ok && pass.attachment_count == 2 && pass.color_count == 1 && pass.colors[0] == 0 && pass.depth == 1, "one subpass taken");
	info.subpassCount = 2;
	check(powervr_pass_build(&info, &pass) == ENOBUFS, "two subpasses refused with ENOBUFS");
	pass_info(VK_ATTACHMENT_STORE_OP_DONT_CARE);
	subpasses[0].colorAttachmentCount = 5;
	check(powervr_pass_build(&info, &pass) == ENOBUFS, "five colour attachments refused");
	pass_info(VK_ATTACHMENT_STORE_OP_DONT_CARE);
	attachments[0].samples = VK_SAMPLE_COUNT_4_BIT;
	check(powervr_pass_build(&info, &pass) == ENOBUFS, "four samples refused");
	pass_info(VK_ATTACHMENT_STORE_OP_DONT_CARE);
	subpasses[0].pResolveAttachments = colors;
	check(powervr_pass_build(&info, &pass) == ENOBUFS, "resolve attachments refused");

	/* A full-area instance: the surface is level 1 of layer 1, the clear values are kept. */
	pass_info(VK_ATTACHMENT_STORE_OP_DONT_CARE);
	(void)powervr_pass_build(&info, &pass);
	framebuffer_of(&framebuffer);
	memset(clears, 0, sizeof(clears));
	clears[0].words[0] = 0x3f800000U;
	clears[0].words[3] = 0x3f800000U;
	clears[1].is_depth = 1;
	clears[1].words[0] = 0x3f800000U;
	area.x = 0;
	area.y = 0;
	area.width = 64;
	area.height = 64;
	ok = powervr_render_job_begin(&pass, &framebuffer, &area, clears, 2, &job) == 0;
	ok = ok && job.color_count == 1 && job.colors[0].surface.address == 0x100000U + 0x20000U + 0x10000U;
	ok = ok && job.colors[0].surface.width == 64 && job.colors[0].surface.padded_width == 64 && job.colors[0].surface.row_pitch == 512;
	ok = ok && job.colors[0].load_op == VK_ATTACHMENT_LOAD_OP_CLEAR && job.colors[0].store_op == VK_ATTACHMENT_STORE_OP_STORE;
	ok = ok && job.colors[0].clear[0] == 0x3f800000U && job.depth.present && job.depth.address == 0x200000U && job.depth.clear_depth == 0x3f800000U;
	check(ok, "full-area job");

	/* A partial area: taken with a depth not stored, refused with one stored. */
	area.x = 8;
	area.width = 32;
	check(powervr_render_job_begin(&pass, &framebuffer, &area, clears, 2, &job) == 0, "partial area with a colour and an unstored depth taken");
	pass_info(VK_ATTACHMENT_STORE_OP_STORE);
	(void)powervr_pass_build(&info, &pass);
	check(powervr_render_job_begin(&pass, &framebuffer, &area, clears, 2, &job) == EPERM, "partial area with a depth store refused with EPERM");
	area.x = 0;
	area.width = 64;
	check(powervr_render_job_begin(&pass, &framebuffer, &area, clears, 2, &job) == 0, "full area with a depth store taken");

	/* Refusals: an area past the framebuffer, a cleared attachment without its value. */
	area.width = 65;
	check(powervr_render_job_begin(&pass, &framebuffer, &area, clears, 2, &job) == EINVAL, "area past the framebuffer refused");
	area.width = 64;
	check(powervr_render_job_begin(&pass, &framebuffer, &area, clears, 1, &job) == EINVAL, "missing depth clear value refused");

	/* vkCmdClearAttachments over the whole area before any draw: the colour's load clear. */
	pass_info(VK_ATTACHMENT_STORE_OP_DONT_CARE);
	attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
	(void)powervr_pass_build(&info, &pass);
	ok = powervr_render_job_begin(&pass, &framebuffer, &area, clears, 2, &job) == 0 && job.colors[0].load_op == VK_ATTACHMENT_LOAD_OP_LOAD;
	memset(&clear, 0, sizeof(clear));
	clear.aspects = VK_IMAGE_ASPECT_COLOR_BIT;
	clear.value.words[1] = 0x3f000000U;
	memset(&rect, 0, sizeof(rect));
	rect.rect.extent.width = 64;
	rect.rect.extent.height = 64;
	rect.layerCount = 1;
	ok = ok && powervr_render_job_clear(&job, &clear, 1, &rect, 1) == 0;
	check(ok && job.colors[0].load_op == VK_ATTACHMENT_LOAD_OP_CLEAR && job.colors[0].clear[1] == 0x3f000000U, "whole clear becomes the load clear");

	/* A part of the area, or after a draw: not taken until p011. */
	rect.rect.extent.width = 32;
	check(powervr_render_job_clear(&job, &clear, 1, &rect, 1) == ENOTSUP, "partial clear not taken");
	rect.rect.extent.width = 64;
	job.draw_count = 1;
	check(powervr_render_job_clear(&job, &clear, 1, &rect, 1) == ENOTSUP, "clear after a draw not taken");
	job.draw_count = 0;
	clear.color_index = 3;
	check(powervr_render_job_clear(&job, &clear, 1, &rect, 1) == EINVAL, "clear of a missing colour refused");

	printf("render-pass host test: %u passed, %u failed\n", passed, failed);
	return failed == 0 ? 0 : 1;
}
