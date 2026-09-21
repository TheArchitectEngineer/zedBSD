/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Baseline shader code generator: lowers the scalar IR to Gen12 EU code one
 * instruction at a time, no optimization.
 *
 * An IR value is one float per SIMD channel, which is exactly one general
 * register in a SIMD8 dispatch, so a value lives in one register from its
 * definition to its last use (the IR is straight-line SSA, so the last use is
 * known from one backward look).
 *
 * The register and message conventions are the ones Mesa's compiler uses for
 * the same shaders (tools/refvk.c, disassembled with gentool) -- see the table
 * below -- and the emitted kernels are judged by Mesa's assembler and
 * disassembler (plan/ws031/tests/run-vk-gentool-test.sh) before they meet a
 * GPU.  The encoded words are returned for the caller to place in a GPU
 * object.
 */

#include "compiler.h"
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

/* Vertex: the push constants start here. */
#define COMPILE_PAYLOAD_GRF	2U

/* Fragment: the two perspective barycentrics. */
#define COMPILE_FS_BARY1_GRF	2U
#define COMPILE_FS_BARY2_GRF	3U

/* Fragment: the push constants, then the input planes. */
#define COMPILE_FS_SETUP_GRF	4U

/* The one temporary of the interpolation. */
#define COMPILE_SCRATCH_GRF	15U

/* The registers values are given. */
#define COMPILE_FIRST_VALUE_GRF	16U
#define COMPILE_LAST_VALUE_GRF	95U

/* Vertex: the VUE header in r100..r103, the position in r104..r107. */
#define COMPILE_VUE_GRF		100U

/* A message that ends the thread reads r112..r127 only. */
#define COMPILE_EOT_GRF		112U

#define COMPILE_MAX_GRF		127U

/* The value_grf entry of a value that has no register yet. */
#define COMPILE_NO_GRF		0U

/* Vertex: 4 * 3 registers of varyings below the handles in r127. */
#define COMPILE_MAX_VARYINGS	3U

/* Payload registers below COMPILE_SCRATCH_GRF. */
#define COMPILE_MAX_INPUTS	3U

/* The highest sampler index the sample descriptor can name. */
#define COMPILE_MAX_SAMPLER	14U

/* Every kernel is a SIMD8 kernel. */
#define COMPILE_SIMD		8U

/* The EU's signed 32-bit integer type, which the VUE header is written as. */
#define COMPILE_TYPE_D		6U

/* The shared functions the kernels send messages to. */
#define COMPILE_SFID_SAMPLER		2U
#define COMPILE_SFID_RENDER_CACHE	5U
#define COMPILE_SFID_URB		6U

/*
 * Message descriptors (gentool's reading of Mesa's kernels for the same shaders).
 *
 * URB write: mlen 1 (handles), header, SIMD8 write.  Sample: mlen 1, rlen 4,
 * SIMD8 sample.  Render-target write: mlen 4, SIMD8 single source, last
 * target, entry 0.  The extended descriptor carries the second run's length.
 */
#define COMPILE_DESC_URB_WRITE(slot)	(0x02080007U | ((uint32_t)(slot) << 4))
#define COMPILE_DESC_SAMPLE(bti, smp)	(0x02420000U | ((uint32_t)(smp) << 8) | (uint32_t)(bti))
#define COMPILE_DESC_RT_WRITE		0x08031400U
#define COMPILE_EX_MLEN(n)		((uint32_t)(n) << 6)

/*
 * The lowering state threaded through the instruction walk.
 *
 * It lives on the stack of drv_i915_shader_compile() for one compile and
 * owns the encoder buffer and the value maps until the compile ends.
 */
struct i915_compile_state {
	const struct i915_shader_ir *ir;
	struct i915_eu_buf code;

	/* The register a value lives in; COMPILE_NO_GRF before its definition. */
	uint32_t *value_grf;

	/* The index of the last instruction reading a value. */
	uint32_t *last_use;

	uint8_t grf_busy[COMPILE_MAX_GRF + 1U];

	/* The instruction being lowered. */
	uint32_t index;

	/* One past the highest value register ever used. */
	uint32_t grf_high;

	int error;

	/* The IR asks for something this compiler cannot lower. */
	int unsupported;

	/* The input locations, ascending. */
	uint32_t inputs[COMPILE_MAX_INPUTS];
	uint32_t input_count;

	/* Vertex: the output locations other than Position, ascending. */
	uint32_t varyings[COMPILE_MAX_VARYINGS];
	uint32_t varying_count;

	uint32_t push_regs;
};

static uint32_t i915_compile_sources(const struct i915_shader_ir_inst *inst);
static uint32_t i915_compile_grf(struct i915_compile_state *state, uint32_t value);
static uint32_t i915_compile_define(struct i915_compile_state *state, uint32_t value, uint32_t count);
static void i915_compile_release(struct i915_compile_state *state, const struct i915_shader_ir_inst *inst);
static void i915_compile_instruction(struct i915_compile_state *state, const struct i915_shader_ir_inst *inst);
static void i915_compile_load_input(struct i915_compile_state *state, const struct i915_shader_ir_inst *inst, uint32_t payload_inputs);
static void i915_compile_load_push(struct i915_compile_state *state, const struct i915_shader_ir_inst *inst, uint32_t payload_inputs);
static void i915_compile_store_output(struct i915_compile_state *state, const struct i915_shader_ir_inst *inst);
static void i915_compile_arithmetic(struct i915_compile_state *state, const struct i915_shader_ir_inst *inst);
static void i915_compile_negate(struct i915_compile_state *state, const struct i915_shader_ir_inst *inst);
static void i915_compile_math(struct i915_compile_state *state, const struct i915_shader_ir_inst *inst);
static void i915_compile_sample(struct i915_compile_state *state, const struct i915_shader_ir_inst *inst);
static int i915_compile_rank(const uint32_t *list, uint32_t count, uint32_t location, uint32_t *rank);
static void i915_compile_note(struct i915_compile_state *state, uint32_t *list, uint32_t *count, uint32_t limit, uint32_t location);
static void i915_compile_interface(struct i915_compile_state *state);
static void i915_compile_prologue(struct i915_compile_state *state);
static void i915_compile_terminate(struct i915_compile_state *state);

/*
 * Compiles one shader IR into a Gen12 EU binary.
 *
 * Returns 0 and the binary in `*out`, ENOTSUP for IR this compiler cannot
 * lower (never approximated), EINVAL for inconsistent IR or an encoder
 * failure, or ENOMEM.  On a failure `*out` is NULL.
 */
int
drv_i915_shader_compile(
	const struct i915_shader_ir *ir,
	struct i915_shader_binary **out)
{
	struct i915_compile_state state;
	struct i915_shader_binary *binary;
	const uint32_t *words;
	uint32_t index;
	uint32_t source;
	uint32_t value;
	size_t map_entries;
	size_t code_size;
	size_t bytes;

	/* The caller receives nothing unless the whole shader lowers. */
	*out = NULL;

	/* Allocates the binary and records what is known before lowering. */
	binary = kern_calloc(1U, sizeof(*binary));
	if (binary == NULL)
		return ENOMEM;
	binary->stage = ir->stage;
	binary->simd = COMPILE_SIMD;
	binary->sampler_count = ir->uniform_count;

	/* Prepares the lowering state with no register given out. */
	memset(&state, 0, sizeof(state));
	state.ir = ir;
	state.grf_high = COMPILE_FIRST_VALUE_GRF;
	drv_i915_eu_init(&state.code);

	/* The register map and the last-use map share one allocation. */
	if (ir->value_count == 0U) {
		map_entries = 2U;
	} else {
		map_entries = 2U * (size_t)ir->value_count;
	}

	/* The map starts every value without a register. */
	state.value_grf = kern_calloc(map_entries, sizeof(uint32_t));
	if (state.value_grf == NULL) {
		kern_free(binary);
		return ENOMEM;
	}
	state.last_use = state.value_grf + ir->value_count;

	/* The last reader of each value: where its register becomes free again. */
	for (index = 0U; index < ir->instruction_count; index++) {
		/* Notes this instruction as the latest reader of each value it reads. */
		for (source = 0U; source < i915_compile_sources(&ir->instructions[index]); source++) {
			value = ir->instructions[index].src[source];
			if (value < ir->value_count)
				state.last_use[value] = index;
		}
	}

	/* What the shader reads and writes fixes where its payload and its outputs are. */
	i915_compile_interface(&state);
	i915_compile_prologue(&state);

	/* Each IR instruction lowers to a short EU sequence in order. */
	for (index = 0U; index < ir->instruction_count; index++) {
		state.index = index;
		i915_compile_instruction(&state, &ir->instructions[index]);
		i915_compile_release(&state, &ir->instructions[index]);
	}

	/* The shader ends by writing its output and retiring the thread. */
	i915_compile_terminate(&state);

	/* A shader that needs something this compiler cannot lower is refused, never approximated. */
	if (state.unsupported != 0) {
		drv_i915_eu_free(&state.code);
		kern_free(state.value_grf);
		kern_free(binary);
		return ENOTSUP;
	}

	/* An encoder or allocator failure abandons the whole shader. */
	if (state.error != 0 || state.code.error != 0) {
		drv_i915_eu_free(&state.code);
		kern_free(state.value_grf);
		kern_free(binary);
		return EINVAL;
	}

	/* Reads the encoded words; an empty kernel still gets one byte of storage. */
	words = drv_i915_eu_data(&state.code, &bytes);
	if (bytes == 0U) {
		code_size = 1U;
	} else {
		code_size = bytes;
	}

	/* The encoded words are copied into the binary the caller owns. */
	binary->code = kern_calloc(code_size, 1U);
	if (binary->code == NULL) {
		drv_i915_eu_free(&state.code);
		kern_free(state.value_grf);
		kern_free(binary);
		return ENOMEM;
	}
	memcpy(binary->code, words, bytes);
	binary->code_bytes = (uint32_t)bytes;

	/* Records what a draw has to program around the kernel. */
	binary->grf_used = state.grf_high;
	binary->thread_count = 1U;
	binary->push_regs = state.push_regs;
	binary->input_count = state.input_count;
	memcpy(binary->input_locations, state.inputs, sizeof(state.inputs));

	/* A vertex shader passes its varyings on; a fragment shader's varyings are its inputs. */
	if (ir->stage == I915_STAGE_VERTEX) {
		binary->varying_count = state.varying_count;
		binary->dispatch_grf_start = COMPILE_PAYLOAD_GRF;
	} else {
		binary->varying_count = state.input_count;
		binary->dispatch_grf_start = COMPILE_FS_SETUP_GRF;
	}

	/* The encoder buffer and the value maps are only needed while lowering. */
	drv_i915_eu_free(&state.code);
	kern_free(state.value_grf);

	/* Succeeded: the caller can place and run this shader. */
	*out = binary;
	return 0;
}

/*
 * Releases a compiled shader binary and its EU code.
 */
void
drv_i915_shader_binary_free(
	struct i915_shader_binary *binary)
{
	/* Nothing was compiled. */
	if (binary == NULL)
		return;

	/* Releases the code, then the binary. */
	if (binary->code != NULL)
		kern_free(binary->code);
	kern_free(binary);
}

/* Returns how many of an instruction's src[] name values. */
static uint32_t
i915_compile_sources(
	const struct i915_shader_ir_inst *inst)
{
	/* The operation decides how many sources it reads. */
	switch (inst->op) {
	case I915_IR_STORE_OUTPUT:
	case I915_IR_FNEG:
	case I915_IR_RSQ:
	case I915_IR_SIN:
	case I915_IR_COS:
		return 1U;

	case I915_IR_FADD:
	case I915_IR_FSUB:
	case I915_IR_FMUL:
	case I915_IR_SAMPLE:
		return 2U;

	default:
		break;
	}

	/* Constants, loads and unknown operations read no value. */
	return 0U;
}

/* Returns the register a value lives in; reading a value that has no definition is an error. */
static uint32_t
i915_compile_grf(
	struct i915_compile_state *state,
	uint32_t value)
{
	/* A value outside the IR has no register. */
	if (value >= state->ir->value_count) {
		state->error = 1;
		return COMPILE_FIRST_VALUE_GRF;
	}

	/* A value read before its definition has no register either. */
	if (state->value_grf[value] == COMPILE_NO_GRF) {
		state->error = 1;
		return COMPILE_FIRST_VALUE_GRF;
	}

	/* Succeeded: the value lives in this register. */
	return state->value_grf[value];
}

/*
 * Gives `count` consecutive values, starting at `value`, `count` consecutive
 * free registers (a message reply is consecutive registers) and returns the
 * first.  SSA: a value is defined once.
 */
static uint32_t
i915_compile_define(
	struct i915_compile_state *state,
	uint32_t value,
	uint32_t count)
{
	uint32_t grf;
	uint32_t run;

	/* The values must exist in the IR. */
	if (value >= state->ir->value_count || count > state->ir->value_count - value) {
		state->error = 1;
		return COMPILE_FIRST_VALUE_GRF;
	}

	/* A value defined a second time breaks SSA. */
	for (run = 0U; run < count; run++) {
		if (state->value_grf[value + run] != COMPILE_NO_GRF) {
			state->error = 1;
			return COMPILE_FIRST_VALUE_GRF;
		}
	}

	/* Takes the lowest run of `count` free value registers. */
	for (grf = COMPILE_FIRST_VALUE_GRF; grf + count <= COMPILE_LAST_VALUE_GRF + 1U; grf++) {
		/* Measures how many free registers start here. */
		for (run = 0U; run < count && state->grf_busy[grf + run] == 0U; run++)
			;

		/* A run cut short by a busy register is not taken. */
		if (run != count)
			continue;

		/* Marks the run busy and gives each value its register. */
		for (run = 0U; run < count; run++) {
			state->grf_busy[grf + run] = 1U;
			state->value_grf[value + run] = grf + run;
		}

		/* Remembers the highest register the kernel uses. */
		if (grf + count > state->grf_high)
			state->grf_high = grf + count;

		/* Succeeded: the values live from this register on. */
		return grf;
	}

	/* More values live at once than registers: refused, never spilled wrongly. */
	state->unsupported = 1;
	return COMPILE_FIRST_VALUE_GRF;
}

/* Frees the registers of the values whose last reader was this instruction (or that nothing reads). */
static void
i915_compile_release(
	struct i915_compile_state *state,
	const struct i915_shader_ir_inst *inst)
{
	uint32_t sources;
	uint32_t source;
	uint32_t value;
	uint32_t defined;

	/* Frees each source whose last reader this is. */
	sources = i915_compile_sources(inst);
	for (source = 0U; source < sources; source++) {
		value = inst->src[source];
		if (value < state->ir->value_count &&
		    state->last_use[value] == state->index &&
		    state->value_grf[value] != COMPILE_NO_GRF)
			state->grf_busy[state->value_grf[value]] = 0U;
	}

	/* Counts the values the instruction defines: four for a sample, none for a store or a no-op. */
	if (inst->op == I915_IR_SAMPLE) {
		defined = 4U;
	} else if (inst->op == I915_IR_STORE_OUTPUT || inst->op == I915_IR_NOP) {
		defined = 0U;
	} else {
		defined = 1U;
	}

	/* Frees each defined value that nothing reads afterwards. */
	for (value = inst->dst;
	     defined != 0U && value < state->ir->value_count;
	     value++, defined--) {
		/* last_use stays 0 unless instruction 0 reads the value. */
		if (state->last_use[value] <= state->index && state->value_grf[value] != COMPILE_NO_GRF)
			state->grf_busy[state->value_grf[value]] = 0U;
	}
}

/* Lowers one IR instruction to its EU sequence. */
static void
i915_compile_instruction(
	struct i915_compile_state *state,
	const struct i915_shader_ir_inst *inst)
{
	uint32_t payload_inputs;
	uint32_t dst;

	/* The inputs follow the fixed payload registers and the push constants. */
	if (state->ir->stage == I915_STAGE_VERTEX) {
		payload_inputs = COMPILE_PAYLOAD_GRF + state->push_regs;
	} else {
		payload_inputs = COMPILE_FS_SETUP_GRF + state->push_regs;
	}

	/* Lowers by the operation. */
	switch (inst->op) {
	case I915_IR_NOP:
		break;

	case I915_IR_CONST:
		/* A constant enters a value register as an immediate. */
		dst = i915_compile_define(state, inst->dst, 1U);
		drv_i915_eu_mov(&state->code, drv_i915_eu_grf(dst), drv_i915_eu_imm_f(inst->immediate));
		break;

	case I915_IR_LOAD_INPUT:
		i915_compile_load_input(state, inst, payload_inputs);
		break;

	case I915_IR_LOAD_PUSH:
		i915_compile_load_push(state, inst, payload_inputs);
		break;

	case I915_IR_STORE_OUTPUT:
		i915_compile_store_output(state, inst);
		break;

	case I915_IR_FADD:
	case I915_IR_FSUB:
	case I915_IR_FMUL:
		i915_compile_arithmetic(state, inst);
		break;

	case I915_IR_FNEG:
		i915_compile_negate(state, inst);
		break;

	case I915_IR_SIN:
	case I915_IR_COS:
	case I915_IR_RSQ:
		i915_compile_math(state, inst);
		break;

	case I915_IR_SAMPLE:
		i915_compile_sample(state, inst);
		break;

	default:
		/* An IR operation this compiler does not know is never dropped. */
		state->unsupported = 1;
		break;
	}
}

/* Lowers an input load: a payload register, or an interpolation over the input's plane. */
static void
i915_compile_load_input(
	struct i915_compile_state *state,
	const struct i915_shader_ir_inst *inst,
	uint32_t payload_inputs)
{
	struct i915_eu_buf *code;
	uint32_t rank;
	uint32_t plane;
	uint32_t first;
	uint32_t dst;
	int found;

	/* Emits into the shader's encoder buffer. */
	code = &state->code;

	/* A component beyond w is not lowered. */
	if (inst->component > 3U) {
		state->unsupported = 1;
		return;
	}

	/* Finds the input's place in the payload order. */
	found = i915_compile_rank(state->inputs, state->input_count, inst->location, &rank);
	if (found != 0) {
		state->unsupported = 1;
		return;
	}

	/* Gives the loaded value its register. */
	dst = i915_compile_define(state, inst->dst, 1U);

	/* An attribute component of a vertex is a payload register of its own. */
	if (state->ir->stage == I915_STAGE_VERTEX) {
		drv_i915_eu_mov(code, drv_i915_eu_grf(dst), drv_i915_eu_grf(payload_inputs + 4U * rank + inst->component));
		return;
	}

	/* Locates the component's plane (see the conventions). */
	plane = COMPILE_FS_SETUP_GRF + state->push_regs + 2U * rank + inst->component / 2U;
	first = (inst->component & 1U) * 16U;

	/* origin + d1 * bary1 + d2 * bary2, written out: Gen11+ has no PLN. */
	drv_i915_eu_alu2(code, I915_EU_MUL, drv_i915_eu_grf(dst),
		drv_i915_eu_grf_scalar(plane, first + 4U),
		drv_i915_eu_grf(COMPILE_FS_BARY2_GRF));
	drv_i915_eu_alu2(code, I915_EU_ADD, drv_i915_eu_grf(dst),
		drv_i915_eu_grf(dst),
		drv_i915_eu_grf_scalar(plane, first + 12U));
	drv_i915_eu_alu2(code, I915_EU_MUL, drv_i915_eu_grf(COMPILE_SCRATCH_GRF),
		drv_i915_eu_grf_scalar(plane, first),
		drv_i915_eu_grf(COMPILE_FS_BARY1_GRF));
	drv_i915_eu_alu2(code, I915_EU_ADD, drv_i915_eu_grf(dst),
		drv_i915_eu_grf(dst),
		drv_i915_eu_grf(COMPILE_SCRATCH_GRF));
}

/* Lowers a push-constant load: one float of the push registers, read as a scalar. */
static void
i915_compile_load_push(
	struct i915_compile_state *state,
	const struct i915_shader_ir_inst *inst,
	uint32_t payload_inputs)
{
	uint32_t push_grf;
	uint32_t dst;

	/* Not a float inside the declared push-constant bytes. */
	if ((inst->immediate & 3U) != 0U || inst->immediate + 4U > state->ir->push_bytes) {
		state->error = 1;
		return;
	}

	/* Gives the loaded value its register. */
	dst = i915_compile_define(state, inst->dst, 1U);

	/* The push constants sit just below the inputs, 32 bytes to a register. */
	push_grf = payload_inputs - state->push_regs + inst->immediate / 32U;
	drv_i915_eu_mov(&state->code, drv_i915_eu_grf(dst), drv_i915_eu_grf_scalar(push_grf, inst->immediate % 32U));
}

/* Lowers an output store: the component leaves through its staging register. */
static void
i915_compile_store_output(
	struct i915_compile_state *state,
	const struct i915_shader_ir_inst *inst)
{
	uint32_t source_grf;
	uint32_t rank;
	uint32_t grf;
	int found;

	/* A component beyond w is not lowered. */
	if (inst->component > 3U) {
		state->unsupported = 1;
		return;
	}

	/* Finds the staging registers of the output. */
	if (state->ir->stage == I915_STAGE_VERTEX && inst->location == I915_IR_LOCATION_POSITION) {
		/* The position follows the VUE header, wherever the header is staged. */
		if (state->varying_count != 0U) {
			grf = COMPILE_VUE_GRF + 4U;
		} else {
			grf = COMPILE_MAX_GRF - 8U + 4U;
		}
	} else if (state->ir->stage == I915_STAGE_VERTEX) {
		/* A varying sits below the handles in r127, in ascending location order. */
		found = i915_compile_rank(state->varyings, state->varying_count, inst->location, &rank);
		if (found != 0) {
			state->unsupported = 1;
			return;
		}
		grf = COMPILE_MAX_GRF - 4U * state->varying_count + 4U * rank;
	} else if (inst->location == 0U) {
		/* The colour goes to r124..r127. */
		grf = COMPILE_MAX_GRF - 3U;
	} else {
		/* XXX: one colour output; Position belongs to a vertex shader. */
		state->unsupported = 1;
		return;
	}

	/* Moves the value into the component's staging register. */
	source_grf = i915_compile_grf(state, inst->src[0]);
	drv_i915_eu_mov(&state->code, drv_i915_eu_grf(grf + inst->component), drv_i915_eu_grf(source_grf));
}

/* Lowers an add, subtract or multiply: sources are read before the destination gets its register. */
static void
i915_compile_arithmetic(
	struct i915_compile_state *state,
	const struct i915_shader_ir_inst *inst)
{
	struct i915_eu_reg left;
	struct i915_eu_reg right;
	enum i915_eu_alu op;
	uint32_t left_grf;
	uint32_t right_grf;
	uint32_t dst;

	/* Reads both sources. */
	left_grf = i915_compile_grf(state, inst->src[0]);
	left = drv_i915_eu_grf(left_grf);
	right_grf = i915_compile_grf(state, inst->src[1]);
	right = drv_i915_eu_grf(right_grf);

	/* a - b = a + (-b): ADD with the SECOND source negated (operand order matters). */
	if (inst->op == I915_IR_FSUB)
		right = drv_i915_eu_negate(right);

	/* Gives the result its register. */
	dst = i915_compile_define(state, inst->dst, 1U);

	/* A multiply is MUL; an add or a subtract is ADD. */
	if (inst->op == I915_IR_FMUL) {
		op = I915_EU_MUL;
	} else {
		op = I915_EU_ADD;
	}

	/* Emits the arithmetic. */
	drv_i915_eu_alu2(&state->code, op, drv_i915_eu_grf(dst), left, right);
}

/* Lowers a negation: a MOV whose source carries the negate modifier. */
static void
i915_compile_negate(
	struct i915_compile_state *state,
	const struct i915_shader_ir_inst *inst)
{
	struct i915_eu_reg source;
	uint32_t source_grf;
	uint32_t dst;

	/* Reads the source negated. */
	source_grf = i915_compile_grf(state, inst->src[0]);
	source = drv_i915_eu_grf(source_grf);
	source = drv_i915_eu_negate(source);

	/* Gives the result its register and moves the negated source into it. */
	dst = i915_compile_define(state, inst->dst, 1U);
	drv_i915_eu_mov(&state->code, drv_i915_eu_grf(dst), source);
}

/* Lowers a sine, cosine or inverse square root to one math instruction. */
static void
i915_compile_math(
	struct i915_compile_state *state,
	const struct i915_shader_ir_inst *inst)
{
	struct i915_eu_reg source;
	enum i915_eu_math func;
	uint32_t source_grf;
	uint32_t dst;

	/* Reads the source. */
	source_grf = i915_compile_grf(state, inst->src[0]);
	source = drv_i915_eu_grf(source_grf);

	/* Gives the result its register. */
	dst = i915_compile_define(state, inst->dst, 1U);

	/* Chooses the math function of the IR operation. */
	if (inst->op == I915_IR_SIN) {
		func = I915_EU_MATH_SIN;
	} else if (inst->op == I915_IR_COS) {
		func = I915_EU_MATH_COS;
	} else {
		func = I915_EU_MATH_RSQ;
	}

	/* Emits the math; its second operand is the null register. */
	drv_i915_eu_math(&state->code, func, drv_i915_eu_grf(dst), source, drv_i915_eu_null());
}

/*
 * Lowers texture(): u and v are the two payload runs, the reply is four
 * registers (see the conventions).
 */
static void
i915_compile_sample(
	struct i915_compile_state *state,
	const struct i915_shader_ir_inst *inst)
{
	struct i915_eu_reg u;
	struct i915_eu_reg v;
	uint32_t u_grf;
	uint32_t v_grf;
	uint32_t sampler;
	uint32_t dst;

	/* Reads the coordinate. */
	u_grf = i915_compile_grf(state, inst->src[0]);
	u = drv_i915_eu_grf(u_grf);
	v_grf = i915_compile_grf(state, inst->src[1]);
	v = drv_i915_eu_grf(v_grf);

	/* Finds the sampled image the instruction names by set and binding. */
	for (sampler = 0U; sampler < state->ir->uniform_count; sampler++) {
		if (state->ir->uniforms[sampler].set == inst->location &&
		    state->ir->uniforms[sampler].binding == inst->immediate)
			break;
	}

	/* An unknown image, or one past what the descriptor can name, is not lowered. */
	if (sampler >= state->ir->uniform_count || sampler > COMPILE_MAX_SAMPLER) {
		state->unsupported = 1;
		return;
	}

	/* Gives the four reply values four consecutive registers. */
	dst = i915_compile_define(state, inst->dst, 4U);

	/* Emits the sample: binding table entry 1 + n, sampler n. */
	drv_i915_eu_send(&state->code,
			 drv_i915_eu_grf(dst),
			 u,
			 v,
			 COMPILE_SFID_SAMPLER,
			 COMPILE_DESC_SAMPLE(1U + sampler, sampler),
			 COMPILE_EX_MLEN(1U),
			 0,
			 0);
}

/* Finds the position of `location` in an ascending list. */
static int
i915_compile_rank(
	const uint32_t *list,
	uint32_t count,
	uint32_t location,
	uint32_t *rank)
{
	uint32_t index;

	/* Looks the location up. */
	for (index = 0U; index < count; index++) {
		if (list[index] == location) {
			/* Succeeded: the location is the index-th in the list. */
			*rank = index;
			return 0;
		}
	}

	/* The location is not in the list. */
	return ENOENT;
}

/* Adds `location` to an ascending list without duplicates; more than `limit` entries is refused. */
static void
i915_compile_note(
	struct i915_compile_state *state,
	uint32_t *list,
	uint32_t *count,
	uint32_t limit,
	uint32_t location)
{
	uint32_t index;
	uint32_t at;

	/* Finds where the location belongs in ascending order. */
	for (at = 0U; at < *count && list[at] < location; at++)
		;

	/* A location already listed is not listed twice. */
	if (at < *count && list[at] == location)
		return;

	/* A list that is full refuses the shader. */
	if (*count >= limit) {
		state->unsupported = 1;
		return;
	}

	/* Shifts the larger locations up to open the place. */
	for (index = *count; index > at; index--)
		list[index] = list[index - 1U];

	/* Inserts the location. */
	list[at] = location;
	(*count)++;
}

/* Reads the interface off the instructions: the locations read, the locations written. */
static void
i915_compile_interface(
	struct i915_compile_state *state)
{
	const struct i915_shader_ir_inst *inst;
	uint32_t payload_end;
	uint32_t index;

	/* The push constants take whole registers of 32 bytes. */
	state->push_regs = (state->ir->push_bytes + 31U) / 32U;

	/* Lists every input read and, for a vertex shader, every varying written. */
	for (index = 0U; index < state->ir->instruction_count; index++) {
		inst = &state->ir->instructions[index];
		if (inst->op == I915_IR_LOAD_INPUT) {
			i915_compile_note(state, state->inputs, &state->input_count, COMPILE_MAX_INPUTS, inst->location);
		} else if (inst->op == I915_IR_STORE_OUTPUT &&
		    state->ir->stage == I915_STAGE_VERTEX &&
		    inst->location != I915_IR_LOCATION_POSITION) {
			i915_compile_note(state, state->varyings, &state->varying_count, COMPILE_MAX_VARYINGS, inst->location);
		}
	}

	/* Finds where the payload ends: four registers to an attribute, two to an interpolated input. */
	if (state->ir->stage == I915_STAGE_VERTEX) {
		payload_end = COMPILE_PAYLOAD_GRF + state->push_regs + 4U * state->input_count;
	} else {
		payload_end = COMPILE_FS_SETUP_GRF + state->push_regs + 2U * state->input_count;
	}

	/* The payload has to end below the scratch register. */
	if (payload_end > COMPILE_SCRATCH_GRF)
		state->unsupported = 1;
}

/* Fills what the outputs hold where the shader stores nothing: zeros, and the VUE header. */
static void
i915_compile_prologue(
	struct i915_compile_state *state)
{
	struct i915_eu_reg header;
	uint32_t first;
	uint32_t grf;

	/* A fragment shader's colour starts as zeros. */
	if (state->ir->stage != I915_STAGE_VERTEX) {
		for (grf = COMPILE_MAX_GRF - 3U; grf <= COMPILE_MAX_GRF; grf++)
			drv_i915_eu_mov(&state->code, drv_i915_eu_grf(grf), drv_i915_eu_imm_f(0U));
		return;
	}

	/* The VUE is staged at r100, or right below the handles when there is no varying. */
	if (state->varying_count != 0U) {
		first = COMPILE_VUE_GRF;
	} else {
		first = COMPILE_MAX_GRF - 8U;
	}

	/* The header (point size, layer, viewport index) is integer zeros. */
	for (grf = first; grf < first + 4U; grf++) {
		header = drv_i915_eu_grf_ud(grf);
		header.type = COMPILE_TYPE_D;
		drv_i915_eu_mov(&state->code, header, drv_i915_eu_imm_d(0U));
	}

	/* The position starts as zeros. */
	for (grf = first + 4U; grf < first + 8U; grf++)
		drv_i915_eu_mov(&state->code, drv_i915_eu_grf(grf), drv_i915_eu_imm_f(0U));

	/* The varyings start as zeros. */
	for (grf = COMPILE_MAX_GRF - 4U * state->varying_count; grf < COMPILE_MAX_GRF; grf++)
		drv_i915_eu_mov(&state->code, drv_i915_eu_grf(grf), drv_i915_eu_imm_f(0U));
}

/* Emits the shader's terminating output message. */
static void
i915_compile_terminate(
	struct i915_compile_state *state)
{
	struct i915_eu_buf *code;
	uint32_t varying_grf;

	/* Emits into the shader's encoder buffer. */
	code = &state->code;

	/* The colour in r124..r127 to the render target: SENDC, as a render-target write must be. */
	if (state->ir->stage != I915_STAGE_VERTEX) {
		drv_i915_eu_send(code,
				 drv_i915_eu_null(),
				 drv_i915_eu_grf(COMPILE_MAX_GRF - 3U),
				 drv_i915_eu_null(),
				 COMPILE_SFID_RENDER_CACHE,
				 COMPILE_DESC_RT_WRITE,
				 0U,
				 1,
				 1);
		return;
	}

	/* With no varying, the header and position are the one write that ends the thread. */
	if (state->varying_count == 0U) {
		drv_i915_eu_mov(code, drv_i915_eu_grf_ud(COMPILE_MAX_GRF), drv_i915_eu_grf_ud(1U));
		drv_i915_eu_send(code,
				 drv_i915_eu_null(),
				 drv_i915_eu_grf(COMPILE_MAX_GRF),
				 drv_i915_eu_grf(COMPILE_MAX_GRF - 8U),
				 COMPILE_SFID_URB,
				 COMPILE_DESC_URB_WRITE(0U),
				 COMPILE_EX_MLEN(8U),
				 0,
				 1);
		return;
	}

	/* Writes the header and the position, slots 0 and 1, with the handles from r1. */
	drv_i915_eu_send(code,
			 drv_i915_eu_null(),
			 drv_i915_eu_grf(1U),
			 drv_i915_eu_grf(COMPILE_VUE_GRF),
			 COMPILE_SFID_URB,
			 COMPILE_DESC_URB_WRITE(0U),
			 COMPILE_EX_MLEN(8U),
			 0,
			 0);

	/* Writes the varyings from slot 2 and ends the thread: handles and payload in r112..r127. */
	varying_grf = COMPILE_MAX_GRF - 4U * state->varying_count;
	drv_i915_eu_mov(code, drv_i915_eu_grf_ud(COMPILE_MAX_GRF), drv_i915_eu_grf_ud(1U));
	drv_i915_eu_send(code,
			 drv_i915_eu_null(),
			 drv_i915_eu_grf(COMPILE_MAX_GRF),
			 drv_i915_eu_grf(varying_grf),
			 COMPILE_SFID_URB,
			 COMPILE_DESC_URB_WRITE(2U),
			 COMPILE_EX_MLEN(4U * state->varying_count),
			 0,
			 1);
}
