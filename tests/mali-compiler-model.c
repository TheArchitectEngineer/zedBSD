/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The one-lane model of Valhall and the IR reference of the Mali
 * compiler's host check.
 *
 * The reference runs the shared IR of a module for one invocation.  The
 * model runs the compiled program's words for one lane: each word is
 * named by the generated ISA table (the decoder of the host test) and
 * executed by that name; the fields are read where the ISA description
 * puts them.  The model also checks the program's waits: a register a
 * message writes may not be read or written before the program waits on
 * the message's slot, ATEST needs the wait0126 flow before it and BLEND
 * the wait flow.  Both sides get the same random attributes, varyings,
 * push constants, buffers and textures; the outputs must agree.
 *
 * The model's arithmetic is that of the host's floats.  FREXPE, FLOGD,
 * FADD_LSCALE and FEXP are modelled as the logarithm and exponent
 * sequences the compiler writes expect them (an inference, not a
 * documented behaviour).  Nothing here is evidence about the GPU.
 */

#include "tests/mali-compiler-model.h"

#include "drivers/gpu/compiler/ir.h"
#include "drivers/gpu/compiler/spirv.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* One instruction of the generated ISA table (as in the host test). */
struct isa_entry {
	const char *name;
	uint64_t mask;
	uint64_t value;
	uint32_t sources;
	uint8_t source_flags[4];
	uint16_t source_sizes[4];
	uint32_t dests;
	uint32_t staging_read;
	uint32_t staging_write;
	uint32_t message;
};

static const struct isa_entry model_table[] = {
#include "tests/mali-compiler-isa-table.inc"
};

static const uint32_t model_immediates[32] = ISA_TABLE_IMMEDIATES;

#define WORDS		1024U
#define BUFFERS		MALI_SHADER_BUFFERS
#define OUTPUT_BASE	0x7000000000ULL
#define BUFFER_BASE	0x1000000000ULL
#define SHARED_BASE	0x2000000000ULL
#define OUTPUT_INDEX	0x40U
#define MAX_STEPS	200000U

/* The inputs of one trial, shared by the reference and the model. */
struct trial {
	uint32_t attributes[16][4];
	uint32_t varyings[16][4];
	uint32_t push[64];
	uint32_t viewport[6];
	uint32_t buffers[BUFFERS][WORDS];
	uint32_t shared[WORDS];
	uint32_t local_id[3];
	uint32_t group_id[3];
	uint32_t vertex_id;
	uint32_t instance_id;
	uint32_t frag_x;
	uint32_t frag_y;
};

/* What one side produced. */
struct result {
	uint32_t position[4];
	uint32_t varyings[16][4];
	uint32_t colors[8][4];
	uint32_t color_written;
	int killed;
	uint32_t buffers[BUFFERS][WORDS];
	const char *why;
};

static uint32_t random_state;

static uint32_t next_random(void);
static float as_float(uint32_t bits);
static uint32_t as_bits(float value);
static uint32_t to_half(float value);
static float from_half(uint32_t half);
static void texture_value(uint32_t slot, float u, float v, uint32_t *out);
static int find_buffer(const struct mali_shader_program *program, const struct drv_gpu_shader_ir *ir, uint32_t uniform);
static int find_texture(const struct mali_shader_program *program, uint32_t set, uint32_t binding);
static void run_reference(const struct drv_gpu_shader_ir *ir, const struct mali_shader_program *program, const struct trial *trial, struct result *result);
static void run_model(const uint8_t *code, size_t length, uint32_t stage, const struct mali_shader_program *program, const struct trial *trial, struct result *result);
static int close_enough(uint32_t a, uint32_t b);

/* A small linear congruential generator, the same for every run. */
static uint32_t
next_random(void)
{
	random_state = random_state * 1664525U + 1013904223U;
	return random_state;
}

static float
as_float(
	uint32_t bits)
{
	float value;

	memcpy(&value, &bits, 4U);
	return value;
}

static uint32_t
as_bits(
	float value)
{
	uint32_t bits;

	memcpy(&bits, &value, 4U);
	return bits;
}

/* Rounds a float to the nearest 16-bit float (ties to even). */
static uint32_t
to_half(
	float value)
{
	uint32_t bits;
	uint32_t sign;
	int exponent;
	uint32_t mantissa;
	uint32_t half;
	uint32_t round;

	bits = as_bits(value);
	sign = (bits >> 16) & 0x8000U;
	exponent = (int)((bits >> 23) & 0xFFU) - 127 + 15;
	mantissa = bits & 0x7FFFFFU;
	if (((bits >> 23) & 0xFFU) == 0xFFU)
		return sign | 0x7C00U | (mantissa != 0U ? 0x200U : 0U);
	if (exponent >= 31)
		return sign | 0x7C00U;
	if (exponent <= 0) {
		if (exponent < -10)
			return sign;
		mantissa |= 0x800000U;
		round = (uint32_t)(14 - exponent);
		half = mantissa >> round;
		if ((mantissa >> (round - 1U)) & 1U) {
			if ((mantissa & ((1U << (round - 1U)) - 1U)) != 0U || (half & 1U) != 0U)
				half++;
		}
		return sign | half;
	}
	half = sign | ((uint32_t)exponent << 10) | (mantissa >> 13);
	if ((mantissa & 0x1000U) != 0U && ((mantissa & 0xFFFU) != 0U || (half & 1U) != 0U))
		half++;
	return half;
}

static float
from_half(
	uint32_t half)
{
	uint32_t sign;
	uint32_t exponent;
	uint32_t mantissa;
	float value;

	sign = (half >> 15) & 1U;
	exponent = (half >> 10) & 0x1FU;
	mantissa = half & 0x3FFU;
	if (exponent == 0U) {
		value = ldexpf((float)mantissa, -24);
	} else if (exponent == 31U) {
		value = mantissa != 0U ? NAN : INFINITY;
	} else {
		value = ldexpf((float)(mantissa | 0x400U), (int)exponent - 25);
	}
	return sign != 0U ? -value : value;
}

/* The texel both sides return for a texture slot at (u, v). */
static void
texture_value(
	uint32_t slot,
	float u,
	float v,
	uint32_t *out)
{
	out[0] = as_bits(u * 0.5f + (float)slot * 0.125f);
	out[1] = as_bits(v * 0.25f - 0.5f);
	out[2] = as_bits(u * v);
	out[3] = as_bits(1.0f - u * 0.125f);
}

static int
find_buffer(
	const struct mali_shader_program *program,
	const struct drv_gpu_shader_ir *ir,
	uint32_t uniform)
{
	uint32_t index;

	if (uniform >= ir->uniform_count)
		return -1;
	for (index = 0U; index < program->buffer_count; index++) {
		if (program->buffers[index].set == ir->uniforms[uniform].set &&
		    program->buffers[index].binding == ir->uniforms[uniform].binding)
			return (int)index;
	}
	return -1;
}

static int
find_texture(
	const struct mali_shader_program *program,
	uint32_t set,
	uint32_t binding)
{
	uint32_t index;

	for (index = 0U; index < program->texture_count; index++) {
		if (program->textures[index].set == set && program->textures[index].binding == binding)
			return (int)index;
	}
	return -1;
}

static uint32_t
float_to_int(
	float value,
	int is_signed)
{
	if (isnan(value))
		return 0U;
	value = truncf(value);
	if (is_signed) {
		if (value >= 2147483647.0f)
			return 0x7FFFFFFFU;
		if (value <= -2147483648.0f)
			return 0x80000000U;
		return (uint32_t)(int32_t)value;
	}
	if (value <= 0.0f)
		return 0U;
	if (value >= 4294967295.0f)
		return 0xFFFFFFFFU;
	return (uint32_t)value;
}

/* Runs the IR for one invocation. */
static void
run_reference(
	const struct drv_gpu_shader_ir *ir,
	const struct mali_shader_program *program,
	const struct trial *trial,
	struct result *result)
{
	const struct drv_gpu_shader_ir_inst *inst;
	uint32_t *value;
	uint32_t loop_stack[16];
	uint32_t depth;
	uint32_t index;
	uint32_t steps;
	uint32_t a;
	uint32_t b;
	uint32_t texel[4];
	int slot;
	float fa;
	float fb;
	uint32_t word;
	uint32_t component;
	uint32_t message;

	value = calloc((size_t)ir->value_count + 8U, sizeof(*value));
	if (value == NULL) {
		result->why = "out of memory";
		return;
	}
	depth = 0U;
	steps = 0U;
	for (index = 0U; index < ir->instruction_count && steps < MAX_STEPS; index++, steps++) {
		inst = &ir->instructions[index];
		a = inst->src[0] <= ir->value_count ? value[inst->src[0]] : 0U;
		b = inst->src[1] <= ir->value_count ? value[inst->src[1]] : 0U;
		fa = as_float(a);
		fb = as_float(b);
#define DST(V) do { if (inst->dst <= ir->value_count) value[inst->dst] = (V); } while (0)
#define FDST(V) DST(as_bits(V))
#define BOOLV(C) ((C) ? 0xFFFFFFFFU : 0U)
		switch (inst->op) {
		case DRV_GPU_IR_NOP:
		case DRV_GPU_IR_SKIP_BEGIN:
		case DRV_GPU_IR_SKIP_END:
		case DRV_GPU_IR_FENCE:
		case DRV_GPU_IR_BARRIER:
			break;
		case DRV_GPU_IR_CONST:
		case DRV_GPU_IR_ICONST:
		case DRV_GPU_IR_BOOL:
			DST(inst->immediate);
			break;
		case DRV_GPU_IR_LOAD_INPUT:
			if (inst->location < 16U && ir->stage == DRV_GPU_STAGE_VERTEX) {
				DST(trial->attributes[inst->location][inst->component & 3U]);
			} else if (inst->location < 16U) {
				DST(trial->varyings[inst->location][inst->component & 3U]);
			} else if (inst->location == DRV_GPU_SHADER_LOCATION_VERTEX_INDEX) {
				DST(trial->vertex_id);
			} else if (inst->location == DRV_GPU_SHADER_LOCATION_INSTANCE_INDEX) {
				DST(trial->instance_id);
			} else if (inst->location == DRV_GPU_SHADER_LOCATION_FRAG_COORD) {
				if (inst->component == 0U)
					FDST((float)trial->frag_x + 0.5f);
				else if (inst->component == 1U)
					FDST((float)trial->frag_y + 0.5f);
				else if (inst->component == 2U)
					FDST(0.5f);
				else
					FDST(1.0f);
			} else {
				result->why = "reference: an input it does not model";
			}
			break;
		case DRV_GPU_IR_STORE_OUTPUT:
			if (inst->location == DRV_GPU_IR_LOCATION_POSITION && inst->component < 4U)
				result->position[inst->component] = a;
			else if (inst->location < 16U && ir->stage == DRV_GPU_STAGE_VERTEX)
				result->varyings[inst->location][inst->component & 3U] = a;
			else if (inst->location < 8U && ir->stage == DRV_GPU_STAGE_FRAGMENT) {
				result->colors[inst->location][inst->component & 3U] = a;
				result->color_written |= 1U << inst->location;
			}
			break;
		case DRV_GPU_IR_LOAD_PUSH:
			DST(trial->push[(inst->immediate / 4U) & 63U]);
			break;
		case DRV_GPU_IR_LOAD_UBO:
			slot = find_buffer(program, ir, inst->location);
			DST(slot < 0 ? 0U : trial->buffers[slot][(inst->immediate / 4U) % WORDS]);
			break;
		case DRV_GPU_IR_LOAD_STORAGE:
			slot = find_buffer(program, ir, inst->location);
			if (slot >= 0 && (inst->component != 1U || b != 0U))
				DST(result->buffers[slot][(a / 4U) % WORDS]);
			break;
		case DRV_GPU_IR_STORE_STORAGE:
			slot = find_buffer(program, ir, inst->location);
			if (slot >= 0 && (inst->component != 1U || value[inst->src[2]] != 0U))
				result->buffers[slot][(a / 4U) % WORDS] = b;
			break;
		case DRV_GPU_IR_FADD: FDST(fa + fb); break;
		case DRV_GPU_IR_FSUB: FDST(fa - fb); break;
		case DRV_GPU_IR_FMUL: FDST(fa * fb); break;
		case DRV_GPU_IR_FNEG: FDST(-fa); break;
		case DRV_GPU_IR_RSQ: FDST(1.0f / sqrtf(fa)); break;
		case DRV_GPU_IR_RCP: FDST(1.0f / fa); break;
		case DRV_GPU_IR_SQRT: FDST(sqrtf(fa)); break;
		case DRV_GPU_IR_EXP2: FDST(exp2f(fa)); break;
		case DRV_GPU_IR_LOG2: FDST(log2f(fa)); break;
		case DRV_GPU_IR_FABS: FDST(fabsf(fa)); break;
		case DRV_GPU_IR_FLOOR: FDST(floorf(fa)); break;
		case DRV_GPU_IR_FRACT: FDST(fa - floorf(fa)); break;
		case DRV_GPU_IR_FTRUNC: FDST(truncf(fa)); break;
		case DRV_GPU_IR_FROUND_EVEN: FDST(rintf(fa)); break;
		case DRV_GPU_IR_FMIN: FDST(fminf(fa, fb)); break;
		case DRV_GPU_IR_FMAX: FDST(fmaxf(fa, fb)); break;
		case DRV_GPU_IR_FLT: DST(BOOLV(fa < fb)); break;
		case DRV_GPU_IR_FGE: DST(BOOLV(fa >= fb)); break;
		case DRV_GPU_IR_FEQ: DST(BOOLV(fa == fb)); break;
		case DRV_GPU_IR_FNEU: DST(BOOLV(!(fa == fb))); break;
		case DRV_GPU_IR_AND: case DRV_GPU_IR_IAND: DST(a & b); break;
		case DRV_GPU_IR_OR: case DRV_GPU_IR_IOR: DST(a | b); break;
		case DRV_GPU_IR_IXOR: DST(a ^ b); break;
		case DRV_GPU_IR_NOT: case DRV_GPU_IR_INOT: DST(~a); break;
		case DRV_GPU_IR_SELECT: DST(a != 0U ? b : value[inst->src[2]]); break;
		case DRV_GPU_IR_KILL: if (a != 0U) result->killed = 1; break;
		case DRV_GPU_IR_IADD: DST(a + b); break;
		case DRV_GPU_IR_ISUB: DST(a - b); break;
		case DRV_GPU_IR_IMUL: DST(a * b); break;
		case DRV_GPU_IR_INEG: DST(0U - a); break;
		case DRV_GPU_IR_SHL: DST(a << (b & 31U)); break;
		case DRV_GPU_IR_SHR: DST(a >> (b & 31U)); break;
		case DRV_GPU_IR_ASR: DST((uint32_t)((int32_t)a >> (b & 31U))); break;
		case DRV_GPU_IR_I2F: FDST((float)(int32_t)a); break;
		case DRV_GPU_IR_U2F: FDST((float)a); break;
		case DRV_GPU_IR_F2I: DST(float_to_int(fa, 1)); break;
		case DRV_GPU_IR_F2U: DST(float_to_int(fa, 0)); break;
		case DRV_GPU_IR_ILT: DST(BOOLV((int32_t)a < (int32_t)b)); break;
		case DRV_GPU_IR_IGE: DST(BOOLV((int32_t)a >= (int32_t)b)); break;
		case DRV_GPU_IR_ULT: DST(BOOLV(a < b)); break;
		case DRV_GPU_IR_UGE: DST(BOOLV(a >= b)); break;
		case DRV_GPU_IR_IEQ: DST(BOOLV(a == b)); break;
		case DRV_GPU_IR_INE: DST(BOOLV(a != b)); break;
		case DRV_GPU_IR_MOVE: DST(a); break;
		case DRV_GPU_IR_PACK_HALF: DST(to_half(fa) | (to_half(fb) << 16)); break;
		case DRV_GPU_IR_UNPACK_HALF: FDST(from_half((a >> (16U * (inst->component & 1U))) & 0xFFFFU)); break;
		case DRV_GPU_IR_LOOP_BEGIN:
			if (depth < 16U)
				loop_stack[depth++] = index;
			break;
		case DRV_GPU_IR_LOOP_END:
			if (depth == 0U)
				break;
			if (a != 0U)
				index = loop_stack[depth - 1U];
			else
				depth--;
			break;
		case DRV_GPU_IR_LOAD_SYSTEM:
			if (inst->component < 3U)
				DST(trial->local_id[inst->component]);
			else if (inst->component == DRV_GPU_IR_SYSTEM_LOCAL_INDEX)
				DST(trial->local_id[0] + trial->local_id[1] * ir->local_size[0] +
				    trial->local_id[2] * ir->local_size[0] * ir->local_size[1]);
			else if (inst->component <= DRV_GPU_IR_SYSTEM_GROUP_ID_Z)
				DST(trial->group_id[inst->component - DRV_GPU_IR_SYSTEM_GROUP_ID_X]);
			break;
		case DRV_GPU_IR_SAMPLE:
		case DRV_GPU_IR_SAMPLE_BIAS:
		case DRV_GPU_IR_SAMPLE_LOD:
		case DRV_GPU_IR_TEXTURE:
			slot = find_texture(program, inst->location, inst->immediate);
			if (inst->op == DRV_GPU_IR_TEXTURE) {
				message = inst->component & DRV_GPU_IR_TEXTURE_MESSAGE_MASK;
				word = inst->src[0];
				if (message == DRV_GPU_IR_TEXTURE_SAMPLE_BIAS || message == DRV_GPU_IR_TEXTURE_SAMPLE_LOD)
					word++;
				if (message == DRV_GPU_IR_TEXTURE_LD)
					texture_value((uint32_t)slot, (float)(int32_t)value[word], (float)(int32_t)value[word + 1U], texel);
				else
					texture_value((uint32_t)slot, as_float(value[word]), as_float(value[word + 1U]), texel);
			} else {
				texture_value((uint32_t)slot, fa, fb, texel);
			}
			for (component = 0U; component < 4U; component++) {
				if (inst->dst + component <= ir->value_count)
					value[inst->dst + component] = texel[component];
			}
			break;
		default:
			result->why = "reference: an operation it does not model";
			break;
		}
#undef DST
#undef FDST
#undef BOOLV
	}
	free(value);
}

/* Finds the table entry of a word (the most specific match). */
static int
model_decode(
	uint64_t word)
{
	unsigned best_bits;
	unsigned bits;
	unsigned index;
	int best;

	best = -1;
	best_bits = 0U;
	for (index = 0U; index < sizeof(model_table) / sizeof(model_table[0]); index++) {
		if ((word & model_table[index].mask) != model_table[index].value)
			continue;
		bits = (unsigned)__builtin_popcountll(model_table[index].mask);
		if (best < 0 || bits > best_bits) {
			best = (int)index;
			best_bits = bits;
		}
	}
	return best;
}

/* The state of the one-lane model. */
struct model {
	uint32_t r[64];
	uint32_t fau[256];
	uint64_t pending[8];
	const struct mali_shader_program *program;
	const struct trial *trial;
	struct result *result;
	uint32_t stage;
};

/* Reads a 32-bit source byte. */
static uint32_t
read_source(
	struct model *m,
	uint8_t byte,
	uint32_t page)
{
	uint32_t number;

	if (byte < 0x80U)
		return m->r[byte & 0x3FU];
	if (byte < 0xC0U)
		return m->fau[(page * 64U + ((byte >> 1) & 0x1FU) * 2U + (byte & 1U)) & 255U];
	if (byte < 0xE0U)
		return model_immediates[byte & 0x1FU];
	number = (byte >> 1) & 0xFU;
	if (page == 1U && number == 3U)
		return (byte & 1U) != 0U ? (uint32_t)(SHARED_BASE >> 32) : (uint32_t)SHARED_BASE;
	return 0U;
}

/* Reads a 64-bit source (a register pair or a FAU slot). */
static uint64_t
read_source64(
	struct model *m,
	uint8_t byte,
	uint32_t page)
{
	uint64_t low;
	uint64_t high;

	if (byte < 0x80U) {
		low = m->r[byte & 0x3FU];
		high = m->r[((byte & 0x3FU) + 1U) & 63U];
	} else {
		low = read_source(m, (uint8_t)(byte & ~1U), page);
		high = read_source(m, (uint8_t)(byte | 1U), page);
	}
	return low | (high << 32);
}

/* A float source with its negate and absolute value bits. */
static float
float_source(
	struct model *m,
	uint64_t word,
	unsigned index,
	const struct isa_entry *entry)
{
	float value;
	unsigned neg;
	unsigned abs;

	value = as_float(read_source(m, (uint8_t)(word >> (8U * index)), (unsigned)((word >> 57) & 3U)));
	if ((entry->source_flags[index] & 1U) != 0U) {
		neg = 32U + 2U + (2U - index) * 2U;
		abs = neg + 1U;
		if (((word >> abs) & 1U) != 0U)
			value = fabsf(value);
		if (((word >> neg) & 1U) != 0U)
			value = -value;
	}
	return value;
}

/* Maps a model address to a word of memory, or NULL. */
static uint32_t *
memory_word(
	struct model *m,
	uint64_t address)
{
	uint64_t record;
	uint64_t offset;

	if (address >= OUTPUT_BASE && address < OUTPUT_BASE + 0x100000ULL) {
		record = (address - OUTPUT_BASE) / 256U;
		offset = (address - OUTPUT_BASE) % 256U;
		if (record == OUTPUT_INDEX && m->stage == 1U && offset < 16U)
			return &m->result->position[offset / 4U];
		if (record == OUTPUT_INDEX && m->stage == 2U && offset < 256U)
			return &m->result->varyings[offset / 16U][(offset / 4U) % 4U];
		if (record == OUTPUT_INDEX + 4U)
			return &m->result->varyings[15][3];
		return NULL;
	}
	if (address >= BUFFER_BASE && address < BUFFER_BASE + (uint64_t)BUFFERS * 0x100000ULL) {
		record = (address - BUFFER_BASE) / 0x100000ULL;
		offset = (address - BUFFER_BASE) % 0x100000ULL;
		return &m->result->buffers[record][(offset / 4U) % WORDS];
	}
	if (address >= SHARED_BASE && address < SHARED_BASE + WORDS * 4U)
		return &m->result->buffers[BUFFERS - 1U][((address - SHARED_BASE) / 4U) % WORDS];
	return NULL;
}

/* Evaluates a comparison condition. */
static int
compare_float(
	unsigned condition,
	float a,
	float b)
{
	switch (condition) {
	case 0U: return a == b;
	case 1U: return a > b;
	case 2U: return a >= b;
	case 3U: return !(a == b);
	case 4U: return a < b;
	case 5U: return a <= b;
	default: return 0;
	}
}

static int
compare_int(
	unsigned condition,
	uint32_t a,
	uint32_t b,
	int is_signed)
{
	int64_t x;
	int64_t y;

	x = is_signed ? (int64_t)(int32_t)a : (int64_t)a;
	y = is_signed ? (int64_t)(int32_t)b : (int64_t)b;
	switch (condition) {
	case 0U: return x == y;
	case 1U: return x > y;
	case 2U: return x >= y;
	case 3U: return x != y;
	case 4U: return x < y;
	case 5U: return x <= y;
	default: return 0;
	}
}

/* The exponent e of x = m * 2^e with m in [1, 2). */
static int
exponent_of(
	float x)
{
	int e;

	if (x == 0.0f || isinf(x) || isnan(x))
		return 0;
	frexpf(x, &e);
	return e - 1;
}

/* Runs one program of the compiled shader on the model. */
static void
run_model(
	const uint8_t *code,
	size_t length,
	uint32_t stage,
	const struct mali_shader_program *program,
	const struct trial *trial,
	struct result *result)
{
	const struct isa_entry *entry;
	struct model m;
	uint64_t word;
	uint64_t reads;
	uint64_t writes;
	uint64_t address;
	uint32_t *memory;
	uint32_t pc;
	uint32_t steps;
	uint32_t flow;
	uint32_t last_flow;
	uint32_t page;
	uint32_t dest;
	uint32_t staging;
	uint32_t count;
	uint32_t slot;
	uint32_t index;
	uint32_t a;
	uint32_t b;
	uint32_t c;
	uint32_t texel[4];
	unsigned shift;
	int found;
	int32_t offset;
	float fa;
	float fb;
	const char *name;

	memset(&m, 0, sizeof(m));
	m.program = program;
	m.trial = trial;
	m.result = result;
	m.stage = stage;

	/* Lays out the FAU: push constants, the viewport, the buffers' addresses. */
	for (index = 0U; index < program->push_bytes / 4U && index < 64U; index++)
		m.fau[index] = trial->push[index];
	if (program->stage == MALI_SHADER_VERTEX) {
		for (index = 0U; index < 6U; index++)
			m.fau[program->viewport_fau_word + index] = trial->viewport[index];
	}
	for (index = 0U; index < program->buffer_count; index++) {
		address = BUFFER_BASE + (uint64_t)index * 0x100000ULL;
		m.fau[program->buffers[index].fau_word] = (uint32_t)address;
		m.fau[program->buffers[index].fau_word + 1U] = (uint32_t)(address >> 32);
	}

	/* The preloads. */
	m.r[55] = trial->local_id[0] | (trial->local_id[1] << 16);
	m.r[56] = trial->local_id[2];
	if (program->stage == MALI_SHADER_COMPUTE) {
		m.r[57] = trial->group_id[0];
		m.r[58] = trial->group_id[1];
		m.r[59] = trial->group_id[2];
	} else if (program->stage == MALI_SHADER_VERTEX) {
		m.r[59] = OUTPUT_INDEX;
		m.r[60] = trial->vertex_id;
		m.r[61] = trial->instance_id;
	} else {
		m.r[59] = trial->frag_x | (trial->frag_y << 16);
		m.r[60] = 1U;
		m.r[61] = 0U;
	}

	pc = 0U;
	last_flow = 0U;
	for (steps = 0U; steps < MAX_STEPS; steps++) {
		if ((size_t)pc * 8U + 8U > length) {
			result->why = "the program runs past its end";
			return;
		}
		memcpy(&word, code + (size_t)pc * 8U, 8U);
		found = model_decode(word);
		if (found < 0) {
			result->why = "a word that does not decode";
			return;
		}
		entry = &model_table[found];
		name = entry->name;
		flow = (uint32_t)((word >> 59) & 0xFU);
		page = (uint32_t)((word >> 57) & 3U);
		dest = (uint32_t)((word >> 40) & 0x3FU);
		count = (uint32_t)((word >> 33) & 7U);

		/* The registers it touches, for the wait check. */
		reads = 0U;
		for (index = 0U; index < entry->sources && index < 3U; index++) {
			if (((word >> (8U * index)) & 0xC0U) < 0x80U) {
				reads |= 1ULL << ((word >> (8U * index)) & 0x3FU);
				if (entry->source_sizes[index] == 64U)
					reads |= 2ULL << ((word >> (8U * index)) & 0x3FU);
			}
		}
		writes = 0U;
		if (entry->dests != 0U)
			writes |= 1ULL << dest;
		if (entry->staging_read != 0U || entry->staging_write != 0U) {
			for (index = 0U; index < count; index++)
				reads |= 1ULL << ((dest + index) & 63U);
		}
		if (strncmp(name, "TEX_", 4) == 0 || strncmp(name, "ATOM_RETURN", 11) == 0) {
			staging = (uint32_t)((word >> 16) & 0x3FU);
			for (index = 0U; index <= ((word >> 36) & 7U); index++)
				writes |= 1ULL << ((staging + index) & 63U);
		}
		for (slot = 0U; slot < 8U; slot++) {
			if ((m.pending[slot] & (reads | writes)) != 0U) {
				result->why = "an instruction touches a register before the wait on the message writing it";
				return;
			}
		}
		if (strcmp(name, "ATEST") == 0 && last_flow != 8U && last_flow != 9U) {
			result->why = "ATEST without wait0126 before it";
			return;
		}
		if (strcmp(name, "BLEND") == 0 && last_flow != 9U) {
			result->why = "BLEND without the wait before it";
			return;
		}

		a = read_source(&m, (uint8_t)word, page);
		b = read_source(&m, (uint8_t)(word >> 8), page);
		c = read_source(&m, (uint8_t)(word >> 16), page);
		pc++;

		if (strcmp(name, "NOP") == 0 || strcmp(name, "BARRIER") == 0) {
		} else if (strcmp(name, "MOV.i32") == 0) {
			m.r[dest] = a;
		} else if (strcmp(name, "IADD_IMM.i32") == 0) {
			m.r[dest] = a + (uint32_t)(word >> 8);
		} else if (strcmp(name, "FADD.f32") == 0) {
			m.r[dest] = as_bits(float_source(&m, word, 0U, entry) + float_source(&m, word, 1U, entry));
		} else if (strcmp(name, "FMA.f32") == 0) {
			m.r[dest] = as_bits(fmaf(float_source(&m, word, 0U, entry), float_source(&m, word, 1U, entry), float_source(&m, word, 2U, entry)));
		} else if (strcmp(name, "FMIN.f32") == 0) {
			m.r[dest] = as_bits(fminf(float_source(&m, word, 0U, entry), float_source(&m, word, 1U, entry)));
		} else if (strcmp(name, "FMAX.f32") == 0) {
			m.r[dest] = as_bits(fmaxf(float_source(&m, word, 0U, entry), float_source(&m, word, 1U, entry)));
		} else if (strcmp(name, "FRCP.f32") == 0) {
			m.r[dest] = as_bits(1.0f / float_source(&m, word, 0U, entry));
		} else if (strcmp(name, "FRSQ.f32") == 0) {
			m.r[dest] = as_bits(1.0f / sqrtf(float_source(&m, word, 0U, entry)));
		} else if (strcmp(name, "FROUND.f32") == 0) {
			fa = float_source(&m, word, 0U, entry);
			switch ((word >> 30) & 3U) {
			case 0U: fa = rintf(fa); break;
			case 1U: fa = ceilf(fa); break;
			case 2U: fa = floorf(fa); break;
			default: fa = truncf(fa); break;
			}
			m.r[dest] = as_bits(fa);
		} else if (strcmp(name, "FREXPE.f32") == 0) {
			m.r[dest] = (uint32_t)exponent_of(as_float(a));
		} else if (strcmp(name, "FADD_LSCALE.f32") == 0) {
			fb = float_source(&m, word, 1U, entry);
			m.r[dest] = as_bits(float_source(&m, word, 0U, entry) + ldexpf(fb, -exponent_of(fb)));
		} else if (strcmp(name, "FLOGD.f32") == 0) {
			fa = ldexpf(as_float(a), -exponent_of(as_float(a)));
			if (fa == 1.0f)
				m.r[dest] = as_bits(1.0f / logf(2.0f));
			else
				m.r[dest] = as_bits(log2f(fa) / (fa - 1.0f));
			if (as_float(a) <= 0.0f || isinf(as_float(a)) || isnan(as_float(a)))
				m.r[dest] = as_bits(log2f(as_float(a)));
		} else if (strcmp(name, "FEXP.f32") == 0) {
			fb = float_source(&m, word, 1U, entry);
			if (isnan(fb) || fb > 128.0f || fb < -16777216.0f * 150.0f)
				m.r[dest] = as_bits(exp2f(fb / 16777216.0f));
			else
				m.r[dest] = as_bits(exp2f((float)(int32_t)a / 16777216.0f));
		} else if (strcmp(name, "S32_TO_F32") == 0) {
			m.r[dest] = as_bits((float)(int32_t)a);
		} else if (strcmp(name, "U32_TO_F32") == 0) {
			m.r[dest] = as_bits((float)a);
		} else if (strcmp(name, "F32_TO_S32") == 0 || strcmp(name, "F32_TO_U32") == 0) {
			fa = float_source(&m, word, 0U, entry);
			if (((word >> 30) & 3U) == 0U)
				fa = rintf(fa);
			m.r[dest] = float_to_int(fa, name[7] == 'S');
		} else if (strcmp(name, "F16_TO_F32") == 0) {
			m.r[dest] = as_bits(from_half((a >> (16U * ((word >> 28) & 1U))) & 0xFFFFU));
		} else if (strcmp(name, "U16_TO_F32") == 0) {
			m.r[dest] = as_bits((float)((a >> (16U * ((word >> 28) & 1U))) & 0xFFFFU));
		} else if (strcmp(name, "V2F32_TO_V2F16") == 0) {
			m.r[dest] = to_half(float_source(&m, word, 0U, entry)) | (to_half(float_source(&m, word, 1U, entry)) << 16);
		} else if (strcmp(name, "MKVEC.v2i16") == 0) {
			m.r[dest] = ((a >> (16U * ((word >> 28) & 1U))) & 0xFFFFU) | (((b >> (16U * ((word >> 26) & 1U))) & 0xFFFFU) << 16);
		} else if (strcmp(name, "FCMP_OR.f32") == 0) {
			m.r[dest] = (compare_float((unsigned)((word >> 32) & 7U), float_source(&m, word, 0U, entry), float_source(&m, word, 1U, entry)) ? 0xFFFFFFFFU : 0U) | c;
		} else if (strcmp(name, "ICMP_OR.u32") == 0 || strcmp(name, "ICMP_OR.s32") == 0) {
			m.r[dest] = (compare_int((unsigned)((word >> 32) & 7U), a, b, name[9] == 's') ? 0xFFFFFFFFU : 0U) | c;
		} else if (strncmp(name, "LSHIFT_", 7) == 0 || strncmp(name, "RSHIFT_", 7) == 0 || strncmp(name, "ARSHIFT_", 8) == 0) {
			shift = b & 0xFFU;
			if (((word >> 35) & 1U) != 0U)
				c = ~c;
			if (name[0] == 'L')
				a = shift >= 32U ? 0U : a << shift;
			else if (((word >> 34) & 1U) != 0U || name[0] == 'A')
				a = (uint32_t)((int32_t)a >> (shift >= 32U ? 31U : shift));
			else
				a = shift >= 32U ? 0U : a >> shift;
			if (strstr(name, "_AND") != NULL)
				m.r[dest] = a & c;
			else if (strstr(name, "_OR") != NULL)
				m.r[dest] = a | c;
			else
				m.r[dest] = a ^ c;
		} else if (strcmp(name, "MUX.i32") == 0) {
			if (((word >> 32) & 3U) != 1U) {
				result->why = "a MUX mode the model does not run";
				return;
			}
			m.r[dest] = c == 0U ? a : b;
		} else if (strcmp(name, "IADD.u32") == 0) {
			m.r[dest] = a + b;
		} else if (strcmp(name, "ISUB.u32") == 0) {
			m.r[dest] = a - b;
		} else if (strcmp(name, "IMUL.i32") == 0) {
			m.r[dest] = a * b;
		} else if (strcmp(name, "SHADDX.u64") == 0) {
			address = read_source64(&m, (uint8_t)word, page) + ((uint64_t)b << ((word >> 20) & 7U));
			m.r[dest] = (uint32_t)address;
			m.r[(dest + 1U) & 63U] = (uint32_t)(address >> 32);
		} else if (strcmp(name, "DISCARD.f32") == 0) {
			if (compare_float((unsigned)((word >> 32) & 7U), float_source(&m, word, 0U, entry), float_source(&m, word, 1U, entry))) {
				result->killed = 1;
				return;
			}
		} else if (strcmp(name, "BRANCHZ") == 0) {
			offset = (int32_t)(((word >> 8) & 0x7FFFFFFU) << 5) >> 5;
			if ((((word >> 36) & 1U) != 0U) == (a == 0U))
				pc = (uint32_t)((int32_t)pc + offset);
		} else if (strcmp(name, "LD_ATTR_IMM") == 0) {
			index = (uint32_t)((word >> 20) & 0xFU);
			if (((word >> 16) & 0xFU) != MALI_SHADER_TABLE_ATTRIBUTES) {
				result->why = "LD_ATTR_IMM of another table";
				return;
			}
			for (slot = 0U; slot < count; slot++)
				m.r[(dest + slot) & 63U] = trial->attributes[index][slot & 3U];
		} else if (strcmp(name, "LD_VAR_BUF_IMM.f32") == 0) {
			index = (uint32_t)((word >> 16) & 0xFFU);
			for (slot = 0U; slot < count; slot++)
				m.r[(dest + slot) & 63U] = trial->varyings[(index / 16U) & 15U][(index / 4U + slot) & 3U];
		} else if (strcmp(name, "LD_VAR_SPECIAL") == 0) {
			index = (uint32_t)((word >> 12) & 0xFU);
			m.r[dest] = as_bits(index == 3U ? 0.5f : 1.0f);
		} else if (strcmp(name, "LEA_BUF_IMM") == 0) {
			if (((word >> 8) & 0xFU) != 13U) {
				result->why = "LEA_BUF_IMM of a table other than 61";
				return;
			}
			address = OUTPUT_BASE + (uint64_t)a * 256U;
			m.r[dest] = (uint32_t)address;
			m.r[(dest + 1U) & 63U] = (uint32_t)(address >> 32);
		} else if (strncmp(name, "LOAD.", 5) == 0 || strncmp(name, "STORE.", 6) == 0) {
			address = read_source64(&m, (uint8_t)word, page) + (uint64_t)(int64_t)(int16_t)(uint16_t)(word >> 8);
			for (slot = 0U; slot < count; slot++) {
				memory = memory_word(&m, address + 4U * slot);
				if (memory == NULL) {
					result->why = "a memory access outside the model's memory";
					return;
				}
				if (name[0] == 'L')
					m.r[(dest + slot) & 63U] = *memory;
				else if (strcmp(name, "STORE.i16") == 0)
					*memory = as_bits(from_half(m.r[(dest + slot) & 63U] & 0xFFFFU));
				else
					*memory = m.r[(dest + slot) & 63U];
			}
		} else if (strcmp(name, "ATOM_RETURN.i32") == 0) {
			address = read_source64(&m, (uint8_t)word, page) + ((word >> 8) & 0xFFU);
			memory = memory_word(&m, address);
			if (memory == NULL) {
				result->why = "an atomic outside the model's memory";
				return;
			}
			a = *memory;
			b = m.r[dest];
			switch ((word >> 22) & 0xFU) {
			case 2U: *memory = a + b; break;
			case 8U: *memory = (int32_t)a < (int32_t)b ? a : b; break;
			case 9U: *memory = (int32_t)a > (int32_t)b ? a : b; break;
			case 10U: *memory = a < b ? a : b; break;
			case 11U: *memory = a > b ? a : b; break;
			case 12U: *memory = a & b; break;
			case 13U: *memory = a | b; break;
			case 14U: *memory = a ^ b; break;
			default: *memory = b; break;
			}
			m.r[(word >> 16) & 0x3FU] = a;
		} else if (strcmp(name, "TEX_SINGLE") == 0 || strcmp(name, "TEX_FETCH") == 0) {
			a = m.r[word & 0x3FU];
			index = a & 0x3FFU;
			if (strcmp(name, "TEX_FETCH") == 0) {
				fa = (float)(int32_t)m.r[dest];
				fb = (float)(int32_t)m.r[(dest + 1U) & 63U];
			} else {
				fa = as_float(m.r[dest]);
				fb = as_float(m.r[(dest + 1U) & 63U]);
			}
			texture_value(index, fa, fb, texel);
			staging = (uint32_t)((word >> 16) & 0x3FU);
			for (slot = 0U; slot < 4U; slot++)
				m.r[(staging + slot) & 63U] = texel[slot];
		} else if (strcmp(name, "ATEST") == 0) {
			m.r[dest] = a;
		} else if (strcmp(name, "BLEND") == 0) {
			index = (((uint32_t)word >> 1) & 0xFU) - 8U;
			for (slot = 0U; slot < 4U && index < 8U; slot++)
				result->colors[index][slot] = m.r[(dest + slot) & 63U];
			if (index < 8U)
				result->color_written |= 1U << index;
		} else {
			result->why = name;
			return;
		}

		/* A message's results arrive at its slot's wait. */
		if (entry->message != 0U && strcmp(name, "BLEND") != 0) {
			slot = (uint32_t)((word >> 30) & 7U);
			m.pending[slot] |= writes;
			if (entry->staging_write != 0U && strncmp(name, "TEX_", 4) != 0 && strncmp(name, "ATOM_RETURN", 11) != 0) {
				for (index = 0U; index < count; index++)
					m.pending[slot] |= 1ULL << ((dest + index) & 63U);
			}
			if (strncmp(name, "STORE", 5) == 0)
				m.pending[slot] &= ~(reads | writes);
		}
		if (flow >= 1U && flow <= 7U) {
			for (slot = 0U; slot < 3U; slot++) {
				if ((flow & (1U << slot)) != 0U)
					m.pending[slot] = 0U;
			}
		} else if (flow == 8U) {
			m.pending[0] = 0U;
			m.pending[1] = 0U;
			m.pending[2] = 0U;
		} else if (flow == 9U) {
			memset(m.pending, 0, sizeof(m.pending));
		} else if (flow == 15U) {
			return;
		}
		last_flow = flow;
	}
	result->why = "the program runs too long";
}

static int
close_enough(
	uint32_t a,
	uint32_t b)
{
	float x;
	float y;
	float scale;

	if (a == b)
		return 1;
	x = as_float(a);
	y = as_float(b);
	if (isnan(x) && isnan(y))
		return 1;
	if (isnan(x) || isnan(y) || isinf(x) || isinf(y))
		return 0;
	scale = fmaxf(1.0f, fmaxf(fabsf(x), fabsf(y)));
	return fabsf(x - y) <= 2e-4f * scale;
}

/* Fills one trial's inputs. */
static void
fill_trial(
	struct trial *trial)
{
	uint32_t index;
	uint32_t component;

	for (index = 0U; index < 16U; index++) {
		for (component = 0U; component < 4U; component++) {
			trial->attributes[index][component] = as_bits(((float)(next_random() % 4001U) - 2000.0f) / 1000.0f);
			trial->varyings[index][component] = as_bits(((float)(next_random() % 2001U)) / 1000.0f);
		}
	}
	for (index = 0U; index < 64U; index++)
		trial->push[index] = as_bits(((float)(next_random() % 4001U) - 2000.0f) / 1000.0f);
	for (index = 0U; index < 6U; index++)
		trial->viewport[index] = as_bits(((float)(next_random() % 2001U)) / 4.0f + 1.0f);
	for (index = 0U; index < BUFFERS; index++) {
		for (component = 0U; component < WORDS; component++)
			trial->buffers[index][component] = as_bits(((float)(next_random() % 4001U) - 2000.0f) / 1000.0f);
	}
	for (index = 0U; index < 3U; index++) {
		trial->local_id[index] = next_random() % 4U;
		trial->group_id[index] = next_random() % 8U;
	}
	trial->vertex_id = next_random() % 1000U;
	trial->instance_id = next_random() % 4U;
	trial->frag_x = next_random() % 1920U;
	trial->frag_y = next_random() % 1080U;
}

/* The screen space position the position program must store for a clip space one. */
static void
expected_position(
	const struct trial *trial,
	const uint32_t *clip,
	uint32_t *screen)
{
	float recip;
	uint32_t axis;

	recip = 1.0f / as_float(clip[3]);
	recip = fminf(recip, 32768.0f);
	recip = fmaxf(recip, -32768.0f);
	for (axis = 0U; axis < 3U; axis++)
		screen[axis] = as_bits(fmaf(as_float(clip[axis]) * recip, as_float(trial->viewport[axis]), as_float(trial->viewport[3U + axis])));
	screen[3] = as_bits(recip);
}

int
mali_model_check(
	const uint32_t *words,
	size_t count,
	uint32_t stage,
	const struct mali_shader_program *program,
	unsigned trials,
	const char *name)
{
	struct drv_gpu_compile_diagnostic diagnostic;
	struct drv_gpu_shader_ir *ir;
	struct trial *trial;
	struct result *reference;
	struct result *model;
	uint32_t screen[4];
	uint32_t location;
	uint32_t component;
	uint32_t buffer;
	unsigned run;
	int failed;
	enum drv_gpu_shader_stage ir_stage;

	ir_stage = stage == MALI_SHADER_VERTEX ? DRV_GPU_STAGE_VERTEX :
		   stage == MALI_SHADER_FRAGMENT ? DRV_GPU_STAGE_FRAGMENT : DRV_GPU_STAGE_COMPUTE;
	ir = NULL;
	if (drv_gpu_shader_parse(words, count, ir_stage, &ir, &diagnostic) != 0) {
		printf("FAIL %s: the reference cannot parse it\n", name);
		return 1;
	}
	trial = malloc(sizeof(*trial));
	reference = malloc(sizeof(*reference));
	model = malloc(sizeof(*model));
	if (trial == NULL || reference == NULL || model == NULL) {
		printf("FAIL %s: out of memory\n", name);
		return 1;
	}
	random_state = 12345U;
	failed = 0;
	for (run = 0U; run < trials && failed == 0; run++) {
		fill_trial(trial);
		memset(reference, 0, sizeof(*reference));
		memcpy(reference->buffers, trial->buffers, sizeof(trial->buffers));
		reference->position[3] = as_bits(1.0f);
		run_reference(ir, program, trial, reference);
		if (reference->why != NULL) {
			printf("FAIL %s: %s\n", name, reference->why);
			failed = 1;
			break;
		}

		memset(model, 0, sizeof(*model));
		memcpy(model->buffers, trial->buffers, sizeof(trial->buffers));
		if (stage == MALI_SHADER_VERTEX) {
			run_model(program->code, program->varying_offset != 0U ? program->varying_offset : program->length, 1U, program, trial, model);
			if (model->why == NULL && program->varying_offset != 0U)
				run_model(program->code + program->varying_offset, program->length - program->varying_offset, 2U, program, trial, model);
		} else {
			run_model(program->code, program->length, 0U, program, trial, model);
		}
		if (model->why != NULL) {
			printf("FAIL %s: trial %u: model: %s\n", name, run, model->why);
			failed = 1;
			break;
		}

		if (stage == MALI_SHADER_VERTEX) {
			expected_position(trial, reference->position, screen);
			for (component = 0U; component < 4U; component++) {
				if (!close_enough(model->position[component], screen[component])) {
					printf("FAIL %s: trial %u: position %u is %08x, the reference %08x\n", name, run, component,
					       model->position[component], screen[component]);
					failed = 1;
				}
			}
			for (location = 0U; location < 16U; location++) {
				if ((program->varying_mask & (1U << location)) == 0U)
					continue;
				for (component = 0U; component < 4U; component++) {
					if (!close_enough(model->varyings[location][component], reference->varyings[location][component])) {
						printf("FAIL %s: trial %u: varying %u.%u is %08x, the reference %08x\n", name, run, location, component,
						       model->varyings[location][component], reference->varyings[location][component]);
						failed = 1;
					}
				}
			}
		} else if (stage == MALI_SHADER_FRAGMENT) {
			if (reference->killed != model->killed) {
				printf("FAIL %s: trial %u: discarded %d, the reference %d\n", name, run, model->killed, reference->killed);
				failed = 1;
			} else if (reference->killed == 0) {
				for (location = 0U; location < 8U; location++) {
					if ((reference->color_written & (1U << location)) == 0U)
						continue;
					for (component = 0U; component < 4U; component++) {
						if (!close_enough(model->colors[location][component], reference->colors[location][component])) {
							printf("FAIL %s: trial %u: colour %u.%u is %08x, the reference %08x\n", name, run, location, component,
							       model->colors[location][component], reference->colors[location][component]);
							failed = 1;
						}
					}
				}
			}
		}
		for (buffer = 0U; buffer < BUFFERS && failed == 0; buffer++) {
			for (location = 0U; location < WORDS; location++) {
				if (!close_enough(model->buffers[buffer][location], reference->buffers[buffer][location])) {
					printf("FAIL %s: trial %u: buffer %u word %u is %08x, the reference %08x\n", name, run, buffer, location,
					       model->buffers[buffer][location], reference->buffers[buffer][location]);
					failed = 1;
					break;
				}
			}
		}
	}
	free(trial);
	free(reference);
	free(model);
	drv_gpu_shader_ir_free(ir);
	return failed;
}
