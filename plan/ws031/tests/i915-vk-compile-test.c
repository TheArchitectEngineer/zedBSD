/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Host fixture for the baseline shader compiler (p006). Parses the vkdemo
 * shaders, lowers them to Gen12 GEN code and checks the binary is non-empty,
 * one register per value, and carries the expected instruction kinds. Semantic
 * correctness on hardware is the big-bang test; this verifies the lowering.
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
#include "../../../src/drivers/gpu/i915/vk/eu.c"
#include "../../../src/drivers/gpu/i915/vk/compile.c"

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
	code = malloc((size_t)size);
	assert(fread(code, 1, (size_t)size, file) == (size_t)size);
	fclose(file);
	*words = (size_t)size / 4U;
	return code;
}

/* Reports whether any instruction in the code carries the given hw opcode. */
static int
has_opcode(const struct i915_vk_shader_binary *binary, uint32_t opcode)
{
	uint32_t dwords;
	uint32_t index;

	dwords = binary->code_bytes / 4U;
	for (index = 0U; index + 4U <= dwords; index += 4U) {
		if ((binary->code[index] & 0x7FU) == opcode)
			return 1;
	}
	return 0;
}

static void
compile_shader(const char *name, enum i915_vk_stage stage, uint32_t expect_math)
{
	uint32_t *spv;
	size_t words;
	struct i915_vk_shader_ir *ir;
	struct i915_vk_shader_binary *binary;
	int error;

	spv = load_spv(name, &words);
	error = i915_vk_spirv_parse(spv, words, stage, &ir);
	assert(error == 0);

	error = i915_vk_compile(NULL, ir, &binary);
	assert(error == 0);

	/* The binary carries whole instructions, more than the payload registers. */
	assert(binary->code_bytes > 0U);
	assert((binary->code_bytes % (4U * 4U)) == 0U);
	assert(binary->stage == stage);
	assert(binary->grf_used > COMPILE_FIRST_VALUE_GRF);

	/* Every shader ends by sending its output and retiring the thread. */
	assert(has_opcode(binary, EU_OP_SEND));

	/* The vertex shader's rotation lowers to math instructions. */
	if (expect_math != 0U)
		assert(has_opcode(binary, EU_OP_MATH));

	i915_vk_shader_binary_free(binary);
	i915_vk_spirv_free(ir);
	free(spv);
}

int
main(void)
{
	compile_shader("cuboid.frag.spv", I915_VK_STAGE_FRAGMENT, 0U);
	compile_shader("cuboid.vert.spv", I915_VK_STAGE_VERTEX, 1U);
	assert(fixture_live == 0U);
	printf("i915 vk compile host test PASS\n");
	return 0;
}
