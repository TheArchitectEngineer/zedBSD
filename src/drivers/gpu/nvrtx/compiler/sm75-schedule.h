/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The dependence marking of the nvrtx shader compiler (sm75-schedule.c,
 * ws037-p008 stage D).
 *
 * After the lowering has emitted a program, this pass sets every
 * instruction's scheduling control (sm75-isa.h, NVRTX_SM75_DELAY_LOW ..):
 * a fixed-latency result holds its reader back only as many cycles as
 * the shader model's latency table says for that writer and reader (Turing's,
 * or the one Ampere and Ada share, ws242-p002), and a
 * variable-latency instruction releases a scoreboard of its own that only
 * the instructions that need its result (or its sources' registers) wait
 * for.  Branches, convergence barriers, barriers, discards, EXIT and the
 * targets of branches start a new block with every result complete.
 *
 * The pass reads the instructions back from their words: it knows the
 * operands of every instruction the lowering emits and refuses any other.
 */

#ifndef DRIVERS_GPU_NVRTX_COMPILER_SM75_SCHEDULE_H
#define DRIVERS_GPU_NVRTX_COMPILER_SM75_SCHEDULE_H

#include <stdint.h>

/*
 * How an instruction's results become ready (Turing's latency classes).
 * Values referenced: Mesa 25.3.6 src/nouveau/compiler/nak/sm75_instr_latencies.rs (the latency classes).
 */
#define NVRTX_SM75_CLASS_ALU		0U
#define NVRTX_SM75_CLASS_FMA		1U
#define NVRTX_SM75_CLASS_IMAD		2U
#define NVRTX_SM75_CLASS_IMAD_WIDE	3U
#define NVRTX_SM75_CLASS_DISPATCH	4U
#define NVRTX_SM75_CLASS_DECOUPLED	5U

/*
 * A read by a variable-latency instruction of the address unit (the global
 * and shared memory accesses and the attribute stores), which Ampere's and
 * Ada's table tells from the other variable-latency reads; only a read
 * has it, the instruction's class is NVRTX_SM75_CLASS_DECOUPLED.
 * Value referenced: Mesa 25.3.6 src/nouveau/compiler/nak/sm80_instr_latencies.rs (DecoupledAgu).
 */
#define NVRTX_SM75_CLASS_AGU		6U

/* The most registers and predicates one instruction reads and writes. */
#define NVRTX_SM75_OPERANDS_READS	8U
#define NVRTX_SM75_OPERANDS_WRITES	4U

/*
 * What one instruction reads and writes, as the dependence marking sees
 * it: the general registers (RZ left out) with, for each read, the class
 * the read counts as (an IMAD.WIDE's addend reads late); the guard
 * predicate and the predicate sources (PT left out); the predicate it
 * writes; its latency class; whether it ends a block; and the fewest
 * cycles before the next instruction may issue.
 */
struct nvrtx_sm75_operands {
	uint32_t reads[NVRTX_SM75_OPERANDS_READS];
	uint32_t read_class[NVRTX_SM75_OPERANDS_READS];
	uint32_t read_count;
	uint32_t writes[NVRTX_SM75_OPERANDS_WRITES];
	uint32_t write_count;
	uint32_t guard;
	uint32_t predicate_read;
	uint32_t predicate_write;
	uint32_t class_of;
	int boundary;
	uint32_t min_delay;
};

/* Reads an instruction's operands from its four words; nonzero for an instruction the pass does not know. */
int nvrtx_sm75_operands(const uint32_t *words, struct nvrtx_sm75_operands *operands);

/*
 * Reorders the instructions inside each block of a program for a shader
 * model (NVRTX_SM_*) so that the long-latency ones issue early; nonzero,
 * with the instruction and a reason, when one is not known or there is no
 * memory.  Branches, convergence and barriers, NOPs and the blocks' bounds
 * keep their places, so every branch offset stays valid.
 */
int nvrtx_sm75_reorder(uint32_t *code, uint32_t count, uint32_t sm, uint32_t *instruction, const char **reason);

/*
 * Sets the scheduling control of a program's instructions for a shader
 * model (NVRTX_SM_*); nonzero, with the instruction and a reason, when one
 * is not known or a delay would not fit its field.
 */
int nvrtx_sm75_schedule(uint32_t *code, uint32_t count, uint32_t sm, uint32_t *instruction, const char **reason);

#endif /* DRIVERS_GPU_NVRTX_COMPILER_SM75_SCHEDULE_H */
