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

/* The draws one render job keeps, the vertex buffers and descriptor sets of a draw, its push constant bytes. */
#define POWERVR_RENDER_DRAWS		16U
#define POWERVR_DRAW_VERTEX_BUFFERS	4U
#define POWERVR_DRAW_SETS		4U
#define POWERVR_DRAW_PUSH_BYTES		128U

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

/*
 * The depth target of a render job, when there is one: its format, the
 * address of its layer, its edges and its padded (physical) edges, its
 * layout and layer size, its operations, its clear value's bits (1.0
 * when it is not cleared), and whether the render area misses part of
 * it (a stored depth is then loaded first, render-depth.c).
 */
struct powervr_render_depth {
	bool present;
	const struct powervr_format *format;
	uint64_t address;
	uint32_t width;
	uint32_t height;
	uint32_t physical_width;
	uint32_t physical_height;
	bool twiddled;
	uint64_t layer_bytes;
	uint32_t load_op;
	uint32_t store_op;
	uint32_t clear_depth;
	bool partial;
};

/*
 * One draw of a render job: the graphics pipeline (the executor's
 * powervr_vk_graphics) and descriptor sets bound, the vertex buffers'
 * addresses by binding, the index buffer, the counts, the viewport and
 * scissor in force, and the push constants.
 */
struct powervr_render_draw {
	const void *pipeline;
	const void *sets[POWERVR_DRAW_SETS];
	uint64_t vertex_addresses[POWERVR_DRAW_VERTEX_BUFFERS];
	bool indexed;
	uint64_t index_address;
	uint32_t index_bytes;
	uint32_t count;
	uint32_t instances;
	uint32_t vertex_offset;
	VkViewport viewport;
	VkRect2D scissor;
	uint32_t push_bytes;
	uint8_t push[POWERVR_DRAW_PUSH_BYTES];
};

/*
 * One render job (a render pass instance, or a part of one): the render
 * area, the colour targets in the subpass's order, the depth target, and
 * the draws recorded in it.  Continued is set on a job that goes on with
 * the instance of the job before it (powervr_render_job_continue), so the
 * jobs of one instance are the first one and the continued ones after
 * it.  The kick takes it as it is.
 */
struct powervr_render_job {
	struct powervr_render_area area;
	bool continued;
	uint32_t width;
	uint32_t height;
	uint32_t color_count;
	struct powervr_render_target colors[POWERVR_PASS_COLORS];
	struct powervr_render_depth depth;
	uint32_t draw_count;
	struct powervr_render_draw draws[POWERVR_RENDER_DRAWS];
};

/* One attachment of vkCmdClearAttachments: its aspects, its colour index and value. */
struct powervr_pass_clear_attachment {
	uint32_t aspects;
	uint32_t color_index;
	struct powervr_pass_clear value;
};

int powervr_pass_build(const VkRenderPassCreateInfo *info, struct powervr_pass *pass);
int powervr_render_job_begin(const struct powervr_pass *pass, const struct powervr_pass_framebuffer *framebuffer, const struct powervr_render_area *area, const struct powervr_pass_clear *clears, uint32_t clear_count, struct powervr_render_job *job);
int powervr_render_job_continue(struct powervr_render_job *job, struct powervr_render_job *next);
int powervr_render_job_clear(struct powervr_render_job *job, const struct powervr_pass_clear_attachment *attachments, uint32_t count, const VkClearRect *rects, uint32_t rect_count);

#endif /* KERN_DRIVERS_GPU_POWERVR_RENDER_PASS_H */
