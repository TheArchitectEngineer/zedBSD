/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws039-p007: the reference interpreter of the scalar IR (gfx10-reference.h).
 */

#include "plan/ws039/tests/gfx10-reference.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* The most IR instructions one workgroup runs, and the deepest loop nest. */
#define REFERENCE_STEPS		20000000U
#define REFERENCE_LOOPS		32U

/* One run: the program, its memory, the values of every lane and the lanes running. */
struct reference_run {
	const struct drv_gpu_shader_ir *ir;
	struct reference_memory *memory;
	const uint32_t *group_id;
	uint32_t lanes;
	uint32_t *values;
	uint8_t *active;
	int *undefined;
	uint8_t *shared;
	uint32_t shared_bytes;
};

static void execute(struct reference_run *run, const struct drv_gpu_shader_ir_inst *inst, uint32_t lane);
static uint32_t *value(struct reference_run *run, uint32_t index, uint32_t lane);
static float as_float(uint32_t bits);
static uint32_t as_bits(float value);
static uint32_t truth(int condition);
static uint32_t half_bits(float value);
static float half_value(uint32_t bits);
static uint32_t storage_word(struct reference_run *run, uint32_t uniform, uint32_t offset);
static void storage_store(struct reference_run *run, uint32_t uniform, uint32_t offset, uint32_t data);
static uint32_t atomic_apply(uint32_t operation, uint32_t old, uint32_t data, uint32_t compare);

/* Runs one compute workgroup of a parsed program. */
const char *
reference_run_group(
	const struct drv_gpu_shader_ir *ir,
	const uint32_t *group_id,
	struct reference_memory *memory,
	int *undefined)
{
	struct reference_run run;
	const struct drv_gpu_shader_ir_inst *inst;
	uint32_t begins[REFERENCE_LOOPS];
	uint8_t *saved[REFERENCE_LOOPS];
	uint32_t depth;
	uint32_t pc;
	uint32_t steps;
	uint32_t lane;
	int any;

	*undefined = 0;
	memset(&run, 0, sizeof(run));
	run.ir = ir;
	run.memory = memory;
	run.group_id = group_id;
	run.undefined = undefined;
	run.lanes = ir->local_size[0] * ir->local_size[1] * ir->local_size[2];
	run.shared_bytes = ir->shared_bytes;
	run.shared = calloc(ir->shared_bytes + 4U, 1U);

	/* The values of every lane, and every lane running. */
	run.values = calloc((size_t)ir->value_count * run.lanes + 1U, sizeof(*run.values));
	run.active = malloc(run.lanes);
	if (run.values == NULL || run.active == NULL) {
		free(run.values);
		free(run.active);
		return "no memory";
	}
	memset(run.active, 1, run.lanes);

	/* The instructions in order for every running lane, the loops repeated. */
	depth = 0U;
	pc = 0U;
	for (steps = 0U; pc < ir->instruction_count; steps++) {
		if (steps == REFERENCE_STEPS) {
			free(run.values);
			free(run.active);
			return "the reference runs too long";
		}
		inst = &ir->instructions[pc];

		/* A loop's start remembers the lanes that enter. */
		if (inst->op == DRV_GPU_IR_LOOP_BEGIN) {
			if (depth == REFERENCE_LOOPS) {
				free(run.values);
				free(run.active);
				return "loops nested too deep";
			}
			begins[depth] = pc;
			saved[depth] = malloc(run.lanes);
			memcpy(saved[depth], run.active, run.lanes);
			depth++;
			pc++;
			continue;
		}

		/* A loop's end: the lanes whose condition is false stop; the loop runs again while any lane is left. */
		if (inst->op == DRV_GPU_IR_LOOP_END) {
			any = 0;
			for (lane = 0U; lane < run.lanes; lane++) {
				if (run.active[lane] == 0U)
					continue;
				if (*value(&run, inst->src[0], lane) == 0U)
					run.active[lane] = 0U;
				else
					any = 1;
			}
			if (any != 0) {
				pc = begins[depth - 1U] + 1U;
				continue;
			}
			depth--;
			memcpy(run.active, saved[depth], run.lanes);
			free(saved[depth]);
			pc++;
			continue;
		}

		/* Every other instruction runs for each running lane. */
		for (lane = 0U; lane < run.lanes; lane++) {
			if (run.active[lane] != 0U)
				execute(&run, inst, lane);
		}
		pc++;
	}

	free(run.values);
	free(run.active);
	free(run.shared);
	return NULL;
}

/* Runs one instruction for one lane. */
static void
execute(
	struct reference_run *run,
	const struct drv_gpu_shader_ir_inst *inst,
	uint32_t lane)
{
	const struct drv_gpu_shader_ir *ir;
	uint32_t a;
	uint32_t b;
	uint32_t c;
	uint32_t *d;
	uint32_t linear;
	float fa;
	float fb;
	float t;

	ir = run->ir;
	a = inst->src[0] < ir->value_count ? *value(run, inst->src[0], lane) : 0U;
	b = inst->src[1] < ir->value_count ? *value(run, inst->src[1], lane) : 0U;
	c = inst->src[2] < ir->value_count ? *value(run, inst->src[2], lane) : 0U;
	d = inst->dst < ir->value_count ? value(run, inst->dst, lane) : NULL;
	fa = as_float(a);
	fb = as_float(b);

	/* Picks the operation. */
	switch (inst->op) {
	case DRV_GPU_IR_NOP:
	case DRV_GPU_IR_SKIP_BEGIN:
	case DRV_GPU_IR_SKIP_END:
		break;
	case DRV_GPU_IR_CONST:
	case DRV_GPU_IR_ICONST:
	case DRV_GPU_IR_BOOL:
		*d = inst->immediate;
		break;
	case DRV_GPU_IR_MOVE:
		*d = a;
		break;
	case DRV_GPU_IR_FADD:
		*d = as_bits(fa + fb);
		break;
	case DRV_GPU_IR_FSUB:
		*d = as_bits(fa - fb);
		break;
	case DRV_GPU_IR_FMUL:
		*d = as_bits(fa * fb);
		break;
	case DRV_GPU_IR_FNEG:
		*d = a ^ 0x80000000U;
		break;
	case DRV_GPU_IR_FABS:
		*d = a & 0x7fffffffU;
		break;
	case DRV_GPU_IR_FMIN:
		*d = as_bits(fminf(fa, fb));
		break;
	case DRV_GPU_IR_FMAX:
		*d = as_bits(fmaxf(fa, fb));
		break;
	case DRV_GPU_IR_RSQ:
		*d = as_bits(1.0f / sqrtf(fa));
		break;
	case DRV_GPU_IR_RCP:
		*d = as_bits(1.0f / fa);
		break;
	case DRV_GPU_IR_SQRT:
		*d = as_bits(sqrtf(fa));
		break;
	case DRV_GPU_IR_EXP2:
		*d = as_bits(exp2f(fa));
		break;
	case DRV_GPU_IR_LOG2:
		*d = as_bits(log2f(fa));
		break;
	case DRV_GPU_IR_SIN:
		*d = as_bits(sinf(fa));
		break;
	case DRV_GPU_IR_COS:
		*d = as_bits(cosf(fa));
		break;
	case DRV_GPU_IR_FLOOR:
		*d = as_bits(floorf(fa));
		break;
	case DRV_GPU_IR_FRACT:
		*d = as_bits(fa - floorf(fa));
		break;
	case DRV_GPU_IR_FTRUNC:
		*d = as_bits(truncf(fa));
		break;
	case DRV_GPU_IR_FROUND_EVEN:
		*d = as_bits(rintf(fa));
		break;
	case DRV_GPU_IR_FLT:
		*d = truth(fa < fb);
		break;
	case DRV_GPU_IR_FGE:
		*d = truth(fa >= fb);
		break;
	case DRV_GPU_IR_FEQ:
		*d = truth(fa == fb);
		break;
	case DRV_GPU_IR_FNEU:
		*d = truth(!(fa == fb));
		break;
	case DRV_GPU_IR_AND:
	case DRV_GPU_IR_IAND:
		*d = a & b;
		break;
	case DRV_GPU_IR_OR:
	case DRV_GPU_IR_IOR:
		*d = a | b;
		break;
	case DRV_GPU_IR_IXOR:
		*d = a ^ b;
		break;
	case DRV_GPU_IR_NOT:
	case DRV_GPU_IR_INOT:
		*d = ~a;
		break;
	case DRV_GPU_IR_SELECT:
		*d = a != 0U ? b : c;
		break;
	case DRV_GPU_IR_IADD:
		*d = a + b;
		break;
	case DRV_GPU_IR_ISUB:
		*d = a - b;
		break;
	case DRV_GPU_IR_IMUL:
		*d = a * b;
		break;
	case DRV_GPU_IR_INEG:
		*d = 0U - a;
		break;
	case DRV_GPU_IR_SHL:
		*d = a << (b & 31U);
		break;
	case DRV_GPU_IR_SHR:
		*d = a >> (b & 31U);
		break;
	case DRV_GPU_IR_ASR:
		*d = (uint32_t)((int32_t)a >> (b & 31U));
		break;
	case DRV_GPU_IR_I2F:
		*d = as_bits((float)(int32_t)a);
		break;
	case DRV_GPU_IR_U2F:
		*d = as_bits((float)a);
		break;
	case DRV_GPU_IR_F2I:
		/* Outside the integers (NaN included) the result is undefined. */
		t = truncf(fa);
		if (!(t >= -2147483648.0f && t < 2147483648.0f)) {
			*run->undefined = 1;
			*d = 0U;
		} else {
			*d = (uint32_t)(int32_t)t;
		}
		break;
	case DRV_GPU_IR_F2U:
		t = truncf(fa);
		if (!(t >= 0.0f && t < 4294967296.0f)) {
			*run->undefined = 1;
			*d = 0U;
		} else {
			*d = (uint32_t)t;
		}
		break;
	case DRV_GPU_IR_ILT:
		*d = truth((int32_t)a < (int32_t)b);
		break;
	case DRV_GPU_IR_IGE:
		*d = truth((int32_t)a >= (int32_t)b);
		break;
	case DRV_GPU_IR_ULT:
		*d = truth(a < b);
		break;
	case DRV_GPU_IR_UGE:
		*d = truth(a >= b);
		break;
	case DRV_GPU_IR_IEQ:
		*d = truth(a == b);
		break;
	case DRV_GPU_IR_INE:
		*d = truth(a != b);
		break;
	case DRV_GPU_IR_PACK_HALF:
		*d = half_bits(fa) | (half_bits(fb) << 16);
		break;
	case DRV_GPU_IR_UNPACK_HALF:
		*d = as_bits(half_value((a >> (16U * inst->component)) & 0xffffU));
		break;
	case DRV_GPU_IR_LOAD_PUSH:
		*d = 0U;
		if (inst->immediate + 4U <= run->memory->push_size)
			memcpy(d, run->memory->push + inst->immediate, 4U);
		break;
	case DRV_GPU_IR_LOAD_UBO:
		*d = storage_word(run, inst->location, inst->immediate);
		break;
	case DRV_GPU_IR_LOAD_SYSTEM:
		linear = lane;
		switch (inst->component) {
		case DRV_GPU_IR_SYSTEM_LOCAL_ID_X:
			*d = linear % ir->local_size[0];
			break;
		case DRV_GPU_IR_SYSTEM_LOCAL_ID_Y:
			*d = (linear / ir->local_size[0]) % ir->local_size[1];
			break;
		case DRV_GPU_IR_SYSTEM_LOCAL_ID_Z:
			*d = linear / (ir->local_size[0] * ir->local_size[1]);
			break;
		case DRV_GPU_IR_SYSTEM_LOCAL_INDEX:
			*d = linear;
			break;
		case DRV_GPU_IR_SYSTEM_GROUP_ID_X:
		case DRV_GPU_IR_SYSTEM_GROUP_ID_Y:
		case DRV_GPU_IR_SYSTEM_GROUP_ID_Z:
			*d = run->group_id[inst->component - DRV_GPU_IR_SYSTEM_GROUP_ID_X];
			break;
		default:
			*d = 0U;
			break;
		}
		break;
	case DRV_GPU_IR_LOAD_STORAGE:
		/* A guarded load leaves the lanes outside its predicate as they were. */
		if (inst->component != 0U && b == 0U)
			break;
		*d = storage_word(run, inst->location, a);
		break;
	case DRV_GPU_IR_STORE_STORAGE:
		if (inst->component != 0U && c == 0U)
			break;
		storage_store(run, inst->location, a, b);
		break;
	case DRV_GPU_IR_STORAGE_SIZE:
		*d = run->memory->size[inst->location];
		break;
	case DRV_GPU_IR_UDIV:
	case DRV_GPU_IR_UMOD:
		if (b == 0U) {
			*run->undefined = 1;
			*d = 0U;
		} else if (inst->op == DRV_GPU_IR_UDIV) {
			*d = a / b;
		} else {
			*d = a % b;
		}
		break;
	case DRV_GPU_IR_IDIV:
	case DRV_GPU_IR_IREM:
		if (b == 0U || (a == 0x80000000U && b == 0xffffffffU)) {
			*run->undefined = 1;
			*d = 0U;
		} else if (inst->op == DRV_GPU_IR_IDIV) {
			*d = (uint32_t)((int32_t)a / (int32_t)b);
		} else {
			*d = (uint32_t)((int32_t)a % (int32_t)b);
		}
		break;
	case DRV_GPU_IR_LOAD_SHARED:
		if (inst->component != 0U && b == 0U)
			break;
		*d = 0U;
		if ((uint64_t)a + 4U <= run->shared_bytes)
			memcpy(d, run->shared + a, 4U);
		else
			*run->undefined = 1;
		break;
	case DRV_GPU_IR_STORE_SHARED:
		if (inst->component != 0U && c == 0U)
			break;
		if ((uint64_t)a + 4U <= run->shared_bytes)
			memcpy(run->shared + a, &b, 4U);
		else
			*run->undefined = 1;
		break;
	case DRV_GPU_IR_ATOMIC:
		/* The predicate follows the values (and the compare). */
		if (inst->component == DRV_GPU_IR_ATOMIC_PREDICATED) {
			linear = inst->immediate == DRV_GPU_IR_ATOMIC_CMPXCHG ? inst->src[3] : inst->src[2];
			if (*value(run, linear, lane) == 0U)
				break;
		}
		if (inst->location == DRV_GPU_IR_LOCATION_SHARED) {
			uint32_t old = 0U;
			uint32_t updated;

			if ((uint64_t)a + 4U > run->shared_bytes) {
				*run->undefined = 1;
				break;
			}
			memcpy(&old, run->shared + a, 4U);
			updated = atomic_apply(inst->immediate, old, b, c);
			memcpy(run->shared + a, &updated, 4U);
			*d = old;
		} else {
			uint32_t old = storage_word(run, inst->location, a);

			storage_store(run, inst->location, a, atomic_apply(inst->immediate, old, b, c));
			*d = old;
		}
		break;
	case DRV_GPU_IR_BARRIER:
	case DRV_GPU_IR_FENCE:
		/* The lanes run in lockstep, so every one has come and every access is done. */
		break;
	default:
		*run->undefined = 1;
		break;
	}
}

/* The value of one lane. */
static uint32_t *
value(
	struct reference_run *run,
	uint32_t index,
	uint32_t lane)
{
	return &run->values[(size_t)index * run->lanes + lane];
}

static float
as_float(
	uint32_t bits)
{
	float result;

	memcpy(&result, &bits, sizeof(result));
	return result;
}

static uint32_t
as_bits(
	float number)
{
	uint32_t bits;

	memcpy(&bits, &number, sizeof(bits));
	return bits;
}

/* The Boolean of a condition: all ones or zero. */
static uint32_t
truth(
	int condition)
{
	return condition ? 0xffffffffU : 0U;
}

/* The 16-bit float nearest a float. */
static uint32_t
half_bits(
	float number)
{
	_Float16 half;
	uint16_t bits;

	half = (_Float16)number;
	memcpy(&bits, &half, sizeof(bits));
	return bits;
}

/* The float of a 16-bit float. */
static float
half_value(
	uint32_t bits)
{
	_Float16 half;
	uint16_t low;

	low = (uint16_t)bits;
	memcpy(&half, &low, sizeof(half));
	return (float)half;
}

/* Reads a word of a uniform's bytes: 0 past its end. */
static uint32_t
storage_word(
	struct reference_run *run,
	uint32_t uniform,
	uint32_t offset)
{
	uint32_t word;

	word = 0U;
	if (uniform < REFERENCE_UNIFORMS && (uint64_t)offset + 4U <= run->memory->size[uniform])
		memcpy(&word, run->memory->bytes[uniform] + offset, 4U);
	return word;
}

/* Writes a word of a uniform's bytes: nothing past its end. */
static void
storage_store(
	struct reference_run *run,
	uint32_t uniform,
	uint32_t offset,
	uint32_t data)
{
	if (uniform < REFERENCE_UNIFORMS && (uint64_t)offset + 4U <= run->memory->size[uniform])
		memcpy(run->memory->bytes[uniform] + offset, &data, 4U);
}

/* The new value of an atomic operation (DRV_GPU_IR_ATOMIC_*). */
static uint32_t
atomic_apply(
	uint32_t operation,
	uint32_t old,
	uint32_t data,
	uint32_t compare)
{
	switch (operation) {
	case DRV_GPU_IR_ATOMIC_AND:
		return old & data;
	case DRV_GPU_IR_ATOMIC_OR:
		return old | data;
	case DRV_GPU_IR_ATOMIC_XOR:
		return old ^ data;
	case DRV_GPU_IR_ATOMIC_XCHG:
		return data;
	case DRV_GPU_IR_ATOMIC_ADD:
		return old + data;
	case DRV_GPU_IR_ATOMIC_SUB:
		return old - data;
	case DRV_GPU_IR_ATOMIC_SMAX:
		return (int32_t)data > (int32_t)old ? data : old;
	case DRV_GPU_IR_ATOMIC_SMIN:
		return (int32_t)data < (int32_t)old ? data : old;
	case DRV_GPU_IR_ATOMIC_UMAX:
		return data > old ? data : old;
	case DRV_GPU_IR_ATOMIC_UMIN:
		return data < old ? data : old;
	case DRV_GPU_IR_ATOMIC_CMPXCHG:
		return old == compare ? data : old;
	default:
		return old;
	}
}
