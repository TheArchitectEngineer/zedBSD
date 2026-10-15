/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The waits, the hazard breaks and the assembly of the amdrdna shader
 * compiler (gfx10-wait.h).
 *
 * The waits: an instruction that reads or writes a register a memory
 * instruction has not yet filled waits first, by the counter of that
 * memory instruction, until the counter is zero.  The export reads its
 * registers late, so a write to one of them waits for the export counter.
 * Waiting to zero is always correct; waiting for fewer is left for later.
 *
 * The hazards: GFX10.1 has four sequences the hardware does not guard,
 * each broken by an instruction placed between their two halves (the
 * VMEMtoScalarWrite, SMEMtoVectorWrite, VcmpxExecWAR and LdsBranchVmemWAR
 * hazards).  The pass follows one straight line of instructions; at a
 * label and before a branch it waits for everything and breaks every
 * hazard still open, so that every path into a label starts clean.
 */

#include "gfx10-wait.h"

#include <kern/kcrt.h>
#include <kern/kmem.h>
#include <uapi/errno.h>

#include <stddef.h>
#include <stdint.h>

/* The operand codes a register can have: the scalar ones below 128, the VGPRs from 256. */
#define GFX10_CODES			512U

/* The scalar registers whose reads the hazards follow (SGPRs and the special ones), by code. */
#define GFX10_SCALAR_CODES		128U

/* The words of a bit set of the scalar registers. */
#define GFX10_SCALAR_SET_WORDS		(GFX10_SCALAR_CODES / 32U)

/* The instructions the pass may add before one instruction, and at the end. */
#define GFX10_ADDED_PER_INST		8U
#define GFX10_ADDED_AT_END		8U

/* The bit of a counter in a set of counters. */
#define GFX10_COUNTER_BIT(counter)	(1U << (unsigned)(counter))

/*
 * The fields of the dependency wait's immediate that say whether it waits
 * for the vector memory instructions to read their scalar operands (bits
 * 4:2) and for the scalar instructions to read theirs (bit 0); zero means
 * it waits.
 *
 * Values referenced: Mesa 25.0.7 src/amd/compiler/aco_insert_NOPs.cpp
 * (handle_instruction_gfx10: vm_vsrc = (imm >> 2) & 0x7, sa_sdst =
 * imm & 0x1).
 */
#define GFX10_DEPEND_VM_VSRC_SHIFT	2U
#define GFX10_DEPEND_VM_VSRC_MASK	0x7U
#define GFX10_DEPEND_SA_SDST_MASK	0x1U

/*
 * What the pass knows at one point of the straight line.
 *
 * Lives on the stack of amdrdna_gfx10_place_waits(): emptied at every
 * label and every branch.
 */
struct wait_state {
	/* For each register (by code), the counters whose completion it waits for. */
	uint8_t pending[GFX10_CODES];

	/* The counters any register waits for. */
	uint32_t outstanding;

	/*
	 * The scalar registers read by a vector memory load, by a vector
	 * memory store and by a shared memory access since the last vector
	 * ALU instruction or wait (VMEMtoScalarWrite), and by a scalar load
	 * since the last scalar ALU write (SMEMtoVectorWrite).
	 */
	uint32_t read_by_load[GFX10_SCALAR_SET_WORDS];
	uint32_t read_by_store[GFX10_SCALAR_SET_WORDS];
	uint32_t read_by_shared[GFX10_SCALAR_SET_WORDS];
	uint32_t read_by_scalar_load[GFX10_SCALAR_SET_WORDS];

	/* A non-vector instruction read the lane mask since the last vector ALU SGPR write (VcmpxExecWAR). */
	int lane_mask_read;

	/*
	 * A vector memory access, a shared memory access, and a branch after
	 * either, since the last store counter wait (LdsBranchVmemWAR).
	 */
	int vector_memory;
	int shared_memory;
	int branch_after_vector;
	int branch_after_shared;

	/* The last instruction was a scalar write of M0. */
	int m0_written;
};

/*
 * The list being built.
 *
 * Lives on the stack of amdrdna_gfx10_place_waits(); its array is handed
 * to the caller.
 */
struct wait_output {
	struct amdrdna_gfx10_inst *list;
	uint32_t count;
	uint32_t capacity;
	int track_branch_hazard;
};

static void wait_counters(struct wait_state *state, struct wait_output *output, uint32_t counters);
static void resolve_all(struct wait_state *state, struct wait_output *output);
static void break_hazards(struct wait_state *state, struct wait_output *output, const struct amdrdna_gfx10_inst *inst, const struct amdrdna_gfx10_access *access);
static void note_issued(struct wait_state *state, const struct amdrdna_gfx10_inst *inst, const struct amdrdna_gfx10_access *access);
static void note_wait(struct wait_state *state, const struct amdrdna_gfx10_inst *inst);
static void append(struct wait_output *output, const struct amdrdna_gfx10_inst *inst);
static void append_control(struct wait_output *output, unsigned opcode, uint32_t immediate);
static uint32_t needed_counters(const struct wait_state *state, const struct amdrdna_gfx10_access *access);
static int is_vector_alu(const struct amdrdna_gfx10_inst *inst);
static int is_scalar_alu(const struct amdrdna_gfx10_inst *inst);
static int is_vector_memory(const struct amdrdna_gfx10_inst *inst);
static int reads_range_in(const uint32_t *set, const struct amdrdna_gfx10_range *range);
static void add_range(uint32_t *set, const struct amdrdna_gfx10_range *range);
static int set_any(const uint32_t *set);
static void set_clear(uint32_t *set);
static int writes_code(const struct amdrdna_gfx10_access *access, unsigned code);
static int writes_scalar(const struct amdrdna_gfx10_access *access);
static int writes_any_in(const struct amdrdna_gfx10_access *access, const uint32_t *set);
static int reads_code(const struct amdrdna_gfx10_access *access, unsigned code);
static unsigned instruction_words(const struct amdrdna_gfx10_inst *inst, int *refused);
static int insert_idle_after(struct amdrdna_gfx10_inst **list, uint32_t *count, uint32_t index);

/*
 * Puts the waits and the hazard breaks into an instruction list.
 *
 * Each instruction is preceded by what it needs: a wait for the counters
 * of the registers it reads or writes that a memory instruction has not
 * filled, and the break of any hazard it would complete.  Labels and
 * branches are preceded by a wait for everything and the break of every
 * open hazard.
 */
int
amdrdna_gfx10_place_waits(
	const struct amdrdna_gfx10_inst *list,
	uint32_t count,
	struct amdrdna_gfx10_inst **out,
	uint32_t *out_count)
{
	struct wait_state state;
	struct wait_output output;
	struct amdrdna_gfx10_access access;
	const struct amdrdna_gfx10_inst *inst;
	uint32_t needed;
	uint32_t index;
	int has_vector_memory;
	int has_shared_memory;
	int vector_memory;

	*out = NULL;
	*out_count = 0U;

	/* Makes room for every instruction and what may come before it. */
	kern_memset(&output, 0, sizeof(output));
	output.capacity = count * GFX10_ADDED_PER_INST + GFX10_ADDED_AT_END;
	output.list = kern_calloc(output.capacity, sizeof(*output.list));
	if (output.list == NULL)
		return ENOMEM;

	/* The branch hazard needs both a vector memory access and a shared memory access in the program. */
	has_vector_memory = 0;
	has_shared_memory = 0;
	for (index = 0U; index < count; index++) {
		vector_memory = is_vector_memory(&list[index]);
		if (vector_memory != 0)
			has_vector_memory = 1;
		if (list[index].kind == AMDRDNA_GFX10_KIND_SHARED)
			has_shared_memory = 1;
	}
	output.track_branch_hazard = 0;
	if (has_vector_memory != 0 && has_shared_memory != 0)
		output.track_branch_hazard = 1;

	/* Starts with nothing outstanding. */
	kern_memset(&state, 0, sizeof(state));

	/* Walks the straight line once. */
	for (index = 0U; index < count; index++) {
		inst = &list[index];

		/* A label is reached from elsewhere: everything is settled before it. */
		if (inst->kind == AMDRDNA_GFX10_KIND_LABEL) {
			resolve_all(&state, &output);
			append(&output, inst);
			continue;
		}

		/* A branch leaves the line: everything is settled before it. */
		if (inst->kind == AMDRDNA_GFX10_KIND_BRANCH)
			resolve_all(&state, &output);

		/* Waits for the registers the instruction uses that memory has not filled. */
		amdrdna_gfx10_describe(inst, &access);
		needed = needed_counters(&state, &access);
		if (needed != 0U)
			wait_counters(&state, &output, needed);

		/* Breaks the hazards the instruction would complete. */
		break_hazards(&state, &output, inst, &access);

		/* Places the instruction and records what it leaves outstanding. */
		append(&output, inst);
		note_wait(&state, inst);
		note_issued(&state, inst, &access);
	}

	/* Succeeded: the caller owns the new list. */
	*out = output.list;
	*out_count = output.count;
	return 0;
}

/*
 * Writes an instruction list as code.
 *
 * The branch distances are counted in words from the word after the
 * branch to the label.  A branch whose distance is the one GFX10
 * mishandles gets an idle instruction after it, and the distances are
 * counted again, until no branch has it.  The code is then padded with
 * the end-of-code instruction past what the prefetcher reads, to a whole
 * cache line.
 */
int
amdrdna_gfx10_assemble(
	struct amdrdna_gfx10_inst **list,
	uint32_t *count,
	uint32_t **words,
	uint32_t *word_count,
	uint32_t *failed)
{
	struct amdrdna_gfx10_inst *inst;
	uint32_t *offset;
	uint32_t *label_offset;
	uint32_t *code;
	uint32_t label_count;
	uint32_t index;
	uint32_t total;
	uint32_t padded;
	uint32_t written;
	uint32_t moved;
	int32_t distance;
	unsigned size;
	int refused;
	int again;
	int error;

	*words = NULL;
	*word_count = 0U;
	*failed = 0U;

	/* Counts the labels: one past the highest number used. */
	label_count = 0U;
	for (index = 0U; index < *count; index++) {
		inst = &(*list)[index];
		if (inst->kind == AMDRDNA_GFX10_KIND_LABEL || inst->kind == AMDRDNA_GFX10_KIND_BRANCH) {
			if (inst->label + 1U > label_count)
				label_count = inst->label + 1U;
		}
	}

	/* The label offsets live as long as the assembly. */
	label_offset = kern_calloc(label_count + 1U, sizeof(*label_offset));
	if (label_offset == NULL)
		return ENOMEM;

	/* Lays the code out until no branch has the mishandled distance. */
	offset = NULL;
	moved = 0U;
	do {
		again = 0;

		/* The offsets are counted afresh for the current list. */
		kern_free(offset);
		offset = kern_calloc(*count + 1U, sizeof(*offset));
		if (offset == NULL) {
			kern_free(label_offset);
			return ENOMEM;
		}

		/* Places every instruction and every label. */
		total = 0U;
		for (index = 0U; index < *count; index++) {
			inst = &(*list)[index];
			offset[index] = total;
			if (inst->kind == AMDRDNA_GFX10_KIND_LABEL)
				label_offset[inst->label] = total;

			/* An instruction the encoder refuses is reported. */
			size = instruction_words(inst, &refused);
			if (refused != 0) {
				kern_free(offset);
				kern_free(label_offset);
				*failed = index;
				return EINVAL;
			}
			total += size;
		}

		/* Resolves every branch, moving the first one that lands on the mishandled distance. */
		for (index = 0U; index < *count; index++) {
			inst = &(*list)[index];
			if (inst->kind != AMDRDNA_GFX10_KIND_BRANCH)
				continue;

			/* The distance from the word after the branch. */
			distance = (int32_t)label_offset[inst->label] - (int32_t)offset[index] - 1;
			if (distance < -32768 || distance > 32767) {
				kern_free(offset);
				kern_free(label_offset);
				*failed = index;
				return EINVAL;
			}
			inst->immediate = (uint32_t)distance & 0xffffU;

			/* The mishandled distance gets an idle instruction after the branch, and everything is laid out again. */
			if (distance == (int32_t)AMDRDNA_GFX10_UNSAFE_BRANCH_DISTANCE) {
				error = insert_idle_after(list, count, index);
				if (error != 0) {
					kern_free(offset);
					kern_free(label_offset);
					return error;
				}
				moved++;
				again = 1;
				break;
			}
		}

		/* Each move lengthens the code, so the moves end; a runaway is refused. */
		if (moved > *count) {
			kern_free(offset);
			kern_free(label_offset);
			return EINVAL;
		}
	} while (again != 0);

	/* The code, padded for the prefetcher to a whole line of 16 words. */
	padded = (total + AMDRDNA_GFX10_PREFETCH_WORDS + 15U) & ~15U;
	code = kern_calloc(padded, sizeof(*code));
	if (code == NULL) {
		kern_free(offset);
		kern_free(label_offset);
		return ENOMEM;
	}

	/* Writes every instruction. */
	written = 0U;
	for (index = 0U; index < *count; index++) {
		inst = &(*list)[index];
		if (inst->kind == AMDRDNA_GFX10_KIND_LABEL)
			continue;
		size = amdrdna_gfx10_encode(inst, &code[written]);
		written += size;
	}

	/* Fills the rest with the end-of-code instruction. */
	for (index = written; index < padded; index++)
		code[index] = AMDRDNA_GFX10_FORM_CONTROL | ((uint32_t)AMDRDNA_GFX10_S_CODE_END << 16);

	kern_free(offset);
	kern_free(label_offset);

	/* Succeeded: the caller owns the code. */
	*words = code;
	*word_count = padded;
	return 0;
}

/* Waits until the given counters are zero, and forgets the registers they held. */
static void
wait_counters(
	struct wait_state *state,
	struct wait_output *output,
	uint32_t counters)
{
	struct amdrdna_gfx10_inst inst;
	uint32_t immediate;
	uint32_t index;

	/* The counter wait zeroes the fields of the counters asked for and leaves the rest at their largest. */
	immediate = AMDRDNA_GFX10_WAIT_NONE;
	if ((counters & GFX10_COUNTER_BIT(AMDRDNA_GFX10_COUNTER_VECTOR_LOAD)) != 0U)
		immediate &= ~AMDRDNA_GFX10_WAIT_VECTOR_MASK;
	if ((counters & GFX10_COUNTER_BIT(AMDRDNA_GFX10_COUNTER_EXPORT)) != 0U)
		immediate &= ~AMDRDNA_GFX10_WAIT_EXPORT_MASK;
	if ((counters & GFX10_COUNTER_BIT(AMDRDNA_GFX10_COUNTER_SCALAR)) != 0U)
		immediate &= ~AMDRDNA_GFX10_WAIT_SCALAR_MASK;

	/* Places the counter wait, when it waits for anything. */
	if (immediate != AMDRDNA_GFX10_WAIT_NONE) {
		append_control(output, AMDRDNA_GFX10_S_WAITCNT, immediate);
		kern_memset(&inst, 0, sizeof(inst));
		inst.kind = AMDRDNA_GFX10_KIND_CONTROL;
		inst.opcode = AMDRDNA_GFX10_S_WAITCNT;
		inst.immediate = immediate;
		note_wait(state, &inst);
	}

	/* The stores are waited for by their own instruction. */
	if ((counters & GFX10_COUNTER_BIT(AMDRDNA_GFX10_COUNTER_VECTOR_STORE)) != 0U) {
		kern_memset(&inst, 0, sizeof(inst));
		inst.kind = AMDRDNA_GFX10_KIND_SCALAR_CONSTANT;
		inst.opcode = AMDRDNA_GFX10_S_WAITCNT_VSCNT;
		inst.dst = AMDRDNA_GFX10_NULL;
		append(output, &inst);
		note_wait(state, &inst);
	}

	/* Forgets the registers the zeroed counters held. */
	for (index = 0U; index < GFX10_CODES; index++)
		state->pending[index] = (uint8_t)(state->pending[index] & ~counters);
	state->outstanding &= ~counters;
}

/* Waits for every counter and breaks every open hazard, so that what follows starts clean. */
static void
resolve_all(
	struct wait_state *state,
	struct wait_output *output)
{
	struct amdrdna_gfx10_inst inst;
	uint32_t depend;
	int open;

	/* Waits for every counter a register still waits for. */
	if (state->outstanding != 0U)
		wait_counters(state, output, state->outstanding);

	/* The scalar registers vector memory or shared memory read, and the lane mask a scalar read, are let go. */
	depend = 0xffffU;
	open = set_any(state->read_by_load);
	open |= set_any(state->read_by_store);
	open |= set_any(state->read_by_shared);
	if (open != 0)
		depend &= AMDRDNA_GFX10_DEPEND_VECTOR_MEMORY_READS;
	if (state->lane_mask_read != 0)
		depend &= AMDRDNA_GFX10_DEPEND_LANE_MASK_READS;
	if (depend != 0xffffU)
		append_control(output, AMDRDNA_GFX10_S_WAITCNT_DEPCTR, depend);
	set_clear(state->read_by_load);
	set_clear(state->read_by_store);
	set_clear(state->read_by_shared);
	state->lane_mask_read = 0;

	/* The scalar registers a scalar load read are let go by a scalar write. */
	open = set_any(state->read_by_scalar_load);
	if (open != 0) {
		kern_memset(&inst, 0, sizeof(inst));
		inst.kind = AMDRDNA_GFX10_KIND_SCALAR_ONE;
		inst.opcode = AMDRDNA_GFX10_S_MOV_B32;
		inst.dst = AMDRDNA_GFX10_NULL;
		inst.src[0] = AMDRDNA_GFX10_ZERO;
		append(output, &inst);
		set_clear(state->read_by_scalar_load);
	}

	/* A memory access before the branch is separated from one after it. */
	if (output->track_branch_hazard != 0) {
		if (state->vector_memory != 0 || state->shared_memory != 0 ||
		    state->branch_after_vector != 0 || state->branch_after_shared != 0) {
			kern_memset(&inst, 0, sizeof(inst));
			inst.kind = AMDRDNA_GFX10_KIND_SCALAR_CONSTANT;
			inst.opcode = AMDRDNA_GFX10_S_WAITCNT_VSCNT;
			inst.dst = AMDRDNA_GFX10_NULL;
			append(output, &inst);
		}
	}
	state->vector_memory = 0;
	state->shared_memory = 0;
	state->branch_after_vector = 0;
	state->branch_after_shared = 0;
	state->m0_written = 0;
}

/*
 * Places, before an instruction, the break of each GFX10 hazard it would
 * complete.
 *
 * Procedure referenced: Mesa 25.0.7 src/amd/compiler/aco_insert_NOPs.cpp
 * (handle_instruction_gfx10: VMEMtoScalarWriteHazard,
 * VcmpxExecWARHazard, SMEMtoVectorWriteHazard, LdsBranchVmemWARHazard)
 * and src/amd/compiler/README-ISA.md ("RDNA / GFX10 hazards").
 */
static void
break_hazards(
	struct wait_state *state,
	struct wait_output *output,
	const struct amdrdna_gfx10_inst *inst,
	const struct amdrdna_gfx10_access *access)
{
	struct amdrdna_gfx10_inst nop;
	int vector_alu;
	int scalar_alu;
	int vector_memory;
	int written;
	int writes_lane_mask;
	int writes_sgpr;

	vector_alu = is_vector_alu(inst);
	scalar_alu = is_scalar_alu(inst);
	vector_memory = is_vector_memory(inst);

	/* A scalar write of a register a memory access still reads waits for the reads to finish. */
	if (scalar_alu != 0 || inst->kind == AMDRDNA_GFX10_KIND_SCALAR_LOAD) {
		written = writes_any_in(access, state->read_by_load);
		written |= writes_any_in(access, state->read_by_store);
		written |= writes_any_in(access, state->read_by_shared);
		if (written != 0) {
			append_control(output, AMDRDNA_GFX10_S_WAITCNT_DEPCTR, AMDRDNA_GFX10_DEPEND_VECTOR_MEMORY_READS);
			set_clear(state->read_by_load);
			set_clear(state->read_by_store);
			set_clear(state->read_by_shared);
		}
	}

	/* A vector write of the lane mask a non-vector instruction read waits for the read to finish. */
	if (vector_alu != 0 && state->lane_mask_read != 0) {
		writes_lane_mask = writes_code(access, AMDRDNA_GFX10_EXEC_LO);
		if (writes_lane_mask != 0) {
			append_control(output, AMDRDNA_GFX10_S_WAITCNT_DEPCTR, AMDRDNA_GFX10_DEPEND_LANE_MASK_READS);
			state->lane_mask_read = 0;
		}
	}

	/* A vector write of a scalar register a scalar load read is separated by a scalar write. */
	if (vector_alu != 0) {
		written = writes_any_in(access, state->read_by_scalar_load);
		if (written != 0) {
			kern_memset(&nop, 0, sizeof(nop));
			nop.kind = AMDRDNA_GFX10_KIND_SCALAR_ONE;
			nop.opcode = AMDRDNA_GFX10_S_MOV_B32;
			nop.dst = AMDRDNA_GFX10_NULL;
			nop.src[0] = AMDRDNA_GFX10_ZERO;
			append(output, &nop);
			set_clear(state->read_by_scalar_load);
		}
	}

	/* A memory access of the other kind after a branch waits for the stores. */
	if (output->track_branch_hazard != 0) {
		if ((vector_memory != 0 && state->branch_after_shared != 0) ||
		    (inst->kind == AMDRDNA_GFX10_KIND_SHARED && state->branch_after_vector != 0)) {
			kern_memset(&nop, 0, sizeof(nop));
			nop.kind = AMDRDNA_GFX10_KIND_SCALAR_CONSTANT;
			nop.opcode = AMDRDNA_GFX10_S_WAITCNT_VSCNT;
			nop.dst = AMDRDNA_GFX10_NULL;
			append(output, &nop);
			state->vector_memory = 0;
			state->shared_memory = 0;
			state->branch_after_vector = 0;
			state->branch_after_shared = 0;
		}
	}

	/*
	 * An interpolation right after a scalar write of M0 is given one idle
	 * instruction between them.  This is zedBSD's own caution, not a
	 * documented GFX10 hazard.
	 */
	if (inst->kind == AMDRDNA_GFX10_KIND_INTERPOLATE && state->m0_written != 0)
		append_control(output, AMDRDNA_GFX10_S_NOP, 0U);

	/* A vector write of a scalar register is noted for the lane mask hazard. */
	writes_sgpr = writes_scalar(access);
	if (vector_alu != 0 && writes_sgpr != 0)
		state->lane_mask_read = 0;
}

/* Records what an instruction leaves outstanding and which hazards it opens. */
static void
note_issued(
	struct wait_state *state,
	const struct amdrdna_gfx10_inst *inst,
	const struct amdrdna_gfx10_access *access)
{
	uint32_t bit;
	uint32_t index;
	uint32_t code;
	uint32_t *read_set;
	int vector_alu;
	int scalar_alu;
	int vector_memory;
	int reads_lane_mask;

	vector_alu = is_vector_alu(inst);
	scalar_alu = is_scalar_alu(inst);
	vector_memory = is_vector_memory(inst);

	/* The registers a memory instruction writes wait for its counter; an export's read ones wait for the export counter. */
	if (access->counter != AMDRDNA_GFX10_COUNTER_NONE) {
		bit = GFX10_COUNTER_BIT(access->counter);
		state->outstanding |= bit;

		/* The writes of a load, and the reads of an export. */
		if (access->counter == AMDRDNA_GFX10_COUNTER_EXPORT) {
			for (index = 0U; index < access->read_count; index++) {
				for (code = access->reads[index].first; code < access->reads[index].first + access->reads[index].count; code++) {
					if (code < GFX10_CODES)
						state->pending[code] = (uint8_t)(state->pending[code] | bit);
				}
			}
		} else {
			for (index = 0U; index < access->write_count; index++) {
				for (code = access->writes[index].first; code < access->writes[index].first + access->writes[index].count; code++) {
					if (code < GFX10_CODES)
						state->pending[code] = (uint8_t)(state->pending[code] | bit);
				}
			}
		}
	}

	/* The scalar registers a vector memory or shared memory access reads stay read for a while. */
	read_set = NULL;
	if (vector_memory != 0) {
		read_set = state->read_by_load;
		if (access->write_count == 0U)
			read_set = state->read_by_store;
	} else if (inst->kind == AMDRDNA_GFX10_KIND_SHARED) {
		read_set = state->read_by_shared;
	} else if (inst->kind == AMDRDNA_GFX10_KIND_SCALAR_LOAD) {
		read_set = state->read_by_scalar_load;
	}
	if (read_set != NULL) {
		for (index = 0U; index < access->read_count; index++)
			add_range(read_set, &access->reads[index]);
	}

	/* A vector ALU instruction ends the vector memory reads' hazard. */
	if (vector_alu != 0) {
		set_clear(state->read_by_load);
		set_clear(state->read_by_store);
		set_clear(state->read_by_shared);
	}

	/* A scalar write (not a control instruction) ends the scalar load reads' hazard. */
	if (scalar_alu != 0 && access->write_count != 0U &&
	    inst->kind != AMDRDNA_GFX10_KIND_CONTROL && inst->kind != AMDRDNA_GFX10_KIND_BRANCH)
		set_clear(state->read_by_scalar_load);

	/* A non-vector read of the lane mask opens its hazard. */
	reads_lane_mask = reads_code(access, AMDRDNA_GFX10_EXEC_LO);
	if (vector_alu == 0 && reads_lane_mask != 0)
		state->lane_mask_read = 1;

	/* The memory accesses and the branches between them. */
	if (vector_memory != 0) {
		state->vector_memory = 1;
		state->shared_memory = 0;
		state->branch_after_vector = 0;
		state->branch_after_shared = 0;
	} else if (inst->kind == AMDRDNA_GFX10_KIND_SHARED) {
		state->shared_memory = 1;
		state->vector_memory = 0;
		state->branch_after_vector = 0;
		state->branch_after_shared = 0;
	} else if (inst->kind == AMDRDNA_GFX10_KIND_BRANCH) {
		state->branch_after_vector |= state->vector_memory;
		state->branch_after_shared |= state->shared_memory;
		state->vector_memory = 0;
		state->shared_memory = 0;
	}

	/* A scalar write of M0. */
	state->m0_written = 0;
	if (scalar_alu != 0) {
		state->m0_written = writes_code(access, AMDRDNA_GFX10_M0);
	}
}

/* Records what a wait the list already has lets go. */
static void
note_wait(
	struct wait_state *state,
	const struct amdrdna_gfx10_inst *inst)
{
	uint32_t zeroed;
	uint32_t index;
	unsigned vector_count;
	unsigned depend_vector;

	/* A counter wait: the counters whose fields are zero are done. */
	if (inst->kind == AMDRDNA_GFX10_KIND_CONTROL && inst->opcode == AMDRDNA_GFX10_S_WAITCNT) {
		zeroed = 0U;
		vector_count = (inst->immediate & 0xfU) | ((inst->immediate >> 10) & 0x30U);
		if (vector_count == 0U) {
			zeroed |= GFX10_COUNTER_BIT(AMDRDNA_GFX10_COUNTER_VECTOR_LOAD);
			set_clear(state->read_by_load);
		}
		if ((inst->immediate & AMDRDNA_GFX10_WAIT_EXPORT_MASK) == 0U)
			zeroed |= GFX10_COUNTER_BIT(AMDRDNA_GFX10_COUNTER_EXPORT);
		if ((inst->immediate & AMDRDNA_GFX10_WAIT_SCALAR_MASK) == 0U) {
			zeroed |= GFX10_COUNTER_BIT(AMDRDNA_GFX10_COUNTER_SCALAR);
			set_clear(state->read_by_shared);
			set_clear(state->read_by_scalar_load);
		}

		/* Forgets the registers the zeroed counters held. */
		for (index = 0U; index < GFX10_CODES; index++)
			state->pending[index] = (uint8_t)(state->pending[index] & ~zeroed);
		state->outstanding &= ~zeroed;
		return;
	}

	/* The store counter wait to zero ends the store reads' hazard and the branch hazard. */
	if (inst->kind == AMDRDNA_GFX10_KIND_SCALAR_CONSTANT &&
	    inst->opcode == AMDRDNA_GFX10_S_WAITCNT_VSCNT &&
	    inst->dst == AMDRDNA_GFX10_NULL &&
	    inst->immediate == 0U) {
		set_clear(state->read_by_store);
		state->vector_memory = 0;
		state->shared_memory = 0;
		state->branch_after_vector = 0;
		state->branch_after_shared = 0;
		return;
	}

	/* A dependency wait lets go of the reads it waits for. */
	if (inst->kind == AMDRDNA_GFX10_KIND_CONTROL && inst->opcode == AMDRDNA_GFX10_S_WAITCNT_DEPCTR) {
		depend_vector = (inst->immediate >> GFX10_DEPEND_VM_VSRC_SHIFT) & GFX10_DEPEND_VM_VSRC_MASK;
		if (depend_vector == 0U) {
			set_clear(state->read_by_load);
			set_clear(state->read_by_store);
			set_clear(state->read_by_shared);
		}
		if ((inst->immediate & GFX10_DEPEND_SA_SDST_MASK) == 0U)
			state->lane_mask_read = 0;
	}
}

/* Appends an instruction to the list being built; the room was made for it. */
static void
append(
	struct wait_output *output,
	const struct amdrdna_gfx10_inst *inst)
{
	/* The capacity covers every instruction the pass adds, so this never runs out. */
	if (output->count == output->capacity)
		return;

	output->list[output->count] = *inst;
	output->count++;
}

/* Appends a control instruction with a 16-bit immediate. */
static void
append_control(
	struct wait_output *output,
	unsigned opcode,
	uint32_t immediate)
{
	struct amdrdna_gfx10_inst inst;

	/* Builds the instruction. */
	kern_memset(&inst, 0, sizeof(inst));
	inst.kind = AMDRDNA_GFX10_KIND_CONTROL;
	inst.opcode = (uint16_t)opcode;
	inst.immediate = immediate;

	/* Places it. */
	append(output, &inst);
}

/* Reports the counters the registers an instruction reads or writes wait for. */
static uint32_t
needed_counters(
	const struct wait_state *state,
	const struct amdrdna_gfx10_access *access)
{
	uint32_t needed;
	uint32_t index;
	uint32_t code;

	/* Every register read. */
	needed = 0U;
	for (index = 0U; index < access->read_count; index++) {
		for (code = access->reads[index].first; code < access->reads[index].first + access->reads[index].count; code++) {
			if (code < GFX10_CODES)
				needed |= state->pending[code];
		}
	}

	/* Every register written: a late result must not land after the new value. */
	for (index = 0U; index < access->write_count; index++) {
		for (code = access->writes[index].first; code < access->writes[index].first + access->writes[index].count; code++) {
			if (code < GFX10_CODES)
				needed |= state->pending[code];
		}
	}

	/* Reports the counters. */
	return needed;
}

/* Reports whether an instruction is a vector ALU instruction (interpolation included). */
static int
is_vector_alu(
	const struct amdrdna_gfx10_inst *inst)
{
	/* Picks by the kind. */
	switch (inst->kind) {
	case AMDRDNA_GFX10_KIND_VECTOR_ONE:
	case AMDRDNA_GFX10_KIND_VECTOR_TWO:
	case AMDRDNA_GFX10_KIND_VECTOR_COMPARE:
	case AMDRDNA_GFX10_KIND_VECTOR_WIDE:
	case AMDRDNA_GFX10_KIND_INTERPOLATE:
		return 1;
	default:
		return 0;
	}
}

/* Reports whether an instruction is a scalar ALU, control or branch instruction. */
static int
is_scalar_alu(
	const struct amdrdna_gfx10_inst *inst)
{
	/* Picks by the kind. */
	switch (inst->kind) {
	case AMDRDNA_GFX10_KIND_SCALAR_TWO:
	case AMDRDNA_GFX10_KIND_SCALAR_CONSTANT:
	case AMDRDNA_GFX10_KIND_SCALAR_ONE:
	case AMDRDNA_GFX10_KIND_SCALAR_COMPARE:
	case AMDRDNA_GFX10_KIND_CONTROL:
	case AMDRDNA_GFX10_KIND_BRANCH:
		return 1;
	default:
		return 0;
	}
}

/* Reports whether an instruction is a vector memory (buffer or image) access. */
static int
is_vector_memory(
	const struct amdrdna_gfx10_inst *inst)
{
	/* Buffers and images. */
	if (inst->kind == AMDRDNA_GFX10_KIND_BUFFER)
		return 1;
	if (inst->kind == AMDRDNA_GFX10_KIND_IMAGE)
		return 1;

	/* Anything else. */
	return 0;
}

/* Reports whether a set holds any scalar register of a range. */
static int
reads_range_in(
	const uint32_t *set,
	const struct amdrdna_gfx10_range *range)
{
	uint32_t code;

	/* Each scalar register of the range. */
	for (code = range->first; code < (uint32_t)range->first + range->count; code++) {
		if (code >= GFX10_SCALAR_CODES)
			break;
		if ((set[code / 32U] & (1U << (code % 32U))) != 0U)
			return 1;
	}

	/* None of them. */
	return 0;
}

/* Adds the scalar registers of a range to a set. */
static void
add_range(
	uint32_t *set,
	const struct amdrdna_gfx10_range *range)
{
	uint32_t code;

	/* Each scalar register of the range; VGPRs are not followed. */
	for (code = range->first; code < (uint32_t)range->first + range->count; code++) {
		if (code >= GFX10_SCALAR_CODES)
			break;
		set[code / 32U] |= 1U << (code % 32U);
	}
}

/* Reports whether a set holds any register. */
static int
set_any(
	const uint32_t *set)
{
	uint32_t index;

	/* Each word of the set. */
	for (index = 0U; index < GFX10_SCALAR_SET_WORDS; index++) {
		if (set[index] != 0U)
			return 1;
	}

	/* An empty set. */
	return 0;
}

/* Empties a set. */
static void
set_clear(
	uint32_t *set)
{
	uint32_t index;

	/* Each word of the set. */
	for (index = 0U; index < GFX10_SCALAR_SET_WORDS; index++)
		set[index] = 0U;
}

/* Reports whether an instruction writes a register. */
static int
writes_code(
	const struct amdrdna_gfx10_access *access,
	unsigned code)
{
	uint32_t index;

	/* Each range written. */
	for (index = 0U; index < access->write_count; index++) {
		if (code >= access->writes[index].first && code < (unsigned)access->writes[index].first + access->writes[index].count)
			return 1;
	}

	/* Not written. */
	return 0;
}

/* Reports whether an instruction writes a scalar register. */
static int
writes_scalar(
	const struct amdrdna_gfx10_access *access)
{
	uint32_t index;

	/* Each range written. */
	for (index = 0U; index < access->write_count; index++) {
		if (access->writes[index].first < GFX10_SCALAR_CODES)
			return 1;
	}

	/* Only VGPRs, or nothing. */
	return 0;
}

/* Reports whether an instruction writes a scalar register a set holds. */
static int
writes_any_in(
	const struct amdrdna_gfx10_access *access,
	const uint32_t *set)
{
	uint32_t index;
	int found;

	/* Each range written. */
	for (index = 0U; index < access->write_count; index++) {
		found = reads_range_in(set, &access->writes[index]);
		if (found != 0)
			return 1;
	}

	/* None of them. */
	return 0;
}

/* Reports whether an instruction reads a register. */
static int
reads_code(
	const struct amdrdna_gfx10_access *access,
	unsigned code)
{
	uint32_t index;

	/* Each range read. */
	for (index = 0U; index < access->read_count; index++) {
		if (code >= access->reads[index].first && code < (unsigned)access->reads[index].first + access->reads[index].count)
			return 1;
	}

	/* Not read. */
	return 0;
}

/* Reports the words an instruction is written as (0 for a label); *refused is 1 when the encoder refuses it. */
static unsigned
instruction_words(
	const struct amdrdna_gfx10_inst *inst,
	int *refused)
{
	uint32_t scratch[AMDRDNA_GFX10_MAX_WORDS];
	unsigned size;

	*refused = 0;

	/* A label is no word. */
	if (inst->kind == AMDRDNA_GFX10_KIND_LABEL)
		return 0U;

	/* Writes the instruction aside to count its words. */
	size = amdrdna_gfx10_encode(inst, scratch);
	if (size == 0U)
		*refused = 1;

	/* Reports the count. */
	return size;
}

/* Inserts an idle instruction after the instruction at index, growing the list. */
static int
insert_idle_after(
	struct amdrdna_gfx10_inst **list,
	uint32_t *count,
	uint32_t index)
{
	struct amdrdna_gfx10_inst *grown;

	/* A list one longer. */
	grown = kern_calloc(*count + 1U, sizeof(*grown));
	if (grown == NULL)
		return ENOMEM;

	/* Copies the instructions up to the branch, the idle one, and the rest. */
	kern_memcpy(grown, *list, (index + 1U) * sizeof(*grown));
	grown[index + 1U].kind = AMDRDNA_GFX10_KIND_CONTROL;
	grown[index + 1U].opcode = AMDRDNA_GFX10_S_NOP;
	kern_memcpy(&grown[index + 2U], &(*list)[index + 1U], (*count - index - 1U) * sizeof(*grown));

	/* The new list replaces the old. */
	kern_free(*list);
	*list = grown;
	*count = *count + 1U;

	/* Succeeded: the branch is followed by the idle instruction. */
	return 0;
}
