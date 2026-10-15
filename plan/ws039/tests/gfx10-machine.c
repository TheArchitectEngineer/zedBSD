/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws039-p007: the GFX10 interpreter of the host tests (gfx10-machine.h).
 *
 * A wave is 128 scalar registers by operand code (SGPRs, VCC, M0, EXEC
 * among them), 256 VGPRs of 32 lanes, and SCC.  A memory instruction does
 * its work when it issues, but the registers it fills are marked pending
 * on its counter until a wait zeroes that counter; touching a pending
 * register is an error.  The hazard checks follow each wave's executed
 * instructions in order.
 */

#include "plan/ws039/tests/gfx10-machine.h"
#include "plan/ws039/tests/gfx10-texture.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* The most instructions one workgroup runs before the run is called stuck. */
#define MACHINE_STEP_LIMIT	20000000ULL

/* The counters, as bits of a register's pending set. */
#define PENDING_VECTOR		0x01U
#define PENDING_EXPORT		0x02U
#define PENDING_SCALAR		0x04U

/* The operand codes of the special scalar registers. */
#define CODE_VCC		106U
#define CODE_M0			124U
#define CODE_NULL		125U
#define CODE_EXEC		126U
#define CODE_LITERAL		255U

/* What a wave is doing. */
#define WAVE_RUNNING		0
#define WAVE_AT_BARRIER		1
#define WAVE_ENDED		2

/* One wave's registers, counters and hazard state. */
struct wave {
	uint32_t s[256];
	uint32_t v[256][MACHINE_LANES];
	int scc;
	uint32_t pc;
	int state;
	uint8_t pending[512];

	/* The scalar registers vector memory (loads, stores) and shared memory read, and scalar loads read. */
	uint8_t read_by_load[128];
	uint8_t read_by_store[128];
	uint8_t read_by_shared[128];
	uint8_t read_by_scalar_load[128];

	/* The memory accesses and branches for the branch hazard. */
	int vector_memory;
	int shared_memory;
	int branch_after_vector;
	int branch_after_shared;
};

/* What one run shares: the dispatch, the memory, the shared memory and the result. */
struct run {
	const struct machine_dispatch *dispatch;
	struct machine_memory *memory;
	uint8_t *shared;
	struct machine_result *result;
	struct wave *wave;
	uint32_t word;
	struct machine_graphics *graphics;
};

static int step(struct run *run, struct wave *wave);
static int fail(struct run *run, const char *error);
static int check_scalar_read(struct run *run, struct wave *wave, unsigned code);
static int check_vector_read(struct run *run, struct wave *wave, unsigned code);
static int check_write(struct run *run, struct wave *wave, unsigned code);
static int source_value(struct run *run, struct wave *wave, unsigned code, unsigned lane, uint32_t literal, uint32_t *value);
static uint32_t lane_mask(const struct wave *wave);
static uint8_t *memory_at(struct run *run, uint64_t address, uint32_t size);
static float as_float(uint32_t bits);
static uint32_t as_bits(float value);
static uint32_t half_from_float(float value);
static float float_from_half(uint32_t half);
static int scalar_alu(struct run *run, struct wave *wave, uint32_t word, uint32_t literal, int *used_literal);
static int control(struct run *run, struct wave *wave, uint32_t word);
static int scalar_load(struct run *run, struct wave *wave, uint32_t word0, uint32_t word1);
static int vector_alu(struct run *run, struct wave *wave, uint32_t word0, uint32_t word1, int wide, uint32_t literal, int *used_literal);
static int buffer_access(struct run *run, struct wave *wave, uint32_t word0, uint32_t word1);
static int shared_access(struct run *run, struct wave *wave, uint32_t word0, uint32_t word1);
static int export_access(struct run *run, struct wave *wave, uint32_t word0, uint32_t word1);
static int interpolate(struct run *run, struct wave *wave, uint32_t word);
static int format_fetch(struct run *run, struct wave *wave, uint32_t word0, uint32_t word1);
static int image_access(struct run *run, struct wave *wave, uint32_t word0, uint32_t word1);
static uint32_t vector_operation(unsigned opcode, uint32_t a, uint32_t b, uint32_t c, int *known);
static void note_wait(struct wave *wave, uint32_t immediate);
static void clear_set(uint8_t *set);
static void mark_scalar_reads(uint8_t *set, unsigned code, unsigned count);

/* Runs one compute workgroup. */
void
machine_run_group(
	const struct machine_dispatch *dispatch,
	struct machine_memory *memory,
	struct machine_result *result)
{
	static struct wave waves[MACHINE_WAVES];
	static uint8_t shared[65536];
	struct run run;
	uint32_t invocations;
	uint32_t wave_count;
	uint32_t index;
	uint32_t lane;
	uint32_t linear;
	uint32_t running;
	uint32_t at_barrier;
	int stopped;

	memset(result, 0, sizeof(*result));
	memset(&run, 0, sizeof(run));
	run.dispatch = dispatch;
	run.memory = memory;
	run.shared = shared;
	run.result = result;
	memset(shared, 0, sizeof(shared));

	/* The workgroup's waves, 32 invocations each. */
	invocations = dispatch->local_size[0] * dispatch->local_size[1] * dispatch->local_size[2];
	wave_count = (invocations + MACHINE_LANES - 1U) / MACHINE_LANES;
	if (wave_count > MACHINE_WAVES || wave_count == 0U) {
		result->error = "the workgroup has no wave or more than the machine runs";
		return;
	}

	/* Each wave starts with the table address, the workgroup id, the local ids and its lanes. */
	for (index = 0U; index < wave_count; index++) {
		struct wave *wave = &waves[index];

		memset(wave, 0, sizeof(*wave));
		wave->s[0] = (uint32_t)dispatch->table;
		wave->s[1] = (uint32_t)(dispatch->table >> 32);
		wave->s[2] = dispatch->group_id[0];
		wave->s[3] = dispatch->group_id[1];
		wave->s[4] = dispatch->group_id[2];
		for (lane = 0U; lane < MACHINE_LANES; lane++) {
			linear = index * MACHINE_LANES + lane;
			if (linear >= invocations)
				continue;
			wave->s[CODE_EXEC] |= 1U << lane;
			wave->v[0][lane] = linear % dispatch->local_size[0];
			wave->v[1][lane] = (linear / dispatch->local_size[0]) % dispatch->local_size[1];
			wave->v[2][lane] = linear / (dispatch->local_size[0] * dispatch->local_size[1]);
		}

		/* The VGPRs past the inputs start as garbage, which a program must not read. */
		for (linear = 3U; linear < 256U; linear++) {
			for (lane = 0U; lane < MACHINE_LANES; lane++)
				wave->v[linear][lane] = 0xdeadbeefU ^ (linear * 77U + lane);
		}
	}

	/* Runs the waves in turn, each until it ends or reaches a barrier, until all have ended. */
	for (;;) {
		running = 0U;
		at_barrier = 0U;
		for (index = 0U; index < wave_count; index++) {
			struct wave *wave = &waves[index];

			run.wave = wave;
			while (wave->state == WAVE_RUNNING) {
				stopped = step(&run, wave);
				if (stopped != 0)
					return;
				if (result->steps > MACHINE_STEP_LIMIT) {
					fail(&run, "the workgroup runs too long");
					return;
				}
			}
			if (wave->state == WAVE_AT_BARRIER)
				at_barrier++;
		}

		/* Every wave has ended. */
		if (at_barrier == 0U)
			break;

		/* Every wave is at the barrier or has ended: the barrier lets them go. */
		for (index = 0U; index < wave_count; index++) {
			if (waves[index].state == WAVE_AT_BARRIER) {
				waves[index].state = WAVE_RUNNING;
				running++;
			}
		}
		(void)running;
	}
}

/* Adds a region to the memory; returns its address. */
uint64_t
machine_add_region(
	struct machine_memory *memory,
	uint8_t *bytes,
	uint32_t size)
{
	struct machine_region *region;

	/* The regions are placed 256 MiB apart from 4 GiB. */
	region = &memory->regions[memory->count];
	region->address = 0x100000000ULL + (uint64_t)memory->count * 0x10000000ULL;
	region->bytes = bytes;
	region->size = size;
	memory->count++;

	/* The region's address. */
	return region->address;
}

/* Runs one instruction of a wave; nonzero when the run stops on an error. */
static int
step(
	struct run *run,
	struct wave *wave)
{
	const struct machine_dispatch *dispatch;
	uint32_t word0;
	uint32_t word1;
	uint32_t literal;
	uint32_t length;
	uint32_t top7;
	uint32_t top6;
	uint32_t top9;
	int used_literal;
	int error;

	dispatch = run->dispatch;
	run->result->steps++;

	/* The pc stays inside the code. */
	if (wave->pc + 1U > dispatch->code_words)
		return fail(run, "the pc left the code");
	word0 = dispatch->code[wave->pc];
	word1 = 0U;
	if (wave->pc + 1U < dispatch->code_words)
		word1 = dispatch->code[wave->pc + 1U];
	run->word = word0;
	literal = word1;

	top9 = word0 >> 23;
	top7 = word0 >> 25;
	top6 = word0 >> 26;
	used_literal = 0;
	length = 1U;
	error = 0;

	/* Decodes the form from the high bits and runs it. */
	if (top9 == 0x17fU) {
		error = control(run, wave, word0);
		if (error != 0)
			return error;
		return 0;
	} else if (top9 == 0x17dU || top9 == 0x17eU || (word0 >> 28) == 0xbU || (word0 >> 30) == 2U) {
		error = scalar_alu(run, wave, word0, literal, &used_literal);
	} else if (top7 == 0x3fU || top7 == 0x3eU || (word0 >> 31) == 0U) {
		error = vector_alu(run, wave, word0, 0U, 0, literal, &used_literal);
	} else if (top6 == 0x35U) {
		length = 2U;
		literal = 0U;
		if (wave->pc + 2U < dispatch->code_words)
			literal = dispatch->code[wave->pc + 2U];
		error = vector_alu(run, wave, word0, word1, 1, literal, &used_literal);
	} else if (top6 == 0x3dU) {
		length = 2U;
		error = scalar_load(run, wave, word0, word1);
	} else if (top6 == 0x38U) {
		length = 2U;
		error = buffer_access(run, wave, word0, word1);
	} else if (top6 == 0x36U) {
		length = 2U;
		error = shared_access(run, wave, word0, word1);
	} else if (top6 == 0x3cU) {
		length = 2U;
		error = image_access(run, wave, word0, word1);
	} else if (top6 == 0x32U) {
		error = interpolate(run, wave, word0);
	} else if (top6 == 0x3eU) {
		length = 2U;
		error = export_access(run, wave, word0, word1);
	} else {
		return fail(run, "an instruction form the machine does not know");
	}
	if (error != 0)
		return error;

	/* Moves past the instruction and its literal. */
	wave->pc += length;
	if (used_literal != 0)
		wave->pc++;
	return 0;
}

/* Records the first error and stops the run. */
static int
fail(
	struct run *run,
	const char *error)
{
	/* The first error is the one reported. */
	if (run->result->error == NULL) {
		run->result->error = error;
		run->result->error_pc = run->wave != NULL ? run->wave->pc : 0U;
		run->result->error_word = run->word;
	}
	return 1;
}

/* Checks that a scalar register read is not waiting for its counter. */
static int
check_scalar_read(
	struct run *run,
	struct wave *wave,
	unsigned code)
{
	/* Constants are always ready. */
	if (code >= 128U)
		return 0;

	/* A pending register is read before its wait. */
	if (wave->pending[code] != 0U)
		return fail(run, "a scalar register is read before the wait for it");
	return 0;
}

/* Checks that a VGPR read is not waiting for its counter. */
static int
check_vector_read(
	struct run *run,
	struct wave *wave,
	unsigned code)
{
	/* A pending register is read before its wait. */
	if (wave->pending[code] != 0U)
		return fail(run, "a register is read before the wait for it");
	return 0;
}

/* Checks that a register write is not overtaken by a pending result or an export's late read. */
static int
check_write(
	struct run *run,
	struct wave *wave,
	unsigned code)
{
	/* NULL and constants are no registers. */
	if (code == CODE_NULL || (code >= 128U && code < 256U))
		return 0;

	/* A pending register is written before its wait. */
	if (wave->pending[code] != 0U)
		return fail(run, "a register is written before the wait for it");
	return 0;
}

/* Reads a source operand for one lane: a scalar register, a constant, the literal, or a VGPR. */
static int
source_value(
	struct run *run,
	struct wave *wave,
	unsigned code,
	unsigned lane,
	uint32_t literal,
	uint32_t *value)
{
	int error;

	/* A VGPR. */
	if (code >= 256U) {
		error = check_vector_read(run, wave, code);
		if (error != 0)
			return error;
		*value = wave->v[code - 256U][lane];
		return 0;
	}

	/* A scalar register. */
	if (code < 128U) {
		error = check_scalar_read(run, wave, code);
		if (error != 0)
			return error;
		*value = wave->s[code];
		if (code == CODE_NULL)
			*value = 0U;
		return 0;
	}

	/* The inline integers and floats, and the literal. */
	if (code <= 192U) {
		*value = code - 128U;
	} else if (code <= 208U) {
		*value = 0U - (code - 192U);
	} else if (code == 240U) {
		*value = 0x3f000000U;
	} else if (code == 241U) {
		*value = 0xbf000000U;
	} else if (code == 242U) {
		*value = 0x3f800000U;
	} else if (code == 243U) {
		*value = 0xbf800000U;
	} else if (code == 244U) {
		*value = 0x40000000U;
	} else if (code == 245U) {
		*value = 0xc0000000U;
	} else if (code == 246U) {
		*value = 0x40800000U;
	} else if (code == 247U) {
		*value = 0xc0800000U;
	} else if (code == 248U) {
		*value = 0x3e22f983U;
	} else if (code == CODE_LITERAL) {
		*value = literal;
	} else {
		return fail(run, "an operand code the machine does not know");
	}
	return 0;
}

/* The lanes the wave runs. */
static uint32_t
lane_mask(
	const struct wave *wave)
{
	return wave->s[CODE_EXEC];
}

/* Finds the host bytes of a GPU range, NULL when no region holds all of it. */
static uint8_t *
memory_at(
	struct run *run,
	uint64_t address,
	uint32_t size)
{
	struct machine_region *region;
	uint32_t index;

	/* The region that holds the whole range. */
	for (index = 0U; index < run->memory->count; index++) {
		region = &run->memory->regions[index];
		if (address >= region->address && address + size <= region->address + region->size)
			return region->bytes + (address - region->address);
	}
	return NULL;
}

/* The float of 32 bits. */
static float
as_float(
	uint32_t bits)
{
	float value;

	memcpy(&value, &bits, sizeof(value));
	return value;
}

/* The 32 bits of a float. */
static uint32_t
as_bits(
	float value)
{
	uint32_t bits;

	memcpy(&bits, &value, sizeof(bits));
	return bits;
}

/* The 16-bit float nearest a float (round to nearest even). */
static uint32_t
half_from_float(
	float value)
{
	_Float16 half;
	uint16_t bits;

	half = (_Float16)value;
	memcpy(&bits, &half, sizeof(bits));
	return bits;
}

/* The float of a 16-bit float. */
static float
float_from_half(
	uint32_t half)
{
	_Float16 value;
	uint16_t bits;

	bits = (uint16_t)half;
	memcpy(&value, &bits, sizeof(value));
	return (float)value;
}

/* Runs a scalar ALU instruction (two sources, a constant, one source or a comparison). */
static int
scalar_alu(
	struct run *run,
	struct wave *wave,
	uint32_t word,
	uint32_t literal,
	int *used_literal)
{
	unsigned opcode;
	unsigned dst;
	unsigned src0;
	unsigned src1;
	uint32_t a;
	uint32_t b;
	uint32_t value;
	uint64_t wide;
	int writes;
	int error;
	int index;

	a = 0U;
	b = 0U;
	writes = 1;
	value = 0U;

	/* A scalar write ends the scalar load reads' hazard. */
	src0 = word & 0xffU;
	src1 = (word >> 8) & 0xffU;
	dst = (word >> 16) & 0x7fU;

	/* The one-source form. */
	if ((word >> 23) == 0x17dU) {
		opcode = (word >> 8) & 0xffU;
		error = source_value(run, wave, src0, 0U, literal, &a);
		if (error != 0)
			return error;
		if (src0 == CODE_LITERAL)
			*used_literal = 1;
		switch (opcode) {
		case 0x03U:
			value = a;
			break;
		case 0x07U:
			value = ~a;
			wave->scc = value != 0U;
			break;
		case 0x09U:
			/* Every quad of lanes with any lane set becomes whole. */
			value = 0U;
			for (index = 0; index < 32; index += 4) {
				if (((a >> index) & 0xfU) != 0U)
					value |= 0xfU << index;
			}
			wave->scc = value != 0U;
			break;
		case 0x3cU:
			/* The old lane mask to the destination, the source ANDed into the lane mask. */
			error = check_write(run, wave, dst);
			if (error != 0)
				return error;
			if (wave->read_by_load[CODE_EXEC] || wave->read_by_store[CODE_EXEC] || wave->read_by_shared[CODE_EXEC])
				return fail(run, "a scalar write of the lane mask vector or shared memory still reads (VMEMtoScalarWrite)");
			value = wave->s[CODE_EXEC];
			wave->s[CODE_EXEC] = a & wave->s[CODE_EXEC];
			wave->scc = wave->s[CODE_EXEC] != 0U;
			break;
		default:
			return fail(run, "a scalar one-source opcode the machine does not know");
		}
	} else if ((word >> 23) == 0x17eU) {
		/* The comparison form: sets SCC only. */
		opcode = (word >> 16) & 0x7fU;
		error = source_value(run, wave, src0, 0U, literal, &a);
		if (error == 0)
			error = source_value(run, wave, src1, 0U, literal, &b);
		if (error != 0)
			return error;
		if (src0 == CODE_LITERAL || src1 == CODE_LITERAL)
			*used_literal = 1;
		writes = 0;
		switch (opcode) {
		case 0x06U:
			wave->scc = a == b;
			break;
		case 0x07U:
			wave->scc = a != b;
			break;
		case 0x0dU:
			wave->scc = ((a >> (b & 31U)) & 1U) != 0U;
			break;
		default:
			return fail(run, "a scalar comparison opcode the machine does not know");
		}
	} else if ((word >> 28) == 0xbU) {
		/* The constant form. */
		opcode = (word >> 23) & 0x1fU;
		switch (opcode) {
		case 0x00U:
			value = (uint32_t)(int32_t)(int16_t)(word & 0xffffU);
			break;
		case 0x17U:
			/* The store counter wait: stores are done when they issue here. */
			writes = 0;
			if ((word & 0xffffU) == 0U && dst == CODE_NULL) {
				clear_set(wave->read_by_store);
				wave->vector_memory = 0;
				wave->shared_memory = 0;
				wave->branch_after_vector = 0;
				wave->branch_after_shared = 0;
			}
			break;
		default:
			return fail(run, "a scalar constant opcode the machine does not know");
		}
	} else {
		/* The two-source form. */
		opcode = (word >> 23) & 0x7fU;
		error = source_value(run, wave, src0, 0U, literal, &a);
		if (error == 0)
			error = source_value(run, wave, src1, 0U, literal, &b);
		if (error != 0)
			return error;
		if (src0 == CODE_LITERAL || src1 == CODE_LITERAL)
			*used_literal = 1;
		switch (opcode) {
		case 0x00U:
			wide = (uint64_t)a + b;
			value = (uint32_t)wide;
			wave->scc = (wide >> 32) != 0U;
			break;
		case 0x01U:
			value = a - b;
			wave->scc = b > a;
			break;
		case 0x02U:
			value = a + b;
			break;
		case 0x0aU:
			value = wave->scc ? a : b;
			break;
		case 0x0eU:
			value = a & b;
			wave->scc = value != 0U;
			break;
		case 0x10U:
			value = a | b;
			wave->scc = value != 0U;
			break;
		case 0x12U:
			value = a ^ b;
			wave->scc = value != 0U;
			break;
		case 0x14U:
			value = a & ~b;
			wave->scc = value != 0U;
			break;
		case 0x1eU:
			value = a << (b & 31U);
			wave->scc = value != 0U;
			break;
		case 0x20U:
			value = a >> (b & 31U);
			wave->scc = value != 0U;
			break;
		case 0x26U:
			value = a * b;
			break;
		default:
			return fail(run, "a scalar two-source opcode the machine does not know");
		}
	}

	/* The write, and what it does to the hazards. */
	if (writes != 0 && dst != CODE_NULL) {
		error = check_write(run, wave, dst);
		if (error != 0)
			return error;
		if (wave->read_by_load[dst] || wave->read_by_store[dst] || wave->read_by_shared[dst])
			return fail(run, "a scalar write of a register vector or shared memory still reads (VMEMtoScalarWrite)");
		wave->s[dst] = value;
	}
	if (writes != 0)
		clear_set(wave->read_by_scalar_load);
	return 0;
}

/* Runs a program control instruction (a branch, a wait, the end, a barrier, an idle). */
static int
control(
	struct run *run,
	struct wave *wave,
	uint32_t word)
{
	unsigned opcode;
	int16_t distance;
	int taken;

	opcode = (word >> 16) & 0x7fU;
	distance = (int16_t)(word & 0xffffU);
	taken = -1;

	/* Picks by the opcode. */
	switch (opcode) {
	case 0x00U:
		break;
	case 0x01U:
		wave->state = WAVE_ENDED;
		return 0;
	case 0x02U:
		taken = 1;
		break;
	case 0x04U:
		taken = wave->scc == 0;
		break;
	case 0x05U:
		taken = wave->scc != 0;
		break;
	case 0x06U:
	case 0x07U:
		if (check_scalar_read(run, wave, CODE_VCC) != 0)
			return 1;
		taken = (wave->s[CODE_VCC] == 0U) == (opcode == 0x06U);
		break;
	case 0x08U:
	case 0x09U:
		taken = (wave->s[CODE_EXEC] == 0U) == (opcode == 0x08U);
		break;
	case 0x0aU:
		/* A barrier waits for the other waves. */
		wave->pc++;
		wave->state = WAVE_AT_BARRIER;
		return 0;
	case 0x0cU:
		note_wait(wave, word & 0xffffU);
		break;
	case 0x1fU:
		return fail(run, "the end-of-code padding was run");
	case 0x23U:
		/* The dependency wait: 0xffe3 lets the vector memory reads of scalar registers finish. */
		if (((word >> 2) & 7U) == 0U) {
			clear_set(wave->read_by_load);
			clear_set(wave->read_by_store);
			clear_set(wave->read_by_shared);
		}
		break;
	default:
		return fail(run, "a control opcode the machine does not know");
	}

	/* A branch: the mishandled distance is an error; the branch hazard notes it. */
	if (taken >= 0) {
		if (distance == 0x3f)
			return fail(run, "a branch with the distance GFX10 mishandles");
		wave->branch_after_vector |= wave->vector_memory;
		wave->branch_after_shared |= wave->shared_memory;
		wave->vector_memory = 0;
		wave->shared_memory = 0;
		if (taken != 0) {
			wave->pc = (uint32_t)((int32_t)wave->pc + 1 + distance);
			return 0;
		}
	}

	wave->pc++;
	return 0;
}

/* Runs a scalar load: from an address pair, or through a buffer descriptor. */
static int
scalar_load(
	struct run *run,
	struct wave *wave,
	uint32_t word0,
	uint32_t word1)
{
	unsigned opcode;
	unsigned data;
	unsigned base;
	unsigned count;
	unsigned index;
	uint32_t offset;
	uint64_t address;
	uint8_t *bytes;
	uint32_t value;
	int error;

	opcode = (word0 >> 18) & 0xffU;
	data = (word0 >> 6) & 0x7fU;
	base = (word0 & 0x3fU) << 1;
	offset = word1 & 0x1fffffU;

	/* The words loaded. */
	switch (opcode) {
	case 0x00U:
		count = 1U;
		break;
	case 0x01U:
		count = 2U;
		break;
	case 0x02U:
		count = 4U;
		break;
	case 0x03U:
		count = 8U;
		break;
	case 0x08U:
		count = 1U;
		break;
	case 0x0aU:
		count = 4U;
		break;
	default:
		return fail(run, "a scalar load opcode the machine does not know");
	}

	/* The base registers are ready, and the destination is not waiting for anything. */
	for (index = 0U; index < (opcode >= 0x08U ? 4U : 2U); index++) {
		error = check_scalar_read(run, wave, base + index);
		if (error != 0)
			return error;
	}
	for (index = 0U; index < count; index++) {
		error = check_write(run, wave, data + index);
		if (error != 0)
			return error;
		if (wave->read_by_load[data + index] || wave->read_by_store[data + index] || wave->read_by_shared[data + index])
			return fail(run, "a scalar load writes a register vector or shared memory still reads (VMEMtoScalarWrite)");
	}

	/* Loads each word: a buffer load reads 0 past the descriptor's record count. */
	for (index = 0U; index < count; index++) {
		value = 0U;
		if (opcode >= 0x08U) {
			address = (uint64_t)wave->s[base] | ((uint64_t)(wave->s[base + 1U] & 0xffffU) << 32);
			if (offset + 4U * index + 4U <= wave->s[base + 2U]) {
				bytes = memory_at(run, address + offset + 4U * index, 4U);
				if (bytes == NULL)
					return fail(run, "a scalar buffer load outside memory");
				memcpy(&value, bytes, 4U);
			}
		} else {
			address = (uint64_t)wave->s[base] | ((uint64_t)wave->s[base + 1U] << 32);
			bytes = memory_at(run, address + offset + 4U * index, 4U);
			if (bytes == NULL)
				return fail(run, "a scalar load outside memory");
			memcpy(&value, bytes, 4U);
		}
		wave->s[data + index] = value;
		wave->pending[data + index] |= PENDING_SCALAR;
	}

	/* The base registers stay read for the SMEMtoVectorWrite hazard. */
	mark_scalar_reads(wave->read_by_scalar_load, base, opcode >= 0x08U ? 4U : 2U);
	return 0;
}

/* Runs a vector ALU instruction in the short or the wide form. */
static int
vector_alu(
	struct run *run,
	struct wave *wave,
	uint32_t word0,
	uint32_t word1,
	int wide,
	uint32_t literal,
	int *used_literal)
{
	unsigned opcode;
	unsigned dst;
	unsigned src[3];
	unsigned neg;
	unsigned abs;
	unsigned lane;
	unsigned index;
	unsigned sources;
	uint32_t mask;
	uint32_t values[3];
	uint32_t result;
	uint32_t compare;
	uint32_t condition;
	int is_compare;
	int known;
	int error;

	src[1] = 0U;
	src[2] = 0U;
	neg = 0U;
	abs = 0U;
	is_compare = 0;
	sources = 2U;

	/* Decodes the fields of the form; the wide opcode is the short one moved by its offset. */
	if (wide != 0) {
		opcode = (word0 >> 16) & 0x3ffU;
		dst = word0 & 0xffU;
		abs = (word0 >> 8) & 7U;
		neg = (word1 >> 29) & 7U;
		src[0] = word1 & 0x1ffU;
		src[1] = (word1 >> 9) & 0x1ffU;
		src[2] = (word1 >> 18) & 0x1ffU;
		sources = 3U;
		if (opcode < 0x100U) {
			is_compare = 1;
		} else {
			dst += 256U;
			if (opcode >= 0x180U && opcode < 0x200U && (opcode - 0x180U) == 0x02U)
				dst -= 256U;
		}
	} else if ((word0 >> 25) == 0x3fU) {
		opcode = 0x180U + ((word0 >> 9) & 0xffU);
		dst = 256U + ((word0 >> 17) & 0xffU);
		if (opcode == 0x182U)
			dst -= 256U;
		src[0] = word0 & 0x1ffU;
		sources = 1U;
	} else if ((word0 >> 25) == 0x3eU) {
		opcode = (word0 >> 17) & 0xffU;
		dst = CODE_VCC;
		src[0] = word0 & 0x1ffU;
		src[1] = 256U + ((word0 >> 9) & 0xffU);
		is_compare = 1;
	} else {
		opcode = 0x100U + ((word0 >> 25) & 0x3fU);
		dst = 256U + ((word0 >> 17) & 0xffU);
		src[0] = word0 & 0x1ffU;
		src[1] = 256U + ((word0 >> 9) & 0xffU);
		if (opcode == 0x101U) {
			src[2] = CODE_VCC;
			sources = 3U;
		}
	}
	for (index = 0U; index < 3U; index++) {
		if (src[index] == CODE_LITERAL)
			*used_literal = 1;
	}

	/* The constant bus: at most two distinct scalar values (SGPRs, VCC, the literal) among the sources read. */
	{
		unsigned seen[3];
		unsigned seen_count = 0U;
		unsigned reads = sources;
		unsigned other;
		int known_scalar;

		if (wide != 0 && opcode >= 0x180U && opcode < 0x200U)
			reads = 1U;
		if (wide != 0 && opcode >= 0x100U && opcode < 0x180U && opcode != 0x101U)
			reads = 2U;
		if (wide != 0 && opcode < 0x100U)
			reads = 2U;
		if (wide != 0 && opcode >= 0x200U && opcode != 0x143U && opcode != 0x14bU)
			reads = 2U;
		for (index = 0U; index < reads; index++) {
			if (src[index] >= 128U && src[index] != CODE_LITERAL)
				continue;
			known_scalar = 0;
			for (other = 0U; other < seen_count; other++) {
				if (seen[other] == src[index])
					known_scalar = 1;
			}
			if (known_scalar == 0) {
				if (seen_count == 2U)
					return fail(run, "a vector instruction reads more than two scalar values (the constant bus)");
				seen[seen_count] = src[index];
				seen_count++;
			}
		}
	}

	/* The destination is not waiting for anything; a scalar one ends no hazard here but is checked. */
	error = check_write(run, wave, dst);
	if (error != 0)
		return error;
	if (dst < 128U) {
		if (wave->read_by_scalar_load[dst] != 0U)
			return fail(run, "a vector write of a scalar register a scalar load reads (SMEMtoVectorWrite)");
		if (dst == CODE_EXEC)
			return fail(run, "a vector instruction writes the lane mask, which the compiler never does");
	}

	/* A vector ALU instruction ends the vector memory reads' hazard. */
	clear_set(wave->read_by_load);
	clear_set(wave->read_by_store);
	clear_set(wave->read_by_shared);

	/* Each lane the wave runs. */
	mask = lane_mask(wave);
	compare = 0U;
	for (lane = 0U; lane < MACHINE_LANES; lane++) {
		if ((mask & (1U << lane)) == 0U)
			continue;

		/* The sources, with the float modifiers. */
		for (index = 0U; index < sources; index++) {
			values[index] = 0U;
			if (wide != 0 && index > 0U && opcode >= 0x180U && opcode < 0x200U)
				continue;
			error = source_value(run, wave, src[index], lane, literal, &values[index]);
			if (error != 0)
				return error;
			if ((abs & (1U << index)) != 0U)
				values[index] &= 0x7fffffffU;
			if ((neg & (1U << index)) != 0U)
				values[index] ^= 0x80000000U;
		}

		/* The selection reads its condition register's bit. */
		if (opcode == 0x101U) {
			condition = values[2];
			result = ((condition >> lane) & 1U) != 0U ? values[1] : values[0];
			wave->v[dst - 256U][lane] = result;
			continue;
		}

		/* The first lane's value goes to a scalar register. */
		if (opcode == 0x182U) {
			wave->s[dst] = values[0];
			break;
		}

		/* Computes the lane's result. */
		known = 1;
		result = vector_operation(opcode, values[0], values[1], values[2], &known);
		if (known == 0)
			return fail(run, "a vector opcode the machine does not know");
		if (is_compare != 0) {
			if (result != 0U)
				compare |= 1U << lane;
		} else {
			wave->v[dst - 256U][lane] = result;
		}
	}

	/* A comparison writes the mask, zero for the lanes the wave does not run. */
	if (is_compare != 0)
		wave->s[dst] = compare;
	return 0;
}

/* Computes one lane of a vector operation by its wide opcode; *known is 0 for an opcode not known. */
static uint32_t
vector_operation(
	unsigned opcode,
	uint32_t a,
	uint32_t b,
	uint32_t c,
	int *known)
{
	float fa;
	float fb;
	float fc;
	double angle;

	fa = as_float(a);
	fb = as_float(b);
	fc = as_float(c);

	/* Picks the operation by the wide opcode. */
	switch (opcode) {
	case 0x001U:
		return fa < fb;
	case 0x002U:
		return fa == fb;
	case 0x006U:
		return fa >= fb;
	case 0x00dU:
		return !(fa == fb);
	case 0x081U:
		return (int32_t)a < (int32_t)b;
	case 0x082U:
	case 0x0c2U:
		return a == b;
	case 0x085U:
	case 0x0c5U:
		return a != b;
	case 0x086U:
		return (int32_t)a >= (int32_t)b;
	case 0x0c1U:
		return a < b;
	case 0x0c6U:
		return a >= b;
	case 0x103U:
		return as_bits(fa + fb);
	case 0x104U:
		return as_bits(fa - fb);
	case 0x108U:
		return as_bits(fa * fb);
	case 0x10fU:
		return as_bits(fminf(fa, fb));
	case 0x110U:
		return as_bits(fmaxf(fa, fb));
	case 0x111U:
		return (int32_t)a < (int32_t)b ? a : b;
	case 0x112U:
		return (int32_t)a > (int32_t)b ? a : b;
	case 0x113U:
		return a < b ? a : b;
	case 0x114U:
		return a > b ? a : b;
	case 0x116U:
		return b >> (a & 31U);
	case 0x118U:
		return (uint32_t)((int32_t)b >> (a & 31U));
	case 0x11aU:
		return b << (a & 31U);
	case 0x11bU:
		return a & b;
	case 0x11cU:
		return a | b;
	case 0x11dU:
		return a ^ b;
	case 0x125U:
		return a + b;
	case 0x126U:
		return a - b;
	case 0x181U:
		return a;
	case 0x185U:
		return as_bits((float)(int32_t)a);
	case 0x186U:
		return as_bits((float)a);
	case 0x187U:
		/* NaN and negatives give 0, large values the largest. */
		if (fa != fa || fa <= 0.0f)
			return 0U;
		if (fa >= 4294967296.0f)
			return 0xffffffffU;
		return (uint32_t)fa;
	case 0x188U:
		if (fa != fa)
			return 0U;
		if (fa >= 2147483648.0f)
			return 0x7fffffffU;
		if (fa <= -2147483648.0f)
			return 0x80000000U;
		return (uint32_t)(int32_t)fa;
	case 0x18aU:
		return half_from_float(fa);
	case 0x18bU:
		return as_bits(float_from_half(a & 0xffffU));
	case 0x1a1U:
		return as_bits(truncf(fa));
	case 0x1a3U:
		return as_bits(rintf(fa));
	case 0x1a4U:
		return as_bits(floorf(fa));
	case 0x1a5U:
		return as_bits(exp2f(fa));
	case 0x1a7U:
		return as_bits(log2f(fa));
	case 0x1aaU:
	case 0x1abU:
		return as_bits(1.0f / fa);
	case 0x1aeU:
		return as_bits(1.0f / sqrtf(fa));
	case 0x1b3U:
		return as_bits(sqrtf(fa));
	case 0x1b5U:
		/* The angle is in turns. */
		angle = (double)fa * 6.283185307179586;
		return as_bits((float)sin(angle));
	case 0x1b6U:
		angle = (double)fa * 6.283185307179586;
		return as_bits((float)cos(angle));
	case 0x1b7U:
		return ~a;
	case 0x143U:
		return (a & 0xffffffU) * (b & 0xffffffU) + c;
	case 0x14bU:
		return as_bits(fmaf(fa, fb, fc));
	case 0x169U:
		return a * b;
	case 0x16aU:
		return (uint32_t)(((uint64_t)a * b) >> 32);
	case 0x311U:
		return (a & 0xffffU) | (b << 16);
	default:
		*known = 0;
		return 0U;
	}
}

/* Runs a buffer access: a load, a store, an atomic, or a cache invalidation. */
static int
buffer_access(
	struct run *run,
	struct wave *wave,
	uint32_t word0,
	uint32_t word1)
{
	unsigned opcode;
	unsigned vaddr;
	unsigned vdata;
	unsigned descriptor;
	unsigned soffset_code;
	unsigned lane;
	uint32_t offset;
	uint32_t soffset;
	uint32_t mask;
	uint32_t byte;
	uint32_t old;
	uint32_t value;
	uint64_t base;
	uint32_t records;
	uint8_t *bytes;
	int offen;
	int glc;
	int error;
	int index;

	opcode = (word0 >> 18) & 0xffU;

	/* The cache invalidations do nothing here. */
	if (opcode == 0x71U || opcode == 0x72U)
		return 0;

	offen = ((word0 >> 12) & 1U) != 0U;
	glc = ((word0 >> 14) & 1U) != 0U;
	offset = word0 & 0xfffU;
	vaddr = 256U + (word1 & 0xffU);
	vdata = 256U + ((word1 >> 8) & 0xffU);
	descriptor = ((word1 >> 16) & 0x1fU) << 2;
	soffset_code = (word1 >> 24) & 0xffU;

	/* A typed fetch by record index. */
	if (opcode == 0x03U)
		return format_fetch(run, wave, word0, word1);

	/* Only byte offsets are used for storage. */
	if (offen == 0)
		return fail(run, "a buffer access without a byte offset register");

	/* The descriptor, the offset and the address are ready. */
	for (index = 0; index < 4; index++) {
		error = check_scalar_read(run, wave, descriptor + (unsigned)index);
		if (error != 0)
			return error;
	}
	error = source_value(run, wave, soffset_code, 0U, 0U, &soffset);
	if (error != 0)
		return error;
	error = check_vector_read(run, wave, vaddr);
	if (error != 0)
		return error;

	base = (uint64_t)wave->s[descriptor] | ((uint64_t)(wave->s[descriptor + 1U] & 0xffffU) << 32);
	records = wave->s[descriptor + 2U];

	/* The data register is ready for a store or an atomic, and free for a load. */
	if (opcode == 0x0cU) {
		error = check_write(run, wave, vdata);
	} else {
		error = check_vector_read(run, wave, vdata);
		if (error == 0 && opcode == 0x31U)
			error = check_vector_read(run, wave, vdata + 1U);
		if (error == 0 && glc)
			error = check_write(run, wave, vdata);
	}
	if (error != 0)
		return error;

	/* Each lane the wave runs, in lane order. */
	mask = lane_mask(wave);
	for (lane = 0U; lane < MACHINE_LANES; lane++) {
		if ((mask & (1U << lane)) == 0U)
			continue;
		byte = wave->v[vaddr - 256U][lane] + soffset + offset;

		/* Out of the record count: a load reads 0, a store or an atomic does nothing. */
		bytes = NULL;
		if ((uint64_t)byte + 4U <= records) {
			bytes = memory_at(run, base + byte, 4U);
			if (bytes == NULL)
				return fail(run, "a buffer access inside its records but outside memory");
		}

		/* Picks the operation. */
		old = 0U;
		if (bytes != NULL)
			memcpy(&old, bytes, 4U);
		value = wave->v[vdata - 256U][lane];
		switch (opcode) {
		case 0x0cU:
			wave->v[vdata - 256U][lane] = old;
			continue;
		case 0x1cU:
			break;
		case 0x30U:
			break;
		case 0x31U:
			/* The compare is the next register; the data replaces the word only where it matches. */
			if (old != wave->v[vdata + 1U - 256U][lane])
				value = old;
			break;
		case 0x32U:
			value = old + value;
			break;
		case 0x33U:
			value = old - value;
			break;
		case 0x35U:
			value = (int32_t)value < (int32_t)old ? value : old;
			break;
		case 0x36U:
			value = value < old ? value : old;
			break;
		case 0x37U:
			value = (int32_t)value > (int32_t)old ? value : old;
			break;
		case 0x38U:
			value = value > old ? value : old;
			break;
		case 0x39U:
			value = old & value;
			break;
		case 0x3aU:
			value = old | value;
			break;
		case 0x3bU:
			value = old ^ value;
			break;
		default:
			return fail(run, "a buffer opcode the machine does not know");
		}
		if (bytes != NULL)
			memcpy(bytes, &value, 4U);
		if (opcode != 0x1cU && glc)
			wave->v[vdata - 256U][lane] = old;
	}

	/* A load or a returning atomic leaves its register pending on the vector counter. */
	if (opcode == 0x0cU || (opcode != 0x1cU && glc))
		wave->pending[vdata] |= PENDING_VECTOR;

	/* The scalar registers read stay read for the VMEMtoScalarWrite hazard; the lane mask too. */
	if (opcode == 0x0cU || (opcode != 0x1cU && glc)) {
		mark_scalar_reads(wave->read_by_load, descriptor, 4U);
		mark_scalar_reads(wave->read_by_load, soffset_code, 1U);
		mark_scalar_reads(wave->read_by_load, CODE_EXEC, 1U);
	} else {
		mark_scalar_reads(wave->read_by_store, descriptor, 4U);
		mark_scalar_reads(wave->read_by_store, soffset_code, 1U);
		mark_scalar_reads(wave->read_by_store, CODE_EXEC, 1U);
	}

	/* The branch hazard: a shared memory access before a branch before this one. */
	if (wave->branch_after_shared != 0)
		return fail(run, "a buffer access after a branch after a shared access, without a store wait (LdsBranchVmemWAR)");
	wave->vector_memory = 1;
	wave->shared_memory = 0;
	wave->branch_after_vector = 0;
	return 0;
}

/* Runs a shared memory access: a read, a write, or an atomic that returns the old value. */
static int
shared_access(
	struct run *run,
	struct wave *wave,
	uint32_t word0,
	uint32_t word1)
{
	unsigned opcode;
	unsigned addr;
	unsigned data0;
	unsigned data1;
	unsigned vdst;
	unsigned lane;
	uint32_t offset;
	uint32_t mask;
	uint32_t byte;
	uint32_t old;
	uint32_t value;
	int returns;
	int reads_data0;
	int reads_data1;
	int error;

	opcode = (word0 >> 18) & 0xffU;
	offset = word0 & 0xffffU;
	addr = 256U + (word1 & 0xffU);
	data0 = 256U + ((word1 >> 8) & 0xffU);
	data1 = 256U + ((word1 >> 16) & 0xffU);
	vdst = 256U + ((word1 >> 24) & 0xffU);

	/* The registers the operation reads and writes. */
	returns = opcode != 0x0dU;
	reads_data0 = opcode != 0x36U;
	reads_data1 = opcode == 0x30U;
	error = check_vector_read(run, wave, addr);
	if (error == 0 && reads_data0)
		error = check_vector_read(run, wave, data0);
	if (error == 0 && reads_data1)
		error = check_vector_read(run, wave, data1);
	if (error == 0 && returns)
		error = check_write(run, wave, vdst);
	if (error != 0)
		return error;

	/* Each lane the wave runs, in lane order. */
	mask = lane_mask(wave);
	for (lane = 0U; lane < MACHINE_LANES; lane++) {
		if ((mask & (1U << lane)) == 0U)
			continue;
		byte = wave->v[addr - 256U][lane] + offset;
		if ((uint64_t)byte + 4U > run->dispatch->shared_bytes)
			return fail(run, "a shared memory access outside the workgroup's shared memory");
		memcpy(&old, run->shared + byte, 4U);
		value = wave->v[data0 - 256U][lane];
		switch (opcode) {
		case 0x36U:
			wave->v[vdst - 256U][lane] = old;
			continue;
		case 0x0dU:
			break;
		case 0x20U:
			value = old + value;
			break;
		case 0x21U:
			value = old - value;
			break;
		case 0x25U:
			value = (int32_t)value < (int32_t)old ? value : old;
			break;
		case 0x26U:
			value = (int32_t)value > (int32_t)old ? value : old;
			break;
		case 0x27U:
			value = value < old ? value : old;
			break;
		case 0x28U:
			value = value > old ? value : old;
			break;
		case 0x29U:
			value = old & value;
			break;
		case 0x2aU:
			value = old | value;
			break;
		case 0x2bU:
			value = old ^ value;
			break;
		case 0x2dU:
			break;
		case 0x30U:
			/* data0 is the compare, data1 the value stored where it matches. */
			value = old == value ? wave->v[data1 - 256U][lane] : old;
			break;
		default:
			return fail(run, "a shared memory opcode the machine does not know");
		}
		memcpy(run->shared + byte, &value, 4U);
		if (returns)
			wave->v[vdst - 256U][lane] = old;
	}

	/* A returning access leaves its register pending on the scalar (and shared) counter. */
	if (returns)
		wave->pending[vdst] |= PENDING_SCALAR;
	mark_scalar_reads(wave->read_by_shared, CODE_EXEC, 1U);

	/* The branch hazard: a vector memory access before a branch before this one. */
	if (wave->branch_after_vector != 0)
		return fail(run, "a shared access after a branch after a buffer access, without a store wait (LdsBranchVmemWAR)");
	wave->shared_memory = 1;
	wave->vector_memory = 0;
	wave->branch_after_shared = 0;
	return 0;
}

/* Records a counter wait: the counters whose fields are zero are done. */
static void
note_wait(
	struct wave *wave,
	uint32_t immediate)
{
	unsigned vector_count;
	unsigned index;
	uint8_t done;

	done = 0U;
	vector_count = (immediate & 0xfU) | ((immediate >> 10) & 0x30U);
	if (vector_count == 0U) {
		done |= PENDING_VECTOR;
		clear_set(wave->read_by_load);
	}
	if (((immediate >> 4) & 7U) == 0U)
		done |= PENDING_EXPORT;
	if (((immediate >> 8) & 0x3fU) == 0U) {
		done |= PENDING_SCALAR;
		clear_set(wave->read_by_shared);
		clear_set(wave->read_by_scalar_load);
	}
	for (index = 0U; index < 512U; index++)
		wave->pending[index] = (uint8_t)(wave->pending[index] & ~done);
}

/* Empties a set of scalar registers. */
static void
clear_set(
	uint8_t *set)
{
	memset(set, 0, 128U);
}

/* Adds count scalar registers from code to a set (VGPRs and constants are not followed). */
static void
mark_scalar_reads(
	uint8_t *set,
	unsigned code,
	unsigned count)
{
	unsigned index;

	for (index = 0U; index < count; index++) {
		if (code + index < 128U)
			set[code + index] = 1U;
	}
}

/* Runs one wave of a vertex or pixel program. */
void
machine_run_graphics(
	const struct machine_dispatch *dispatch,
	struct machine_memory *memory,
	int pixel,
	struct machine_graphics *graphics,
	struct machine_result *result)
{
	static struct wave wave;
	struct run run;
	uint32_t lane;
	uint32_t index;
	uint32_t next;
	int stopped;

	memset(result, 0, sizeof(*result));
	memset(&run, 0, sizeof(run));
	memset(&wave, 0, sizeof(wave));
	memset(graphics->exported, 0, sizeof(graphics->exported));
	memset(graphics->export_channels, 0, sizeof(graphics->export_channels));
	memset(graphics->export_lanes, 0, sizeof(graphics->export_lanes));
	memset(graphics->export_flags, 0, sizeof(graphics->export_flags));
	graphics->export_count = 0U;
	run.dispatch = dispatch;
	run.memory = memory;
	run.result = result;
	run.wave = &wave;
	run.graphics = graphics;

	/* The table address and the user SGPRs; the VGPRs past the inputs are garbage. */
	wave.s[0] = (uint32_t)dispatch->table;
	wave.s[1] = (uint32_t)(dispatch->table >> 32);
	for (index = 3U; index < 256U; index++) {
		for (lane = 0U; lane < MACHINE_LANES; lane++)
			wave.v[index][lane] = 0xdeadbeefU ^ (index * 77U + lane);
	}

	/* A vertex wave: s2 .. s4, v0 the vertex id, v3 the instance id. */
	if (pixel == 0) {
		wave.s[2] = graphics->user[0];
		wave.s[3] = graphics->user[1];
		wave.s[4] = graphics->user[2];
		for (lane = 0U; lane < MACHINE_LANES; lane++) {
			if (lane < graphics->lanes)
				wave.s[CODE_EXEC] |= 1U << lane;
			wave.v[0][lane] = graphics->first_vertex_id + lane;
			wave.v[1][lane] = 0U;
			wave.v[2][lane] = 0U;
			wave.v[3][lane] = graphics->instance_id;
		}
	} else {
		/* A pixel wave: s2 the primitive mask, the enabled inputs packed from v0. */
		wave.s[2] = 0x1234U;
		wave.s[CODE_EXEC] = graphics->coverage;
		for (lane = 0U; lane < MACHINE_LANES; lane++) {
			next = 0U;
			memcpy(&wave.v[next++][lane], &graphics->perspective[lane][0], 4U);
			memcpy(&wave.v[next++][lane], &graphics->perspective[lane][1], 4U);
			if ((dispatch->pixel_inputs & (1U << 5)) != 0U) {
				memcpy(&wave.v[next++][lane], &graphics->linear[lane][0], 4U);
				memcpy(&wave.v[next++][lane], &graphics->linear[lane][1], 4U);
			}
			if ((dispatch->pixel_inputs & (0xfU << 8)) != 0U) {
				for (index = 0U; index < 4U; index++)
					memcpy(&wave.v[next++][lane], &graphics->position[lane][index], 4U);
			}
			if ((dispatch->pixel_inputs & (1U << 12)) != 0U)
				memcpy(&wave.v[next++][lane], &graphics->face[lane], 4U);
		}
	}

	/* Runs the wave to its end. */
	while (wave.state == WAVE_RUNNING) {
		stopped = step(&run, &wave);
		if (stopped != 0)
			return;
		if (wave.state == WAVE_AT_BARRIER)
			wave.state = WAVE_RUNNING;
		if (result->steps > MACHINE_STEP_LIMIT) {
			fail(&run, "the wave runs too long");
			return;
		}
	}
}

/* Runs an export: the enabled channels of the lanes the wave runs, kept by target. */
static int
export_access(
	struct run *run,
	struct wave *wave,
	uint32_t word0,
	uint32_t word1)
{
	unsigned target;
	unsigned mask;
	unsigned channel;
	unsigned lane;
	unsigned code;
	int error;

	if (run->graphics == NULL)
		return fail(run, "an export in a compute program");
	target = (word0 >> 4) & 0x3fU;
	mask = word0 & 0xfU;

	/* The registers of the enabled channels are ready; they stay read until the export counter. */
	for (channel = 0U; channel < 4U; channel++) {
		if ((mask & (1U << channel)) == 0U)
			continue;
		code = 256U + ((word1 >> (8U * channel)) & 0xffU);
		error = check_vector_read(run, wave, code);
		if (error != 0)
			return error;
		for (lane = 0U; lane < MACHINE_LANES; lane++) {
			if ((wave->s[CODE_EXEC] & (1U << lane)) != 0U)
				run->graphics->exported[target][lane][channel] = wave->v[code - 256U][lane];
		}
		wave->pending[code] |= PENDING_EXPORT;
	}
	if ((run->graphics->export_flags[target] & 1U) != 0U)
		return fail(run, "a target exported twice");
	run->graphics->export_channels[target] = mask;
	run->graphics->export_lanes[target] = wave->s[CODE_EXEC];
	run->graphics->export_flags[target] = 1U | ((word0 >> 11) & 1U) << 1 | ((word0 >> 12) & 1U) << 2;
	run->graphics->export_count++;
	return 0;
}

/* Runs an interpolation: P0 + P10 i (the first step), P20 j added (the second), or P0 (the flat move). */
static int
interpolate(
	struct run *run,
	struct wave *wave,
	uint32_t word)
{
	unsigned vdst;
	unsigned opcode;
	unsigned attribute;
	unsigned channel;
	unsigned source;
	unsigned lane;
	const float *vertices;
	float barycentric;
	float result;
	int error;

	if (run->graphics == NULL || run->graphics->attribute == NULL)
		return fail(run, "an interpolation outside a pixel program");
	vdst = 256U + ((word >> 18) & 0xffU);
	opcode = (word >> 16) & 3U;
	attribute = (word >> 10) & 0x3fU;
	channel = (word >> 8) & 3U;
	source = word & 0xffU;

	/* M0 and the barycentric are ready, the destination is free (and ready, for the second step). */
	error = check_scalar_read(run, wave, CODE_M0);
	if (error == 0 && opcode != 2U)
		error = check_vector_read(run, wave, 256U + source);
	if (error == 0)
		error = check_write(run, wave, vdst);
	if (error != 0)
		return error;
	if (opcode == 2U && source != 2U)
		return fail(run, "a flat move of a parameter other than P0");
	clear_set(wave->read_by_load);
	clear_set(wave->read_by_store);
	clear_set(wave->read_by_shared);

	/* Each lane the wave runs, by its primitive's three vertices. */
	for (lane = 0U; lane < MACHINE_LANES; lane++) {
		if ((wave->s[CODE_EXEC] & (1U << lane)) == 0U)
			continue;
		vertices = run->graphics->attribute[run->graphics->primitive[lane]][attribute][channel];
		if (opcode == 2U) {
			result = vertices[0];
		} else if (opcode == 0U) {
			barycentric = as_float(wave->v[source][lane]);
			result = fmaf(vertices[1] - vertices[0], barycentric, vertices[0]);
		} else {
			barycentric = as_float(wave->v[source][lane]);
			result = fmaf(vertices[2] - vertices[0], barycentric, as_float(wave->v[vdst - 256U][lane]));
		}
		wave->v[vdst - 256U][lane] = as_bits(result);
	}
	return 0;
}

/* Runs a typed fetch by record index: the descriptor's format and channel selects. */
static int
format_fetch(
	struct run *run,
	struct wave *wave,
	uint32_t word0,
	uint32_t word1)
{
	unsigned vaddr;
	unsigned vdata;
	unsigned descriptor;
	unsigned lane;
	unsigned channel;
	unsigned select;
	unsigned components;
	unsigned format;
	uint32_t stride;
	uint32_t records;
	uint32_t index;
	uint32_t raw[4];
	uint32_t value;
	uint64_t base;
	uint8_t *bytes;
	int is_unorm;
	int error;

	vaddr = 256U + (word1 & 0xffU);
	vdata = 256U + ((word1 >> 8) & 0xffU);
	descriptor = ((word1 >> 16) & 0x1fU) << 2;
	if (((word0 >> 13) & 1U) == 0U)
		return fail(run, "a typed fetch without a record index");

	/* The descriptor, the index ready; the four destination registers free. */
	for (channel = 0U; channel < 4U; channel++) {
		error = check_scalar_read(run, wave, descriptor + channel);
		if (error == 0)
			error = check_write(run, wave, vdata + channel);
		if (error != 0)
			return error;
	}
	error = check_vector_read(run, wave, vaddr);
	if (error != 0)
		return error;

	base = (uint64_t)wave->s[descriptor] | ((uint64_t)(wave->s[descriptor + 1U] & 0xffffU) << 32);
	stride = (wave->s[descriptor + 1U] >> 16) & 0x3fffU;
	records = wave->s[descriptor + 2U];
	format = (wave->s[descriptor + 3U] >> 12) & 0x7fU;

	/* The channels of the format. */
	is_unorm = 0;
	switch (format) {
	case 20U: case 22U: components = 1U; break;
	case 62U: case 64U: components = 2U; break;
	case 72U: case 74U: components = 3U; break;
	case 75U: case 77U: components = 4U; break;
	case 56U: components = 4U; is_unorm = 1; break;
	default:
		return fail(run, "a vertex format the machine does not know");
	}

	/* Each lane: the record's channels, then the selects. */
	for (lane = 0U; lane < MACHINE_LANES; lane++) {
		if ((wave->s[CODE_EXEC] & (1U << lane)) == 0U)
			continue;
		index = wave->v[vaddr - 256U][lane];
		if (index >= records)
			return fail(run, "a vertex fetch past the records (the test does not use it)");
		memset(raw, 0, sizeof(raw));
		if (is_unorm != 0) {
			bytes = memory_at(run, base + (uint64_t)index * stride, 4U);
			if (bytes == NULL)
				return fail(run, "a vertex fetch outside memory");
			for (channel = 0U; channel < 4U; channel++)
				raw[channel] = as_bits((float)bytes[channel] / 255.0f);
		} else {
			bytes = memory_at(run, base + (uint64_t)index * stride, 4U * components);
			if (bytes == NULL)
				return fail(run, "a vertex fetch outside memory");
			memcpy(raw, bytes, 4U * components);
		}
		for (channel = 0U; channel < 4U; channel++) {
			select = (wave->s[descriptor + 3U] >> (3U * channel)) & 7U;
			if (select == 0U) {
				value = 0U;
			} else if (select == 1U) {
				value = (format == 20U || format == 62U || format == 72U || format == 75U) ? 1U : 0x3f800000U;
			} else if (select >= 4U) {
				value = raw[select - 4U];
			} else {
				return fail(run, "a channel select the machine does not know");
			}
			wave->v[vdata - 256U + channel][lane] = value;
		}
	}
	for (channel = 0U; channel < 4U; channel++)
		wave->pending[vdata + channel] |= PENDING_VECTOR;
	mark_scalar_reads(wave->read_by_load, descriptor, 4U);
	mark_scalar_reads(wave->read_by_load, CODE_EXEC, 1U);
	if (wave->branch_after_shared != 0)
		return fail(run, "a buffer access after a branch after a shared access (LdsBranchVmemWAR)");
	wave->vector_memory = 1;
	return 0;
}

/* Runs an image access: a sample at normalized coordinates (with an offset, a bias or a level), or a texel fetch. */
static int
image_access(
	struct run *run,
	struct wave *wave,
	uint32_t word0,
	uint32_t word1)
{
	unsigned opcode;
	unsigned vaddr;
	unsigned vdata;
	unsigned image;
	unsigned sampler;
	unsigned count;
	unsigned lane;
	unsigned index;
	unsigned next;
	uint32_t descriptor[8];
	uint32_t sampler_words[4];
	uint32_t out[4];
	uint32_t offset;
	uint32_t quad;
	int has_offset;
	int has_bias;
	int has_level;
	int implicit;
	int fetch;
	int offset_u;
	int offset_v;
	uint8_t *texels;
	int error;

	opcode = (word0 >> 18) & 0x7fU;
	vaddr = 256U + (word1 & 0xffU);
	vdata = 256U + ((word1 >> 8) & 0xffU);
	image = ((word1 >> 16) & 0x1fU) << 2;
	sampler = ((word1 >> 21) & 0x1fU) << 2;
	if (((word0 >> 8) & 0xfU) != 0xfU || ((word0 >> 3) & 7U) != 1U || ((word0 >> 1) & 3U) != 0U)
		return fail(run, "an image access other than four channels of a 2D image without NSA");

	/* The address count by the opcode. */
	has_offset = 0;
	has_bias = 0;
	has_level = 0;
	implicit = 0;
	fetch = 0;
	switch (opcode) {
	case 0x01U: fetch = 1; has_level = 1; break;
	case 0x20U: implicit = 1; break;
	case 0x24U: has_level = 1; break;
	case 0x25U: has_bias = 1; implicit = 1; break;
	case 0x27U: break;
	case 0x30U: has_offset = 1; implicit = 1; break;
	case 0x34U: has_offset = 1; has_level = 1; break;
	case 0x35U: has_offset = 1; has_bias = 1; implicit = 1; break;
	case 0x37U: has_offset = 1; break;
	default:
		return fail(run, "an image opcode the machine does not know");
	}
	if (fetch != 0 && ((word0 >> 12) & 1U) == 0U)
		return fail(run, "a texel fetch without unnormalized coordinates");
	count = 2U + (unsigned)has_offset + (unsigned)has_bias + (unsigned)has_level;

	/* The descriptors, the addresses ready; the destination free. */
	for (index = 0U; index < 8U; index++) {
		error = check_scalar_read(run, wave, image + index);
		if (error != 0)
			return error;
		descriptor[index] = wave->s[image + index];
	}
	memset(sampler_words, 0, sizeof(sampler_words));
	if (fetch == 0) {
		for (index = 0U; index < 4U; index++) {
			error = check_scalar_read(run, wave, sampler + index);
			if (error != 0)
				return error;
			sampler_words[index] = wave->s[sampler + index];
		}
	}
	for (index = 0U; index < count; index++) {
		error = check_vector_read(run, wave, vaddr + index);
		if (error != 0)
			return error;
	}
	for (index = 0U; index < 4U; index++) {
		error = check_write(run, wave, vdata + index);
		if (error != 0)
			return error;
	}

	/* A sample with derivatives needs whole quads. */
	if (implicit != 0) {
		for (quad = 0U; quad < 32U; quad += 4U) {
			if (((wave->s[CODE_EXEC] >> quad) & 0xfU) != 0U && ((wave->s[CODE_EXEC] >> quad) & 0xfU) != 0xfU)
				return fail(run, "a sample with derivatives without whole quads");
		}
	}

	texels = memory_at(run, texture_address(descriptor), 1U);
	if (texels == NULL)
		return fail(run, "an image outside memory");

	/* Each lane: its offset, then u and v (past the bias), filtered. */
	for (lane = 0U; lane < MACHINE_LANES; lane++) {
		if ((wave->s[CODE_EXEC] & (1U << lane)) == 0U)
			continue;
		next = vaddr - 256U;
		offset_u = 0;
		offset_v = 0;
		if (has_offset != 0) {
			offset = wave->v[next][lane];
			offset_u = (int)(int8_t)(uint8_t)((offset & 0x3fU) << 2) >> 2;
			offset_v = (int)(int8_t)(uint8_t)(((offset >> 8) & 0x3fU) << 2) >> 2;
			next++;
		}
		if (has_bias != 0)
			next++;
		texture_filter(texels, descriptor, sampler_words, wave->v[next][lane], wave->v[next + 1U][lane], fetch, offset_u, offset_v, out);
		for (index = 0U; index < 4U; index++)
			wave->v[vdata - 256U + index][lane] = out[index];
	}
	for (index = 0U; index < 4U; index++)
		wave->pending[vdata + index] |= PENDING_VECTOR;
	mark_scalar_reads(wave->read_by_load, image, 8U);
	if (fetch == 0)
		mark_scalar_reads(wave->read_by_load, sampler, 4U);
	mark_scalar_reads(wave->read_by_load, CODE_EXEC, 1U);
	if (wave->branch_after_shared != 0)
		return fail(run, "an image access after a branch after a shared access (LdsBranchVmemWAR)");
	wave->vector_memory = 1;
	return 0;
}
