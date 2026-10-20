/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The executor's render passes, framebuffers and render jobs: what a
 * VkRenderPass keeps, the attachments a framebuffer names, and one job of
 * a render pass instance (the attachments with their load and store
 * operations, clear values and the render area) in the form the render
 * kick takes.  Nothing here holds a lock or memory; the host check
 * builds it unchanged.  design-p010.md sections 2 and 4.3.
 */

#ifndef KERN_DRIVERS_GPU_POWERVR_RENDER_PASS_H
#define KERN_DRIVERS_GPU_POWERVR_RENDER_PASS_H

#include <stdbool.h>
#include <stdint.h>

#include <libc/vulkan/vulkan_core.h>

#include "drivers/gpu/powervr/image-state.h"
#include "drivers/gpu/powervr/render-program.h"

/* The colour attachments of a subpass and the attachments of a pass (four colour, one depth). */
#define POWERVR_PASS_COLORS		4U
#define POWERVR_PASS_ATTACHMENTS	5U

/* The clear rectangles one vkCmdClearAttachments takes. */
#define POWERVR_PASS_CLEAR_RECTS	8U

/* No attachment. */
#define POWERVR_PASS_UNUSED		0xffffffffU

/* One attachment of a pass: its format and its operations (VkAttachmentLoadOp, VkAttachmentStoreOp). */
struct powervr_pass_attachment {
	uint32_t format;
	uint32_t load_op;
	uint32_t store_op;
	uint32_t stencil_load_op;
	uint32_t stencil_store_op;
};

/* A VkRenderPass of one subpass: its attachments, the subpass's colour attachments and its depth attachment. */
struct powervr_pass {
	uint32_t attachment_count;
	struct powervr_pass_attachment attachments[POWERVR_PASS_ATTACHMENTS];
	uint32_t color_count;
	uint32_t colors[POWERVR_PASS_COLORS];
	uint32_t depth;
};

/* One attachment of a framebuffer: the image view's shape (its format, layout, address, level and layer). */
struct powervr_pass_view {
	struct powervr_view_shape shape;
};

/* A VkFramebuffer: its extent and layers, and its attachments. */
struct powervr_pass_framebuffer {
	uint32_t width;
	uint32_t height;
	uint32_t layers;
	uint32_t view_count;
	struct powervr_pass_view views[POWERVR_PASS_ATTACHMENTS];
};

/* One clear value: four colour words, or a depth's bits and a stencil. */
struct powervr_pass_clear {
	bool is_depth;
	uint32_t words[4];
};

/* One colour target of a render job: the surface stored to, the view loaded from, its operations and clear value. */
struct powervr_render_target {
	struct powervr_render_surface surface;
	struct powervr_view_shape view;
	uint32_t load_op;
	uint32_t store_op;
	uint32_t clear[4];
};

/* The depth target of a render job, when there is one. */
struct powervr_render_depth {
	bool present;
	const struct powervr_format *format;
	uint64_t address;
	uint32_t width;
	uint32_t height;
	uint32_t load_op;
	uint32_t store_op;
	uint32_t clear_depth;
};

/*
 * One render job (a render pass instance): the render area, the colour
 * targets in the subpass's order, the depth target, and the draws
 * recorded in it.  The kick takes it as it is.
 */
struct powervr_render_job {
	struct powervr_render_area area;
	uint32_t width;
	uint32_t height;
	uint32_t color_count;
	struct powervr_render_target colors[POWERVR_PASS_COLORS];
	struct powervr_render_depth depth;
	uint32_t draw_count;
};

/* One attachment of vkCmdClearAttachments: its aspects, its colour index and value. */
struct powervr_pass_clear_attachment {
	uint32_t aspects;
	uint32_t color_index;
	struct powervr_pass_clear value;
};

int powervr_pass_build(const VkRenderPassCreateInfo *info, struct powervr_pass *pass);
int powervr_render_job_begin(const struct powervr_pass *pass, const struct powervr_pass_framebuffer *framebuffer, const struct powervr_render_area *area, const struct powervr_pass_clear *clears, uint32_t clear_count, struct powervr_render_job *job);
int powervr_render_job_clear(struct powervr_render_job *job, const struct powervr_pass_clear_attachment *attachments, uint32_t count, const VkClearRect *rects, uint32_t rect_count);

#endif /* KERN_DRIVERS_GPU_POWERVR_RENDER_PASS_H */
