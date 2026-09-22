/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Gen12 3D state: the heaps a draw points at and the packets that program
 * the pipeline around them.
 *
 * The draw path and the rectangle path share these writers.  The state is
 * written into the session's state object at the offsets of heap.h and the
 * packets into a batch of batch.h.  Bit positions are those of Mesa's genxml
 * (gen120); the packets follow what anv programs for an ordinary Vulkan
 * pipeline.
 */

#ifndef DRIVERS_GPU_I915_RENDER_STATE_H
#define DRIVERS_GPU_I915_RENDER_STATE_H

#include <stdint.h>

#include "gfx.h"

#include "../compiler/compiler.h"

struct i915_gfx_batch;

/*
 * What one stage's push data carries: the push constants from the start of
 * the command buffer's block, then the ranges of the uniform blocks the
 * kernel reads.
 *
 * It is part of struct i915_gfx_kernels; `blocks` borrows the binary's
 * list.
 */
struct i915_gfx_push_layout {
	/* The registers of push data the kernel reads, 32 bytes each. */
	uint32_t regs;

	/* The bytes of push constants at the start. */
	uint32_t constant_bytes;

	/* The uniform blocks after them, as the compiler laid them out. */
	uint32_t block_count;
	const struct i915_shader_block *blocks;
};

/*
 * The two kernels of a draw and what has to be programmed around them.
 *
 * It is filled from a pipeline's compiled binaries, or for a rectangle from
 * the transfer kernel, just before the state is written, and lives on the
 * caller's stack.  The code pointers borrow the binaries' code.
 */
struct i915_gfx_kernels {
	/* The vertex kernel's code and size. */
	const uint32_t *vs_code;
	uint32_t vs_bytes;

	/* The pixel kernel's code and size. */
	const uint32_t *ps_code;
	uint32_t ps_bytes;

	/* The vertex kernel's first payload register and push data. */
	uint32_t vs_grf_start;
	uint32_t vs_push_regs;
	struct i915_gfx_push_layout vs_push;

	/* The attribute locations the vertex kernel reads, in payload order. */
	uint32_t vs_input_count;
	uint32_t vs_inputs[I915_GFX_MAX_VERTEX_ATTRIBUTES];

	/* The VUE slots after the position, which are the fragment inputs. */
	uint32_t varyings;

	/* The pixel kernel's first payload register and sampled images. */
	uint32_t ps_grf_start;
	uint32_t ps_samplers;

	/* The (set, binding) of each sampled image, in the kernel's order; NULL for a rectangle. */
	const uint32_t *ps_sampler_sets;
	const uint32_t *ps_sampler_bindings;

	/* The pixel kernel's push data, which comes in front of its setup data. */
	uint32_t ps_push_regs;
	struct i915_gfx_push_layout ps_push;

	/* Nonzero when the pixel kernel discards pixels (3DSTATE_PS_EXTRA Pixel Shader Kills Pixel). */
	uint32_t ps_kills;
};

/*
 * The 3DPRIMITIVE of one draw.
 *
 * It lives on the caller's stack while the batch is written.
 */
struct i915_gfx_primitive {
	/* The 3D_Prim_Topo_Type. */
	uint32_t topology;

	/* Nonzero when the vertices are fetched through the index buffer. */
	int random_access;

	/* The vertices, or the indices, of each instance, and the first of them. */
	uint32_t vertex_count;
	uint32_t start_vertex;

	/* The instances and the first of them. */
	uint32_t instance_count;
	uint32_t start_instance;

	/* What is added to every index of a random-access draw. */
	int32_t base_vertex;
};

void drv_i915_gfx_pipeline_kernels(const struct i915_gfx_pipeline *pipeline, struct i915_gfx_kernels *kernels);

int drv_i915_gfx_write_state(uint8_t *page, const struct i915_gfx_draw_state *state, const struct i915_gfx_kernels *kernels, const struct i915_gfx_image *target, uint32_t mocs);
int drv_i915_gfx_surface_write(uint32_t *rss, const struct i915_gfx_surface *surface, uint32_t mocs);
void drv_i915_gfx_sampler_write(uint32_t *state, const struct i915_gfx_sampler *sampler);
void drv_i915_gfx_instruction_heap_clear(uint8_t *window);

void drv_i915_gfx_emit_context_setup(struct i915_gfx_batch *batch, uint64_t state_va, uint64_t instruction_va, uint32_t mocs);
int drv_i915_gfx_emit_vertex_input(struct i915_gfx_batch *batch, const struct i915_gfx_draw_state *state, const struct i915_gfx_kernels *kernels, uint32_t mocs);
int drv_i915_gfx_emit_index_buffer(struct i915_gfx_batch *batch, const struct i915_gfx_draw_state *state, uint32_t mocs);
void drv_i915_gfx_emit_urb(struct i915_gfx_batch *batch, uint32_t entry_size);
void drv_i915_gfx_emit_constants(struct i915_gfx_batch *batch, uint64_t vs_push_va, uint32_t vs_push_regs, uint64_t ps_push_va, uint32_t ps_push_regs, uint32_t mocs);
void drv_i915_gfx_emit_raster(struct i915_gfx_batch *batch, const struct i915_gfx_pipeline *pipeline);
int drv_i915_gfx_emit_depth(struct i915_gfx_batch *batch, const struct i915_gfx_draw_state *state, const struct i915_gfx_image *depth, uint64_t scratch_va, uint32_t mocs);
void drv_i915_gfx_emit_vertex_shader(struct i915_gfx_batch *batch, const struct i915_gfx_kernels *kernels);
void drv_i915_gfx_emit_pixel_shader(struct i915_gfx_batch *batch, const struct i915_gfx_kernels *kernels);
void drv_i915_gfx_emit_ps_blend(struct i915_gfx_batch *batch, const struct i915_gfx_pipeline *pipeline);
void drv_i915_gfx_emit_primitive(struct i915_gfx_batch *batch, uint32_t width, uint32_t height, const struct i915_gfx_primitive *primitive);
void drv_i915_gfx_emit_draw(struct i915_gfx_batch *batch, uint32_t width, uint32_t height, const struct i915_gfx_primitive *primitive);
void drv_i915_gfx_emit_batch_end(struct i915_gfx_batch *batch);

#endif
