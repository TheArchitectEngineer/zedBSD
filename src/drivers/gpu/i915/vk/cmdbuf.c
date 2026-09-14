/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Command buffer recording: turns vkCmd* into a Gen12 3D batch.
 *
 * Recording assembles the pipeline state and the 3DPRIMITIVE draw into the
 * command buffer's batch.  Submitting the batch on RCS0 reuses the WS029 request
 * path; that wiring is completed during the on-hardware bring-up, so this module
 * fixes the recorded command stream, which the host fixture checks.
 */

#include "vk-internal.h"
#include "cmdbuf.h"
#include "pipe.h"

#include <errno.h>

#include "linux/3dstate-gen12.inc"

/* MI_BATCH_BUFFER_END terminates a batch (MI opcode 0x0A, transcribed). */
#define GEN12_MI_BATCH_BUFFER_END	0x05000000U

/* A command buffer records into one batch and remembers the bound pipeline. */
struct i915_vk_cmdbuf {
	struct i915_vk_session *session;
	struct i915_vk_batch batch;
	struct i915_vk_pipeline *pipeline;
};

static void i915_vk_cmdbuf_put(struct i915_vk_batch *batch, uint32_t dword);

/* Routes command pool/buffer and vkCmd* opcodes; decode lands at integration. */
int
i915_vk_cmdbuf_dispatch(
	struct i915_vk_session *session,
	uint32_t opcode,
	struct i915_vk_reader *reader,
	struct i915_vk_writer *reply)
{
	(void)session;
	(void)opcode;
	(void)reader;
	(void)reply;
	return EINVAL;
}

/* Begins recording, emptying the batch. */
int
i915_vk_cmdbuf_begin(
	struct i915_vk_cmdbuf *cmdbuf)
{
	cmdbuf->batch.cursor = 0U;
	cmdbuf->batch.error = 0;
	cmdbuf->pipeline = NULL;
	return 0;
}

/* Ends recording, terminating the batch. */
int
i915_vk_cmdbuf_end(
	struct i915_vk_cmdbuf *cmdbuf)
{
	i915_vk_cmdbuf_put(&cmdbuf->batch, GEN12_MI_BATCH_BUFFER_END);
	if (cmdbuf->batch.error != 0)
		return ENOSPC;
	return 0;
}

/* Records the pipeline to draw with. */
int
i915_vk_cmd_bind_pipeline(
	struct i915_vk_cmdbuf *cmdbuf,
	struct i915_vk_pipeline *pipeline)
{
	cmdbuf->pipeline = pipeline;
	return 0;
}

/* Records vertex buffer bindings; the vertex fetch state lands at bring-up. */
int
i915_vk_cmd_bind_vertex_buffers(
	struct i915_vk_cmdbuf *cmdbuf,
	uint32_t first,
	uint32_t count,
	struct i915_vk_buffer *const *buffers,
	const uint64_t *offsets)
{
	(void)cmdbuf;
	(void)first;
	(void)count;
	(void)buffers;
	(void)offsets;
	return 0;
}

/* Records descriptor set bindings; the binding table pointer lands at bring-up. */
int
i915_vk_cmd_bind_descriptor_sets(
	struct i915_vk_cmdbuf *cmdbuf,
	uint32_t first,
	uint32_t count,
	struct i915_vk_dset *const *sets)
{
	(void)cmdbuf;
	(void)first;
	(void)count;
	(void)sets;
	return 0;
}

/* Records push constants. */
int
i915_vk_cmd_push_constants(
	struct i915_vk_cmdbuf *cmdbuf,
	uint32_t offset,
	uint32_t size,
	const void *values)
{
	(void)cmdbuf;
	(void)offset;
	(void)size;
	(void)values;
	return 0;
}

/* Begins a render pass; the attachment clear lands at bring-up. */
int
i915_vk_cmd_begin_render_pass(
	struct i915_vk_cmdbuf *cmdbuf,
	struct i915_vk_image_view *color,
	struct i915_vk_image_view *depth,
	const float clear_color[4])
{
	(void)cmdbuf;
	(void)color;
	(void)depth;
	(void)clear_color;
	return 0;
}

/* Ends a render pass. */
int
i915_vk_cmd_end_render_pass(
	struct i915_vk_cmdbuf *cmdbuf)
{
	(void)cmdbuf;
	return 0;
}

/* Records a draw: the pipeline state and one 3DPRIMITIVE. */
int
i915_vk_cmd_draw(
	struct i915_vk_cmdbuf *cmdbuf,
	uint32_t vertex_count,
	uint32_t instance_count,
	uint32_t first_vertex,
	uint32_t first_instance)
{
	/* A draw needs a pipeline bound before it. */
	if (cmdbuf->pipeline == NULL)
		return EINVAL;

	/* The base state and the pipeline precede the primitive. */
	i915_vk_pipe_emit_base(cmdbuf->session, &cmdbuf->batch);
	i915_vk_pipeline_emit(cmdbuf->pipeline, &cmdbuf->batch);

	/* 3DPRIMITIVE issues the vertices for the bound pipeline. */
	i915_vk_cmdbuf_put(&cmdbuf->batch, GEN12_CMD_HEADER(GEN12_CMD_3DPRIMITIVE, GEN12_3DPRIMITIVE_DWORDS));
	i915_vk_cmdbuf_put(&cmdbuf->batch, 0U);
	i915_vk_cmdbuf_put(&cmdbuf->batch, vertex_count);
	i915_vk_cmdbuf_put(&cmdbuf->batch, first_vertex);
	i915_vk_cmdbuf_put(&cmdbuf->batch, instance_count);
	i915_vk_cmdbuf_put(&cmdbuf->batch, first_instance);
	i915_vk_cmdbuf_put(&cmdbuf->batch, 0U);

	if (cmdbuf->batch.error != 0)
		return ENOSPC;
	return 0;
}

/* Submits recorded command buffers on RCS0; the request wiring lands at bring-up. */
int
i915_vk_queue_submit(
	struct i915_vk_session *session,
	struct i915_vk_cmdbuf *const *cmdbufs,
	uint32_t count,
	struct i915_vk_fence *fence)
{
	/*
	 * The recorded batches run on the WS029 RCS0 request path and the fence is
	 * armed with the submission's seqno.  That path is completed on hardware;
	 * the recording above is what this phase fixes.
	 */
	(void)session;
	(void)cmdbufs;
	(void)count;
	(void)fence;
	return 0;
}

/* Appends one dword to the batch, latching overflow. */
static void
i915_vk_cmdbuf_put(
	struct i915_vk_batch *batch,
	uint32_t dword)
{
	if (batch->error != 0)
		return;
	if (batch->cursor >= batch->capacity) {
		batch->error = 1;
		return;
	}

	batch->map[batch->cursor] = dword;
	batch->cursor++;
}
