/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Gen12 3D pipeline state: builds the 3DSTATE_* dwords for a pipeline from
 * the compiled shaders and fixed-function state, and emits them into a batch.
 * Contract for p007; see external-design.md section 4.7.  Dword layouts are
 * transcribed with attribution into vk/linux/3dstate-gen12.inc.
 */

#ifndef I915_VK_PIPE_H
#define I915_VK_PIPE_H

#include "vk-internal.h"

/* The shaders and fixed-function state a graphics pipeline is built from.  The
   kernel fields are the GPU addresses where the shader code has been placed. */
struct i915_vk_pipeline_info {
	struct i915_vk_shader_binary *vs;
	struct i915_vk_shader_binary *fs;
	uint64_t vs_kernel;
	uint64_t fs_kernel;
	uint32_t topology;
	uint32_t color_format;
};

/* Routes pipeline/shader-module/render-pass/framebuffer opcodes. */
int
i915_vk_pipe_dispatch(
	struct i915_vk_session *session,
	uint32_t opcode,
	struct i915_vk_reader *reader,
	struct i915_vk_writer *reply);

/* Builds and retains the 3DSTATE_* for one graphics pipeline. */
int
i915_vk_pipeline_create(
	struct i915_vk_session *session,
	const struct i915_vk_pipeline_info *info,
	struct i915_vk_pipeline **out);

void
i915_vk_pipeline_destroy(
	struct i915_vk_pipeline *pipeline);

/* Emits the retained pipeline state into a batch. */
void
i915_vk_pipeline_emit(
	struct i915_vk_pipeline *pipeline,
	struct i915_vk_batch *batch);

/* Emits per-draw base state once: PIPELINE_SELECT, STATE_BASE_ADDRESS, URB. */
void
i915_vk_pipe_emit_base(
	struct i915_vk_session *session,
	struct i915_vk_batch *batch);

#endif /* I915_VK_PIPE_H */
