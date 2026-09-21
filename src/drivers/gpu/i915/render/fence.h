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

#endif
