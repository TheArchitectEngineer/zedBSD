/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Baseline shader compiler: lowers the scalar SPIR-V IR to Gen12 GEN code one
 * instruction at a time, no optimization.  An IR value is one float per SIMD
 * channel, which is exactly one general register in a SIMD8 dispatch, so a
 * value lives in one register from its definition to its last use (the IR is
 * straight-line SSA, so the last use is known from one backward look).
 *
 * The payload, push-constant, output-register and message conventions are
 * baseline choices that have NOT been verified on hardware; the lowering shape
 * (one IR instruction to a small EU sequence, operands and modifiers in the
 * right places) is what this module fixes and what the host tests check.  The
 * encoded words are returned for the caller to place in a GEM object.
 */

#include "vk-internal.h"
#include "compile.h"
#include "spirv.h"
#include "eu.h"

#include <kern/kmem.h>

#include <errno.h>
#include <string.h>

/*
 * Baseline register conventions (NOT verified on hardware).  The payload follows the
 * usual order: push constants first (32 bytes to a register, read as scalar regions),
 * then the inputs, four registers (components) to a location.  Outputs are staged in
 * four registers to a slot: a vertex shader's slot 0 is Position and location n is
 * slot n + 1; a fragment shader's location n is slot n.
 */
#define COMPILE_PAYLOAD_GRF	2U
#define COMPILE_FIRST_VALUE_GRF	16U
#define COMPILE_OUTPUT_GRF	112U
#define COMPILE_MAX_GRF		127U
#define COMPILE_NO_GRF		0U

/* The lowering state threaded through the instruction walk. */
struct compile_state {
	const struct i915_vk_shader_ir *ir;
	struct i915_vk_eu_buf code;
	uint32_t *value_grf;		/* the register a value lives in; COMPILE_NO_GRF before its definition */
	uint32_t *last_use;		/* index of the last instruction reading a value */
	uint8_t grf_busy[COMPILE_MAX_GRF + 1U];
	uint32_t index;			/* instruction being lowered */
	uint32_t grf_high;		/* one past the highest value register ever used */
	int error;
	int unsupported;	/* the IR asks for something this compiler cannot lower */
};

static uint32_t compile_grf(struct compile_state *state, uint32_t value);
static uint32_t compile_define(struct compile_state *state, uint32_t value, uint32_t count);
static uint32_t compile_sources(const struct i915_vk_inst *inst);
static void compile_release(struct compile_state *state, const struct i915_vk_inst *inst);
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

	/* The map starts every value without a register; two arrays share one allocation. */
	memset(&state, 0, sizeof(state));
	state.ir = ir;
	state.grf_high = COMPILE_FIRST_VALUE_GRF;
	i915_vk_eu_init(&state.code);
	state.value_grf = kern_calloc(ir->value_count == 0U ? 2U : 2U * (size_t)ir->value_count, sizeof(uint32_t));
	if (state.value_grf == NULL) {
		kern_free(binary);
		return ENOMEM;
	}
	state.last_use = state.value_grf + ir->value_count;

	/* The last reader of each value: where its register becomes free again. */
	for (index = 0U; index < ir->instruction_count; index++) {
		uint32_t source;

		for (source = 0U; source < compile_sources(&ir->instructions[index]); source++)
			if (ir->instructions[index].src[source] < ir->value_count)
				state.last_use[ir->instructions[index].src[source]] = index;
	}

	/* Each IR instruction lowers to a short EU sequence in order. */
	for (index = 0U; index < ir->instruction_count; index++) {
		state.index = index;
		compile_instruction(&state, &ir->instructions[index]);
		compile_release(&state, &ir->instructions[index]);
	}

	/* The shader ends by writing its output and retiring the thread. */
	compile_terminate(&state);

	/* A shader that needs something this compiler cannot lower is refused, never approximated. */
	if (state.unsupported != 0) {
		i915_vk_eu_free(&state.code);
		kern_free(state.value_grf);
		kern_free(binary);
		return ENOTSUP;
	}

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
	binary->grf_used = state.grf_high;
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

/* How many of an instruction's src[] name values. */
static uint32_t
compile_sources(
	const struct i915_vk_inst *inst)
{
	switch (inst->op) {
	case I915_VK_IR_STORE_OUTPUT: case I915_VK_IR_FNEG: case I915_VK_IR_RSQ: case I915_VK_IR_SIN:
	case I915_VK_IR_COS:
		return 1U;
	case I915_VK_IR_FADD: case I915_VK_IR_FSUB: case I915_VK_IR_FMUL: case I915_VK_IR_SAMPLE:
		return 2U;
	default:
		return 0U;
	}
}

/* Returns the register a value lives in; reading a value that has no definition is an error. */
static uint32_t
compile_grf(
	struct compile_state *state,
	uint32_t value)
{
	if (value >= state->ir->value_count || state->value_grf[value] == COMPILE_NO_GRF) {
		state->error = 1;
		return COMPILE_FIRST_VALUE_GRF;
	}
	return state->value_grf[value];
}

/*
 * Gives `count` consecutive values, starting at `value`, `count` consecutive free registers
 * (a message reply is consecutive registers) and returns the first.  SSA: a value is defined once.
 */
static uint32_t
compile_define(
	struct compile_state *state,
	uint32_t value,
	uint32_t count)
{
	uint32_t grf;
	uint32_t run;

	if (value >= state->ir->value_count || count > state->ir->value_count - value) {
		state->error = 1;
		return COMPILE_FIRST_VALUE_GRF;
	}
	for (run = 0U; run < count; run++) {
		if (state->value_grf[value + run] != COMPILE_NO_GRF) {
			state->error = 1;
			return COMPILE_FIRST_VALUE_GRF;
		}
	}
	for (grf = COMPILE_FIRST_VALUE_GRF; grf + count <= COMPILE_OUTPUT_GRF; grf++) {
		for (run = 0U; run < count && state->grf_busy[grf + run] == 0U; run++)
			;
		if (run != count)
			continue;
		for (run = 0U; run < count; run++) {
			state->grf_busy[grf + run] = 1U;
			state->value_grf[value + run] = grf + run;
		}
		if (grf + count > state->grf_high)
			state->grf_high = grf + count;
		return grf;
	}

	/* more values live at once than registers: refused, never spilled wrongly */
	state->unsupported = 1;
	return COMPILE_FIRST_VALUE_GRF;
}

/* Frees the registers of the values whose last reader was this instruction (or that nothing reads). */
static void
compile_release(
	struct compile_state *state,
	const struct i915_vk_inst *inst)
{
	uint32_t source;
	uint32_t value;
	uint32_t defined;

	for (source = 0U; source < compile_sources(inst); source++) {
		value = inst->src[source];
		if (value < state->ir->value_count && state->last_use[value] == state->index &&
		    state->value_grf[value] != COMPILE_NO_GRF)
			state->grf_busy[state->value_grf[value]] = 0U;
	}
	defined = inst->op == I915_VK_IR_SAMPLE ? 4U : inst->op == I915_VK_IR_STORE_OUTPUT ||
		inst->op == I915_VK_IR_NOP ? 0U : 1U;
	for (value = inst->dst; defined != 0U && value < state->ir->value_count; value++, defined--) {
		/* a value nothing reads afterwards (last_use stays 0 unless instruction 0 reads it) */
		if (state->last_use[value] <= state->index && state->value_grf[value] != COMPILE_NO_GRF)
			state->grf_busy[state->value_grf[value]] = 0U;
	}
}

/* Lowers one IR instruction to its EU sequence. */
static void
compile_instruction(
	struct compile_state *state,
	const struct i915_vk_inst *inst)
{
	struct i915_vk_eu_buf *code;
	struct i915_vk_eu_reg a;
	struct i915_vk_eu_reg b;
	uint32_t payload_inputs;
	uint32_t grf;
	uint32_t dst;

	code = &state->code;
	payload_inputs = COMPILE_PAYLOAD_GRF + (state->ir->push_bytes + 31U) / 32U;

	switch (inst->op) {
	case I915_VK_IR_NOP:
		break;

	/* A constant, input or push constant enters a value register. */
	case I915_VK_IR_CONST:
		dst = compile_define(state, inst->dst, 1U);
		i915_vk_eu_mov(code, i915_vk_eu_grf(dst), i915_vk_eu_imm_f(inst->immediate));
		break;
	case I915_VK_IR_LOAD_INPUT:
		grf = payload_inputs + 4U * inst->location + inst->component;
		if (inst->component > 3U || inst->location > COMPILE_FIRST_VALUE_GRF || grf >= COMPILE_FIRST_VALUE_GRF) {
			state->unsupported = 1;	/* more inputs than the payload convention has registers for */
			break;
		}
		dst = compile_define(state, inst->dst, 1U);
		i915_vk_eu_mov(code, i915_vk_eu_grf(dst), i915_vk_eu_grf(grf));
		break;
	case I915_VK_IR_LOAD_PUSH:
		if ((inst->immediate & 3U) != 0U || inst->immediate + 4U > state->ir->push_bytes) {
			state->error = 1;	/* not a float inside the declared push-constant bytes */
			break;
		}
		dst = compile_define(state, inst->dst, 1U);
		i915_vk_eu_mov(code, i915_vk_eu_grf(dst),
			i915_vk_eu_grf_scalar(COMPILE_PAYLOAD_GRF + inst->immediate / 32U, inst->immediate % 32U));
		break;

	/* An output component leaves through its staging register. */
	case I915_VK_IR_STORE_OUTPUT:
		if (inst->location == I915_VK_IR_LOCATION_POSITION)
			grf = state->ir->stage == I915_VK_STAGE_VERTEX ? 0U : COMPILE_MAX_GRF;
		else
			grf = inst->location < COMPILE_MAX_GRF ?
				inst->location + (state->ir->stage == I915_VK_STAGE_VERTEX ? 1U : 0U) : COMPILE_MAX_GRF;
		grf = COMPILE_OUTPUT_GRF + 4U * grf + inst->component;
		if (inst->component > 3U || grf > COMPILE_MAX_GRF) {
			state->unsupported = 1;	/* more outputs than staging registers, or Position outside a vertex shader */
			break;
		}
		i915_vk_eu_mov(code, i915_vk_eu_grf(grf), i915_vk_eu_grf(compile_grf(state, inst->src[0])));
		break;

	/* Arithmetic: sources are read before the destination gets its register. */
	case I915_VK_IR_FADD:
	case I915_VK_IR_FSUB:
	case I915_VK_IR_FMUL:
		a = i915_vk_eu_grf(compile_grf(state, inst->src[0]));
		b = i915_vk_eu_grf(compile_grf(state, inst->src[1]));
		/* a - b = a + (-b): ADD with the SECOND source negated (operand order matters) */
		if (inst->op == I915_VK_IR_FSUB)
			b = i915_vk_eu_negate(b);
		dst = compile_define(state, inst->dst, 1U);
		i915_vk_eu_alu2(code, inst->op == I915_VK_IR_FMUL ? I915_VK_EU_MUL : I915_VK_EU_ADD, i915_vk_eu_grf(dst), a, b);
		break;
	case I915_VK_IR_FNEG:
		/* -a: a MOV whose source carries the negate modifier */
		a = i915_vk_eu_negate(i915_vk_eu_grf(compile_grf(state, inst->src[0])));
		dst = compile_define(state, inst->dst, 1U);
		i915_vk_eu_mov(code, i915_vk_eu_grf(dst), a);
		break;
	case I915_VK_IR_SIN:
	case I915_VK_IR_COS:
	case I915_VK_IR_RSQ:
		a = i915_vk_eu_grf(compile_grf(state, inst->src[0]));
		dst = compile_define(state, inst->dst, 1U);
		i915_vk_eu_math(code, inst->op == I915_VK_IR_SIN ? I915_VK_EU_MATH_SIN : inst->op == I915_VK_IR_COS ?
			I915_VK_EU_MATH_COS : I915_VK_EU_MATH_RSQ, i915_vk_eu_grf(dst), a, i915_vk_eu_null());
		break;

	/*
	 * texture(): the message payload is u then v in two consecutive registers, the reply is
	 * the four colour components in four consecutive registers.  The descriptor (binding
	 * table index, message type, SIMD mode) is not filled in yet -- see the header comment.
	 */
	case I915_VK_IR_SAMPLE:
		a = i915_vk_eu_grf(compile_grf(state, inst->src[0]));
		b = i915_vk_eu_grf(compile_grf(state, inst->src[1]));
		dst = compile_define(state, inst->dst, 4U);
		for (grf = COMPILE_FIRST_VALUE_GRF; grf + 2U <= COMPILE_OUTPUT_GRF; grf++)
			if (state->grf_busy[grf] == 0U && state->grf_busy[grf + 1U] == 0U)
				break;
		if (grf + 2U > COMPILE_OUTPUT_GRF) {
			state->unsupported = 1;
			break;
		}
		if (grf + 2U > state->grf_high)
			state->grf_high = grf + 2U;
		i915_vk_eu_mov(code, i915_vk_eu_grf(grf), a);
		i915_vk_eu_mov(code, i915_vk_eu_grf(grf + 1U), b);
		i915_vk_eu_send(code, i915_vk_eu_grf(dst), i915_vk_eu_grf(grf), 2U, 0U, 0U, 2U, 4U, 0);
		break;

	default:
		/* an IR operation this compiler does not know is never dropped */
		state->unsupported = 1;
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
