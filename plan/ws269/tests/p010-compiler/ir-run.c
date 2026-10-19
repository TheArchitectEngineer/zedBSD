/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * A host interpreter of the PowerVR compiler's intermediate form, for
 * the compiler's host test: it runs a shader over up to 32 lanes with a
 * mask per lane, the way the core runs a task, so the compiled program's
 * simulated results can be checked against it.
 */

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "ir-run.h"

/* How deep ifs and loops may nest. */
#define RUN_DEPTH	32

/* One open if: the lanes active at it and those whose condition held. */
struct run_if {
	uint32_t saved;
	uint32_t taken;
};

/* One open loop: the lanes at its start, those that broke and those that continued. */
struct run_loop {
	uint32_t entry;
	uint32_t broken;
	uint32_t continued;
	unsigned start;
};

static float as_float(uint32_t word);
static uint32_t as_word(float value);
static uint32_t read_operand(const struct ir_run *run, const struct powervr_ir_operand *operand, unsigned lane);
static uint32_t float_op(unsigned opcode, uint32_t a, uint32_t b, uint32_t c);

/*
 * Runs a shader on the lanes of a mask; reports 0, or -1 for a shader
 * the interpreter cannot follow.
 */
int
ir_run(
	struct ir_run *run,
	const struct powervr_ir_shader *shader,
	uint32_t lanes)
{
	const struct powervr_ir_instruction *instruction;
	struct run_if ifs[RUN_DEPTH];
	struct run_loop loops[RUN_DEPTH];
	unsigned if_depth;
	unsigned loop_depth;
	unsigned at;
	unsigned lane;
	unsigned word;
	uint32_t active;
	uint32_t a;
	uint32_t b;
	uint32_t c;
	uint32_t d;
	uint32_t result[4];
	uint64_t address;
	unsigned steps;

	/* Starts with every lane given. */
	active = lanes;
	if_depth = 0;
	loop_depth = 0;
	steps = 0;
	for (at = 0; at < shader->instruction_count; at++) {
		instruction = &shader->instructions[at];
		if (++steps > 10000000U)
			return -1;

		/* Structure moves the mask. */
		switch (instruction->opcode) {
		case POWERVR_IR_IF:
			if (if_depth >= RUN_DEPTH)
				return -1;
			ifs[if_depth].saved = active;
			ifs[if_depth].taken = 0;
			for (lane = 0; lane < 32; lane++) {
				if ((active >> lane & 1U) != 0 && read_operand(run, &instruction->source[0], lane) != 0)
					ifs[if_depth].taken |= 1U << lane;
			}
			active = ifs[if_depth].taken;
			if_depth++;
			continue;
		case POWERVR_IR_ELSE:
			if (if_depth == 0)
				return -1;
			active = ifs[if_depth - 1].saved & ~ifs[if_depth - 1].taken;
			continue;
		case POWERVR_IR_END_IF:
			if (if_depth == 0)
				return -1;
			if_depth--;
			active = ifs[if_depth].saved;
			if (loop_depth != 0)
				active &= ~(loops[loop_depth - 1].broken | loops[loop_depth - 1].continued);
			continue;
		case POWERVR_IR_LOOP:
			if (loop_depth >= RUN_DEPTH)
				return -1;
			loops[loop_depth].entry = active;
			loops[loop_depth].broken = 0;
			loops[loop_depth].continued = 0;
			loops[loop_depth].start = at;
			loop_depth++;
			continue;
		case POWERVR_IR_BREAK:
			if (loop_depth == 0)
				return -1;
			loops[loop_depth - 1].broken |= active;
			active = 0;
			continue;
		case POWERVR_IR_CONTINUE:
			if (loop_depth == 0)
				return -1;
			loops[loop_depth - 1].continued |= active;
			active = 0;
			continue;
		case POWERVR_IR_LOOP_CONTINUE:
			if (loop_depth == 0)
				return -1;
			active |= loops[loop_depth - 1].continued;
			loops[loop_depth - 1].continued = 0;
			continue;
		case POWERVR_IR_END_LOOP:
			if (loop_depth == 0)
				return -1;
			active |= loops[loop_depth - 1].continued;
			loops[loop_depth - 1].continued = 0;
			if (active != 0) {
				at = loops[loop_depth - 1].start;
				continue;
			}
			loop_depth--;
			active = loops[loop_depth].broken;
			continue;
		default:
			break;
		}

		/* Every other instruction runs on each active lane. */
		for (lane = 0; lane < 32; lane++) {
			if ((active >> lane & 1U) == 0)
				continue;
			a = read_operand(run, &instruction->source[0], lane);
			b = read_operand(run, &instruction->source[1], lane);
			c = read_operand(run, &instruction->source[2], lane);
			d = read_operand(run, &instruction->source[3], lane);
			result[0] = 0;
			result[1] = 0;
			result[2] = 0;
			result[3] = 0;

			switch (instruction->opcode) {
			case POWERVR_IR_MOVE:
				result[0] = float_op(instruction->opcode, a, b, c);
				break;
			case POWERVR_IR_INTEGER_ADD:
				result[0] = a + b;
				break;
			case POWERVR_IR_INTEGER_MULTIPLY:
				result[0] = a * b;
				break;
			case POWERVR_IR_BIT_AND:
				result[0] = a & b;
				break;
			case POWERVR_IR_BIT_OR:
				result[0] = a | b;
				break;
			case POWERVR_IR_BIT_XOR:
				result[0] = a ^ b;
				break;
			case POWERVR_IR_SHIFT_LEFT:
				result[0] = a << (b & 31U);
				break;
			case POWERVR_IR_SHIFT_RIGHT:
				result[0] = a >> (b & 31U);
				break;
			case POWERVR_IR_SHIFT_RIGHT_SIGNED:
				result[0] = (uint32_t)((int32_t)a >> (b & 31U));
				break;
			case POWERVR_IR_UNSIGNED_MINIMUM:
				result[0] = a < b ? a : b;
				break;
			case POWERVR_IR_UNSIGNED_MAXIMUM:
				result[0] = a > b ? a : b;
				break;
			case POWERVR_IR_SIGNED_MINIMUM:
				result[0] = (int32_t)a < (int32_t)b ? a : b;
				break;
			case POWERVR_IR_SIGNED_MAXIMUM:
				result[0] = (int32_t)a > (int32_t)b ? a : b;
				break;
			case POWERVR_IR_INTEGER_EQUAL:
				result[0] = a == b ? 0xffffffffU : 0;
				break;
			case POWERVR_IR_INTEGER_NOT_EQUAL:
				result[0] = a != b ? 0xffffffffU : 0;
				break;
			case POWERVR_IR_UNSIGNED_LESS:
				result[0] = a < b ? 0xffffffffU : 0;
				break;
			case POWERVR_IR_UNSIGNED_GREATER_EQUAL:
				result[0] = a >= b ? 0xffffffffU : 0;
				break;
			case POWERVR_IR_SIGNED_LESS:
				result[0] = (int32_t)a < (int32_t)b ? 0xffffffffU : 0;
				break;
			case POWERVR_IR_SIGNED_GREATER_EQUAL:
				result[0] = (int32_t)a >= (int32_t)b ? 0xffffffffU : 0;
				break;
			case POWERVR_IR_SELECT:
				result[0] = a != 0 ? b : c;
				break;
			case POWERVR_IR_SHARED:
				result[0] = run->shared(run, instruction->immediate, instruction->immediate_2);
				break;
			case POWERVR_IR_INSTANCE:
				result[0] = lane;
				break;
			case POWERVR_IR_LOAD:
				address = ((uint64_t)b << 32 | a) + c;
				result[0] = run->load(run, address);
				break;
			case POWERVR_IR_STORE:
				address = ((uint64_t)b << 32 | a) + c;
				run->store(run, address, d);
				break;
			case POWERVR_IR_INPUT:
				result[0] = run->input(run, lane, instruction->immediate);
				break;
			case POWERVR_IR_ITERATE:
				result[0] = run->iterate(run, lane, instruction->immediate, instruction->flags);
				break;
			case POWERVR_IR_OUTPUT:
				if (instruction->immediate < IR_RUN_OUTPUTS)
					run->outputs[instruction->immediate][lane] = a;
				break;
			case POWERVR_IR_SAMPLE:
				run->sample(run, instruction->immediate, instruction->immediate_2, a, b, result);
				break;
			default:
				result[0] = float_op(instruction->opcode, a, b, c);
				break;
			}

			/* Writes the result's words. */
			if (instruction->destination != 0) {
				for (word = 0; word < shader->values[instruction->destination].width && word < 4; word++)
					run->values[instruction->destination][word][lane] = result[word];
			}
		}
	}

	/* Succeeded: the lanes ran to the end. */
	if (if_depth != 0 || loop_depth != 0)
		return -1;
	return 0;
}

/* Reads one operand on one lane, applying its float modifiers. */
static uint32_t
read_operand(
	const struct ir_run *run,
	const struct powervr_ir_operand *operand,
	unsigned lane)
{
	uint32_t word;

	/* The bits, then the modifiers. */
	if (operand->kind == POWERVR_IR_OPERAND_CONSTANT) {
		word = operand->constant;
	} else if (operand->kind == POWERVR_IR_OPERAND_VALUE) {
		word = run->values[operand->value][operand->component][lane];
	} else {
		return 0;
	}
	if ((operand->modifiers & POWERVR_IR_ABSOLUTE) != 0)
		word &= 0x7fffffffU;
	if ((operand->modifiers & POWERVR_IR_NEGATE) != 0)
		word ^= 0x80000000U;
	return word;
}

/* Works out one float operation. */
static uint32_t
float_op(
	unsigned opcode,
	uint32_t a,
	uint32_t b,
	uint32_t c)
{
	float x;
	float y;
	float z;

	/* The floats, then the operation. */
	x = as_float(a);
	y = as_float(b);
	z = as_float(c);
	switch (opcode) {
	case POWERVR_IR_MOVE:
		return a;
	case POWERVR_IR_FLOAT_ADD:
		return as_word(x + y);
	case POWERVR_IR_FLOAT_MULTIPLY:
		return as_word(x * y);
	case POWERVR_IR_FLOAT_MULTIPLY_ADD:
		return as_word(x * y + z);
	case POWERVR_IR_FLOAT_MINIMUM:
		return as_word(x < y ? x : y);
	case POWERVR_IR_FLOAT_MAXIMUM:
		return as_word(x > y ? x : y);
	case POWERVR_IR_FLOAT_FLOOR:
		return as_word(floorf(x));
	case POWERVR_IR_FLOAT_RECIPROCAL:
		return as_word(1.0f / x);
	case POWERVR_IR_FLOAT_RECIPROCAL_ROOT:
		return as_word(1.0f / sqrtf(x));
	case POWERVR_IR_FLOAT_LOG2:
		return as_word(log2f(x));
	case POWERVR_IR_FLOAT_EXP2:
		return as_word(exp2f(x));
	case POWERVR_IR_FLOAT_LESS:
		return x < y ? 0xffffffffU : 0;
	case POWERVR_IR_FLOAT_GREATER_EQUAL:
		return x >= y ? 0xffffffffU : 0;
	case POWERVR_IR_FLOAT_EQUAL:
		return x == y ? 0xffffffffU : 0;
	case POWERVR_IR_FLOAT_NOT_EQUAL:
		return x != y ? 0xffffffffU : 0;
	case POWERVR_IR_FLOAT_TO_SIGNED:
		if (!(x > -2147483648.0f))
			return 0x80000000U;
		if (x >= 2147483647.0f)
			return 0x7fffffffU;
		return (uint32_t)(int32_t)x;
	case POWERVR_IR_FLOAT_TO_UNSIGNED:
		if (!(x > 0.0f))
			return 0;
		if (x >= 4294967295.0f)
			return 0xffffffffU;
		return (uint32_t)x;
	case POWERVR_IR_SIGNED_TO_FLOAT:
		return as_word((float)(int32_t)a);
	case POWERVR_IR_UNSIGNED_TO_FLOAT:
		return as_word((float)a);
	default:
		return 0xdeadbeefU;
	}
}

/* The bits of a word as a float. */
static float
as_float(
	uint32_t word)
{
	float value;

	memcpy(&value, &word, sizeof(value));
	return value;
}

/* The bits of a float as a word. */
static uint32_t
as_word(
	float value)
{
	uint32_t word;

	memcpy(&word, &value, sizeof(word));
	return word;
}
