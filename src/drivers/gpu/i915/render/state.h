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

struct i915_gfx_batch;

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

	/* The vertex kernel's first payload register and push constant registers. */
	uint32_t vs_grf_start;
	uint32_t vs_push_regs;

	/* The attribute locations the vertex kernel reads, in payload order. */
	uint32_t vs_input_count;
	uint32_t vs_inputs[I915_GFX_MAX_VERTEX_ATTRIBUTES];

	/* The VUE slots after the position, which are the fragment inputs. */
	uint32_t varyings;

	/* The pixel kernel's first payload register and sampled images. */
	uint32_t ps_grf_start;
	uint32_t ps_samplers;
};

void drv_i915_gfx_pipeline_kernels(const struct i915_gfx_pipeline *pipeline, struct i915_gfx_kernels *kernels);

int drv_i915_gfx_write_state(uint8_t *page, const struct i915_gfx_draw_state *state, const struct i915_gfx_kernels *kernels, const struct i915_gfx_image *target, uint32_t mocs);
int drv_i915_gfx_surface_write(uint32_t *rss, const struct i915_gfx_surface *surface, uint32_t mocs);
void drv_i915_gfx_sampler_write(uint32_t *state, const struct i915_gfx_sampler *sampler);
void drv_i915_gfx_instruction_heap_clear(uint8_t *page);

void drv_i915_gfx_emit_context_setup(struct i915_gfx_batch *batch, uint64_t state_va, uint32_t mocs);
int drv_i915_gfx_emit_vertex_input(struct i915_gfx_batch *batch, const struct i915_gfx_draw_state *state, const struct i915_gfx_kernels *kernels, uint32_t mocs);
void drv_i915_gfx_emit_urb(struct i915_gfx_batch *batch, uint32_t entry_size);
void drv_i915_gfx_emit_constants(struct i915_gfx_batch *batch, uint64_t push_va, uint32_t push_regs, uint32_t mocs);
void drv_i915_gfx_emit_raster(struct i915_gfx_batch *batch, const struct i915_gfx_pipeline *pipeline);
int drv_i915_gfx_emit_depth(struct i915_gfx_batch *batch, const struct i915_gfx_draw_state *state, const struct i915_gfx_image *depth, uint64_t scratch_va, uint32_t mocs);
void drv_i915_gfx_emit_vertex_shader(struct i915_gfx_batch *batch, const struct i915_gfx_kernels *kernels);
void drv_i915_gfx_emit_pixel_shader(struct i915_gfx_batch *batch, const struct i915_gfx_kernels *kernels);
void drv_i915_gfx_emit_primitive(struct i915_gfx_batch *batch, uint32_t width, uint32_t height, uint32_t topology, uint32_t vertex_count, uint32_t first_vertex, uint32_t instance_count, uint32_t first_instance);

#endif
