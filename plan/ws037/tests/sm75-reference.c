/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws037-p008: the reference interpreter of the scalar IR (sm75-reference.h).
 */

#include "plan/ws037/tests/sm75-reference.h"

#include "drivers/gpu/compiler/spirv.h"
#include "drivers/gpu/nvrtx/compiler/sm75-isa.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* The most IR instructions one invocation runs, and the deepest loop nest. */
#define REFERENCE_STEPS		1000000U
#define REFERENCE_LOOPS		32U

static int reference_step(const struct drv_gpu_shader_ir *ir, const struct nvrtx_sm75_binary *binary, const struct test_run *run, const struct drv_gpu_shader_ir_inst *inst, uint32_t *v, struct sm75_reference_result *result);
static int reference_texture(const struct drv_gpu_shader_ir *ir, const struct nvrtx_sm75_binary *binary, const struct test_run *run, const struct drv_gpu_shader_ir_inst *inst, uint32_t *v, struct sm75_reference_result *result);
static int32_t reference_nibble(uint32_t bits, unsigned low);
static uint32_t reference_handle(const struct nvrtx_sm75_binary *binary, const struct test_run *run, uint32_t set, uint32_t binding, int *found);
static uint32_t reference_input_address(const struct drv_gpu_shader_ir *ir, const struct drv_gpu_shader_ir_inst *inst, int *found);
static uint32_t reference_cb(const struct drv_gpu_shader_ir *ir, uint32_t uniform);
static uint32_t reference_bool(int truth);
static float reference_float(uint32_t bits);
static uint32_t reference_bits(float value);

/*
 * Runs one invocation of a parsed graphics program.
 *
 * The values live in one array indexed by value number; a loop is run again
 * from the instruction after its LOOP_BEGIN while its LOOP_END's Boolean
 * holds.
 */
void
sm75_reference_run(
	const struct drv_gpu_shader_ir *ir,
	const struct nvrtx_sm75_binary *binary,
	const struct test_run *run,
	struct sm75_reference_result *result)
{
	const struct drv_gpu_shader_ir_inst *inst;
	uint32_t begins[REFERENCE_LOOPS];
	uint32_t depth;
	uint32_t pc;
	uint32_t steps;
	uint32_t *v;
	int stop;

	memset(result, 0, sizeof(*result));

	/* The values, all zero to begin with. */
	v = calloc(ir->value_count + 1U, sizeof(*v));
	if (v == NULL) {
		result->failed = 1;
		result->why = "no memory";
		return;
	}

	/* The instructions in order, the loops repeated. */
	depth = 0U;
	pc = 0U;
	for (steps = 0U; pc < ir->instruction_count; steps++) {
		if (steps == REFERENCE_STEPS) {
			result->failed = 1;
			result->why = "too many steps";
			break;
		}
		inst = &ir->instructions[pc];

		/* A loop's beginning is remembered. */
		if (inst->op == DRV_GPU_IR_LOOP_BEGIN) {
			if (depth == REFERENCE_LOOPS) {
				result->failed = 1;
				result->why = "loops too deep";
				break;
			}
			begins[depth] = pc;
			depth++;
			pc++;
			continue;
		}

		/* A loop's end runs the body again while its Boolean holds. */
		if (inst->op == DRV_GPU_IR_LOOP_END) {
			if (depth == 0U) {
				result->failed = 1;
				result->why = "a loop's end without its beginning";
				break;
			}
			if (v[inst->src[0]] != 0U) {
				pc = begins[depth - 1U] + 1U;
			} else {
				depth--;
				pc++;
			}
			continue;
		}

		/* Every other instruction. */
		stop = reference_step(ir, binary, run, inst, v, result);
		if (stop != 0)
			break;
		pc++;
	}

	free(v);
}

/* Runs one instruction; nonzero when the invocation stops (a discard or a failure). */
static int
reference_step(
	const struct drv_gpu_shader_ir *ir,
	const struct nvrtx_sm75_binary *binary,
	const struct test_run *run,
	const struct drv_gpu_shader_ir_inst *inst,
	uint32_t *v,
	struct sm75_reference_result *result)
{
	uint32_t a;
	uint32_t b;
	uint32_t c;
	uint32_t address;
	float fa;
	float fb;
	int found;

	/* The first three sources' words (a field naming no value reads as 0). */
	a = 0U;
	b = 0U;
	c = 0U;
	if (inst->src[0] <= ir->value_count)
		a = v[inst->src[0]];
	if (inst->src[1] <= ir->value_count)
		b = v[inst->src[1]];
	if (inst->src[2] <= ir->value_count)
		c = v[inst->src[2]];
	fa = reference_float(a);
	fb = reference_float(b);

	/* What the operation says. */
	switch (inst->op) {
	case DRV_GPU_IR_NOP:
	case DRV_GPU_IR_SKIP_BEGIN:
	case DRV_GPU_IR_SKIP_END:
		break;
	case DRV_GPU_IR_CONST:
	case DRV_GPU_IR_ICONST:
	case DRV_GPU_IR_BOOL:
		v[inst->dst] = inst->immediate;
		break;
	case DRV_GPU_IR_MOVE:
		v[inst->dst] = a;
		break;
	case DRV_GPU_IR_LOAD_PUSH:
		v[inst->dst] = sm75_machine_cb_word(run, NVRTX_SM75_CB_ROOT, NVRTX_SM75_CB_PUSH + inst->immediate);
		break;
	case DRV_GPU_IR_LOAD_UBO:
		v[inst->dst] = sm75_machine_cb_word(run, reference_cb(ir, inst->location), inst->immediate);
		break;
	case DRV_GPU_IR_LOAD_INPUT:
		address = reference_input_address(ir, inst, &found);
		if (found == 0) {
			result->failed = 1;
			result->why = "an input the reference does not know";
			return 1;
		}
		v[inst->dst] = run->attr_in[address / 4U];
		break;
	case DRV_GPU_IR_STORE_OUTPUT:
		/* A fragment program's colour, or a vertex program's attribute word. */
		if (ir->stage == DRV_GPU_STAGE_FRAGMENT) {
			if (inst->location >= SM75_REFERENCE_COLORS || inst->component > 3U) {
				result->failed = 1;
				result->why = "a fragment output that is not a colour";
				return 1;
			}
			result->color[inst->location][inst->component] = a;
			result->color_written[inst->location][inst->component] = 1U;
			break;
		}
		if (inst->location == DRV_GPU_IR_LOCATION_POSITION) {
			address = NVRTX_SM75_ATTR_POSITION + inst->component * 4U;
		} else if (inst->location == DRV_GPU_IR_LOCATION_POINT_SIZE) {
			address = NVRTX_SM75_ATTR_POINT_SIZE;
		} else if (inst->location < 32U) {
			address = NVRTX_SM75_ATTR_GENERIC + inst->location * 16U + inst->component * 4U;
		} else {
			result->failed = 1;
			result->why = "a vertex output the reference does not know";
			return 1;
		}
		result->attr_out[address / 4U] = a;
		result->attr_written[address / 4U] = 1U;
		break;
	case DRV_GPU_IR_FADD:
		v[inst->dst] = reference_bits(fa + fb);
		break;
	case DRV_GPU_IR_FSUB:
		v[inst->dst] = reference_bits(fa - fb);
		break;
	case DRV_GPU_IR_FMUL:
		v[inst->dst] = reference_bits(fa * fb);
		break;
	case DRV_GPU_IR_FNEG:
		v[inst->dst] = a ^ 0x80000000U;
		break;
	case DRV_GPU_IR_FABS:
		v[inst->dst] = a & 0x7fffffffU;
		break;
	case DRV_GPU_IR_RSQ:
		v[inst->dst] = reference_bits(1.0f / sqrtf(fa));
		break;
	case DRV_GPU_IR_RCP:
		v[inst->dst] = reference_bits(1.0f / fa);
		break;
	case DRV_GPU_IR_SQRT:
		v[inst->dst] = reference_bits(sqrtf(fa));
		break;
	case DRV_GPU_IR_EXP2:
		v[inst->dst] = reference_bits(exp2f(fa));
		break;
	case DRV_GPU_IR_LOG2:
		v[inst->dst] = reference_bits(log2f(fa));
		break;
	case DRV_GPU_IR_SIN:
		v[inst->dst] = reference_bits(sinf(fa));
		break;
	case DRV_GPU_IR_COS:
		v[inst->dst] = reference_bits(cosf(fa));
		break;
	case DRV_GPU_IR_FLOOR:
		v[inst->dst] = reference_bits(floorf(fa));
		break;
	case DRV_GPU_IR_FRACT:
		v[inst->dst] = reference_bits(fa - floorf(fa));
		break;
	case DRV_GPU_IR_FTRUNC:
		v[inst->dst] = reference_bits(truncf(fa));
		break;
	case DRV_GPU_IR_FROUND_EVEN:
		v[inst->dst] = reference_bits(nearbyintf(fa));
		break;
	case DRV_GPU_IR_FMIN:
		v[inst->dst] = reference_bits(fminf(fa, fb));
		break;
	case DRV_GPU_IR_FMAX:
		v[inst->dst] = reference_bits(fmaxf(fa, fb));
		break;
	case DRV_GPU_IR_FLT:
		v[inst->dst] = reference_bool(fa < fb);
		break;
	case DRV_GPU_IR_FGE:
		v[inst->dst] = reference_bool(fa >= fb);
		break;
	case DRV_GPU_IR_FEQ:
		v[inst->dst] = reference_bool(fa == fb);
		break;
	case DRV_GPU_IR_FNEU:
		v[inst->dst] = reference_bool(!(fa == fb));
		break;
	case DRV_GPU_IR_AND:
	case DRV_GPU_IR_IAND:
		v[inst->dst] = a & b;
		break;
	case DRV_GPU_IR_OR:
	case DRV_GPU_IR_IOR:
		v[inst->dst] = a | b;
		break;
	case DRV_GPU_IR_IXOR:
		v[inst->dst] = a ^ b;
		break;
	case DRV_GPU_IR_NOT:
	case DRV_GPU_IR_INOT:
		v[inst->dst] = ~a;
		break;
	case DRV_GPU_IR_SELECT:
		v[inst->dst] = (a != 0U) ? b : c;
		break;
	case DRV_GPU_IR_KILL:
		if (a != 0U) {
			result->killed = 1;
			return 1;
		}
		break;
	case DRV_GPU_IR_IADD:
		v[inst->dst] = a + b;
		break;
	case DRV_GPU_IR_ISUB:
		v[inst->dst] = a - b;
		break;
	case DRV_GPU_IR_IMUL:
		v[inst->dst] = a * b;
		break;
	case DRV_GPU_IR_INEG:
		v[inst->dst] = 0U - a;
		break;
	case DRV_GPU_IR_UDIV:
	case DRV_GPU_IR_UMOD:
	case DRV_GPU_IR_IDIV:
	case DRV_GPU_IR_IREM:
		/* A zero divisor, and the most negative integer by -1, are undefined. */
		if (b == 0U || (a == 0x80000000U && b == 0xffffffffU && (inst->op == DRV_GPU_IR_IDIV || inst->op == DRV_GPU_IR_IREM))) {
			result->undefined = 1;
			v[inst->dst] = 0U;
			break;
		}
		if (inst->op == DRV_GPU_IR_UDIV)
			v[inst->dst] = a / b;
		else if (inst->op == DRV_GPU_IR_UMOD)
			v[inst->dst] = a % b;
		else if (inst->op == DRV_GPU_IR_IDIV)
			v[inst->dst] = (uint32_t)((int32_t)a / (int32_t)b);
		else
			v[inst->dst] = (uint32_t)((int32_t)a % (int32_t)b);
		break;
	case DRV_GPU_IR_SHL:
		v[inst->dst] = a << (b & 31U);
		break;
	case DRV_GPU_IR_SHR:
		v[inst->dst] = a >> (b & 31U);
		break;
	case DRV_GPU_IR_ASR:
		v[inst->dst] = (uint32_t)((int32_t)a >> (b & 31U));
		break;
	case DRV_GPU_IR_I2F:
		v[inst->dst] = reference_bits((float)(int32_t)a);
		break;
	case DRV_GPU_IR_U2F:
		v[inst->dst] = reference_bits((float)a);
		break;
	case DRV_GPU_IR_F2I:
		v[inst->dst] = sm75_machine_f2i(fa, 1);
		break;
	case DRV_GPU_IR_F2U:
		v[inst->dst] = sm75_machine_f2i(fa, 0);
		break;
	case DRV_GPU_IR_ILT:
		v[inst->dst] = reference_bool((int32_t)a < (int32_t)b);
		break;
	case DRV_GPU_IR_IGE:
		v[inst->dst] = reference_bool((int32_t)a >= (int32_t)b);
		break;
	case DRV_GPU_IR_ULT:
		v[inst->dst] = reference_bool(a < b);
		break;
	case DRV_GPU_IR_UGE:
		v[inst->dst] = reference_bool(a >= b);
		break;
	case DRV_GPU_IR_IEQ:
		v[inst->dst] = reference_bool(a == b);
		break;
	case DRV_GPU_IR_INE:
		v[inst->dst] = reference_bool(a != b);
		break;
	case DRV_GPU_IR_SAMPLE:
	case DRV_GPU_IR_SAMPLE_BIAS:
	case DRV_GPU_IR_SAMPLE_LOD:
	case DRV_GPU_IR_TEXTURE:
		found = reference_texture(ir, binary, run, inst, v, result);
		if (found != 0)
			return 1;
		break;
	default:
		result->failed = 1;
		result->why = "an operation the reference does not run";
		return 1;
	}

	/* Succeeded: the invocation goes on. */
	return 0;
}

/*
 * Answers a texture operation with the SM75 interpreter's texel: a sample
 * gives (u, v, the handle as a float, 1 + l + o), l its bias or level (0
 * for none); a fetch gives (x, y, the handle, the level + o), its integer
 * operands made floats; o is the constant texel offset as u * 256 + v * 16
 * + r.  Only the 2D messages the compiler takes are modelled.  Nonzero on
 * a failure.
 */
static int
reference_texture(
	const struct drv_gpu_shader_ir *ir,
	const struct nvrtx_sm75_binary *binary,
	const struct test_run *run,
	const struct drv_gpu_shader_ir_inst *inst,
	uint32_t *v,
	struct sm75_reference_result *result)
{
	uint32_t handle;
	uint32_t message;
	uint32_t first;
	uint32_t count;
	uint32_t offset;
	float o;
	int found;

	(void)ir;

	/* The image's handle. */
	handle = reference_handle(binary, run, inst->location, inst->immediate, &found);
	if (found == 0) {
		result->failed = 1;
		result->why = "a texture the program does not list";
		return 1;
	}

	/* The 2D samples of their own operations: the offset's u in bits 11..8 and v in 7..4. */
	if (inst->op != DRV_GPU_IR_TEXTURE) {
		o = (float)(reference_nibble(inst->component, 8U) * 256 + reference_nibble(inst->component, 4U) * 16);
		v[inst->dst] = v[inst->src[0]];
		v[inst->dst + 1U] = v[inst->src[1]];
		v[inst->dst + 2U] = reference_bits((float)handle);
		v[inst->dst + 3U] = reference_bits(1.0f + o);
		if (inst->op != DRV_GPU_IR_SAMPLE)
			v[inst->dst + 3U] = reference_bits(1.0f + reference_float(v[inst->src[2]]) + o);
		return 0;
	}

	/* The general message: its parameters run from src[0], src[1] of them; the offset u, v, r in bits 11..8, 7..4, 3..0. */
	message = inst->component & 0x1fU;
	first = inst->src[0];
	count = inst->src[1];
	offset = inst->component >> 8;
	o = (float)(reference_nibble(offset, 8U) * 256 + reference_nibble(offset, 4U) * 16 + reference_nibble(offset, 0U));
	if (message == DRV_GPU_IR_TEXTURE_SAMPLE && count == 2U) {
		v[inst->dst] = v[first];
		v[inst->dst + 1U] = v[first + 1U];
		v[inst->dst + 2U] = reference_bits((float)handle);
		v[inst->dst + 3U] = reference_bits(1.0f + o);
		return 0;
	}
	if ((message == DRV_GPU_IR_TEXTURE_SAMPLE_BIAS || message == DRV_GPU_IR_TEXTURE_SAMPLE_LOD) && count == 3U) {
		v[inst->dst] = v[first + 1U];
		v[inst->dst + 1U] = v[first + 2U];
		v[inst->dst + 2U] = reference_bits((float)handle);
		v[inst->dst + 3U] = reference_bits(1.0f + reference_float(v[first]) + o);
		return 0;
	}
	if (message == DRV_GPU_IR_TEXTURE_LD && count == 3U) {
		v[inst->dst] = reference_bits((float)(int32_t)v[first]);
		v[inst->dst + 1U] = reference_bits((float)(int32_t)v[first + 1U]);
		v[inst->dst + 2U] = reference_bits((float)handle);
		v[inst->dst + 3U] = reference_bits((float)(int32_t)v[first + 2U] + o);
		return 0;
	}

	/* Any other message is not modelled. */
	result->failed = 1;
	result->why = "a texture message the reference does not model";
	return 1;
}

/* Reads one signed 4-bit field. */
static int32_t
reference_nibble(
	uint32_t bits,
	unsigned low)
{
	int32_t value;

	/* The four bits, sign-extended. */
	value = (int32_t)((bits >> low) & 0xfU);
	if (value >= 8)
		value -= 16;

	/* Succeeded: the field. */
	return value;
}

/* Reads the texture handle the dispatch wrote for an image (set, binding), from the root constant buffer. */
static uint32_t
reference_handle(
	const struct nvrtx_sm75_binary *binary,
	const struct test_run *run,
	uint32_t set,
	uint32_t binding,
	int *found)
{
	uint32_t index;

	/* The image's place in the program's list. */
	*found = 0;
	for (index = 0U; index < binary->texture_count; index++) {
		if (binary->textures[index].set != set || binary->textures[index].binding != binding)
			continue;
		*found = 1;
		return sm75_machine_cb_word(run, NVRTX_SM75_CB_ROOT, NVRTX_SM75_CB_TEXTURES + index * 4U);
	}

	/* Succeeded in finding none. */
	return 0U;
}

/* Finds the attribute address an input instruction reads (the same words both interpreters read). */
static uint32_t
reference_input_address(
	const struct drv_gpu_shader_ir *ir,
	const struct drv_gpu_shader_ir_inst *inst,
	int *found)
{
	*found = 1;

	/* A generic attribute or varying. */
	if (inst->location < 32U)
		return NVRTX_SM75_ATTR_GENERIC + inst->location * 16U + inst->component * 4U;

	/* The vertex stage's numbers. */
	if (ir->stage == DRV_GPU_STAGE_VERTEX && inst->location == DRV_GPU_SHADER_LOCATION_VERTEX_INDEX)
		return NVRTX_SM75_ATTR_VERTEX_ID;
	if (ir->stage == DRV_GPU_STAGE_VERTEX && inst->location == DRV_GPU_SHADER_LOCATION_INSTANCE_INDEX)
		return NVRTX_SM75_ATTR_INSTANCE_ID;

	/* The fragment's position. */
	if (ir->stage == DRV_GPU_STAGE_FRAGMENT && inst->location == DRV_GPU_SHADER_LOCATION_FRAG_COORD)
		return NVRTX_SM75_ATTR_POSITION + inst->component * 4U;

	/* Succeeded in finding none. */
	*found = 0;
	return 0U;
}

/* Reports the constant buffer of a uniform buffer: the uniform buffers take 1, 2, ... in the list's order. */
static uint32_t
reference_cb(
	const struct drv_gpu_shader_ir *ir,
	uint32_t uniform)
{
	uint32_t index;
	uint32_t next;

	/* Counts the uniform buffers before it. */
	next = NVRTX_SM75_CB_UNIFORM_FIRST;
	for (index = 0U; index < uniform && index < ir->uniform_count; index++) {
		if (ir->uniforms[index].kind == DRV_GPU_IR_UNIFORM_BLOCK)
			next++;
	}

	/* Succeeded: its constant buffer. */
	return next;
}

/* The Boolean of a truth: all ones or zero. */
static uint32_t
reference_bool(
	int truth)
{
	/* True. */
	if (truth != 0)
		return 0xffffffffU;

	/* False. */
	return 0U;
}

/* The float of 32 bits. */
static float
reference_float(
	uint32_t bits)
{
	float value;

	memcpy(&value, &bits, sizeof(value));
	return value;
}

/* The 32 bits of a float. */
static uint32_t
reference_bits(
	float value)
{
	uint32_t bits;

	memcpy(&bits, &value, sizeof(bits));
	return bits;
}
