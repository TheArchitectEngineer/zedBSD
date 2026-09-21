/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Command buffer recording: turns vkCmd* into a GEN 3D batch and submits it
 * to the WS029 RCS0 request path.  Contract for p008; see external-design.md
 * section 4.8.
 */

#ifndef I915_VK_CMDBUF_H
#define I915_VK_CMDBUF_H

#include "vk-internal.h"

/* Routes command pool/buffer, vkCmd* and queue submit opcodes. */
int
i915_vk_cmdbuf_dispatch(
	struct i915_vk_session *session,
	uint32_t opcode,
	struct i915_vk_reader *reader,
	struct i915_vk_writer *reply);

/* Command buffer recording lifetime. */
int
i915_vk_cmdbuf_begin(
	struct i915_vk_cmdbuf *cmdbuf);

int
i915_vk_cmdbuf_end(
	struct i915_vk_cmdbuf *cmdbuf);

/* Recording operations append GEN commands to the buffer's batch. */
int
i915_vk_cmd_bind_pipeline(
	struct i915_vk_cmdbuf *cmdbuf,
	struct i915_vk_pipeline *pipeline);

int
i915_vk_cmd_bind_vertex_buffers(
	struct i915_vk_cmdbuf *cmdbuf,
	uint32_t first,
	uint32_t count,
	struct i915_vk_buffer *const *buffers,
	const uint64_t *offsets);

int
i915_vk_cmd_bind_descriptor_sets(
	struct i915_vk_cmdbuf *cmdbuf,
	uint32_t first,
	uint32_t count,
	struct i915_vk_dset *const *sets);

int
i915_vk_cmd_push_constants(
	struct i915_vk_cmdbuf *cmdbuf,
	uint32_t offset,
	uint32_t size,
	const void *values);

int
i915_vk_cmd_begin_render_pass(
	struct i915_vk_cmdbuf *cmdbuf,
	struct i915_vk_image_view *color,
	struct i915_vk_image_view *depth,
	const float clear_color[4]);

int
i915_vk_cmd_end_render_pass(
	struct i915_vk_cmdbuf *cmdbuf);

int
i915_vk_cmd_draw(
	struct i915_vk_cmdbuf *cmdbuf,
	uint32_t vertex_count,
	uint32_t instance_count,
	uint32_t first_vertex,
	uint32_t first_instance);

/* Submits recorded command buffers on RCS0 and arms the fence. */
int
i915_vk_queue_submit(
	struct i915_vk_session *session,
	struct i915_vk_cmdbuf *const *cmdbufs,
	uint32_t count,
	struct i915_vk_fence *fence);

#endif /* I915_VK_CMDBUF_H */
