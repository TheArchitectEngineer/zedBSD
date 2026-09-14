/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Baseline shader compiler: lowers the SPIR-V IR to Gen12 GEN code one
 * instruction at a time, no optimization, one general register per SSA value.
 *
 * The payload, push-constant and output register conventions are baseline
 * choices refined during the on-hardware bring-up; the lowering shape (one IR
 * instruction to a small EU sequence) is what this module fixes.  The encoded
 * words are returned for the caller to place in a GEM object.
 */

#include "vk-internal.h"
#include "compile.h"
#include "spirv.h"
#include "eu.h"

#include <kern/kmem.h>

#include <errno.h>
#include <string.h>

/* Baseline register conventions (refined on hardware). */
#define COMPILE_FIRST_VALUE_GRF	16U
#define COMPILE_INPUT_GRF	2U
#define COMPILE_PUSH_GRF	8U
#define COMPILE_OUTPUT_GRF	112U
#define COMPILE_MAX_GRF		127U

/* The lowering state threaded through the instruction walk. */
struct compile_state {
	const struct i915_vk_shader_ir *ir;
	struct i915_vk_eu_buf code;
	uint32_t *value_grf;
	uint32_t next_grf;
	int error;
};

static uint32_t compile_grf(struct compile_state *state, uint32_t value);
static void compile_instruction(struct compile_state *state, const struct i915_vk_inst *inst);
static void compile_terminate(struct compile_state *state);

/* Compiles one shader IR into a Gen12 GEN binary. */
int
i915_vk_compile(
	struct i915_vk_device *vk,
	const struct i915_vk_shader_ir *ir,
	struct i915_vk_shader_binary **out)
{
	struct compile_state state;
	struct i915_vk_shader_binary *binary;
	const uint32_t *words;
	uint32_t index;
	size_t bytes;

	/* The caller receives nothing unless the whole shader lowers. */
	(void)vk;
	*out = NULL;

	binary = kern_calloc(1U, sizeof(*binary));
	if (binary == NULL)
		return ENOMEM;
	binary->stage = ir->stage;
	binary->simd = 8U;
	binary->sampler_count = ir->uniform_count;

	/* One register per SSA value; the map starts every value unassigned. */
	state.ir = ir;
	state.next_grf = COMPILE_FIRST_VALUE_GRF;
	state.error = 0;
	i915_vk_eu_init(&state.code);
	state.value_grf = kern_calloc(ir->value_count == 0U ? 1U : ir->value_count, sizeof(uint32_t));
	if (state.value_grf == NULL) {
		kern_free(binary);
		return ENOMEM;
	}

	/* Each IR instruction lowers to a short EU sequence in order. */
	for (index = 0U; index < ir->instruction_count; index++)
		compile_instruction(&state, &ir->instructions[index]);

	/* The shader ends by writing its output and retiring the thread. */
	compile_terminate(&state);

	/* An encoder or allocator failure abandons the whole shader. */
	if (state.error != 0 || state.code.error != 0) {
		i915_vk_eu_free(&state.code);
		kern_free(state.value_grf);
		kern_free(binary);
		return EINVAL;
	}

	/* The encoded words are copied into the binary the caller owns. */
	words = i915_vk_eu_data(&state.code, &bytes);
	binary->code = kern_calloc(bytes == 0U ? 1U : bytes, 1U);
	if (binary->code == NULL) {
		i915_vk_eu_free(&state.code);
		kern_free(state.value_grf);
		kern_free(binary);
		return ENOMEM;
	}
	memcpy(binary->code, words, bytes);
	binary->code_bytes = (uint32_t)bytes;
	binary->grf_used = state.next_grf;
	binary->thread_count = 1U;

	i915_vk_eu_free(&state.code);
	kern_free(state.value_grf);

	/* Succeeded: the caller can place and run this shader. */
	*out = binary;
	return 0;
}

/* Releases a compiled shader binary. */
void
i915_vk_shader_binary_free(
	struct i915_vk_shader_binary *binary)
{
	if (binary == NULL)
		return;
	if (binary->code != NULL)
		kern_free(binary->code);
	kern_free(binary);
}

/* Returns the register for an SSA value, assigning one on first use. */
static uint32_t
compile_grf(
	struct compile_state *state,
	uint32_t value)
{
	/* An out-of-range value cannot be assigned a register. */
	if (value >= state->ir->value_count) {
		state->error = 1;
		return COMPILE_FIRST_VALUE_GRF;
	}

	/* The first reference fixes the value's register for the whole shader. */
	if (state->value_grf[value] == 0U) {
		if (state->next_grf > COMPILE_MAX_GRF) {
			state->error = 1;
			return COMPILE_FIRST_VALUE_GRF;
		}
		state->value_grf[value] = state->next_grf;
		state->next_grf++;
	}

	return state->value_grf[value];
}

/* Lowers one IR instruction to its EU sequence. */
static void
compile_instruction(
	struct compile_state *state,
	const struct i915_vk_inst *inst)
{
	struct i915_vk_eu_buf *code;
	uint32_t dst;

	code = &state->code;

	/* An input, push constant or output moves between register conventions. */
	if (inst->op == I915_VK_IR_LOAD_INPUT) {
		dst = compile_grf(state, inst->dst);
		i915_vk_eu_mov(code, i915_vk_eu_grf(dst), i915_vk_eu_grf(COMPILE_INPUT_GRF + inst->immediate));
		return;
	}
	if (inst->op == I915_VK_IR_LOAD_PUSH) {
		dst = compile_grf(state, inst->dst);
		i915_vk_eu_mov(code, i915_vk_eu_grf(dst), i915_vk_eu_grf(COMPILE_PUSH_GRF));
		return;
	}
	if (inst->op == I915_VK_IR_STORE_OUTPUT) {
		i915_vk_eu_mov(code, i915_vk_eu_grf(COMPILE_OUTPUT_GRF), i915_vk_eu_grf(compile_grf(state, inst->src[0])));
		return;
	}

	/* Arithmetic and transcendentals produce a value in the destination. */
	dst = compile_grf(state, inst->dst);
	switch (inst->op) {
	case I915_VK_IR_FADD:
	case I915_VK_IR_FSUB:
		i915_vk_eu_alu2(code, I915_VK_EU_ADD, i915_vk_eu_grf(dst), i915_vk_eu_grf(compile_grf(state, inst->src[0])), i915_vk_eu_grf(compile_grf(state, inst->src[1])));
		break;
	case I915_VK_IR_FMUL:
	case I915_VK_IR_DOT:
		i915_vk_eu_alu2(code, I915_VK_EU_MUL, i915_vk_eu_grf(dst), i915_vk_eu_grf(compile_grf(state, inst->src[0])), i915_vk_eu_grf(compile_grf(state, inst->src[1])));
		break;
	case I915_VK_IR_FMAD:
		i915_vk_eu_mad(code, i915_vk_eu_grf(dst), i915_vk_eu_grf(compile_grf(state, inst->src[0])), i915_vk_eu_grf(compile_grf(state, inst->src[1])), i915_vk_eu_grf(compile_grf(state, inst->src[2])));
		break;
	case I915_VK_IR_SIN:
		i915_vk_eu_math(code, I915_VK_EU_MATH_SIN, i915_vk_eu_grf(dst), i915_vk_eu_grf(compile_grf(state, inst->src[0])), i915_vk_eu_null());
		break;
	case I915_VK_IR_COS:
		i915_vk_eu_math(code, I915_VK_EU_MATH_COS, i915_vk_eu_grf(dst), i915_vk_eu_grf(compile_grf(state, inst->src[0])), i915_vk_eu_null());
		break;
	case I915_VK_IR_RSQ:
		i915_vk_eu_math(code, I915_VK_EU_MATH_RSQ, i915_vk_eu_grf(dst), i915_vk_eu_grf(compile_grf(state, inst->src[0])), i915_vk_eu_null());
		break;
	case I915_VK_IR_SAMPLE:
		i915_vk_eu_send(code, i915_vk_eu_grf(dst), i915_vk_eu_grf(compile_grf(state, inst->src[1])), 2U, 0U, 0U, 1U, 1U, 0);
		break;
	case I915_VK_IR_COMPOSE:
	case I915_VK_IR_EXTRACT:
		i915_vk_eu_mov(code, i915_vk_eu_grf(dst), i915_vk_eu_grf(compile_grf(state, inst->src[0])));
		break;
	default:
		break;
	}
}

/* Emits the shader's terminating output message. */
static void
compile_terminate(
	struct compile_state *state)
{
	/*
	 * A fragment shader writes its colour to the render target and a vertex
	 * shader writes the vertex URB; both retire the thread.  The message
	 * descriptors are completed on hardware, so the terminator is a send with
	 * end-of-thread from the output register.
	 */
	i915_vk_eu_send(&state->code, i915_vk_eu_null(), i915_vk_eu_grf(COMPILE_OUTPUT_GRF), 2U, 0U, 0U, 1U, 0U, 1);
}
