/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Host fixture for the 3D pipeline state (p007). Emits a pipeline's commands
 * into a batch and checks the command sequence and the shader kernel pointers.
 * The fixed-function field contents are the big-bang test; this verifies the
 * command headers and pointers land.
 */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned fixture_live;

void *
kern_calloc(size_t count, size_t size)
{
	void *pointer = calloc(count, size);
	if (pointer != NULL)
		fixture_live++;
	return pointer;
}

void
kern_free(void *pointer)
{
	if (pointer != NULL)
		fixture_live--;
	free(pointer);
}

#include "../../../src/drivers/gpu/i915/vk/pipe.c"

/* Reports the batch index carrying a command with the given opcode, or -1. */
static int
find_command(const uint32_t *batch, uint32_t used, uint32_t opcode)
{
	uint32_t index;

	for (index = 0U; index < used; index++) {
		if ((batch[index] >> 16) == opcode)
			return (int)index;
	}
	return -1;
}

int
main(void)
{
	uint32_t buffer[256];
	struct i915_vk_batch batch;
	struct i915_vk_shader_binary vs;
	struct i915_vk_shader_binary fs;
	struct i915_vk_pipeline_info info;
	struct i915_vk_pipeline *pipeline;
	int vs_at;
	int ps_at;
	int error;

	memset(buffer, 0, sizeof(buffer));
	batch.object = NULL;
	batch.map = buffer;
	batch.cursor = 0U;
	batch.capacity = 256U;
	batch.error = 0;

	/* Two placed shaders with known register counts. */
	memset(&vs, 0, sizeof(vs));
	memset(&fs, 0, sizeof(fs));
	vs.grf_used = 24U;
	fs.grf_used = 20U;
	memset(&info, 0, sizeof(info));
	info.vs = &vs;
	info.fs = &fs;
	info.vs_kernel = 0x00100000ULL;
	info.fs_kernel = 0x00200000ULL;

	error = i915_vk_pipeline_create(NULL, &info, &pipeline);
	assert(error == 0);

	i915_vk_pipe_emit_base(NULL, &batch);
	i915_vk_pipeline_emit(pipeline, &batch);
	assert(batch.error == 0);

	/* The base state selects the 3D pipeline and anchors the heaps. */
	assert(find_command(buffer, batch.cursor, GEN12_CMD_PIPELINE_SELECT) >= 0);
	assert(find_command(buffer, batch.cursor, GEN12_CMD_STATE_BASE_ADDRESS) >= 0);

	/* The vertex and pixel shader commands carry their kernel pointers. */
	vs_at = find_command(buffer, batch.cursor, GEN12_CMD_3DSTATE_VS);
	ps_at = find_command(buffer, batch.cursor, GEN12_CMD_3DSTATE_PS);
	assert(vs_at >= 0 && ps_at >= 0);
	assert(buffer[vs_at + 1] == 0x00100000U);
	assert(buffer[vs_at + 3] == 24U);
	assert(buffer[ps_at + 1] == 0x00200000U);
	assert(buffer[ps_at + 3] == 20U);

	i915_vk_pipeline_destroy(pipeline);
	assert(fixture_live == 0U);
	printf("i915 vk pipe host test PASS\n");
	return 0;
}
