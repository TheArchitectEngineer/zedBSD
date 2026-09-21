/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Host fixture for the LOWERING stage of the shader compiler (SPIR-V -> scalar IR).
 *
 * The IR is executed by a small interpreter and the outputs are compared with values
 * computed by an independent formula -- never with what the parser says it did.  The
 * inputs are chosen so that a wrong substitute cannot match by accident: distinct
 * powers of two per component, operands whose order matters (5 - 2 against 2 - 5),
 * a local that is overwritten between two loads, dot((1,2,3,4),(5,6,7,8)) = 70.
 *
 * "Lowered" here means exactly this stage.  EU generation is checked in the compile
 * fixture; nothing in this file is evidence about GPU execution.
 */

#include <assert.h>
#include <math.h>
#include <stdarg.h>
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

#include "../../../src/drivers/gpu/i915/compiler/spirv.c"

/* ------------------------------------------------------------------ the IR interpreter */

#define SLOTS 8U
#define SLOT_POSITION (SLOTS - 1U)

struct machine {
	float input[SLOTS][4];
	uint8_t push[64];
	float output[SLOTS][4];
	unsigned written[SLOTS][4];     /* how many times each output component was stored */
};

/* the "texture": every component depends on u, v and the binding in a different way */
static void
fake_texture(uint32_t set, uint32_t binding, float u, float v, float rgba[4])
{
	rgba[0] = u + 10.0f * (float)binding;
	rgba[1] = v + 100.0f * (float)set;
	rgba[2] = u + 2.0f * v;
	rgba[3] = u * v + 0.5f;
}

static float
bits_to_float(uint32_t bits)
{
	float value;

	memcpy(&value, &bits, sizeof(value));
	return value;
}

static uint32_t
float_to_bits(float value)
{
	uint32_t bits;

	memcpy(&bits, &value, sizeof(bits));
	return bits;
}

/* Runs the IR; asserts the SSA discipline (defined once, defined before read). */
static void
run_ir(const struct i915_shader_ir *ir, struct machine *m)
{
	float *value = calloc(ir->value_count + 4U, sizeof(*value));
	uint8_t *defined = calloc(ir->value_count + 4U, 1U);
	unsigned index, k;

	assert(value != NULL && defined != NULL);
	memset(m->output, 0, sizeof(m->output));
	memset(m->written, 0, sizeof(m->written));
	for (index = 0U; index < ir->instruction_count; index++) {
		const struct i915_shader_ir_inst *inst = &ir->instructions[index];
		unsigned sources = 0U, results = 1U, slot;
		float a, b, rgba[4];

		switch (inst->op) {
		case I915_IR_STORE_OUTPUT: sources = 1U; results = 0U; break;
		case I915_IR_FNEG: case I915_IR_SIN: case I915_IR_COS: case I915_IR_RSQ: sources = 1U; break;
		case I915_IR_FADD: case I915_IR_FSUB: case I915_IR_FMUL: sources = 2U; break;
		case I915_IR_SAMPLE: sources = 2U; results = 4U; break;
		case I915_IR_CONST: case I915_IR_LOAD_INPUT: case I915_IR_LOAD_PUSH: break;
		default: assert(!"IR operation the interpreter does not know"); break;
		}
		for (k = 0U; k < sources; k++)
			assert(inst->src[k] < ir->value_count && defined[inst->src[k]] != 0U);
		for (k = 0U; k < results; k++) {
			assert(inst->dst + k < ir->value_count && defined[inst->dst + k] == 0U);
			defined[inst->dst + k] = 1U;
		}
		a = sources >= 1U ? value[inst->src[0]] : 0.0f;
		b = sources >= 2U ? value[inst->src[1]] : 0.0f;

		switch (inst->op) {
		case I915_IR_CONST: value[inst->dst] = bits_to_float(inst->immediate); break;
		case I915_IR_LOAD_INPUT:
			assert(inst->location < SLOTS && inst->component < 4U);
			value[inst->dst] = m->input[inst->location][inst->component];
			break;
		case I915_IR_LOAD_PUSH:
			assert(inst->immediate + 4U <= sizeof(m->push) && inst->immediate + 4U <= ir->push_bytes);
			memcpy(&value[inst->dst], m->push + inst->immediate, 4U);
			break;
		case I915_IR_STORE_OUTPUT:
			slot = inst->location == I915_IR_LOCATION_POSITION ? SLOT_POSITION : inst->location;
			assert(slot < SLOTS && inst->component < 4U);
			assert(inst->location == I915_IR_LOCATION_POSITION || inst->location < SLOT_POSITION);
			m->output[slot][inst->component] = a;
			m->written[slot][inst->component]++;
			break;
		case I915_IR_FADD: value[inst->dst] = a + b; break;
		case I915_IR_FSUB: value[inst->dst] = a - b; break;
		case I915_IR_FMUL: value[inst->dst] = a * b; break;
		case I915_IR_FNEG: value[inst->dst] = -a; break;
		case I915_IR_SIN: value[inst->dst] = sinf(a); break;
		case I915_IR_COS: value[inst->dst] = cosf(a); break;
		case I915_IR_RSQ: value[inst->dst] = 1.0f / sqrtf(a); break;
		case I915_IR_SAMPLE:
			fake_texture(inst->location, inst->immediate, a, b, rgba);
			for (k = 0U; k < 4U; k++)
				value[inst->dst + k] = rgba[k];
			break;
		default: break;
		}
	}
	free(value);
	free(defined);
}

/* ------------------------------------------------------------------ a tiny SPIR-V assembler */

static uint32_t mod[2048];
static unsigned mod_n;

static void
op(unsigned opcode, unsigned operands, ...)
{
	va_list list;
	unsigned k;

	assert(mod_n + 1U + operands <= sizeof(mod) / sizeof(mod[0]));
	mod[mod_n++] = ((operands + 1U) << 16) | opcode;
	va_start(list, operands);
	for (k = 0U; k < operands; k++)
		mod[mod_n++] = va_arg(list, uint32_t);
	va_end(list);
}

/* fixed ids of the common preamble */
enum {
	T_VOID = 1, T_FN, T_FLOAT, T_VEC2, T_VEC3, T_VEC4, T_PTR_FN_FLOAT, T_PTR_FN_VEC4, T_PTR_IN_VEC4,
	T_PTR_OUT_VEC4, T_INT, V_IN0, V_IN1, V_OUT0, C_I0, C_I1, C_I2, C_I3, C_F5, C_F2, C_F16, T_PTR_IN_FLOAT,
	T_PTR_OUT_FLOAT, F_MAIN, L_ENTRY, V_OUT1, T_PTR_FN_VEC3, V_DYN,
	B = 40                          /* first body id */
};

#define U(x) ((uint32_t)(x))

/* A fragment module: in0, in1 : vec4 (locations 0, 1), out0, out1 : vec4 (locations 0, 1). */
static void
begin_module(void)
{
	mod_n = 0U;
	mod[mod_n++] = 0x07230203U; mod[mod_n++] = 0x00010000U; mod[mod_n++] = 0U; mod[mod_n++] = 256U; mod[mod_n++] = 0U;
	op(17U, 1U, U(1));                                                      /* OpCapability Shader */
	op(14U, 2U, U(0), U(1));                                                /* OpMemoryModel Logical GLSL450 */
	op(15U, 7U, U(4), U(F_MAIN), U(0x6E69616D), U(0), U(V_IN0), U(V_IN1), U(V_OUT0));  /* OpEntryPoint Fragment "main" */
	op(16U, 2U, U(F_MAIN), U(7));                                           /* OpExecutionMode OriginUpperLeft */
	op(71U, 3U, U(V_IN0), U(30), U(0));                                     /* OpDecorate Location */
	op(71U, 3U, U(V_IN1), U(30), U(1));
	op(71U, 3U, U(V_OUT0), U(30), U(0));
	op(71U, 3U, U(V_OUT1), U(30), U(1));
	op(19U, 1U, U(T_VOID));
	op(33U, 2U, U(T_FN), U(T_VOID));
	op(22U, 2U, U(T_FLOAT), U(32));
	op(23U, 3U, U(T_VEC2), U(T_FLOAT), U(2));
	op(23U, 3U, U(T_VEC3), U(T_FLOAT), U(3));
	op(23U, 3U, U(T_VEC4), U(T_FLOAT), U(4));
	op(32U, 3U, U(T_PTR_FN_FLOAT), U(7), U(T_FLOAT));
	op(32U, 3U, U(T_PTR_FN_VEC4), U(7), U(T_VEC4));
	op(32U, 3U, U(T_PTR_FN_VEC3), U(7), U(T_VEC3));
	op(32U, 3U, U(T_PTR_IN_VEC4), U(1), U(T_VEC4));
	op(32U, 3U, U(T_PTR_IN_FLOAT), U(1), U(T_FLOAT));
	op(32U, 3U, U(T_PTR_OUT_VEC4), U(3), U(T_VEC4));
	op(32U, 3U, U(T_PTR_OUT_FLOAT), U(3), U(T_FLOAT));
	op(21U, 3U, U(T_INT), U(32), U(1));
	op(43U, 3U, U(T_INT), U(C_I0), U(0));
	op(43U, 3U, U(T_INT), U(C_I1), U(1));
	op(43U, 3U, U(T_INT), U(C_I2), U(2));
	op(43U, 3U, U(T_INT), U(C_I3), U(3));
	op(43U, 3U, U(T_FLOAT), U(C_F5), float_to_bits(5.0f));
	op(43U, 3U, U(T_FLOAT), U(C_F2), float_to_bits(2.0f));
	op(43U, 3U, U(T_FLOAT), U(C_F16), float_to_bits(16.0f));
	op(59U, 3U, U(T_PTR_IN_VEC4), U(V_IN0), U(1));
	op(59U, 3U, U(T_PTR_IN_VEC4), U(V_IN1), U(1));
	op(59U, 3U, U(T_PTR_OUT_VEC4), U(V_OUT0), U(3));
	op(59U, 3U, U(T_PTR_OUT_VEC4), U(V_OUT1), U(3));
	op(54U, 4U, U(T_VOID), U(F_MAIN), U(0), U(T_FN));
	op(248U, 1U, U(L_ENTRY));
}

static int
end_module(struct i915_shader_ir **ir, struct i915_compile_diagnostic *diag)
{
	op(253U, 0U);
	op(56U, 0U);
	return drv_i915_shader_parse(mod, mod_n, I915_STAGE_FRAGMENT, ir, diag);
}

static void
set_inputs(struct machine *m)
{
	static const float in0[4] = { 1.0f, 2.0f, 4.0f, 8.0f };
	static const float in1[4] = { 5.0f, 6.0f, 7.0f, 8.0f };

	memset(m, 0, sizeof(*m));
	memcpy(m->input[0], in0, sizeof(in0));
	memcpy(m->input[1], in1, sizeof(in1));
}

static void
expect_out(const struct machine *m, unsigned slot, float x, float y, float z, float w)
{
	const float want[4] = { x, y, z, w };
	unsigned k;

	for (k = 0U; k < 4U; k++) {
		if (m->written[slot][k] != 1U || m->output[slot][k] != want[k]) {
			printf("  output %u.%u = %g (stored %u times), want %g\n", slot, k, (double)m->output[slot][k],
				m->written[slot][k], (double)want[k]);
			assert(!"output mismatch");
		}
	}
}

/* ------------------------------------------------------------------ the unit tests */

/* Function-storage local: store 5, load, overwrite with 2, load again; the two loads differ. */
static void
test_local_store_load_overwrite(void)
{
	struct i915_shader_ir *ir;
	struct i915_compile_diagnostic diag;
	struct machine m;

	begin_module();
	op(59U, 3U, U(T_PTR_FN_FLOAT), U(B), U(7));                             /* %B = OpVariable Function */
	op(62U, 2U, U(B), U(C_F5));                                             /* store 5 */
	op(61U, 3U, U(T_FLOAT), U(B + 1), U(B));                                /* first  = load  -> 5 */
	op(62U, 2U, U(B), U(C_F2));                                             /* store 2 (overwrite) */
	op(61U, 3U, U(T_FLOAT), U(B + 2), U(B));                                /* second = load  -> 2 */
	op(131U, 4U, U(T_FLOAT), U(B + 3), U(B + 1), U(B + 2));                 /* 5 - 2 =  3 */
	op(131U, 4U, U(T_FLOAT), U(B + 4), U(B + 2), U(B + 1));                 /* 2 - 5 = -3 */
	op(80U, 6U, U(T_VEC4), U(B + 5), U(B + 1), U(B + 2), U(B + 3), U(B + 4));
	op(62U, 2U, U(V_OUT0), U(B + 5));
	assert(end_module(&ir, &diag) == 0);
	set_inputs(&m);
	run_ir(ir, &m);
	expect_out(&m, 0U, 5.0f, 2.0f, 3.0f, -3.0f);
	drv_i915_shader_ir_free(ir);
	printf("  local: store 5 / load / store 2 / load -> (5, 2), 5-2 = 3, 2-5 = -3\n");
}

/* (1,2,4,8): every component extracted on its own, none dropped or duplicated; construct and shuffle. */
static void
test_vector_construct_extract_shuffle(void)
{
	struct i915_shader_ir *ir;
	struct i915_compile_diagnostic diag;
	struct machine m;

	begin_module();
	op(61U, 3U, U(T_VEC4), U(B), U(V_IN0));                                 /* v = (1,2,4,8) */
	op(81U, 4U, U(T_FLOAT), U(B + 1), U(B), U(0));
	op(81U, 4U, U(T_FLOAT), U(B + 2), U(B), U(1));
	op(81U, 4U, U(T_FLOAT), U(B + 3), U(B), U(2));
	op(81U, 4U, U(T_FLOAT), U(B + 4), U(B), U(3));
	op(80U, 6U, U(T_VEC4), U(B + 5), U(B + 4), U(B + 3), U(B + 2), U(B + 1)); /* reversed: (8,4,2,1) */
	op(62U, 2U, U(V_OUT0), U(B + 5));
	/* shuffle v with in1 = (5,6,7,8): components 1, 6, 3, 4 -> (2, 7, 8, 5) */
	op(61U, 3U, U(T_VEC4), U(B + 6), U(V_IN1));
	op(79U, 8U, U(T_VEC4), U(B + 7), U(B), U(B + 6), U(1), U(6), U(3), U(4));
	/* vec2 from the shuffle (components 2, 0 -> (8, 2)), then vec4(vec2, scalar, scalar) = (8, 2, 16, 4) */
	op(79U, 6U, U(T_VEC2), U(B + 8), U(B + 7), U(B + 7), U(2), U(0));
	op(80U, 5U, U(T_VEC4), U(B + 9), U(B + 8), U(C_F16), U(B + 3));
	op(129U, 4U, U(T_VEC4), U(B + 10), U(B + 7), U(B + 9));                 /* (2,7,8,5) + (8,2,16,4) = (10,9,24,9) */
	op(62U, 2U, U(V_OUT1), U(B + 10));
	assert(end_module(&ir, &diag) == 0);
	set_inputs(&m);
	run_ir(ir, &m);
	expect_out(&m, 0U, 8.0f, 4.0f, 2.0f, 1.0f);
	expect_out(&m, 1U, 10.0f, 9.0f, 24.0f, 9.0f);
	drv_i915_shader_ir_free(ir);
	printf("  vector: (1,2,4,8) extracted, reversed, shuffled and rebuilt without losing a component\n");
}

/* Component access chains on a local, an input and an output. */
static void
test_component_access(void)
{
	struct i915_shader_ir *ir;
	struct i915_compile_diagnostic diag;
	struct machine m;

	begin_module();
	op(59U, 3U, U(T_PTR_FN_VEC4), U(B), U(7));                              /* local vec4 */
	op(61U, 3U, U(T_VEC4), U(B + 1), U(V_IN0));
	op(62U, 2U, U(B), U(B + 1));                                            /* local = (1,2,4,8) */
	op(65U, 4U, U(T_PTR_FN_FLOAT), U(B + 2), U(B), U(C_I2));                /* &local.z */
	op(62U, 2U, U(B + 2), U(C_F16));                                        /* local.z = 16 */
	op(61U, 3U, U(T_VEC4), U(B + 3), U(B));                                 /* (1,2,16,8) */
	op(62U, 2U, U(V_OUT0), U(B + 3));
	op(65U, 4U, U(T_PTR_FN_FLOAT), U(B + 4), U(B), U(C_I1));                /* &local.y */
	op(61U, 3U, U(T_FLOAT), U(B + 5), U(B + 4));                            /* 2 */
	op(65U, 4U, U(T_PTR_IN_FLOAT), U(B + 6), U(V_IN1), U(C_I3));            /* &in1.w */
	op(61U, 3U, U(T_FLOAT), U(B + 7), U(B + 6));                            /* 8 */
	op(65U, 4U, U(T_PTR_IN_FLOAT), U(B + 8), U(V_IN1), U(C_I0));            /* &in1.x */
	op(61U, 3U, U(T_FLOAT), U(B + 9), U(B + 8));                            /* 5 */
	op(133U, 4U, U(T_FLOAT), U(B + 10), U(B + 5), U(B + 7));                /* 2 * 8 = 16 */
	/* the output written one component at a time, out of order */
	op(65U, 4U, U(T_PTR_OUT_FLOAT), U(B + 11), U(V_OUT1), U(C_I3));
	op(62U, 2U, U(B + 11), U(B + 5));                                       /* out1.w = 2 */
	op(65U, 4U, U(T_PTR_OUT_FLOAT), U(B + 12), U(V_OUT1), U(C_I0));
	op(62U, 2U, U(B + 12), U(B + 7));                                       /* out1.x = 8 */
	op(65U, 4U, U(T_PTR_OUT_FLOAT), U(B + 13), U(V_OUT1), U(C_I1));
	op(62U, 2U, U(B + 13), U(B + 9));                                       /* out1.y = 5 */
	op(65U, 4U, U(T_PTR_OUT_FLOAT), U(B + 14), U(V_OUT1), U(C_I2));
	op(62U, 2U, U(B + 14), U(B + 10));                                      /* out1.z = 16 */
	assert(end_module(&ir, &diag) == 0);
	set_inputs(&m);
	run_ir(ir, &m);
	expect_out(&m, 0U, 1.0f, 2.0f, 16.0f, 8.0f);
	expect_out(&m, 1U, 8.0f, 5.0f, 16.0f, 2.0f);
	drv_i915_shader_ir_free(ir);
	printf("  access chain: one component of a local overwritten, inputs / outputs addressed by component\n");
}

/* dot((1,2,3,4),(5,6,7,8)) = 70; negate; vector * scalar; float constants as operands. */
static void
test_dot_negate_scale(void)
{
	struct i915_shader_ir *ir;
	struct i915_compile_diagnostic diag;
	struct machine m;
	static const float a[4] = { 1.0f, 2.0f, 3.0f, 4.0f };

	begin_module();
	op(61U, 3U, U(T_VEC4), U(B), U(V_IN0));
	op(61U, 3U, U(T_VEC4), U(B + 1), U(V_IN1));
	op(148U, 4U, U(T_FLOAT), U(B + 2), U(B), U(B + 1));                     /* 70 */
	op(127U, 3U, U(T_FLOAT), U(B + 3), U(B + 2));                           /* -70 */
	op(131U, 4U, U(T_FLOAT), U(B + 4), U(B + 2), U(C_F5));                  /* 70 - 5 = 65 */
	op(131U, 4U, U(T_FLOAT), U(B + 5), U(C_F5), U(B + 2));                  /* 5 - 70 = -65 */
	op(80U, 6U, U(T_VEC4), U(B + 6), U(B + 2), U(B + 3), U(B + 4), U(B + 5));
	op(62U, 2U, U(V_OUT0), U(B + 6));
	op(142U, 4U, U(T_VEC4), U(B + 7), U(B), U(C_F2));                       /* (1,2,3,4) * 2 */
	op(127U, 3U, U(T_VEC4), U(B + 8), U(B + 7));                            /* -(2,4,6,8) */
	op(133U, 4U, U(T_VEC4), U(B + 9), U(B + 8), U(B + 1));                  /* * (5,6,7,8) = (-10,-24,-42,-64) */
	op(62U, 2U, U(V_OUT1), U(B + 9));
	assert(end_module(&ir, &diag) == 0);
	set_inputs(&m);
	memcpy(m.input[0], a, sizeof(a));
	run_ir(ir, &m);
	expect_out(&m, 0U, 70.0f, -70.0f, 65.0f, -65.0f);
	expect_out(&m, 1U, -10.0f, -24.0f, -42.0f, -64.0f);
	drv_i915_shader_ir_free(ir);
	printf("  arithmetic: dot = 70, negate = -70, 70-5 = 65, 5-70 = -65, vector * scalar per component\n");
}

/* What stays refused -- each with the instruction named, none skipped. */
static void
test_refusals(void)
{
	struct i915_shader_ir *ir;
	struct i915_compile_diagnostic diag;

	/* a load of a local (component) that was never stored */
	begin_module();
	op(59U, 3U, U(T_PTR_FN_FLOAT), U(B), U(7));
	op(61U, 3U, U(T_FLOAT), U(B + 1), U(B));
	assert(end_module(&ir, &diag) == ENOTSUP && ir == NULL && diag.opcode == 61U);

	/* only ONE component stored, then the whole vector loaded */
	begin_module();
	op(59U, 3U, U(T_PTR_FN_VEC4), U(B), U(7));
	op(65U, 4U, U(T_PTR_FN_FLOAT), U(B + 1), U(B), U(C_I1));
	op(62U, 2U, U(B + 1), U(C_F5));
	op(61U, 3U, U(T_VEC4), U(B + 2), U(B));
	assert(end_module(&ir, &diag) == ENOTSUP && ir == NULL && diag.opcode == 61U);

	/* a dynamic access-chain index (a loaded value, not a constant) */
	begin_module();
	op(59U, 3U, U(T_PTR_FN_VEC4), U(B), U(7));
	op(61U, 3U, U(T_VEC4), U(B + 1), U(V_IN0));
	op(65U, 4U, U(T_PTR_FN_FLOAT), U(B + 2), U(B), U(B + 1));
	assert(end_module(&ir, &diag) == ENOTSUP && ir == NULL && diag.opcode == 65U);

	/* a local with an initializer */
	begin_module();
	op(59U, 4U, U(T_PTR_FN_FLOAT), U(B), U(7), U(C_F5));
	assert(end_module(&ir, &diag) == ENOTSUP && ir == NULL && diag.opcode == 59U);

	/* a store of a vec4 into a float */
	begin_module();
	op(59U, 3U, U(T_PTR_FN_FLOAT), U(B), U(7));
	op(61U, 3U, U(T_VEC4), U(B + 1), U(V_IN0));
	op(62U, 2U, U(B), U(B + 1));
	assert(end_module(&ir, &diag) == ENOTSUP && ir == NULL && diag.opcode == 62U);

	/* a store through an INPUT pointer */
	begin_module();
	op(61U, 3U, U(T_VEC4), U(B + 1), U(V_IN0));
	op(62U, 2U, U(V_IN1), U(B + 1));
	assert(end_module(&ir, &diag) == ENOTSUP && ir == NULL && diag.opcode == 62U);

	/* composite whose constituents do not fill the vector: malformed, not "unsupported" */
	begin_module();
	op(80U, 4U, U(T_VEC4), U(B), U(C_F5), U(C_F2));
	assert(end_module(&ir, &diag) == EINVAL && ir == NULL);

	/* still not lowered: OpFDiv, OpSelect, a branch */
	begin_module();
	op(136U, 4U, U(T_FLOAT), U(B), U(C_F5), U(C_F2));
	assert(end_module(&ir, &diag) == ENOTSUP && diag.opcode == 136U);
	begin_module();
	op(249U, 1U, U(L_ENTRY));
	assert(end_module(&ir, &diag) == ENOTSUP && diag.opcode == 249U);

	/* a decoration that would qualify data (Flat = 14, Component = 31) is not ignored */
	begin_module();
	/* the decoration goes before the function: lift the OpFunction / OpLabel tail and put it back */
	{
		unsigned function_at = mod_n - 7U;      /* OpFunction (5 words) + OpLabel (2 words) */
		uint32_t tail[7];

		memcpy(tail, mod + function_at, sizeof(tail));
		mod_n = function_at;
		op(71U, 2U, U(V_IN0), U(14));           /* OpDecorate %in0 Flat */
		memcpy(mod + mod_n, tail, sizeof(tail));
		mod_n += 7U;
	}
	assert(end_module(&ir, &diag) == ENOTSUP && ir == NULL && diag.opcode == 71U);
	printf("  refusals: unset local, partial local, dynamic index, initializer, size mismatch, input store, FDiv, branch, Flat\n");
}

/* RelaxedPrecision (no effect on this lowering) is accepted by name. */
static void
test_harmless_decoration(void)
{
	struct i915_shader_ir *ir;
	struct i915_compile_diagnostic diag;
	unsigned function_at;
	uint32_t tail[7];

	begin_module();
	function_at = mod_n - 7U;
	memcpy(tail, mod + function_at, sizeof(tail));
	mod_n = function_at;
	op(71U, 2U, U(V_IN0), U(0));                    /* OpDecorate %in0 RelaxedPrecision */
	memcpy(mod + mod_n, tail, sizeof(tail));
	mod_n += 7U;
	assert(end_module(&ir, &diag) == 0);
	drv_i915_shader_ir_free(ir);
}

/* ------------------------------------------------------------------ the fixed vkdemo shaders */

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

static int
close_to(float got, float want)
{
	float scale = fabsf(want) > 1.0f ? fabsf(want) : 1.0f;

	return fabsf(got - want) <= 2e-6f * scale;
}

/*
 * cuboid.vert as shipped (glslang -O0, not simplified), against the GLSL source evaluated
 * here in C.  Several vertices and times, so that a formula that is wrong in one term
 * cannot pass.
 */
static void
test_vkdemo_vertex_shader(void)
{
	static const float vertices[4][5] = {
		{ 0.5f, -1.25f, 2.0f, 0.25f, 0.75f },
		{ -1.0f, 1.0f, -1.0f, 0.0f, 1.0f },
		{ 1.0f, 0.5f, 0.75f, 1.0f, 0.0f },
		{ -0.3f, -0.7f, 1.9f, 0.125f, 0.625f },
	};
	static const float times[3] = { 0.0f, 1.7f, 12.34f };
	uint32_t *code;
	size_t words;
	struct i915_shader_ir *ir;
	struct i915_compile_diagnostic diag;
	struct machine m;
	unsigned v, t, k;
	int error;

	code = load_spv("cuboid.vert.spv", &words);
	error = drv_i915_shader_parse(code, words, I915_STAGE_VERTEX, &ir, &diag);
	if (error != 0)
		printf("  cuboid.vert.spv refused: opcode %u at word %u: %s\n", diag.opcode, diag.word_offset,
			diag.reason != NULL ? diag.reason : "-");
	assert(error == 0);
	assert(ir->stage == I915_STAGE_VERTEX && ir->push_bytes == 4U);
	assert(ir->input_count == 2U && ir->output_count == 1U);

	for (v = 0U; v < 4U; v++) {
		for (t = 0U; t < 3U; t++) {
			const float *p = vertices[v];
			float seconds = times[t];
			float angle_x = 0.30f + 0.43f * seconds, angle_y = 0.40f + 0.70f * seconds;
			float sx = sinf(angle_x), cx = cosf(angle_x), sy = sinf(angle_y), cy = cosf(angle_y);
			float rx = p[0], ry = cx * p[1] - sx * p[2], rz = sx * p[1] + cx * p[2];
			float vx = cy * rx + sy * rz, vy = ry, vz = -sy * rx + cy * rz + 3.0f;
			float want[4];

			want[0] = 1.2f * vx;
			want[1] = -1.6f * vy;
			want[2] = (10.0f / 9.9f) * vz - (1.0f / 9.9f);
			want[3] = vz;

			memset(&m, 0, sizeof(m));
			memcpy(m.input[0], p, 3U * sizeof(float));
			m.input[0][3] = 1234.5f;                /* not an attribute component: must not be read */
			memcpy(m.input[1], p + 3, 2U * sizeof(float));
			m.input[1][2] = m.input[1][3] = -999.0f;
			memcpy(m.push, &seconds, 4U);
			run_ir(ir, &m);
			for (k = 0U; k < 4U; k++) {
				if (m.written[SLOT_POSITION][k] != 1U || !close_to(m.output[SLOT_POSITION][k], want[k])) {
					printf("  vertex %u time %g: gl_Position.%u = %.9g want %.9g\n", v, (double)seconds, k,
						(double)m.output[SLOT_POSITION][k], (double)want[k]);
					assert(!"gl_Position mismatch");
				}
			}
			assert(m.written[0][0] == 1U && m.written[0][1] == 1U && m.written[0][2] == 0U && m.written[0][3] == 0U);
			assert(m.output[0][0] == p[3] && m.output[0][1] == p[4]);
		}
	}
	printf("  cuboid.vert.spv (-O0, as shipped): %u IR instructions, %u values; gl_Position and texture_coordinate match the GLSL source for 4 vertices x 3 times\n",
		ir->instruction_count, ir->value_count);
	drv_i915_shader_ir_free(ir);
	free(code);
}

static void
test_vkdemo_fragment_shader(void)
{
	uint32_t *code;
	size_t words;
	struct i915_shader_ir *ir;
	struct machine m;
	float want[4];

	code = load_spv("cuboid.frag.spv", &words);
	assert(drv_i915_shader_parse(code, words, I915_STAGE_FRAGMENT, &ir, NULL) == 0);
	assert(ir->uniform_count == 1U && ir->uniforms[0].set == 0U && ir->uniforms[0].binding == 0U);
	memset(&m, 0, sizeof(m));
	m.input[0][0] = 0.3f;
	m.input[0][1] = 0.6f;
	run_ir(ir, &m);
	fake_texture(0U, 0U, 0.3f, 0.6f, want);
	expect_out(&m, 0U, want[0], want[1], want[2], want[3]);
	printf("  cuboid.frag.spv: texture(checker, texture_coordinate) with u, v in the right order, 4 components out\n");
	drv_i915_shader_ir_free(ir);
	free(code);
}

int
main(void)
{
	test_local_store_load_overwrite();
	test_vector_construct_extract_shuffle();
	test_component_access();
	test_dot_negate_scale();
	test_refusals();
	test_harmless_decoration();
	test_vkdemo_vertex_shader();
	test_vkdemo_fragment_shader();
	assert(fixture_live == 0U);
	printf("i915 vk lower host test PASS\n");
	return 0;
}
