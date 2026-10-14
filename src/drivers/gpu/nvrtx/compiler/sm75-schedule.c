/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 */

/*
 * Copyright © 2022 Collabora, Ltd.
 * Copyright © 2024 Collabora, Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

/*
 * The dependence marking of the nvrtx shader compiler (see
 * sm75-schedule.h), ws037-p008 stage D.
 *
 * Modified for zedBSD: the latencies are excerpted by hand from Mesa 25.3.6
 * (src/nouveau/compiler/nak, SPDX MIT) and the code is zedBSD's own.
 * Sources (SHA-256):
 *   nak/sm75_instr_latencies.rs (the latency classes and tables)
 *     419b8b1d25fb16ddc8ae726768b15e593d3b84f467cc6bb59101eb39bf764878
 *   nak/calc_instr_deps.rs (a scoreboard is waited for at least two
 *   cycles after the instruction that sets it; six scoreboards)
 *     8c92e065aa043dcb259cb0bc0ef7436ec692b9b5d10321969fd5adc3d6178695
 *   nak/sm70.rs (the latency class of each operation, the execution
 *   latency of BAR and MEMBAR)
 *
 * The pass walks the program once in order.  For every register and
 * predicate it keeps who wrote it last and when (a cycle count from the
 * start of the block), or which scoreboard will say the write is done, and
 * which scoreboards will say the reads of it are done.  An instruction
 * issues at the first cycle every one of its reads and writes allows, and
 * the instruction before it gets that many cycles as its delay.  The
 * tables' few values that depend on predication or on finer classes are
 * taken at their largest, so the delays are never short.
 */

#include "sm75-schedule.h"
#include "sm75-encode.h"
#include "sm75-isa.h"

#include <kern/kcrt.h>
#include <kern/kmem.h>

/* The scoreboards an instruction may set, and the mask of all of them. */
#define NVRTX_SM75_SCOREBOARDS		6U
#define NVRTX_SM75_SCOREBOARD_ALL	0x3fU

/* The general registers and predicates the pass tracks. */
#define NVRTX_SM75_TRACKED_GPRS		256U
#define NVRTX_SM75_TRACKED_PREDICATES	8U

/* The cycles after a scoreboard is set before an instruction may wait for it. */
#define NVRTX_SM75_SCOREBOARD_SETTLE	2U

/* The worst latency of a fixed-latency general register and predicate result, which a block's end waits out. */
#define NVRTX_SM75_WORST_GPR		6U
#define NVRTX_SM75_WORST_PREDICATE	13U

/* A write after a write, and a variable-latency write after a fixed-latency write or read. */
#define NVRTX_SM75_WAW_FIXED		2U
#define NVRTX_SM75_WAW_DECOUPLED	6U
#define NVRTX_SM75_WAR_DECOUPLED	2U

/* The fewest cycles after a barrier or a memory barrier (its execution takes 5), and after EXIT. */
#define NVRTX_SM75_DELAY_SYNC_MIN	6U
#define NVRTX_SM75_DELAY_EXIT_MIN	5U
#define NVRTX_SM75_DELAY_AFTER_SYNC	2U

/* No scoreboard, no predicate, and a register never written. */
#define NVRTX_SM75_NO_BAR		0xffU
#define NVRTX_SM75_NO_PREDICATE		NVRTX_SM75_PT

/*
 * What the pass knows of one general register or predicate: whether a
 * fixed-latency write is in flight, its class and its cycle; the
 * scoreboard of a variable-latency write in flight; the scoreboards of the
 * variable-latency reads in flight; and the last fixed-latency read.
 */
struct nvrtx_sm75_track {
	int written;
	uint32_t write_class;
	uint32_t write_cycle;
	uint32_t write_bar;
	uint32_t read_bars;
	uint32_t read_cycle;
};

/*
 * The state of one pass: the registers and predicates, each scoreboard's
 * use (busy) and the cycle it was set, and the cycle of the instruction
 * before.  Lives on the stack of nvrtx_sm75_schedule().
 */
struct nvrtx_sm75_scheduler {
	struct nvrtx_sm75_track gpr[NVRTX_SM75_TRACKED_GPRS];
	struct nvrtx_sm75_track predicate[NVRTX_SM75_TRACKED_PREDICATES];
	uint32_t bar_busy;
	uint32_t bar_set_cycle[NVRTX_SM75_SCOREBOARDS];
	uint32_t cycle;
};

static int nvrtx_sm75_operands_alu(const struct nvrtx_sm75_word *word, uint32_t opcode, struct nvrtx_sm75_operands *operands);
static int nvrtx_sm75_operands_other(const struct nvrtx_sm75_word *word, uint32_t opcode, struct nvrtx_sm75_operands *operands);
static void nvrtx_sm75_add_read(struct nvrtx_sm75_operands *operands, uint32_t reg, uint32_t read_class);
static void nvrtx_sm75_add_write(struct nvrtx_sm75_operands *operands, uint32_t reg);
static void nvrtx_sm75_mark_targets(const uint32_t *code, uint32_t count, uint8_t *target);
static void nvrtx_sm75_flush(struct nvrtx_sm75_scheduler *scheduler, uint32_t *earliest, uint32_t *wait);
static void nvrtx_sm75_clear(struct nvrtx_sm75_scheduler *scheduler);
static void nvrtx_sm75_constrain(struct nvrtx_sm75_scheduler *scheduler, const struct nvrtx_sm75_operands *operands, uint32_t *earliest, uint32_t *wait);
static void nvrtx_sm75_take_bars(struct nvrtx_sm75_scheduler *scheduler, const struct nvrtx_sm75_operands *operands, uint32_t *wait, uint32_t *write_bar, uint32_t *read_bar);
static void nvrtx_sm75_record(struct nvrtx_sm75_scheduler *scheduler, const struct nvrtx_sm75_operands *operands, uint32_t issue, uint32_t write_bar, uint32_t read_bar);
static void nvrtx_sm75_release(struct nvrtx_sm75_scheduler *scheduler, uint32_t wait);
static uint32_t nvrtx_sm75_raw(uint32_t writer, uint32_t reader);
static uint32_t nvrtx_sm75_predicate_raw(uint32_t reader);
static uint32_t nvrtx_sm75_later(uint32_t a, uint32_t b);

/*
 * Reads an instruction's operands from its four words.
 *
 * Returns 0 with the operands, or 1 for an opcode the lowering does not
 * emit.  The guard predicate is read by every guarded instruction.
 */
int
nvrtx_sm75_operands(
	const uint32_t *words,
	struct nvrtx_sm75_operands *operands)
{
	struct nvrtx_sm75_word word;
	uint32_t opcode;
	int unknown;

	word.w[0] = words[0];
	word.w[1] = words[1];
	word.w[2] = words[2];
	word.w[3] = words[3];

	/* Nothing read or written yet, one cycle to the next instruction. */
	kern_memset(operands, 0, sizeof(*operands));
	operands->predicate_read = NVRTX_SM75_NO_PREDICATE;
	operands->predicate_write = NVRTX_SM75_NO_PREDICATE;
	operands->min_delay = 1U;

	/* The guard, unless it is PT. */
	operands->guard = (uint32_t)nvrtx_sm75_get(&word, NVRTX_SM75_GUARD_LOW, NVRTX_SM75_PRED_BITS);

	/* The opcodes of their own encodings, then the ALU ones by their 9 bits. */
	opcode = (uint32_t)nvrtx_sm75_get(&word, NVRTX_SM75_OPCODE_LOW, NVRTX_SM75_OPCODE_BITS);
	unknown = nvrtx_sm75_operands_other(&word, opcode, operands);
	if (unknown == 0)
		return 0;

	opcode = (uint32_t)nvrtx_sm75_get(&word, NVRTX_SM75_OPCODE_LOW, NVRTX_SM75_ALU_OPCODE_BITS);
	unknown = nvrtx_sm75_operands_alu(&word, opcode, operands);
	if (unknown != 0)
		return 1;

	/* Succeeded: an ALU instruction. */
	return 0;
}

/*
 * Sets the scheduling control of every instruction of a program.
 *
 * Returns 0, or 1 with the instruction and the reason when an instruction
 * is not known or the wait before one would not fit the delay field.
 */
int
nvrtx_sm75_schedule(
	uint32_t *code,
	uint32_t count,
	uint32_t *instruction,
	const char **reason)
{
	struct nvrtx_sm75_scheduler *scheduler;
	struct nvrtx_sm75_operands operands;
	struct nvrtx_sm75_word word;
	uint8_t *target;
	uint32_t *slot;
	uint32_t *previous;
	uint32_t index;
	uint32_t earliest;
	uint32_t wait;
	uint32_t write_bar;
	uint32_t read_bar;
	uint32_t bar;
	uint32_t previous_min;
	uint32_t delay;
	int previous_sync;
	int unknown;

	/* The pass's state, which is too large for a kernel stack. */
	scheduler = kern_calloc(1U, sizeof(*scheduler));
	if (scheduler == NULL) {
		*instruction = 0U;
		*reason = "no memory for the dependence marking";
		return 1;
	}

	/* The branch targets, which start blocks. */
	target = kern_calloc(count + 1U, sizeof(*target));
	if (target == NULL) {
		kern_free(scheduler);
		*instruction = 0U;
		*reason = "no memory for the branch targets";
		return 1;
	}
	nvrtx_sm75_mark_targets(code, count, target);

	/* Nothing in flight at the program's start. */
	nvrtx_sm75_clear(scheduler);
	scheduler->cycle = 0U;
	previous = NULL;
	previous_min = 0U;
	previous_sync = 0;

	/* Each instruction in order. */
	for (index = 0U; index < count; index++) {
		slot = code + (size_t)index * NVRTX_SM75_INSTRUCTION_WORDS;

		/* What it reads and writes. */
		unknown = nvrtx_sm75_operands(slot, &operands);
		if (unknown != 0) {
			kern_free(target);
			kern_free(scheduler);
			*instruction = index;
			*reason = "the dependence marking does not know an instruction";
			return 1;
		}

		/* The NOP after a barrier waits 2 cycles, as the CUDA compiler writes it (calc_instr_deps.rs). */
		if (previous_sync != 0 && operands.class_of == NVRTX_SM75_CLASS_DISPATCH)
			operands.min_delay = NVRTX_SM75_DELAY_AFTER_SYNC;

		/* The earliest it may issue after the instruction before. */
		earliest = scheduler->cycle + previous_min;
		wait = 0U;

		/* A branch target or a block's last instruction waits for everything. */
		if (target[index] != 0U || operands.boundary != 0) {
			nvrtx_sm75_flush(scheduler, &earliest, &wait);
			nvrtx_sm75_clear(scheduler);
		}

		/* Its own reads and writes, then the scoreboards it sets. */
		nvrtx_sm75_constrain(scheduler, &operands, &earliest, &wait);
		nvrtx_sm75_take_bars(scheduler, &operands, &wait, &write_bar, &read_bar);

		/* A scoreboard is waited for only once it has settled. */
		for (bar = 0U; bar < NVRTX_SM75_SCOREBOARDS; bar++) {
			if ((wait & (1U << bar)) == 0U)
				continue;
			earliest = nvrtx_sm75_later(earliest, scheduler->bar_set_cycle[bar] + NVRTX_SM75_SCOREBOARD_SETTLE);
		}

		/* The instruction before waits that many cycles; the field holds 15 at most. */
		if (previous != NULL) {
			delay = earliest - scheduler->cycle;
			if (delay > NVRTX_SM75_DELAY_MAX) {
				kern_free(target);
				kern_free(scheduler);
				*instruction = index;
				*reason = "a wait longer than the delay field holds";
				return 1;
			}
			kern_memcpy(word.w, previous, sizeof(word.w));
			nvrtx_sm75_set(&word, NVRTX_SM75_DELAY_LOW, NVRTX_SM75_DELAY_BITS, delay);
			kern_memcpy(previous, word.w, sizeof(word.w));
		}

		/* The waits are done when it issues; then its own results and scoreboards are in flight. */
		nvrtx_sm75_release(scheduler, wait);
		nvrtx_sm75_record(scheduler, &operands, earliest, write_bar, read_bar);

		/* Its scoreboards, its waits and its yield; the delay comes with the next one. */
		kern_memcpy(word.w, slot, sizeof(word.w));
		nvrtx_sm75_control(&word, operands.min_delay, operands.boundary, write_bar, read_bar, wait);
		kern_memcpy(slot, word.w, sizeof(word.w));

		/* A block's last instruction leaves nothing in flight behind it. */
		if (operands.boundary != 0)
			nvrtx_sm75_clear(scheduler);

		scheduler->cycle = earliest;
		previous = slot;
		previous_min = operands.min_delay;
		previous_sync = 0;
		if (operands.min_delay == NVRTX_SM75_DELAY_SYNC_MIN)
			previous_sync = 1;
	}

	kern_free(target);
	kern_free(scheduler);

	/* Succeeded: every instruction has its control. */
	return 0;
}

/* Reads the operands of an ALU instruction by its 9-bit opcode; nonzero for one not known. */
static int
nvrtx_sm75_operands_alu(
	const struct nvrtx_sm75_word *word,
	uint32_t opcode,
	struct nvrtx_sm75_operands *operands)
{
	uint32_t form;
	uint32_t dst;
	uint32_t src0;
	uint32_t src1;
	uint32_t src2;
	int has_src0;
	int has_src2;
	int writes_dst;

	form = (uint32_t)nvrtx_sm75_get(word, NVRTX_SM75_ALU_FORM_LOW, NVRTX_SM75_ALU_FORM_BITS);
	dst = (uint32_t)nvrtx_sm75_get(word, NVRTX_SM75_DST_LOW, NVRTX_SM75_REG_BITS);
	src0 = (uint32_t)nvrtx_sm75_get(word, NVRTX_SM75_SRC0_LOW, NVRTX_SM75_REG_BITS);
	src1 = (uint32_t)nvrtx_sm75_get(word, NVRTX_SM75_SRC1_LOW, NVRTX_SM75_REG_BITS);
	src2 = (uint32_t)nvrtx_sm75_get(word, NVRTX_SM75_SRC2_LOW, NVRTX_SM75_REG_BITS);

	/* The forms the lowering emits. */
	if (form < NVRTX_SM75_FORM_REG || form > NVRTX_SM75_FORM_CB)
		return 1;

	/* Which sources the operation has, whether it writes a register, its class. */
	has_src0 = 1;
	has_src2 = 1;
	writes_dst = 1;
	operands->class_of = NVRTX_SM75_CLASS_ALU;
	switch (opcode) {
	case NVRTX_SM75_OP_MOV:
	case NVRTX_SM75_OP_MUFU:
	case NVRTX_SM75_OP_I2F:
	case NVRTX_SM75_OP_F2I:
	case NVRTX_SM75_OP_FRND:
		/* One source, the second. */
		has_src0 = 0;
		has_src2 = 0;
		if (opcode != NVRTX_SM75_OP_MOV)
			operands->class_of = NVRTX_SM75_CLASS_DECOUPLED;
		break;
	case NVRTX_SM75_OP_SEL:
		/* Two sources and the condition. */
		has_src2 = 0;
		operands->predicate_read = (uint32_t)nvrtx_sm75_get(word, NVRTX_SM75_PSRC_A_LOW, NVRTX_SM75_PRED_BITS);
		break;
	case NVRTX_SM75_OP_FSETP:
	case NVRTX_SM75_OP_ISETP:
		/* Two sources and a predicate result. */
		has_src2 = 0;
		writes_dst = 0;
		operands->predicate_write = (uint32_t)nvrtx_sm75_get(word, NVRTX_SM75_PDST0_LOW, NVRTX_SM75_PRED_BITS);
		break;
	case NVRTX_SM75_OP_FADD:
		/* A third source only as the moved second one of an immediate or constant form. */
		operands->class_of = NVRTX_SM75_CLASS_FMA;
		if (form == NVRTX_SM75_FORM_REG || form == NVRTX_SM75_FORM_IMM || form == NVRTX_SM75_FORM_CB)
			has_src2 = 0;
		break;
	case NVRTX_SM75_OP_FMUL:
	case NVRTX_SM75_OP_FFMA:
		operands->class_of = NVRTX_SM75_CLASS_FMA;
		break;
	case NVRTX_SM75_OP_IMAD:
		operands->class_of = NVRTX_SM75_CLASS_IMAD;
		break;
	case NVRTX_SM75_OP_IMAD_WIDE:
		operands->class_of = NVRTX_SM75_CLASS_IMAD_WIDE;
		break;
	case NVRTX_SM75_OP_FMNMX:
	case NVRTX_SM75_OP_IADD3:
	case NVRTX_SM75_OP_LOP3:
	case NVRTX_SM75_OP_SHF:
		break;
	default:
		return 1;
	}

	/* The first source. */
	if (has_src0 != 0)
		nvrtx_sm75_add_read(operands, src0, operands->class_of);

	/* The second source when it is a register. */
	if (form == NVRTX_SM75_FORM_REG)
		nvrtx_sm75_add_read(operands, src1, operands->class_of);

	/* The register in the third slot; an IMAD.WIDE's addend is a pair it reads late. */
	if (has_src2 != 0 && opcode == NVRTX_SM75_OP_IMAD_WIDE && src2 != NVRTX_SM75_RZ) {
		nvrtx_sm75_add_read(operands, src2, NVRTX_SM75_CLASS_DISPATCH);
		nvrtx_sm75_add_read(operands, src2 + 1U, NVRTX_SM75_CLASS_DISPATCH);
	} else if (has_src2 != 0) {
		nvrtx_sm75_add_read(operands, src2, operands->class_of);
	}

	/* The result, a pair for IMAD.WIDE. */
	if (writes_dst != 0)
		nvrtx_sm75_add_write(operands, dst);
	if (writes_dst != 0 && opcode == NVRTX_SM75_OP_IMAD_WIDE && dst != NVRTX_SM75_RZ)
		nvrtx_sm75_add_write(operands, dst + 1U);

	/* Succeeded: the operands are read. */
	return 0;
}

/* Reads the operands of an instruction of its own 12-bit opcode; nonzero for one not known. */
static int
nvrtx_sm75_operands_other(
	const struct nvrtx_sm75_word *word,
	uint32_t opcode,
	struct nvrtx_sm75_operands *operands)
{
	uint32_t dst;
	uint32_t src0;
	uint32_t src1;
	uint32_t src2;
	uint32_t mode;
	uint32_t extra;
	uint32_t index;

	dst = (uint32_t)nvrtx_sm75_get(word, NVRTX_SM75_DST_LOW, NVRTX_SM75_REG_BITS);
	src0 = (uint32_t)nvrtx_sm75_get(word, NVRTX_SM75_SRC0_LOW, NVRTX_SM75_REG_BITS);
	src1 = (uint32_t)nvrtx_sm75_get(word, NVRTX_SM75_SRC1_LOW, NVRTX_SM75_REG_BITS);
	src2 = (uint32_t)nvrtx_sm75_get(word, NVRTX_SM75_SRC2_LOW, NVRTX_SM75_REG_BITS);
	operands->class_of = NVRTX_SM75_CLASS_DECOUPLED;

	/* The operands by the opcode. */
	switch (opcode) {
	case NVRTX_SM75_OP_NOP:
		operands->class_of = NVRTX_SM75_CLASS_DISPATCH;
		break;
	case NVRTX_SM75_OP_S2R:
	case NVRTX_SM75_OP_ALD:
	case NVRTX_SM75_OP_IPA:
		/* A result; the lowering gives none of them a register source (the vertex, offset and interpolation registers are RZ). */
		nvrtx_sm75_add_write(operands, dst);
		break;
	case NVRTX_SM75_OP_LDC:
		/* A result, a pair for a 64-bit load. */
		nvrtx_sm75_add_write(operands, dst);
		if (nvrtx_sm75_get(word, NVRTX_SM75_MEM_TYPE_LOW, 3U) == NVRTX_SM75_MEM_B64)
			nvrtx_sm75_add_write(operands, dst + 1U);
		nvrtx_sm75_add_read(operands, src0, NVRTX_SM75_CLASS_DECOUPLED);
		break;
	case NVRTX_SM75_OP_LDG:
	case NVRTX_SM75_OP_STG:
	case NVRTX_SM75_OP_ATOMG:
	case NVRTX_SM75_OP_ATOMG_CAS:
		/* The 64-bit address pair, the data, the compare-and-exchange's second data, the result. */
		nvrtx_sm75_add_read(operands, src0, NVRTX_SM75_CLASS_DECOUPLED);
		nvrtx_sm75_add_read(operands, src0 + 1U, NVRTX_SM75_CLASS_DECOUPLED);
		if (opcode != NVRTX_SM75_OP_LDG)
			nvrtx_sm75_add_read(operands, src1, NVRTX_SM75_CLASS_DECOUPLED);
		if (opcode == NVRTX_SM75_OP_ATOMG_CAS)
			nvrtx_sm75_add_read(operands, src2, NVRTX_SM75_CLASS_DECOUPLED);
		if (opcode != NVRTX_SM75_OP_STG)
			nvrtx_sm75_add_write(operands, dst);
		break;
	case NVRTX_SM75_OP_LDS:
	case NVRTX_SM75_OP_STS:
	case NVRTX_SM75_OP_ATOMS:
	case NVRTX_SM75_OP_ATOMS_CAS:
		/* The offset, the data, the second data, the result. */
		nvrtx_sm75_add_read(operands, src0, NVRTX_SM75_CLASS_DECOUPLED);
		if (opcode != NVRTX_SM75_OP_LDS)
			nvrtx_sm75_add_read(operands, src1, NVRTX_SM75_CLASS_DECOUPLED);
		if (opcode == NVRTX_SM75_OP_ATOMS_CAS)
			nvrtx_sm75_add_read(operands, src2, NVRTX_SM75_CLASS_DECOUPLED);
		if (opcode != NVRTX_SM75_OP_STS)
			nvrtx_sm75_add_write(operands, dst);
		break;
	case NVRTX_SM75_OP_AST:
		/* The data; the vertex and offset registers are RZ. */
		nvrtx_sm75_add_read(operands, src1, NVRTX_SM75_CLASS_DECOUPLED);
		nvrtx_sm75_add_read(operands, src0, NVRTX_SM75_CLASS_DECOUPLED);
		nvrtx_sm75_add_read(operands, src2, NVRTX_SM75_CLASS_DECOUPLED);
		break;
	case NVRTX_SM75_OP_TEX:
	case NVRTX_SM75_OP_TLD:
		/* The coordinates' pair, the level of detail and the offset after it, the two result pairs. */
		nvrtx_sm75_add_read(operands, src0, NVRTX_SM75_CLASS_DECOUPLED);
		nvrtx_sm75_add_read(operands, src0 + 1U, NVRTX_SM75_CLASS_DECOUPLED);
		mode = (uint32_t)nvrtx_sm75_get(word, NVRTX_SM75_TEX_LOD_LOW, 3U);
		extra = 0U;
		if (mode == NVRTX_SM75_TEX_LOD_BIAS || mode == NVRTX_SM75_TEX_LOD_EXPLICIT)
			extra++;
		if (nvrtx_sm75_get(word, NVRTX_SM75_TEX_AOFFI, 1U) != 0U)
			extra++;
		for (index = 0U; index < extra && src1 != NVRTX_SM75_RZ; index++)
			nvrtx_sm75_add_read(operands, src1 + index, NVRTX_SM75_CLASS_DECOUPLED);
		nvrtx_sm75_add_write(operands, dst);
		if (dst != NVRTX_SM75_RZ)
			nvrtx_sm75_add_write(operands, dst + 1U);
		nvrtx_sm75_add_write(operands, src2);
		if (src2 != NVRTX_SM75_RZ)
			nvrtx_sm75_add_write(operands, src2 + 1U);
		break;
	case NVRTX_SM75_OP_BRA:
		/* The condition; a branch ends its block. */
		operands->predicate_read = (uint32_t)nvrtx_sm75_get(word, NVRTX_SM75_PSRC_A_LOW, NVRTX_SM75_PRED_BITS);
		operands->boundary = 1;
		break;
	case NVRTX_SM75_OP_BSSY:
	case NVRTX_SM75_OP_BSYNC:
	case NVRTX_SM75_OP_KILL:
		/* Convergence and discards end their blocks. */
		operands->boundary = 1;
		break;
	case NVRTX_SM75_OP_BAR:
	case NVRTX_SM75_OP_MEMBAR:
		/* A barrier ends its block and takes 5 cycles to execute. */
		operands->boundary = 1;
		operands->min_delay = NVRTX_SM75_DELAY_SYNC_MIN;
		break;
	case NVRTX_SM75_OP_EXIT:
		/* EXIT reads every output and ends the program. */
		operands->boundary = 1;
		operands->min_delay = NVRTX_SM75_DELAY_EXIT_MIN;
		break;
	default:
		return 1;
	}

	/* Succeeded: the operands are read. */
	return 0;
}

/* Adds a register read of a class, unless it is RZ or the list is full. */
static void
nvrtx_sm75_add_read(
	struct nvrtx_sm75_operands *operands,
	uint32_t reg,
	uint32_t read_class)
{
	/* RZ is never waited for. */
	if (reg >= NVRTX_SM75_RZ)
		return;
	if (operands->read_count == NVRTX_SM75_OPERANDS_READS)
		return;

	/* The register and how it is read. */
	operands->reads[operands->read_count] = reg;
	operands->read_class[operands->read_count] = read_class;
	operands->read_count++;
}

/* Adds a register write, unless it is RZ or the list is full. */
static void
nvrtx_sm75_add_write(
	struct nvrtx_sm75_operands *operands,
	uint32_t reg)
{
	/* A write to RZ is dropped. */
	if (reg >= NVRTX_SM75_RZ)
		return;
	if (operands->write_count == NVRTX_SM75_OPERANDS_WRITES)
		return;

	/* The register. */
	operands->writes[operands->write_count] = reg;
	operands->write_count++;
}

/* Marks the instructions a branch or a convergence barrier leads to. */
static void
nvrtx_sm75_mark_targets(
	const uint32_t *code,
	uint32_t count,
	uint8_t *target)
{
	struct nvrtx_sm75_word word;
	uint64_t offset;
	int64_t to;
	uint32_t opcode;
	uint32_t index;

	/* Each branch and convergence barrier: its target counts in words from the next instruction. */
	for (index = 0U; index < count; index++) {
		kern_memcpy(word.w, code + (size_t)index * NVRTX_SM75_INSTRUCTION_WORDS, sizeof(word.w));
		opcode = (uint32_t)nvrtx_sm75_get(&word, NVRTX_SM75_OPCODE_LOW, NVRTX_SM75_OPCODE_BITS);

		/* The offset field and its sign. */
		if (opcode == NVRTX_SM75_OP_BRA) {
			offset = nvrtx_sm75_get(&word, NVRTX_SM75_BRA_OFFSET_LOW, NVRTX_SM75_BRA_OFFSET_BITS);
			if ((offset >> (NVRTX_SM75_BRA_OFFSET_BITS - 1U)) != 0U)
				offset |= ~((1ULL << NVRTX_SM75_BRA_OFFSET_BITS) - 1U);
		} else if (opcode == NVRTX_SM75_OP_BSSY) {
			offset = nvrtx_sm75_get(&word, NVRTX_SM75_BSSY_OFFSET_LOW, NVRTX_SM75_BSSY_OFFSET_BITS);
			if ((offset >> (NVRTX_SM75_BSSY_OFFSET_BITS - 1U)) != 0U)
				offset |= ~((1ULL << NVRTX_SM75_BSSY_OFFSET_BITS) - 1U);
		} else {
			continue;
		}

		/* A target inside the program starts a block. */
		to = (int64_t)index + 1 + (int64_t)offset / (int64_t)NVRTX_SM75_INSTRUCTION_WORDS;
		if (to >= 0 && to < (int64_t)count)
			target[to] = 1U;
	}
}

/*
 * Makes an instruction wait for everything in flight: every busy
 * scoreboard, every fixed-latency result's worst latency.
 */
static void
nvrtx_sm75_flush(
	struct nvrtx_sm75_scheduler *scheduler,
	uint32_t *earliest,
	uint32_t *wait)
{
	uint32_t index;

	/* Every scoreboard in use. */
	*wait |= scheduler->bar_busy;

	/* Every fixed-latency register result. */
	for (index = 0U; index < NVRTX_SM75_TRACKED_GPRS; index++) {
		if (scheduler->gpr[index].written == 0)
			continue;
		if (scheduler->gpr[index].write_bar != NVRTX_SM75_NO_BAR)
			continue;
		*earliest = nvrtx_sm75_later(*earliest, scheduler->gpr[index].write_cycle + NVRTX_SM75_WORST_GPR);
	}

	/* Every predicate result. */
	for (index = 0U; index < NVRTX_SM75_TRACKED_PREDICATES; index++) {
		if (scheduler->predicate[index].written == 0)
			continue;
		*earliest = nvrtx_sm75_later(*earliest, scheduler->predicate[index].write_cycle + NVRTX_SM75_WORST_PREDICATE);
	}
}

/* Forgets everything in flight (after a block's end has waited it out); the scoreboards' set cycles stay. */
static void
nvrtx_sm75_clear(
	struct nvrtx_sm75_scheduler *scheduler)
{
	uint32_t index;

	/* The registers and predicates. */
	for (index = 0U; index < NVRTX_SM75_TRACKED_GPRS; index++) {
		kern_memset(&scheduler->gpr[index], 0, sizeof(scheduler->gpr[index]));
		scheduler->gpr[index].write_bar = NVRTX_SM75_NO_BAR;
	}
	for (index = 0U; index < NVRTX_SM75_TRACKED_PREDICATES; index++) {
		kern_memset(&scheduler->predicate[index], 0, sizeof(scheduler->predicate[index]));
		scheduler->predicate[index].write_bar = NVRTX_SM75_NO_BAR;
	}

	/* No scoreboard is in use. */
	scheduler->bar_busy = 0U;
}

/*
 * Applies an instruction's reads and writes: a result in flight on a
 * scoreboard is waited for, a fixed-latency one is waited out by cycles.
 */
static void
nvrtx_sm75_constrain(
	struct nvrtx_sm75_scheduler *scheduler,
	const struct nvrtx_sm75_operands *operands,
	uint32_t *earliest,
	uint32_t *wait)
{
	const struct nvrtx_sm75_track *track;
	uint32_t index;
	uint32_t reader;

	/* Each register read: after its writer. */
	for (index = 0U; index < operands->read_count; index++) {
		track = &scheduler->gpr[operands->reads[index]];
		if (track->written == 0)
			continue;
		if (track->write_bar != NVRTX_SM75_NO_BAR) {
			*wait |= 1U << track->write_bar;
			continue;
		}
		*earliest = nvrtx_sm75_later(*earliest, track->write_cycle + nvrtx_sm75_raw(track->write_class, operands->read_class[index]));
	}

	/* Each register write: after the write and the reads before it. */
	for (index = 0U; index < operands->write_count; index++) {
		track = &scheduler->gpr[operands->writes[index]];
		*wait |= track->read_bars;
		if (track->written != 0 && track->write_bar != NVRTX_SM75_NO_BAR)
			*wait |= 1U << track->write_bar;

		/* A fixed-latency write before it. */
		if (track->written != 0 && track->write_bar == NVRTX_SM75_NO_BAR) {
			if (operands->class_of == NVRTX_SM75_CLASS_DECOUPLED) {
				*earliest = nvrtx_sm75_later(*earliest, track->write_cycle + NVRTX_SM75_WAW_DECOUPLED);
			} else {
				*earliest = nvrtx_sm75_later(*earliest, track->write_cycle + NVRTX_SM75_WAW_FIXED);
			}
		}

		/* A fixed-latency read before a variable-latency write. */
		if (operands->class_of == NVRTX_SM75_CLASS_DECOUPLED)
			*earliest = nvrtx_sm75_later(*earliest, track->read_cycle + NVRTX_SM75_WAR_DECOUPLED);
	}

	/* The guard and the predicate source: after the comparison that wrote them. */
	if (operands->guard != NVRTX_SM75_NO_PREDICATE) {
		track = &scheduler->predicate[operands->guard];
		if (track->written != 0)
			*earliest = nvrtx_sm75_later(*earliest, track->write_cycle + nvrtx_sm75_predicate_raw(NVRTX_SM75_CLASS_DECOUPLED));
	}
	if (operands->predicate_read != NVRTX_SM75_NO_PREDICATE) {
		track = &scheduler->predicate[operands->predicate_read];
		reader = operands->class_of;
		if (operands->boundary != 0)
			reader = NVRTX_SM75_CLASS_DECOUPLED;
		if (track->written != 0)
			*earliest = nvrtx_sm75_later(*earliest, track->write_cycle + nvrtx_sm75_predicate_raw(reader));
	}

	/* The predicate written: after the write and the reads before it. */
	if (operands->predicate_write != NVRTX_SM75_NO_PREDICATE) {
		track = &scheduler->predicate[operands->predicate_write];
		if (track->written != 0)
			*earliest = nvrtx_sm75_later(*earliest, track->write_cycle + NVRTX_SM75_WAW_FIXED);
		*earliest = nvrtx_sm75_later(*earliest, track->read_cycle + NVRTX_SM75_WAR_DECOUPLED);
	}
}

/*
 * Chooses the scoreboards a variable-latency instruction sets: one its
 * result releases, when it writes a register, and one the read of its
 * sources releases, when it reads one.  A scoreboard is free when nothing
 * in flight names it and the instruction does not wait for it already
 * ending its use; when none is free, the one set longest ago is waited for
 * by this instruction and reused.
 */
static void
nvrtx_sm75_take_bars(
	struct nvrtx_sm75_scheduler *scheduler,
	const struct nvrtx_sm75_operands *operands,
	uint32_t *wait,
	uint32_t *write_bar,
	uint32_t *read_bar)
{
	uint32_t free_bars;
	uint32_t bar;
	uint32_t oldest;
	uint32_t candidate;
	uint32_t need;
	uint32_t taken;

	/* A fixed-latency instruction sets none. */
	*write_bar = NVRTX_SM75_BAR_NONE;
	*read_bar = NVRTX_SM75_BAR_NONE;
	if (operands->class_of != NVRTX_SM75_CLASS_DECOUPLED)
		return;

	/* How many it needs. */
	need = 0U;
	if (operands->write_count != 0U)
		need++;
	if (operands->read_count != 0U)
		need++;

	/* Each one needed, the lowest free first, else the one set longest ago. */
	taken = 0U;
	while (need > 0U) {
		free_bars = NVRTX_SM75_SCOREBOARD_ALL & ~(scheduler->bar_busy & ~*wait) & ~taken;
		bar = NVRTX_SM75_SCOREBOARDS;
		oldest = 0xffffffffU;
		for (candidate = 0U; candidate < NVRTX_SM75_SCOREBOARDS; candidate++) {
			if ((taken & (1U << candidate)) != 0U)
				continue;

			/* The lowest free one. */
			if ((free_bars & (1U << candidate)) != 0U) {
				bar = candidate;
				break;
			}

			/* Else the one set longest ago so far. */
			if (scheduler->bar_set_cycle[candidate] < oldest) {
				oldest = scheduler->bar_set_cycle[candidate];
				bar = candidate;
			}
		}

		/* A busy one is waited for by this instruction, which ends its use. */
		if ((free_bars & (1U << bar)) == 0U)
			*wait |= 1U << bar;
		taken |= 1U << bar;

		/* The result's first, then the sources'. */
		if (*write_bar == NVRTX_SM75_BAR_NONE && operands->write_count != 0U) {
			*write_bar = bar;
		} else {
			*read_bar = bar;
		}
		need--;
	}
}

/*
 * Records an instruction issued at a cycle: what it reads and writes is in
 * flight, on its scoreboards when it has them.
 */
static void
nvrtx_sm75_record(
	struct nvrtx_sm75_scheduler *scheduler,
	const struct nvrtx_sm75_operands *operands,
	uint32_t issue,
	uint32_t write_bar,
	uint32_t read_bar)
{
	struct nvrtx_sm75_track *track;
	uint32_t index;

	/* The reads: on the read scoreboard, or at this cycle. */
	for (index = 0U; index < operands->read_count; index++) {
		track = &scheduler->gpr[operands->reads[index]];
		if (read_bar != NVRTX_SM75_BAR_NONE) {
			track->read_bars |= 1U << read_bar;
		} else {
			track->read_cycle = issue;
		}
	}

	/* The writes: on the result scoreboard, or fixed-latency of the instruction's class. */
	for (index = 0U; index < operands->write_count; index++) {
		track = &scheduler->gpr[operands->writes[index]];
		track->written = 1;
		track->write_class = operands->class_of;
		track->write_cycle = issue;
		track->write_bar = NVRTX_SM75_NO_BAR;
		if (write_bar != NVRTX_SM75_BAR_NONE)
			track->write_bar = write_bar;
	}

	/* The predicates. */
	if (operands->guard != NVRTX_SM75_NO_PREDICATE)
		scheduler->predicate[operands->guard].read_cycle = issue;
	if (operands->predicate_read != NVRTX_SM75_NO_PREDICATE)
		scheduler->predicate[operands->predicate_read].read_cycle = issue;
	if (operands->predicate_write != NVRTX_SM75_NO_PREDICATE) {
		scheduler->predicate[operands->predicate_write].written = 1;
		scheduler->predicate[operands->predicate_write].write_cycle = issue;
	}

	/* The scoreboards it set are in use from this cycle. */
	if (write_bar != NVRTX_SM75_BAR_NONE) {
		scheduler->bar_busy |= 1U << write_bar;
		scheduler->bar_set_cycle[write_bar] = issue;
	}
	if (read_bar != NVRTX_SM75_BAR_NONE) {
		scheduler->bar_busy |= 1U << read_bar;
		scheduler->bar_set_cycle[read_bar] = issue;
	}
}

/* Ends the use of the scoreboards an instruction waited for: what they guarded is complete. */
static void
nvrtx_sm75_release(
	struct nvrtx_sm75_scheduler *scheduler,
	uint32_t wait)
{
	struct nvrtx_sm75_track *track;
	uint32_t index;

	/* Nothing waited. */
	if (wait == 0U)
		return;

	/* Each register: a result on a waited scoreboard is ready, reads on them are done. */
	for (index = 0U; index < NVRTX_SM75_TRACKED_GPRS; index++) {
		track = &scheduler->gpr[index];
		track->read_bars &= ~wait;
		if (track->write_bar != NVRTX_SM75_NO_BAR && (wait & (1U << track->write_bar)) != 0U) {
			track->written = 0;
			track->write_bar = NVRTX_SM75_NO_BAR;
		}
	}

	/* The scoreboards are free. */
	scheduler->bar_busy &= ~wait;
}

/*
 * Reports the cycles from a fixed-latency register write to a read
 * (sm75_instr_latencies.rs, read_after_write; an IMAD.WIDE writes as its
 * upper half and its addend is read as DISPATCH here, the lower half).
 */
static uint32_t
nvrtx_sm75_raw(
	uint32_t writer,
	uint32_t reader)
{
	/* A variable-latency reader waits 6 for any fixed-latency writer. */
	if (reader == NVRTX_SM75_CLASS_DECOUPLED)
		return 6U;

	/* An IMAD.WIDE's addend (the lower half reader). */
	if (reader == NVRTX_SM75_CLASS_DISPATCH) {
		if (writer == NVRTX_SM75_CLASS_ALU)
			return 5U;
		if (writer == NVRTX_SM75_CLASS_IMAD_WIDE)
			return 2U;
		return 4U;
	}

	/* An IMAD.WIDE's factors. */
	if (reader == NVRTX_SM75_CLASS_IMAD_WIDE) {
		if (writer == NVRTX_SM75_CLASS_ALU)
			return 5U;
		if (writer == NVRTX_SM75_CLASS_IMAD_WIDE)
			return 6U;
		return 4U;
	}

	/* A float or IMAD reader. */
	if (reader == NVRTX_SM75_CLASS_FMA || reader == NVRTX_SM75_CLASS_IMAD) {
		if (writer == NVRTX_SM75_CLASS_ALU || writer == NVRTX_SM75_CLASS_DISPATCH)
			return 5U;
		return 4U;
	}

	/* An ALU reader. */
	if (writer == NVRTX_SM75_CLASS_ALU || writer == NVRTX_SM75_CLASS_DISPATCH)
		return 4U;

	/* Succeeded: an FMA, IMAD or IMAD.WIDE writer of an ALU reader. */
	return 5U;
}

/*
 * Reports the cycles from a comparison's predicate (an ALU write) to a
 * read: 4 for an ALU reader, 5 for a float or IMAD one, 12 for a guard, a
 * branch or a variable-latency reader (pred_read_after_write).
 */
static uint32_t
nvrtx_sm75_predicate_raw(
	uint32_t reader)
{
	/* An ALU reader. */
	if (reader == NVRTX_SM75_CLASS_ALU)
		return 4U;

	/* A float or IMAD reader. */
	if (reader == NVRTX_SM75_CLASS_FMA || reader == NVRTX_SM75_CLASS_IMAD || reader == NVRTX_SM75_CLASS_IMAD_WIDE)
		return 5U;

	/* Succeeded: a guard, a branch or a variable-latency reader. */
	return 12U;
}

/* Reports the later of two cycles. */
static uint32_t
nvrtx_sm75_later(
	uint32_t a,
	uint32_t b)
{
	/* The first when it is later. */
	if (a > b)
		return a;

	/* Succeeded: the second. */
	return b;
}
