/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Host fixture for command buffer recording (p008). Records a draw and checks
 * the batch carries the pipeline state, the 3DPRIMITIVE with its parameters and
 * the terminating MI_BATCH_BUFFER_END. Real RCS0 submission is the big-bang
 * test; this verifies the recorded command stream.
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
#include "../../../src/drivers/gpu/i915/vk/cmdbuf.c"

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
	uint32_t buffer[512];
	struct i915_vk_cmdbuf cmdbuf;
	struct i915_vk_shader_binary vs;
	struct i915_vk_shader_binary fs;
	struct i915_vk_pipeline_info info;
	struct i915_vk_pipeline *pipeline;
	int prim_at;
	int error;

	memset(buffer, 0, sizeof(buffer));
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

	/* One command buffer recording into a local batch. */
	memset(&cmdbuf, 0, sizeof(cmdbuf));
	cmdbuf.batch.map = buffer;
	cmdbuf.batch.capacity = 512U;

	error = i915_vk_cmdbuf_begin(&cmdbuf);
	assert(error == 0);

	/* A draw without a pipeline is refused. */
	error = i915_vk_cmd_draw(&cmdbuf, 3U, 1U, 0U, 0U);
	assert(error == EINVAL);

	error = i915_vk_cmd_bind_pipeline(&cmdbuf, pipeline);
	assert(error == 0);
	error = i915_vk_cmd_draw(&cmdbuf, 3U, 1U, 0U, 0U);
	assert(error == 0);
	error = i915_vk_cmdbuf_end(&cmdbuf);
	assert(error == 0);

	/* The batch carries the pipeline state, the primitive and the terminator. */
	assert(find_command(buffer, cmdbuf.batch.cursor, GEN12_CMD_3DSTATE_VS) >= 0);
	assert(find_command(buffer, cmdbuf.batch.cursor, GEN12_CMD_3DSTATE_PS) >= 0);
	prim_at = find_command(buffer, cmdbuf.batch.cursor, GEN12_CMD_3DPRIMITIVE);
	assert(prim_at >= 0);
	assert(buffer[prim_at + 2] == 3U);	/* vertex count */
	assert(buffer[prim_at + 4] == 1U);	/* instance count */
	assert(buffer[cmdbuf.batch.cursor - 1U] == GEN12_MI_BATCH_BUFFER_END);

	i915_vk_pipeline_destroy(pipeline);
	assert(fixture_live == 0U);
	printf("i915 vk cmdbuf host test PASS\n");
	return 0;
}
