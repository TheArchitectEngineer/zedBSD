/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Host fixture for the SPIR-V parser (p004). Parses the vkdemo vertex and
 * fragment shaders and checks the extracted interface and instruction stream.
 * VK_REPO names the repository root so the .spv files can be read.
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

#include "../../../src/drivers/gpu/i915/vk/spirv.c"

/* Loads a SPIR-V file into a word buffer the caller frees. */
static uint32_t *
load_spv(const char *name, size_t *words)
{
	char path[512];
	FILE *file;
	long size;
	uint32_t *code;

	snprintf(path, sizeof(path), "%s/userland/base/vkdemo/shaders/%s", VK_REPO, name);
	file = fopen(path, "rb");
	assert(file != NULL);
	fseek(file, 0, SEEK_END);
	size = ftell(file);
	fseek(file, 0, SEEK_SET);
	assert(size > 0 && (size % 4) == 0);
	code = malloc((size_t)size);
	assert(code != NULL);
	assert(fread(code, 1, (size_t)size, file) == (size_t)size);
	fclose(file);
	*words = (size_t)size / 4U;
	return code;
}

/* Counts IR instructions of one opcode. */
static unsigned
count_op(const struct i915_vk_shader_ir *ir, enum i915_vk_ir_op op)
{
	unsigned found;
	unsigned index;

	found = 0U;
	for (index = 0U; index < ir->instruction_count; index++) {
		if (ir->instructions[index].op == op)
			found++;
	}
	return found;
}

static void
test_vertex(void)
{
	uint32_t *code;
	size_t words;
	struct i915_vk_shader_ir *ir;
	int error;

	code = load_spv("cuboid.vert.spv", &words);
	error = i915_vk_spirv_parse(code, words, I915_VK_STAGE_VERTEX, &ir);
	assert(error == 0);

	/* The vertex shader takes a vec3 position and a vec2 texture coordinate. */
	assert(ir->stage == I915_VK_STAGE_VERTEX);
	assert(ir->input_count == 2U);
	assert(ir->inputs[0].location == 0U && ir->inputs[0].components == 3U);
	assert(ir->inputs[1].location == 1U && ir->inputs[1].components == 2U);

	/* Its only located output is the passed-through texture coordinate. */
	assert(ir->output_count == 1U);
	assert(ir->outputs[0].location == 0U && ir->outputs[0].components == 2U);

	/* The rotation uses transcendentals and the position is read and written. */
	assert(count_op(ir, I915_VK_IR_SIN) >= 2U);
	assert(count_op(ir, I915_VK_IR_COS) >= 2U);
	assert(count_op(ir, I915_VK_IR_LOAD_INPUT) >= 1U);
	assert(count_op(ir, I915_VK_IR_STORE_OUTPUT) >= 1U);
	assert(count_op(ir, I915_VK_IR_LOAD_PUSH) >= 1U);

	i915_vk_spirv_free(ir);
	free(code);
}

static void
test_fragment(void)
{
	uint32_t *code;
	size_t words;
	struct i915_vk_shader_ir *ir;
	int error;

	code = load_spv("cuboid.frag.spv", &words);
	error = i915_vk_spirv_parse(code, words, I915_VK_STAGE_FRAGMENT, &ir);
	assert(error == 0);

	/* The fragment shader samples one texture at an interpolated coordinate. */
	assert(ir->stage == I915_VK_STAGE_FRAGMENT);
	assert(ir->input_count == 1U);
	assert(ir->inputs[0].location == 0U && ir->inputs[0].components == 2U);
	assert(ir->output_count == 1U);
	assert(ir->outputs[0].location == 0U && ir->outputs[0].components == 4U);
	assert(ir->uniform_count == 1U);
	assert(ir->uniforms[0].set == 0U && ir->uniforms[0].binding == 0U);
	assert(count_op(ir, I915_VK_IR_SAMPLE) >= 1U);

	i915_vk_spirv_free(ir);
	free(code);
}

static void
test_rejects_garbage(void)
{
	uint32_t bad[8];
	struct i915_vk_shader_ir *ir;
	int error;

	/* A wrong magic is rejected without allocating an IR. */
	memset(bad, 0, sizeof(bad));
	bad[0] = 0x12345678U;
	error = i915_vk_spirv_parse(bad, 8U, I915_VK_STAGE_VERTEX, &ir);
	assert(error != 0);
	assert(ir == NULL);
}

int
main(void)
{
	test_vertex();
	test_fragment();
	test_rejects_garbage();
	assert(fixture_live == 0U);
	printf("i915 vk spirv host test PASS\n");
	return 0;
}
