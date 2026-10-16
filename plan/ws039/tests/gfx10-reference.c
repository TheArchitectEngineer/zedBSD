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
	struct reference_graphics *graphics;
};

static const char *run_lanes(struct reference_run *run);
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
static int texture_message(struct reference_run *run, const struct drv_gpu_shader_ir_inst *inst, uint32_t lane, struct texture_request *request);
static int signed_nibble(uint32_t bits);

/* Runs one compute workgroup of a parsed program. */
const char *
reference_run_group(
	const struct drv_gpu_shader_ir *ir,
	const uint32_t *group_id,
	struct reference_memory *memory,
	int *undefined)
{
	struct reference_run run;

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
	return run_lanes(&run);
}

/* Runs one wave of a vertex or pixel program. */
const char *
reference_run_graphics(
	const struct drv_gpu_shader_ir *ir,
	struct reference_memory *memory,
	struct reference_graphics *graphics,
	int *undefined)
{
	static const uint32_t no_group[3] = { 0U, 0U, 0U };
	struct reference_run run;

	*undefined = 0;
	memset(&run, 0, sizeof(run));
	memset(graphics->output, 0, sizeof(graphics->output));
	memset(graphics->written, 0, sizeof(graphics->written));
	memset(graphics->killed, 0, sizeof(graphics->killed));
	run.ir = ir;
	run.memory = memory;
	run.group_id = no_group;
	run.undefined = undefined;
	run.lanes = graphics->lanes;
	run.graphics = graphics;
	run.shared = calloc(4U, 1U);
	return run_lanes(&run);
}

/* Runs the program over the lanes in lockstep. */
static const char *
run_lanes(
	struct reference_run *run_state)
{
	const struct drv_gpu_shader_ir *ir;
	const struct drv_gpu_shader_ir_inst *inst;
	uint32_t begins[REFERENCE_LOOPS];
	uint8_t *saved[REFERENCE_LOOPS];
	uint32_t depth;
	uint32_t pc;
	uint32_t steps;
	uint32_t lane;
	int any;
	struct reference_run run;

	run = *run_state;
	ir = run.ir;
	run.values = calloc((size_t)ir->value_count * run.lanes + 1U, sizeof(*run.values));
	run.active = malloc(run.lanes);
	if (run.values == NULL || run.active == NULL) {
		free(run.values);
		free(run.active);
		return "no memory";
	}
	memset(run.active, 1, run.lanes);
	if (run.graphics != NULL) {
		for (lane = 0U; lane < run.lanes; lane++)
			run.active[lane] = run.graphics->run[lane];
	}

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
			if (run.graphics != NULL) {
				for (lane = 0U; lane < run.lanes; lane++) {
					if (run.graphics->killed[lane] != 0U)
						run.active[lane] = 0U;
				}
			}
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
		if (run->graphics != NULL && run->graphics->helper[lane] != 0U)
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

			if (run->graphics != NULL && run->graphics->helper[lane] != 0U) {
				*d = 0U;
				break;
			}
			storage_store(run, inst->location, a, atomic_apply(inst->immediate, old, b, c));
			*d = old;
		}
		break;
	case DRV_GPU_IR_LOAD_INPUT:
		if (run->graphics == NULL) {
			*run->undefined = 1;
			break;
		}
		if (ir->stage == DRV_GPU_STAGE_VERTEX) {
			if (inst->location == 64U)
				*d = run->graphics->vertex_index[lane];
			else if (inst->location == 65U)
				*d = run->graphics->instance_index[lane];
			else
				*d = run->graphics->input[lane][inst->location][inst->component];
		} else if (inst->location == 67U) {
			*d = run->graphics->frag_coord[lane][inst->component];
		} else if (inst->location == 66U) {
			*d = run->graphics->front_facing[lane];
		} else if (inst->location == 68U) {
			*d = run->graphics->point_coord[lane][inst->component & 1U];
		} else if (inst->location == 69U) {
			*d = run->graphics->primitive_id[lane];
		} else {
			*d = run->graphics->input[lane][inst->location][inst->component];
		}
		break;
	case DRV_GPU_IR_STORE_OUTPUT:
		if (run->graphics == NULL) {
			*run->undefined = 1;
			break;
		}
		linear = inst->location;
		if (linear == DRV_GPU_IR_LOCATION_POSITION)
			linear = REFERENCE_SLOT_POSITION;
		else if (linear == DRV_GPU_IR_LOCATION_POINT_SIZE)
			linear = REFERENCE_SLOT_POINT_SIZE;
		else if (linear == DRV_GPU_IR_LOCATION_SECOND_COLOR)
			linear = REFERENCE_SLOT_SECOND_COLOR;
		if (linear >= REFERENCE_SLOTS) {
			*run->undefined = 1;
			break;
		}
		run->graphics->output[lane][linear][inst->component] = a;
		run->graphics->written[lane][linear][inst->component] = 1U;
		break;
	case DRV_GPU_IR_SAMPLE:
	case DRV_GPU_IR_SAMPLE_BIAS:
	case DRV_GPU_IR_SAMPLE_LOD:
	case DRV_GPU_IR_TEXTURE:
		{
			struct texture_request request;
			uint32_t out[4];
			uint32_t k;

			if (run->graphics == NULL || run->graphics->sample == NULL) {
				*run->undefined = 1;
				break;
			}
			memset(&request, 0, sizeof(request));
			if (inst->op == DRV_GPU_IR_TEXTURE) {
				if (texture_message(run, inst, lane, &request) != 0) {
					*run->undefined = 1;
					return;
				}
			} else {
				uint32_t offset_bits = (inst->component >> 4) & 0xffU;

				request.u = a;
				request.v = b;
				if (inst->op != DRV_GPU_IR_SAMPLE) {
					request.has_lod = 1;
					request.lod = *value(run, inst->src[2], lane);
				}
				request.offset_u = signed_nibble(offset_bits >> 4);
				request.offset_v = signed_nibble(offset_bits);
			}
			run->graphics->sample(run->graphics->sample_context, inst->location, inst->immediate, &request, out);
			for (k = 0U; k < 4U; k++)
				*value(run, inst->dst + k, lane) = out[k];
		}
		break;
	case DRV_GPU_IR_DDX:
	case DRV_GPU_IR_DDX_FINE:
	case DRV_GPU_IR_DDY:
	case DRV_GPU_IR_DDY_FINE:
		{
			uint32_t quad = lane & ~3U;
			uint32_t from;
			uint32_t to;
			uint32_t k;

			/* The quad's lanes: x is bit 0 of the lane, y bit 1; every lane of the quad must still run. */
			if (run->graphics == NULL || quad + 3U >= run->lanes) {
				*run->undefined = 1;
				break;
			}
			for (k = 0U; k < 4U; k++) {
				if (run->active[quad + k] == 0U)
					*run->undefined = 1;
			}
			if (inst->op == DRV_GPU_IR_DDX) {
				from = quad;
				to = quad + 1U;
			} else if (inst->op == DRV_GPU_IR_DDY) {
				from = quad;
				to = quad + 2U;
			} else if (inst->op == DRV_GPU_IR_DDX_FINE) {
				from = quad + (lane & 2U);
				to = from + 1U;
			} else {
				from = quad + (lane & 1U);
				to = from + 2U;
			}
			*d = as_bits(as_float(*value(run, inst->src[0], to)) - as_float(*value(run, inst->src[0], from)));
		}
		break;
	case DRV_GPU_IR_KILL:
		if (a != 0U && run->graphics != NULL) {
			run->graphics->killed[lane] = 1U;
			run->active[lane] = 0U;
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

/* A 4-bit two's complement number, -8 .. 7. */
static int
signed_nibble(
	uint32_t bits)
{
	int number;

	number = (int)(bits & 0xfU);
	if (number >= 8)
		number -= 16;
	return number;
}

/*
 * Reads a TEXTURE message's parameters into a request by its type and the
 * image's shape (ir.h): a 3D image's r; an array's layer, the nearest
 * integer to the one given (a fetch's is one); a cube's direction turned
 * into the face it points at and the coordinates on it, by the table of
 * the Vulkan specification ("Cube Map Face Selection"), computed with the
 * arithmetic the hardware uses (sc * (1 / |2 ma|) + 1.5, less 1) so that
 * the coordinates are bit for bit the GFX10 interpreter's.  Returns
 * nonzero for a message the reference does not model.
 */
static int
texture_message(
	struct reference_run *run,
	const struct drv_gpu_shader_ir_inst *inst,
	uint32_t lane,
	struct texture_request *request)
{
	uint32_t p[DRV_GPU_IR_TEXTURE_MAX_PARAMS];
	uint32_t message;
	uint32_t kind;
	uint32_t count;
	uint32_t next;
	uint32_t k;
	int arrayed;
	float coordinate[3];
	float x;
	float y;
	float z;
	float sc;
	float tc;
	float ma;

	message = inst->component & DRV_GPU_IR_TEXTURE_MESSAGE_MASK;
	kind = inst->image_shape & DRV_GPU_IR_IMAGE_KIND_MASK;
	arrayed = (inst->image_shape & DRV_GPU_IR_IMAGE_ARRAYED) != 0U;
	if (kind == DRV_GPU_IR_IMAGE_BUFFER || (inst->image_shape & DRV_GPU_IR_IMAGE_MULTISAMPLED) != 0U)
		return 1;
	for (k = 0U; k < inst->src[1] && k < DRV_GPU_IR_TEXTURE_MAX_PARAMS; k++)
		p[k] = *value(run, inst->src[0] + k, lane);
	count = 2U;
	if (kind == DRV_GPU_IR_IMAGE_3D || kind == DRV_GPU_IR_IMAGE_CUBE)
		count = 3U;
	request->offset_u = signed_nibble(inst->component >> 16);
	request->offset_v = signed_nibble(inst->component >> 12);
	request->offset_r = signed_nibble(inst->component >> 8);

	/* The parameters before the coordinate. */
	next = 0U;
	switch (message) {
	case DRV_GPU_IR_TEXTURE_SAMPLE:
		break;
	case DRV_GPU_IR_TEXTURE_SAMPLE_BIAS:
	case DRV_GPU_IR_TEXTURE_SAMPLE_LOD:
		request->has_lod = 1;
		request->lod = p[next++];
		break;
	case DRV_GPU_IR_TEXTURE_SAMPLE_COMPARE:
		request->compare = 1;
		request->reference = p[next++];
		break;
	case DRV_GPU_IR_TEXTURE_SAMPLE_BIAS_COMPARE:
	case DRV_GPU_IR_TEXTURE_SAMPLE_LOD_COMPARE:
		request->compare = 1;
		request->reference = p[next++];
		request->has_lod = 1;
		request->lod = p[next++];
		break;
	case DRV_GPU_IR_TEXTURE_SAMPLE_DERIVS:
		request->has_gradients = 1;
		break;
	case DRV_GPU_IR_TEXTURE_SAMPLE_DERIV_COMPARE:
		request->compare = 1;
		request->reference = p[next++];
		request->has_gradients = 1;
		break;
	case DRV_GPU_IR_TEXTURE_GATHER4:
		request->gather = 1;
		request->channel = DRV_GPU_IR_TEXTURE_CHANNEL(inst->component);
		break;
	case DRV_GPU_IR_TEXTURE_GATHER4_COMPARE:
		request->gather = 1;
		request->compare = 1;
		request->reference = p[next++];
		break;
	case DRV_GPU_IR_TEXTURE_LD:
		/* u, v, the level (only 0 is modelled), then r or the layer, integers. */
		request->fetch = 1;
		request->u = p[0];
		request->v = p[1];
		request->offset_u = 0;
		request->offset_v = 0;
		request->offset_r = 0;
		if (kind == DRV_GPU_IR_IMAGE_3D)
			request->r = p[3];
		if (arrayed != 0)
			request->layer = (int32_t)p[3];
		return 0;
	case DRV_GPU_IR_TEXTURE_RESINFO:
		request->size_query = 1;
		request->level = p[0];
		return 0;
	default:
		return 1;
	}

	/* The coordinate, each component followed by its gradients when the message has them. */
	for (k = 0U; k < count; k++) {
		coordinate[k] = as_float(p[next]);
		next++;
		if (request->has_gradients != 0) {
			request->gradients[k] = p[next];
			request->gradients[count + k] = p[next + 1U];
			next += 2U;
		}
	}
	request->u = as_bits(coordinate[0]);
	request->v = as_bits(coordinate[1]);
	if (kind == DRV_GPU_IR_IMAGE_3D)
		request->r = as_bits(coordinate[2]);

	/* The layer of an array: the nearest integer, ties to even. */
	if (arrayed != 0)
		request->layer = (int32_t)rintf(as_float(p[next]));

	/* A cube's face and the coordinates on it. */
	if (kind == DRV_GPU_IR_IMAGE_CUBE) {
		x = coordinate[0];
		y = coordinate[1];
		z = coordinate[2];
		if (fabsf(z) >= fabsf(x) && fabsf(z) >= fabsf(y)) {
			request->face = z < 0.0f ? 5 : 4;
			sc = z < 0.0f ? -x : x;
			tc = -y;
			ma = z;
		} else if (fabsf(y) >= fabsf(x)) {
			request->face = y < 0.0f ? 3 : 2;
			sc = x;
			tc = y < 0.0f ? -z : z;
			ma = y;
		} else {
			request->face = x < 0.0f ? 1 : 0;
			sc = x < 0.0f ? z : -z;
			tc = -y;
			ma = x;
		}
		ma = 1.0f / fabsf(2.0f * ma);
		request->u = as_bits(fmaf(sc, ma, 1.5f) - 1.0f);
		request->v = as_bits(fmaf(tc, ma, 1.5f) - 1.0f);
	}
	return 0;
}
