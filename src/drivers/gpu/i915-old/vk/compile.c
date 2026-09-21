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
 * E-128: the register and message conventions are the ones Mesa's compiler uses for the same shaders
 * (tools/refvk.c, disassembled with gentool) -- see the table below -- and the emitted kernels are
 * judged by Mesa's assembler / disassembler (tests/run-vk-gentool-test.sh) before they meet a GPU.
 * The encoded words are returned for the caller to place in a GEM object.
 */

#include "vk-internal.h"
#include "compile.h"
#include "spirv.h"
#include "eu.h"

#include <kern/kmem.h>

#include <errno.h>
#include <string.h>

/*
 * Register conventions of a SIMD8 dispatch on Gen12 (one register = one float to a channel).
 *
 * Vertex shader payload:   r0 header, r1 the URB handles of the eight vertices,
 *                          r2.. the push constants (32 bytes to a register, scalar regions),
 *                          then four registers (x y z w) to an attribute, attributes in ascending
 *                          location order -- the order of the vertex elements the draw programs.
 * Vertex shader output:    the VUE is [header][position][varyings in ascending location order], four
 *                          registers to a slot.  Header and position go out with a first URB write
 *                          (handles from r1); the varyings with the write that ends the thread, whose
 *                          whole payload has to sit in r112..r127: the handles copied to r127, the
 *                          varyings right below.  With no varying the one write is that last write.
 * Fragment shader payload: r0 header, r1 pixel positions, r2 / r3 the two perspective barycentrics of
 *                          each pixel, push constants, then two registers to an input (ascending
 *                          location): component c keeps its plane in floats 4 * (c & 1) .. + 3 of
 *                          register c / 2 as [d/d bary1, d/d bary2, -, value at the origin].
 *                          Gen11+ has no PLN: value = origin + d1 * bary1 + d2 * bary2, written out.
 * Fragment shader output:  location 0 in r124..r127, the render-target write that ends the thread.
 * Texture:                 one SIMD8 "sample" message, u and v each its own payload run, the reply
 *                          four registers; binding table entry 1 + n, sampler n for the n-th
 *                          sampled image of the shader (entry 0 is the render target).
 */
#define COMPILE_PAYLOAD_GRF	2U	/* vertex: push constants start here */
#define COMPILE_FS_BARY1_GRF	2U
#define COMPILE_FS_BARY2_GRF	3U
#define COMPILE_FS_SETUP_GRF	4U	/* fragment: push constants, then the input planes */
#define COMPILE_SCRATCH_GRF	15U	/* one temporary of the interpolation */
#define COMPILE_FIRST_VALUE_GRF	16U
#define COMPILE_LAST_VALUE_GRF	95U
#define COMPILE_VUE_GRF		100U	/* vertex: header r100..r103, position r104..r107 */
#define COMPILE_EOT_GRF		112U	/* a message that ends the thread reads r112..r127 only */
#define COMPILE_MAX_GRF		127U
#define COMPILE_NO_GRF		0U
#define COMPILE_MAX_VARYINGS	3U	/* 4 * 3 registers below the handles in r127 */
#define COMPILE_MAX_INPUTS	3U	/* payload registers below COMPILE_SCRATCH_GRF */

/* Message descriptors (gentool's reading of Mesa's kernels for the same shaders). */
#define COMPILE_DESC_URB_WRITE(slot)	(0x02080007U | ((uint32_t)(slot) << 4))	/* mlen 1 (handles), header, SIMD8 write */
#define COMPILE_DESC_SAMPLE(bti, smp)	(0x02420000U | ((uint32_t)(smp) << 8) | (uint32_t)(bti))	/* mlen 1, rlen 4, SIMD8 sample */
#define COMPILE_DESC_RT_WRITE		0x08031400U	/* mlen 4, SIMD8 single source, last target, entry 0 */
#define COMPILE_EX_MLEN(n)		((uint32_t)(n) << 6)

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
	uint32_t inputs[COMPILE_MAX_INPUTS];		/* input locations, ascending */
	uint32_t input_count;
	uint32_t varyings[COMPILE_MAX_VARYINGS];	/* vertex: output locations other than Position, ascending */
	uint32_t varying_count;
	uint32_t push_regs;
};

static uint32_t compile_grf(struct compile_state *state, uint32_t value);
static uint32_t compile_define(struct compile_state *state, uint32_t value, uint32_t count);
static uint32_t compile_sources(const struct i915_vk_inst *inst);
static void compile_release(struct compile_state *state, const struct i915_vk_inst *inst);
static void compile_instruction(struct compile_state *state, const struct i915_vk_inst *inst);
static void compile_terminate(struct compile_state *state);
static void compile_interface(struct compile_state *state);
static void compile_prologue(struct compile_state *state);
static int compile_rank(const uint32_t *list, uint32_t count, uint32_t location, uint32_t *rank);

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

	/* What the shader reads and writes fixes where its payload and its outputs are. */
	compile_interface(&state);
	compile_prologue(&state);

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
	binary->push_regs = state.push_regs;
	binary->input_count = state.input_count;
	memcpy(binary->input_locations, state.inputs, sizeof(state.inputs));
	binary->varying_count = ir->stage == I915_VK_STAGE_VERTEX ? state.varying_count : state.input_count;
	binary->dispatch_grf_start = ir->stage == I915_VK_STAGE_VERTEX ? COMPILE_PAYLOAD_GRF : COMPILE_FS_SETUP_GRF;

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
	for (grf = COMPILE_FIRST_VALUE_GRF; grf + count <= COMPILE_LAST_VALUE_GRF + 1U; grf++) {
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
	payload_inputs = (state->ir->stage == I915_VK_STAGE_VERTEX ? COMPILE_PAYLOAD_GRF : COMPILE_FS_SETUP_GRF) +
		state->push_regs;

	switch (inst->op) {
	case I915_VK_IR_NOP:
		break;

	/* A constant, input or push constant enters a value register. */
	case I915_VK_IR_CONST:
		dst = compile_define(state, inst->dst, 1U);
		i915_vk_eu_mov(code, i915_vk_eu_grf(dst), i915_vk_eu_imm_f(inst->immediate));
		break;
	case I915_VK_IR_LOAD_INPUT:
		if (inst->component > 3U || compile_rank(state->inputs, state->input_count, inst->location, &grf) != 0) {
			state->unsupported = 1;
			break;
		}
		dst = compile_define(state, inst->dst, 1U);
		if (state->ir->stage == I915_VK_STAGE_VERTEX) {
			/* an attribute component is a payload register of its own */
			i915_vk_eu_mov(code, i915_vk_eu_grf(dst),
				i915_vk_eu_grf(payload_inputs + 4U * grf + inst->component));
		} else {
			/* origin + d1 * bary1 + d2 * bary2 over the component's plane (see the conventions) */
			uint32_t plane = COMPILE_FS_SETUP_GRF + state->push_regs + 2U * grf + inst->component / 2U;
			uint32_t first = (inst->component & 1U) * 16U;

			i915_vk_eu_alu2(code, I915_VK_EU_MUL, i915_vk_eu_grf(dst),
				i915_vk_eu_grf_scalar(plane, first + 4U), i915_vk_eu_grf(COMPILE_FS_BARY2_GRF));
			i915_vk_eu_alu2(code, I915_VK_EU_ADD, i915_vk_eu_grf(dst),
				i915_vk_eu_grf(dst), i915_vk_eu_grf_scalar(plane, first + 12U));
			i915_vk_eu_alu2(code, I915_VK_EU_MUL, i915_vk_eu_grf(COMPILE_SCRATCH_GRF),
				i915_vk_eu_grf_scalar(plane, first), i915_vk_eu_grf(COMPILE_FS_BARY1_GRF));
			i915_vk_eu_alu2(code, I915_VK_EU_ADD, i915_vk_eu_grf(dst),
				i915_vk_eu_grf(dst), i915_vk_eu_grf(COMPILE_SCRATCH_GRF));
		}
		break;
	case I915_VK_IR_LOAD_PUSH:
		if ((inst->immediate & 3U) != 0U || inst->immediate + 4U > state->ir->push_bytes) {
			state->error = 1;	/* not a float inside the declared push-constant bytes */
			break;
		}
		dst = compile_define(state, inst->dst, 1U);
		i915_vk_eu_mov(code, i915_vk_eu_grf(dst),
			i915_vk_eu_grf_scalar(payload_inputs - state->push_regs + inst->immediate / 32U, inst->immediate % 32U));
		break;

	/* An output component leaves through its staging register. */
	case I915_VK_IR_STORE_OUTPUT:
		if (inst->component > 3U) {
			state->unsupported = 1;
			break;
		}
		if (state->ir->stage == I915_VK_STAGE_VERTEX && inst->location == I915_VK_IR_LOCATION_POSITION) {
			grf = (state->varying_count != 0U ? COMPILE_VUE_GRF : COMPILE_MAX_GRF - 8U) + 4U;
		} else if (state->ir->stage == I915_VK_STAGE_VERTEX) {
			if (compile_rank(state->varyings, state->varying_count, inst->location, &grf) != 0) {
				state->unsupported = 1;
				break;
			}
			grf = COMPILE_MAX_GRF - 4U * state->varying_count + 4U * grf;
		} else if (inst->location == 0U) {
			grf = COMPILE_MAX_GRF - 3U;
		} else {
			state->unsupported = 1;	/* XXX: one colour output; Position belongs to a vertex shader */
			break;
		}
		i915_vk_eu_mov(code, i915_vk_eu_grf(grf + inst->component), i915_vk_eu_grf(compile_grf(state, inst->src[0])));
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

	/* texture(): see the conventions -- u and v are the two payload runs, the reply is four registers. */
	case I915_VK_IR_SAMPLE:
		a = i915_vk_eu_grf(compile_grf(state, inst->src[0]));
		b = i915_vk_eu_grf(compile_grf(state, inst->src[1]));
		for (grf = 0U; grf < state->ir->uniform_count; grf++)
			if (state->ir->uniforms[grf].set == inst->location && state->ir->uniforms[grf].binding == inst->immediate)
				break;
		if (grf >= state->ir->uniform_count || grf > 14U) {
			state->unsupported = 1;
			break;
		}
		dst = compile_define(state, inst->dst, 4U);
		i915_vk_eu_send(code, i915_vk_eu_grf(dst), a, b, 2U /* sampler */, COMPILE_DESC_SAMPLE(1U + grf, grf),
			COMPILE_EX_MLEN(1U), 0, 0);
		break;

	default:
		/* an IR operation this compiler does not know is never dropped */
		state->unsupported = 1;
		break;
	}
}

/* The position of `location` in an ascending list. */
static int
compile_rank(
	const uint32_t *list,
	uint32_t count,
	uint32_t location,
	uint32_t *rank)
{
	uint32_t index;

	for (index = 0U; index < count; index++)
		if (list[index] == location) {
			*rank = index;
			return 0;
		}
	return ENOENT;
}

/* Adds `location` to an ascending list without duplicates; more than `limit` entries is refused. */
static void
compile_note(
	struct compile_state *state,
	uint32_t *list,
	uint32_t *count,
	uint32_t limit,
	uint32_t location)
{
	uint32_t index;
	uint32_t at;

	for (at = 0U; at < *count && list[at] < location; at++)
		;
	if (at < *count && list[at] == location)
		return;
	if (*count >= limit) {
		state->unsupported = 1;
		return;
	}
	for (index = *count; index > at; index--)
		list[index] = list[index - 1U];
	list[at] = location;
	(*count)++;
}

/* Reads the interface off the instructions: the locations read, the locations written. */
static void
compile_interface(
	struct compile_state *state)
{
	const struct i915_vk_inst *inst;
	uint32_t index;

	state->push_regs = (state->ir->push_bytes + 31U) / 32U;
	for (index = 0U; index < state->ir->instruction_count; index++) {
		inst = &state->ir->instructions[index];
		if (inst->op == I915_VK_IR_LOAD_INPUT)
			compile_note(state, state->inputs, &state->input_count, COMPILE_MAX_INPUTS, inst->location);
		else if (inst->op == I915_VK_IR_STORE_OUTPUT && state->ir->stage == I915_VK_STAGE_VERTEX &&
		    inst->location != I915_VK_IR_LOCATION_POSITION)
			compile_note(state, state->varyings, &state->varying_count, COMPILE_MAX_VARYINGS, inst->location);
	}

	/* the payload has to end below the scratch register */
	if ((state->ir->stage == I915_VK_STAGE_VERTEX ?
	    COMPILE_PAYLOAD_GRF + state->push_regs + 4U * state->input_count :
	    COMPILE_FS_SETUP_GRF + state->push_regs + 2U * state->input_count) > COMPILE_SCRATCH_GRF)
		state->unsupported = 1;
}

/* What the outputs hold where the shader stores nothing: zeros, and the VUE header. */
static void
compile_prologue(
	struct compile_state *state)
{
	struct i915_vk_eu_reg header;
	uint32_t first;
	uint32_t grf;

	if (state->ir->stage == I915_VK_STAGE_VERTEX) {
		/* header (point size, layer, viewport index): integer zeros; then position and the varyings */
		first = state->varying_count != 0U ? COMPILE_VUE_GRF : COMPILE_MAX_GRF - 8U;
		for (grf = first; grf < first + 4U; grf++) {
			header = i915_vk_eu_grf_ud(grf);
			header.type = 6U;	/* D */
			i915_vk_eu_mov(&state->code, header, i915_vk_eu_imm_d(0U));
		}
		for (grf = first + 4U; grf < first + 8U; grf++)
			i915_vk_eu_mov(&state->code, i915_vk_eu_grf(grf), i915_vk_eu_imm_f(0U));
		for (grf = COMPILE_MAX_GRF - 4U * state->varying_count; grf < COMPILE_MAX_GRF; grf++)
			i915_vk_eu_mov(&state->code, i915_vk_eu_grf(grf), i915_vk_eu_imm_f(0U));
	} else {
		for (grf = COMPILE_MAX_GRF - 3U; grf <= COMPILE_MAX_GRF; grf++)
			i915_vk_eu_mov(&state->code, i915_vk_eu_grf(grf), i915_vk_eu_imm_f(0U));
	}
}

/* Emits the shader's terminating output message. */
static void
compile_terminate(
	struct compile_state *state)
{
	struct i915_vk_eu_buf *code = &state->code;

	if (state->ir->stage != I915_VK_STAGE_VERTEX) {
		/* the colour in r124..r127 to the render target: SENDC, as a render-target write must be */
		i915_vk_eu_send(code, i915_vk_eu_null(), i915_vk_eu_grf(COMPILE_MAX_GRF - 3U), i915_vk_eu_null(),
			5U /* render cache */, COMPILE_DESC_RT_WRITE, 0U, 1, 1);
		return;
	}

	if (state->varying_count != 0U) {
		/* header and position, slots 0 and 1 */
		i915_vk_eu_send(code, i915_vk_eu_null(), i915_vk_eu_grf(1U), i915_vk_eu_grf(COMPILE_VUE_GRF),
			6U /* URB */, COMPILE_DESC_URB_WRITE(0U), COMPILE_EX_MLEN(8U), 0, 0);
		/* the varyings from slot 2, and the end of the thread: handles and payload in r112..r127 */
		i915_vk_eu_mov(code, i915_vk_eu_grf_ud(COMPILE_MAX_GRF), i915_vk_eu_grf_ud(1U));
		i915_vk_eu_send(code, i915_vk_eu_null(), i915_vk_eu_grf(COMPILE_MAX_GRF),
			i915_vk_eu_grf(COMPILE_MAX_GRF - 4U * state->varying_count), 6U, COMPILE_DESC_URB_WRITE(2U),
			COMPILE_EX_MLEN(4U * state->varying_count), 0, 1);
	} else {
		i915_vk_eu_mov(code, i915_vk_eu_grf_ud(COMPILE_MAX_GRF), i915_vk_eu_grf_ud(1U));
		i915_vk_eu_send(code, i915_vk_eu_null(), i915_vk_eu_grf(COMPILE_MAX_GRF),
			i915_vk_eu_grf(COMPILE_MAX_GRF - 8U), 6U, COMPILE_DESC_URB_WRITE(0U), COMPILE_EX_MLEN(8U), 0, 1);
	}
}
