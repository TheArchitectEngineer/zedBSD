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
	uint8_t push[128];
	uint8_t ubo[4][256];            /* the words of uniform blocks 0 .. 3 of the IR's uniform list */
	float output[SLOTS][4];
	unsigned written[SLOTS][4];     /* how many times each output component was stored */
	int killed;                     /* a KILL whose condition was true ran */
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

/* the IR value bits of a float, and back */
static float
fbits(uint32_t bits)
{
	return bits_to_float(bits);
}

/* The result of a comparison as the IR keeps a Boolean: all ones or zero. */
static uint32_t
boolean(int truth)
{
	return truth ? 0xFFFFFFFFU : 0U;
}

/*
 * Runs the IR for one invocation; asserts the SSA discipline (defined once -- by one instruction, which a loop
 * may run again, or by MOVEs of a loop variable -- and defined before read) and that a Boolean operand is all
 * ones or zero.  Values are kept as bits: a float, an integer, or a Boolean.  A loop runs its body again while
 * LOOP_END's Boolean is true (one invocation: one channel).
 */
static void
run_ir(const struct i915_shader_ir *ir, struct machine *m)
{
	uint32_t *value = calloc(ir->value_count + 4U, sizeof(*value));
	uint32_t *def_at = calloc(ir->value_count + 4U, sizeof(*def_at));
	uint8_t *defined = calloc(ir->value_count + 4U, 1U);
	unsigned loop_top[16], loop_depth = 0U, passes = 0U;
	unsigned index, k;

	assert(value != NULL && defined != NULL && def_at != NULL);
	memset(m->output, 0, sizeof(m->output));
	memset(m->written, 0, sizeof(m->written));
	m->killed = 0;
	for (index = 0U; index < ir->instruction_count; index++) {
		const struct i915_shader_ir_inst *inst = &ir->instructions[index];
		unsigned sources = 0U, results = 1U, slot;
		uint32_t a, b, c;
		float fa, fb, rgba[4];

		switch (inst->op) {
		case I915_IR_STORE_OUTPUT: sources = 1U; results = 0U; break;
		case I915_IR_KILL: sources = 1U; results = 0U; break;
		case I915_IR_LOOP_END: sources = 1U; results = 0U; break;
		case I915_IR_LOOP_BEGIN: results = 0U; break;
		case I915_IR_FNEG: case I915_IR_SIN: case I915_IR_COS: case I915_IR_RSQ:
		case I915_IR_RCP: case I915_IR_SQRT: case I915_IR_EXP2: case I915_IR_LOG2:
		case I915_IR_FABS: case I915_IR_FLOOR: case I915_IR_FRACT: case I915_IR_NOT:
		case I915_IR_FTRUNC: case I915_IR_INEG: case I915_IR_INOT: case I915_IR_I2F: case I915_IR_U2F:
		case I915_IR_F2I: case I915_IR_F2U: case I915_IR_MOVE:
			sources = 1U; break;
		case I915_IR_FADD: case I915_IR_FSUB: case I915_IR_FMUL: case I915_IR_FMIN: case I915_IR_FMAX:
		case I915_IR_FLT: case I915_IR_FGE: case I915_IR_FEQ: case I915_IR_FNEU: case I915_IR_AND: case I915_IR_OR:
		case I915_IR_IADD: case I915_IR_ISUB: case I915_IR_IMUL: case I915_IR_UDIV: case I915_IR_UMOD:
		case I915_IR_IAND: case I915_IR_IOR: case I915_IR_IXOR: case I915_IR_SHL: case I915_IR_SHR: case I915_IR_ASR:
		case I915_IR_ILT: case I915_IR_IGE: case I915_IR_ULT: case I915_IR_UGE: case I915_IR_IEQ: case I915_IR_INE:
			sources = 2U; break;
		case I915_IR_SELECT: sources = 3U; break;
		case I915_IR_SAMPLE: sources = 2U; results = 4U; break;
		case I915_IR_CONST: case I915_IR_BOOL: case I915_IR_ICONST: case I915_IR_LOAD_INPUT: case I915_IR_LOAD_PUSH:
		case I915_IR_LOAD_UBO:
			break;
		default: assert(!"IR operation the interpreter does not know"); break;
		}
		for (k = 0U; k < sources; k++)
			assert(inst->src[k] < ir->value_count && defined[inst->src[k]] != 0U);
		for (k = 0U; k < results; k++) {
			assert(inst->dst + k < ir->value_count);
			/* defined once; again only by the same instruction (a loop's next pass) or by a MOVE */
			assert(defined[inst->dst + k] == 0U || def_at[inst->dst + k] == index || inst->op == I915_IR_MOVE);
			if (defined[inst->dst + k] == 0U)
				def_at[inst->dst + k] = index;
			defined[inst->dst + k] = 1U;
		}
		a = sources >= 1U ? value[inst->src[0]] : 0U;
		b = sources >= 2U ? value[inst->src[1]] : 0U;
		c = sources >= 3U ? value[inst->src[2]] : 0U;
		fa = fbits(a);
		fb = fbits(b);

		/* the logic operations, SELECT's condition, KILL and LOOP_END read Booleans only */
		if (inst->op == I915_IR_AND || inst->op == I915_IR_OR)
			assert((a == 0U || a == 0xFFFFFFFFU) && (b == 0U || b == 0xFFFFFFFFU));
		if (inst->op == I915_IR_NOT || inst->op == I915_IR_SELECT || inst->op == I915_IR_KILL ||
		    inst->op == I915_IR_LOOP_END)
			assert(a == 0U || a == 0xFFFFFFFFU);
		if (inst->op == I915_IR_BOOL)
			assert(inst->immediate == 0U || inst->immediate == 0xFFFFFFFFU);

		switch (inst->op) {
		case I915_IR_CONST: value[inst->dst] = inst->immediate; break;
		case I915_IR_BOOL: value[inst->dst] = inst->immediate; break;
		case I915_IR_ICONST: value[inst->dst] = inst->immediate; break;
		case I915_IR_LOAD_INPUT:
			assert(inst->location < SLOTS && inst->component < 4U);
			value[inst->dst] = float_to_bits(m->input[inst->location][inst->component]);
			break;
		case I915_IR_LOAD_PUSH:
			assert(inst->immediate + 4U <= sizeof(m->push) && inst->immediate + 4U <= ir->push_bytes);
			memcpy(&value[inst->dst], m->push + inst->immediate, 4U);
			break;
		case I915_IR_LOAD_UBO:
			assert(inst->location < ir->uniform_count && ir->uniforms[inst->location].kind == I915_IR_UNIFORM_BLOCK);
			assert(inst->location < 4U && inst->immediate + 4U <= sizeof(m->ubo[0]));
			assert(inst->immediate >= ir->uniforms[inst->location].offset &&
			       inst->immediate + 4U <= ir->uniforms[inst->location].offset + ir->uniforms[inst->location].size);
			memcpy(&value[inst->dst], m->ubo[inst->location] + inst->immediate, 4U);
			break;
		case I915_IR_STORE_OUTPUT:
			slot = inst->location == I915_IR_LOCATION_POSITION ? SLOT_POSITION : inst->location;
			assert(slot < SLOTS && inst->component < 4U);
			assert(inst->location == I915_IR_LOCATION_POSITION || inst->location < SLOT_POSITION);
			m->output[slot][inst->component] = fa;
			m->written[slot][inst->component]++;
			break;
		case I915_IR_KILL: if (a != 0U) m->killed = 1; break;
		case I915_IR_FADD: value[inst->dst] = float_to_bits(fa + fb); break;
		case I915_IR_FSUB: value[inst->dst] = float_to_bits(fa - fb); break;
		case I915_IR_FMUL: value[inst->dst] = float_to_bits(fa * fb); break;
		case I915_IR_FNEG: value[inst->dst] = float_to_bits(-fa); break;
		case I915_IR_SIN: value[inst->dst] = float_to_bits(sinf(fa)); break;
		case I915_IR_COS: value[inst->dst] = float_to_bits(cosf(fa)); break;
		case I915_IR_RSQ: value[inst->dst] = float_to_bits(1.0f / sqrtf(fa)); break;
		case I915_IR_RCP: value[inst->dst] = float_to_bits(1.0f / fa); break;
		case I915_IR_SQRT: value[inst->dst] = float_to_bits(sqrtf(fa)); break;
		case I915_IR_EXP2: value[inst->dst] = float_to_bits(exp2f(fa)); break;
		case I915_IR_LOG2: value[inst->dst] = float_to_bits(log2f(fa)); break;
		case I915_IR_FABS: value[inst->dst] = float_to_bits(fabsf(fa)); break;
		case I915_IR_FLOOR: value[inst->dst] = float_to_bits(floorf(fa)); break;
		case I915_IR_FRACT: value[inst->dst] = float_to_bits(fa - floorf(fa)); break;
		case I915_IR_FTRUNC: value[inst->dst] = float_to_bits(truncf(fa)); break;
		case I915_IR_FMIN: value[inst->dst] = float_to_bits(fa < fb ? fa : fb); break;
		case I915_IR_FMAX: value[inst->dst] = float_to_bits(fa >= fb ? fa : fb); break;
		case I915_IR_FLT: value[inst->dst] = boolean(fa < fb); break;
		case I915_IR_FGE: value[inst->dst] = boolean(fa >= fb); break;
		case I915_IR_FEQ: value[inst->dst] = boolean(fa == fb); break;
		case I915_IR_FNEU: value[inst->dst] = boolean(fa != fb); break;
		case I915_IR_AND: value[inst->dst] = a & b; break;
		case I915_IR_OR: value[inst->dst] = a | b; break;
		case I915_IR_NOT: value[inst->dst] = ~a; break;
		case I915_IR_SELECT: value[inst->dst] = a != 0U ? b : c; break;
		case I915_IR_IADD: value[inst->dst] = a + b; break;
		case I915_IR_ISUB: value[inst->dst] = a - b; break;
		case I915_IR_IMUL: value[inst->dst] = a * b; break;
		case I915_IR_INEG: value[inst->dst] = 0U - a; break;
		case I915_IR_UDIV: value[inst->dst] = b != 0U ? a / b : 0xFFFFFFFFU; break;
		case I915_IR_UMOD: value[inst->dst] = b != 0U ? a % b : a; break;
		case I915_IR_IAND: value[inst->dst] = a & b; break;
		case I915_IR_IOR: value[inst->dst] = a | b; break;
		case I915_IR_IXOR: value[inst->dst] = a ^ b; break;
		case I915_IR_INOT: value[inst->dst] = ~a; break;
		case I915_IR_SHL: value[inst->dst] = a << (b & 31U); break;
		case I915_IR_SHR: value[inst->dst] = a >> (b & 31U); break;
		case I915_IR_ASR: value[inst->dst] = (uint32_t)((int32_t)a >> (b & 31U)); break;
		case I915_IR_I2F: value[inst->dst] = float_to_bits((float)(int32_t)a); break;
		case I915_IR_U2F: value[inst->dst] = float_to_bits((float)a); break;
		case I915_IR_F2I: value[inst->dst] = (uint32_t)(int32_t)fa; break;
		case I915_IR_F2U: value[inst->dst] = (uint32_t)fa; break;
		case I915_IR_ILT: value[inst->dst] = boolean((int32_t)a < (int32_t)b); break;
		case I915_IR_IGE: value[inst->dst] = boolean((int32_t)a >= (int32_t)b); break;
		case I915_IR_ULT: value[inst->dst] = boolean(a < b); break;
		case I915_IR_UGE: value[inst->dst] = boolean(a >= b); break;
		case I915_IR_IEQ: value[inst->dst] = boolean(a == b); break;
		case I915_IR_INE: value[inst->dst] = boolean(a != b); break;
		case I915_IR_MOVE: value[inst->dst] = a; break;
		case I915_IR_LOOP_BEGIN:
			assert(loop_depth < 16U);
			loop_top[loop_depth++] = index;
			break;
		case I915_IR_LOOP_END:
			assert(loop_depth != 0U);
			if (a != 0U) {
				assert(++passes < 100000U);
				index = loop_top[loop_depth - 1U];      /* the body again, after the LOOP_BEGIN */
			} else {
				loop_depth--;
			}
			break;
		case I915_IR_SAMPLE:
			fake_texture(inst->location, inst->immediate, fa, fb, rgba);
			for (k = 0U; k < 4U; k++)
				value[inst->dst + k] = float_to_bits(rgba[k]);
			break;
		default: break;
		}
	}
	assert(loop_depth == 0U);
	free(value);
	free(def_at);
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
	T_PTR_OUT_FLOAT, F_MAIN, L_ENTRY, V_OUT1, T_PTR_FN_VEC3, V_DYN, T_BOOL, T_PTR_FN_BOOL,
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
	op(20U, 1U, U(T_BOOL));                                                 /* OpTypeBool */
	op(32U, 3U, U(T_PTR_FN_BOOL), U(7), U(T_BOOL));
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

	/*
	 * still not lowered: a branch back to a block already lowered that is no loop's header, a return inside a
	 * loop (the entry block made a loop header whose merge block never comes), OpSwitch
	 */
	begin_module();
	op(249U, 1U, U(L_ENTRY));
	assert(end_module(&ir, &diag) == ENOTSUP && diag.opcode == 249U);
	begin_module();
	op(246U, 3U, U(B), U(B + 1), U(0));
	assert(end_module(&ir, &diag) == ENOTSUP && diag.opcode == 253U);
	begin_module();
	op(251U, 2U, U(C_I0), U(B));
	assert(end_module(&ir, &diag) == ENOTSUP && diag.opcode == 251U);
	/* OpKill outside a fragment shader: the entry point's execution model made Vertex */
	begin_module();
	mod[5U + 2U + 3U + 1U] = 0U;
	op(252U, 0U);
	op(56U, 0U);
	assert(drv_i915_shader_parse(mod, mod_n, I915_STAGE_VERTEX, &ir, &diag) == ENOTSUP && ir == NULL && diag.opcode == 252U);

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
	printf("  refusals: unset local, partial local, dynamic index, initializer, size mismatch, input store, back edge, return in a loop, switch, vertex OpKill, Flat\n");
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

/* ------------------------------------------------------------------ p014 stage C: control flow, comparisons, GLSL.std.450 */

/* Loads a .spv file under the repository into a word buffer the caller frees. */
static uint32_t *
load_spv_at(const char *directory, const char *name, size_t *words)
{
	char path[512];
	FILE *file;
	long size;
	uint32_t *code;

	snprintf(path, sizeof(path), "%s/%s/%s", VK_REPO, directory, name);
	file = fopen(path, "rb");
	if (file == NULL)
		printf("  cannot open %s\n", path);
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

#define COMPILER_SHADERS "src/drivers/gpu/i915/tests/render/compiler-shaders"
#define MVIEW_SHADERS "userland/base/mview/shaders"

/* Parses a shader file, printing a refusal before failing. */
static struct i915_shader_ir *
parse_file(const char *directory, const char *name, enum i915_shader_stage stage)
{
	struct i915_shader_ir *ir;
	struct i915_compile_diagnostic diag;
	uint32_t *code;
	size_t words;
	int error;

	code = load_spv_at(directory, name, &words);
	error = drv_i915_shader_parse(code, words, stage, &ir, &diag);
	if (error != 0)
		printf("  %s refused (%d): opcode %u at word %u: %s\n", name, error, diag.opcode, diag.word_offset,
			diag.reason != NULL ? diag.reason : "-");
	assert(error == 0);
	free(code);
	return ir;
}

/* Runs a fragment IR whose one input, location 0, is v. */
static void
run_fragment(const struct i915_shader_ir *ir, struct machine *m, const float v[4])
{
	memset(m, 0, sizeof(*m));
	memcpy(m->input[0], v, 4U * sizeof(float));
	run_ir(ir, m);
}

/* Compares output 0 with four floats bit for bit (NaN never expected). */
static void
expect_color(const struct machine *m, const char *what, const float v[4], const float want[4])
{
	unsigned k;

	for (k = 0U; k < 4U; k++) {
		if (float_to_bits(m->output[0][k]) != float_to_bits(want[k])) {
			printf("  %s at (%g, %g, %g, %g): colour.%u = %.9g want %.9g\n", what, (double)v[0], (double)v[1],
				(double)v[2], (double)v[3], k, (double)m->output[0][k], (double)want[k]);
			assert(!"output mismatch");
		}
	}
}

/*
 * The inputs every GLSL.std.450 check runs over: x of both signs, integer and not, y > 0 for the
 * functions defined there only, z over the range of exp2, w over mix weights and exponents.
 */
static const float math_inputs[][4] = {
	{ 0.0f, 1.0f, 0.0f, 0.0f }, { 1.0f, 2.0f, 1.0f, 1.0f }, { -1.0f, 0.5f, -1.0f, 0.5f },
	{ 2.75f, 3.0f, 4.5f, -2.0f }, { -2.75f, 0.1f, -6.25f, 1.5f }, { 7.5f, 100.0f, 10.0f, 0.25f },
	{ -0.3f, 0.01f, -0.3f, 3.0f }, { 0.75f, 1.5f, 2.0f, -0.5f }, { -8.0f, 42.0f, -9.5f, 0.75f },
	{ 5.0f, 5.0f, 5.0f, 2.5f }, { 0.25f, 4096.0f, 0.001f, -1.25f },
};

/* abs, floor, fract, sqrt, exp2, log2, pow, inversesqrt, min, max, clamp, mix, division, normalize. */
static void
test_glsl_std450_lowering(void)
{
	struct i915_shader_ir *unary, *exponent, *minmax, *divide;
	struct machine m;
	unsigned i;

	unary = parse_file(COMPILER_SHADERS, "unary.frag.spv", I915_STAGE_FRAGMENT);
	exponent = parse_file(COMPILER_SHADERS, "exponent.frag.spv", I915_STAGE_FRAGMENT);
	minmax = parse_file(COMPILER_SHADERS, "minmax.frag.spv", I915_STAGE_FRAGMENT);
	divide = parse_file(COMPILER_SHADERS, "divide.frag.spv", I915_STAGE_FRAGMENT);
	for (i = 0U; i < sizeof(math_inputs) / sizeof(math_inputs[0]); i++) {
		const float *v = math_inputs[i];
		float want[4], larger, length2, scale;

		run_fragment(unary, &m, v);
		want[0] = fabsf(v[0]);
		want[1] = floorf(v[0]);
		want[2] = v[0] - floorf(v[0]);
		want[3] = sqrtf(v[1]);
		expect_color(&m, "unary.frag", v, want);

		/* pow is exp2(log2(x) * y): the operands in that order */
		run_fragment(exponent, &m, v);
		want[0] = exp2f(v[2]);
		want[1] = log2f(v[1]);
		want[2] = exp2f(log2f(v[1]) * v[3]);
		want[3] = 1.0f / sqrtf(v[1]);
		expect_color(&m, "exponent.frag", v, want);

		/* clamp is min(max(x, lo), hi); mix is x * (1 - a) + y * a */
		run_fragment(minmax, &m, v);
		want[0] = v[0] < v[2] ? v[0] : v[2];
		want[1] = v[0] >= v[2] ? v[0] : v[2];
		larger = v[0] >= -0.5f ? v[0] : -0.5f;
		want[2] = larger < 0.75f ? larger : 0.75f;
		want[3] = v[0] * (1.0f - v[3]) + v[2] * v[3];
		expect_color(&m, "minmax.frag", v, want);

		/* a / b is a * (1 / b); normalize is v * inversesqrt(dot(v, v)) */
		run_fragment(divide, &m, v);
		length2 = v[0] * v[0] + v[1] * v[1];
		length2 = length2 + v[2] * v[2];
		scale = 1.0f / sqrtf(length2);
		want[0] = v[0] * (1.0f / v[1]);
		want[1] = 1.0f * (1.0f / v[2]);
		want[2] = v[0] * scale;
		want[3] = v[2] * scale;
		if (v[2] != 0.0f)
			expect_color(&m, "divide.frag", v, want);
	}
	drv_i915_shader_ir_free(unary);
	drv_i915_shader_ir_free(exponent);
	drv_i915_shader_ir_free(minmax);
	drv_i915_shader_ir_free(divide);
	printf("  GLSL.std.450: abs floor fract sqrt exp2 log2 pow inversesqrt min max clamp mix normalize, and a / b, over %u inputs\n",
		(unsigned)(sizeof(math_inputs) / sizeof(math_inputs[0])));
}

/* compare.frag: <, >, <=, >=, ==, !=, &&, || (phis), ?: (a branch and a local), !, mix(bool). */
static void
test_comparisons_and_selection(void)
{
	static const float inputs[][4] = {
		{ 1.0f, 2.0f, 5.0f, 3.0f }, { 2.0f, 1.0f, 3.0f, 5.0f }, { 3.0f, 3.0f, -1.0f, -1.0f },
		{ -4.0f, 7.0f, -9.0f, 0.5f }, { 0.0f, -0.0f, 2.0f, 1.0f }, { 0.0f, 0.0f, 0.0f, 0.0f },
	};
	struct i915_shader_ir *ir;
	struct machine m;
	unsigned i;
	float nan_value;

	ir = parse_file(COMPILER_SHADERS, "compare.frag.spv", I915_STAGE_FRAGMENT);
	nan_value = bits_to_float(0x7FC00000U);
	for (i = 0U; i < sizeof(inputs) / sizeof(inputs[0]) + 2U; i++) {
		float v[4], want[4];

		if (i < sizeof(inputs) / sizeof(inputs[0])) {
			memcpy(v, inputs[i], sizeof(v));
		} else {
			/* a NaN on either side: every ordered test false, != true */
			v[0] = i == 6U ? nan_value : 1.0f;
			v[1] = i == 6U ? 1.0f : nan_value;
			v[2] = 2.0f;
			v[3] = 1.0f;
		}
		run_fragment(ir, &m, v);
		want[0] = (float)(v[0] < v[1]) + 2.0f * (float)(v[0] > v[1]) + 4.0f * (float)(v[0] <= v[1]) +
			8.0f * (float)(v[0] >= v[1]);
		want[1] = (float)(v[0] == v[1]) + 2.0f * (float)(v[0] != v[1]) +
			4.0f * (float)(v[0] < v[1] && v[2] > v[3]) + 8.0f * (float)(v[0] < v[1] || v[2] > v[3]);
		want[2] = v[0] < v[2] ? v[1] : v[3];
		want[3] = (float)(!(v[0] < v[1])) + 2.0f * (v[2] < v[3] ? v[3] : v[2]);
		expect_color(&m, "compare.frag", v, want);
	}
	drv_i915_shader_ir_free(ir);
	printf("  comparisons: < > <= >= == != && || ?: ! mix(bool), NaN on either side, -0 == 0\n");
}

/* Hand-assembled: all twelve float comparisons of SPIR-V, ordered and unordered, against NaN. */
static void
test_unordered_comparisons(void)
{
	static const float pairs[][2] = {
		{ 1.0f, 2.0f }, { 2.0f, 1.0f }, { 3.0f, 3.0f }, { 0.0f, 0.0f },
	};
	struct i915_shader_ir *ir[2];
	struct i915_compile_diagnostic diag;
	struct machine m;
	unsigned half, i, k;
	float nan_value;

	/* two modules of six comparisons each (180 .. 185, 186 .. 191), each result selecting 16 or 2 */
	for (half = 0U; half < 2U; half++) {
		begin_module();
		op(61U, 3U, U(T_VEC4), U(B), U(V_IN0));
		op(81U, 4U, U(T_FLOAT), U(B + 1), U(B), U(0));
		op(81U, 4U, U(T_FLOAT), U(B + 2), U(B), U(1));
		for (k = 0U; k < 6U; k++) {
			op(180U + 6U * half + k, 4U, U(T_BOOL), U(B + 10 + k), U(B + 1), U(B + 2));
			op(169U, 5U, U(T_FLOAT), U(B + 20 + k), U(B + 10 + k), U(C_F16), U(C_F2));
		}
		op(80U, 6U, U(T_VEC4), U(B + 30), U(B + 20), U(B + 21), U(B + 22), U(B + 23));
		op(62U, 2U, U(V_OUT0), U(B + 30));
		op(80U, 6U, U(T_VEC4), U(B + 31), U(B + 24), U(B + 25), U(C_F5), U(C_F5));
		op(62U, 2U, U(V_OUT1), U(B + 31));
		assert(end_module(&ir[half], &diag) == 0);
	}

	nan_value = bits_to_float(0x7FC00000U);
	for (i = 0U; i < sizeof(pairs) / sizeof(pairs[0]) + 3U; i++) {
		float a, b;
		int truth[12];

		a = i < 4U ? pairs[i][0] : (i == 5U ? 1.0f : nan_value);
		b = i < 4U ? pairs[i][1] : (i == 4U ? 1.0f : nan_value);
		truth[0] = a == b;                      /* FOrdEqual */
		truth[1] = !(a < b || a > b);           /* FUnordEqual: equal, or unordered */
		truth[2] = a < b || a > b;              /* FOrdNotEqual: ordered and different */
		truth[3] = a != b;                      /* FUnordNotEqual */
		truth[4] = a < b;                       /* FOrdLessThan */
		truth[5] = !(a >= b);                   /* FUnordLessThan */
		truth[6] = a > b;                       /* FOrdGreaterThan */
		truth[7] = !(a <= b);                   /* FUnordGreaterThan */
		truth[8] = a <= b;                      /* FOrdLessThanEqual */
		truth[9] = !(a > b);                    /* FUnordLessThanEqual */
		truth[10] = a >= b;                     /* FOrdGreaterThanEqual */
		truth[11] = !(a < b);                   /* FUnordGreaterThanEqual */
		for (half = 0U; half < 2U; half++) {
			float v[4];

			v[0] = a;
			v[1] = b;
			v[2] = 0.0f;
			v[3] = 0.0f;
			memset(&m, 0, sizeof(m));
			memcpy(m.input[0], v, sizeof(v));
			run_ir(ir[half], &m);
			for (k = 0U; k < 6U; k++) {
				float got = k < 4U ? m.output[0][k] : m.output[1][k - 4U];
				float want = truth[6U * half + k] ? 16.0f : 2.0f;

				if (got != want) {
					printf("  comparison opcode %u of (%g, %g) = %g, want %g\n", 180U + 6U * half + k,
						(double)a, (double)b, (double)got, (double)want);
					assert(!"comparison mismatch");
				}
			}
		}
	}
	drv_i915_shader_ir_free(ir[0]);
	drv_i915_shader_ir_free(ir[1]);
	printf("  comparisons: the twelve FOrd* / FUnord* opcodes, ordered and unordered, with NaN on either or both sides\n");
}

/* The GLSL of branch.frag evaluated in C at one pixel centre (x, y). */
static void
branch_reference(float x, float y, float want[4])
{
	float c[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
	float t = 1.0f, s;
	unsigned k;

	if (x < 21.0f) {
		if (y < 11.0f) {
			c[0] = 1.0f;
		} else {
			c[1] = 1.0f;
			t = t * 2.0f;
		}
	} else if (y < 33.0f && x > 41.0f) {
		c[2] = 1.0f;
		t = 3.0f;
	} else {
		for (k = 0U; k < 4U; k++)
			c[k] = 0.25f;
	}
	s = (x < y + 0.5f) ? t : -t;
	for (k = 0U; k < 3U; k++)
		want[k] = c[k];
	want[3] = c[3] + s;
}

/* The GLSL of discard.frag evaluated in C: returns 1 when the pixel is discarded. */
static int
discard_reference(float x, float y, float want[4])
{
	float cell = floorf(x * 0.125f) + floorf(y * 0.125f);

	if ((cell * 0.5f - floorf(cell * 0.5f)) > 0.25f)
		return 1;
	want[0] = 0.5f;
	want[1] = 0.25f;
	want[2] = 1.0f;
	want[3] = 1.0f;
	if (y < 32.0f) {
		if ((x * 0.5f - floorf(x * 0.5f)) < 0.5f && x > 16.0f)
			return 1;
		want[0] = 1.0f;
	}
	return 0;
}

/* branch.frag and discard.frag at every pixel centre of a 64 x 64 target. */
static void
test_branches_and_discard(void)
{
	struct i915_shader_ir *branch, *discard;
	struct machine m;
	unsigned x, y, kills;

	branch = parse_file(COMPILER_SHADERS, "branch.frag.spv", I915_STAGE_FRAGMENT);
	discard = parse_file(COMPILER_SHADERS, "discard.frag.spv", I915_STAGE_FRAGMENT);
	kills = 0U;
	for (y = 0U; y < 64U; y++) {
		for (x = 0U; x < 64U; x++) {
			float v[4];
			float want[4];
			int killed;

			v[0] = (float)x + 0.5f;
			v[1] = (float)y + 0.5f;
			v[2] = 0.0f;
			v[3] = 0.0f;
			run_fragment(branch, &m, v);
			branch_reference(v[0], v[1], want);
			expect_color(&m, "branch.frag", v, want);
			assert(m.killed == 0);

			run_fragment(discard, &m, v);
			killed = discard_reference(v[0], v[1], want);
			if (m.killed != killed) {
				printf("  discard.frag at (%u, %u): killed %d, want %d\n", x, y, m.killed, killed);
				assert(!"discard mismatch");
			}
			if (killed == 0)
				expect_color(&m, "discard.frag", v, want);
			kills += (unsigned)killed;
		}
	}
	assert(kills > 1024U && kills < 3072U);
	drv_i915_shader_ir_free(branch);
	drv_i915_shader_ir_free(discard);
	printf("  control flow: nested if / else, && via phi, ?: via a local, discard in and out of branches, at 4096 pixels (%u discarded)\n",
		kills);
}

/* Hand-assembled: a return inside a branch keeps its channels out of what follows the merge. */
static void
test_return_in_branch(void)
{
	struct i915_shader_ir *ir;
	struct i915_compile_diagnostic diag;
	struct machine m;
	unsigned i;

	/* if (in0.x < 5) { out0 = (16,16,16,16); return; } out0 = (2,2,2,2); */
	begin_module();
	op(61U, 3U, U(T_VEC4), U(B), U(V_IN0));
	op(81U, 4U, U(T_FLOAT), U(B + 1), U(B), U(0));
	op(184U, 4U, U(T_BOOL), U(B + 2), U(B + 1), U(C_F5));
	op(247U, 2U, U(B + 4), U(0));                                           /* OpSelectionMerge %merge */
	op(250U, 3U, U(B + 2), U(B + 3), U(B + 4));                             /* OpBranchConditional */
	op(248U, 1U, U(B + 3));
	op(80U, 6U, U(T_VEC4), U(B + 5), U(C_F16), U(C_F16), U(C_F16), U(C_F16));
	op(62U, 2U, U(V_OUT0), U(B + 5));
	op(253U, 0U);                                                           /* OpReturn */
	op(248U, 1U, U(B + 4));
	op(80U, 6U, U(T_VEC4), U(B + 6), U(C_F2), U(C_F2), U(C_F2), U(C_F2));
	op(62U, 2U, U(V_OUT0), U(B + 6));
	assert(end_module(&ir, &diag) == 0);
	for (i = 0U; i < 2U; i++) {
		float v[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		float want = i == 0U ? 16.0f : 2.0f;

		v[0] = i == 0U ? 1.0f : 9.0f;
		run_fragment(ir, &m, v);
		assert(m.output[0][0] == want && m.output[0][3] == want);
	}
	drv_i915_shader_ir_free(ir);
	printf("  control flow: a return in a branch leaves the channels that took it out of the stores after the merge\n");
}

/* The vertex shaders: cells.vert passes its value on, vsmath.vert lights it as mview does. */
static void
test_stage_vertex_shaders(void)
{
	struct i915_shader_ir *cells, *vsmath;
	struct machine m;
	unsigned i, k;

	cells = parse_file(COMPILER_SHADERS, "cells.vert.spv", I915_STAGE_VERTEX);
	vsmath = parse_file(COMPILER_SHADERS, "vsmath.vert.spv", I915_STAGE_VERTEX);
	for (i = 0U; i < sizeof(math_inputs) / sizeof(math_inputs[0]); i++) {
		const float *v = math_inputs[i];
		float n[3], length2, scale, lambert, want[4];

		memset(&m, 0, sizeof(m));
		m.input[0][0] = 0.25f;
		m.input[0][1] = -0.5f;
		m.input[0][2] = 0.0f;
		m.input[0][3] = 1.0f;
		memcpy(m.input[1], v, 4U * sizeof(float));
		run_ir(cells, &m);
		for (k = 0U; k < 4U; k++)
			assert(m.output[0][k] == v[k] && m.output[SLOT_POSITION][k] == m.input[0][k]);

		run_ir(vsmath, &m);
		length2 = v[0] * v[0] + v[1] * v[1];
		length2 = length2 + v[2] * v[2];
		scale = 1.0f / sqrtf(length2);
		for (k = 0U; k < 3U; k++)
			n[k] = v[k] * scale;
		lambert = n[0] * 0.267261f + n[1] * 0.534522f;
		lambert = lambert + n[2] * 0.801784f;
		lambert = lambert >= 0.0f ? lambert : 0.0f;
		want[0] = n[0];
		want[1] = n[1];
		want[2] = lambert;
		want[3] = v[3] >= 0.0f ? v[3] : 0.0f;
		want[3] = want[3] < 1.0f ? want[3] : 1.0f;
		for (k = 0U; k < 4U; k++)
			assert(float_to_bits(m.output[0][k]) == float_to_bits(want[k]));
	}
	drv_i915_shader_ir_free(cells);
	drv_i915_shader_ir_free(vsmath);
	printf("  vertex: normalize, max(dot, 0) with a constant vec3 (OpConstantComposite), clamp\n");
}

/* mview's three shaders as shipped: mview.vert against its GLSL, cutout.frag's discard at alpha 0.5. */
static void
test_mview_shaders(void)
{
	static const float columns[8][4] = {
		{ 0.5f, 0.0f, 0.0f, 0.0f }, { 0.0f, -0.75f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.25f, 1.0f },
		{ 0.125f, -0.25f, 0.5f, 2.0f }, { 0.0f, 1.0f, 0.0f, 0.0f }, { -1.0f, 0.0f, 0.0f, 0.0f },
		{ 0.0f, 0.0f, 1.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 0.5f },
	};
	static const float vertex[8] = { 0.5f, -1.0f, 2.0f, 0.3f, -0.6f, 0.8f, 0.25f, 0.75f };
	struct i915_shader_ir *vert, *frag, *cutout;
	struct machine m;
	float turned[3], length2, scale, lambert, shade, clip[4], want[4], rgba[4];
	unsigned k, i;

	vert = parse_file(MVIEW_SHADERS, "mview.vert.spv", I915_STAGE_VERTEX);
	frag = parse_file(MVIEW_SHADERS, "mview.frag.spv", I915_STAGE_FRAGMENT);
	cutout = parse_file(MVIEW_SHADERS, "cutout.frag.spv", I915_STAGE_FRAGMENT);
	assert(vert->push_bytes == 112U && vert->input_count == 3U && vert->output_count == 2U);
	assert(frag->push_bytes == 128U && cutout->push_bytes == 128U);

	/* mview.vert: the rotated normal lit from a fixed direction, the position through the clip columns */
	memset(&m, 0, sizeof(m));
	memcpy(m.push, columns, sizeof(columns));
	m.input[0][0] = vertex[0];
	m.input[0][1] = vertex[1];
	m.input[0][2] = vertex[2];
	m.input[1][0] = vertex[3];
	m.input[1][1] = vertex[4];
	m.input[1][2] = vertex[5];
	m.input[2][0] = vertex[6];
	m.input[2][1] = vertex[7];
	run_ir(vert, &m);
	for (k = 0U; k < 3U; k++) {
		turned[k] = vertex[3] * columns[4][k] + vertex[4] * columns[5][k];
		turned[k] = turned[k] + vertex[5] * columns[6][k];
	}
	length2 = turned[0] * turned[0] + turned[1] * turned[1];
	length2 = length2 + turned[2] * turned[2];
	scale = 1.0f / sqrtf(length2);
	lambert = turned[0] * scale * 0.267261f + turned[1] * scale * 0.534522f;
	lambert = lambert + turned[2] * scale * 0.801784f;
	lambert = lambert >= 0.0f ? lambert : 0.0f;
	shade = 0.35f + 0.65f * lambert;
	for (k = 0U; k < 4U; k++) {
		clip[k] = columns[0][k] * vertex[0] + columns[1][k] * vertex[1];
		clip[k] = clip[k] + columns[2][k] * vertex[2];
		clip[k] = clip[k] + columns[3][k];
		assert(fabsf(m.output[SLOT_POSITION][k] - clip[k]) <= 1e-6f);
	}
	assert(fabsf(m.output[1][0] - shade) <= 1e-6f);
	assert(m.output[0][0] == vertex[6] && m.output[0][1] == vertex[7]);

	/* mview.frag and cutout.frag: texel times the colour pushed at byte 112, lit; alpha below 0.5 discards */
	for (i = 0U; i < 4U; i++) {
		float u = 0.2f + 0.2f * (float)i, v = 0.9f - 0.2f * (float)i, alpha = 0.3f + 0.2f * (float)i;
		float color[4];

		color[0] = 0.5f;
		color[1] = 0.75f;
		color[2] = 1.0f;
		color[3] = alpha;
		memset(&m, 0, sizeof(m));
		memcpy(m.push + 112, color, sizeof(color));
		m.input[0][0] = u;
		m.input[0][1] = v;
		m.input[1][0] = 0.8f;
		fake_texture(0U, 0U, u, v, rgba);
		for (k = 0U; k < 3U; k++)
			want[k] = (rgba[k] * color[k]) * 0.8f;
		want[3] = rgba[3] * color[3];
		run_ir(frag, &m);
		for (k = 0U; k < 4U; k++)
			assert(m.output[0][k] == want[k]);
		assert(m.killed == 0);

		run_ir(cutout, &m);
		assert(m.killed == (want[3] < 0.5f));
		if (m.killed == 0) {
			for (k = 0U; k < 3U; k++)
				assert(m.output[0][k] == want[k]);
			assert(m.output[0][3] == 1.0f);
		}
	}
	drv_i915_shader_ir_free(vert);
	drv_i915_shader_ir_free(frag);
	drv_i915_shader_ir_free(cutout);
	printf("  mview: mview.vert (normalize, max, OpConstantComposite) against its GLSL; mview.frag; cutout.frag discards below alpha 0.5\n");
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
	test_glsl_std450_lowering();
	test_comparisons_and_selection();
	test_unordered_comparisons();
	test_branches_and_discard();
	test_return_in_branch();
	test_stage_vertex_shaders();
	test_mview_shaders();
	assert(fixture_live == 0U);
	printf("i915 vk lower host test PASS\n");
	return 0;
}
