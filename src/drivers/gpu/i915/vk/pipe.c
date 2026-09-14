/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Gen12 3D pipeline state.  Builds a pipeline from the compiled shaders and
 * emits the 3DSTATE commands a draw needs into a batch.
 *
 * Command opcodes and lengths are transcribed into linux/3dstate-gen12.inc; the
 * assembly is new.  The core commands (PIPELINE_SELECT, STATE_BASE_ADDRESS,
 * 3DSTATE_VS, 3DSTATE_PS) carry the shader kernel pointers and register counts.
 * The remaining fixed-function fields are completed during the on-hardware
 * bring-up; this module fixes the command sequence and the shader pointers.
 */

#include "vk-internal.h"
#include "pipe.h"
#include "cmd.h"
#include "compile.h"

#include <kern/kmem.h>

#include <errno.h>
#include <string.h>

#include "linux/3dstate-gen12.inc"

/* A pipeline retains the shader placement and the register counts to emit. */
struct i915_vk_pipeline {
	uint64_t vs_kernel;
	uint64_t fs_kernel;
	uint32_t vs_grf;
	uint32_t fs_grf;
	uint32_t topology;
};

static void i915_vk_batch_emit(struct i915_vk_batch *batch, uint32_t dword);
static void i915_vk_batch_pad(struct i915_vk_batch *batch, uint32_t count);

/* Routes pipeline/shader-module/render-pass opcodes; decode lands at integration. */
int
i915_vk_pipe_dispatch(
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

/* Builds a pipeline from its shaders and fixed-function state. */
int
i915_vk_pipeline_create(
	struct i915_vk_session *session,
	const struct i915_vk_pipeline_info *info,
	struct i915_vk_pipeline **out)
{
	struct i915_vk_pipeline *pipeline;

	/* The caller receives nothing on failure. */
	(void)session;
	*out = NULL;

	pipeline = kern_calloc(1U, sizeof(*pipeline));
	if (pipeline == NULL)
		return ENOMEM;

	/* The pipeline records where each shader was placed and its register use. */
	pipeline->vs_kernel = info->vs_kernel;
	pipeline->fs_kernel = info->fs_kernel;
	pipeline->vs_grf = info->vs != NULL ? info->vs->grf_used : 0U;
	pipeline->fs_grf = info->fs != NULL ? info->fs->grf_used : 0U;
	pipeline->topology = info->topology;

	/* Succeeded: the pipeline can be emitted into a draw batch. */
	*out = pipeline;
	return 0;
}

/* Releases a pipeline. */
void
i915_vk_pipeline_destroy(
	struct i915_vk_pipeline *pipeline)
{
	if (pipeline != NULL)
		kern_free(pipeline);
}

/* Emits the vertex and pixel shader state for one pipeline. */
void
i915_vk_pipeline_emit(
	struct i915_vk_pipeline *pipeline,
	struct i915_vk_batch *batch)
{
	/* 3DSTATE_VS names the vertex kernel and its register count. */
	i915_vk_batch_emit(batch, GEN12_CMD_HEADER(GEN12_CMD_3DSTATE_VS, GEN12_3DSTATE_VS_DWORDS));
	i915_vk_batch_emit(batch, (uint32_t)pipeline->vs_kernel);
	i915_vk_batch_emit(batch, (uint32_t)(pipeline->vs_kernel >> 32));
	i915_vk_batch_emit(batch, pipeline->vs_grf);
	i915_vk_batch_pad(batch, GEN12_3DSTATE_VS_DWORDS - 4U);

	/* 3DSTATE_PS names the pixel kernel and its register count. */
	i915_vk_batch_emit(batch, GEN12_CMD_HEADER(GEN12_CMD_3DSTATE_PS, GEN12_3DSTATE_PS_DWORDS));
	i915_vk_batch_emit(batch, (uint32_t)pipeline->fs_kernel);
	i915_vk_batch_emit(batch, (uint32_t)(pipeline->fs_kernel >> 32));
	i915_vk_batch_emit(batch, pipeline->fs_grf);
	i915_vk_batch_pad(batch, GEN12_3DSTATE_PS_DWORDS - 4U);
}

/* Emits the once-per-draw base state: the 3D pipeline and the state base. */
void
i915_vk_pipe_emit_base(
	struct i915_vk_session *session,
	struct i915_vk_batch *batch)
{
	(void)session;

	/* PIPELINE_SELECT is a single dword selecting the 3D pipeline. */
	i915_vk_batch_emit(batch, (GEN12_CMD_PIPELINE_SELECT << 16) | GEN12_PIPELINE_SELECT_3D);

	/* STATE_BASE_ADDRESS anchors the heaps; the addresses land at bring-up. */
	i915_vk_batch_emit(batch, GEN12_CMD_HEADER(GEN12_CMD_STATE_BASE_ADDRESS, GEN12_STATE_BASE_ADDRESS_DWORDS));
	i915_vk_batch_pad(batch, GEN12_STATE_BASE_ADDRESS_DWORDS - 1U);
}

/* Appends one dword to a batch, latching overflow. */
static void
i915_vk_batch_emit(
	struct i915_vk_batch *batch,
	uint32_t dword)
{
	/* A prior error or a full batch drops the write. */
	if (batch->error != 0)
		return;
	if (batch->cursor >= batch->capacity) {
		batch->error = 1;
		return;
	}

	batch->map[batch->cursor] = dword;
	batch->cursor++;
}

/* Appends a run of zero dwords to a batch. */
static void
i915_vk_batch_pad(
	struct i915_vk_batch *batch,
	uint32_t count)
{
	uint32_t index;

	for (index = 0U; index < count; index++)
		i915_vk_batch_emit(batch, 0U);
}
