/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Vulkan fences (see fence.c).
 */

#ifndef DRIVERS_GPU_I915_RENDER_FENCE_H
#define DRIVERS_GPU_I915_RENDER_FENCE_H

#include <stdint.h>

struct i915_render_session;
struct i915_vk_fence;
struct i915_wire_reader;
struct i915_wire_writer;

int drv_i915_render_fence_dispatch(struct i915_render_session *session, uint32_t opcode, struct i915_wire_reader *reader, struct i915_wire_writer *reply);
void drv_i915_fence_signal(struct i915_vk_fence *fence);
void drv_i915_fence_free(struct i915_vk_fence *fence);

/* Occlusion queries: a pool's teardown, and a recorded query command's batch commands. */
struct i915_gfx_op;
struct i915_gfx_query_pool;
void drv_i915_gfx_query_pool_free(struct i915_render_session *session, struct i915_gfx_query_pool *pool);
int drv_i915_gfx_query_execute(struct i915_render_session *session, const struct i915_gfx_op *op);

#endif
