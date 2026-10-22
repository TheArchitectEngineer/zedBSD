/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The register allocation of the lowering (see lower.h).
 *
 * It knows which values each IR instruction reads and defines, makes a
 * value read inside a loop it was defined before live to the loop's end,
 * and takes and gives back the general registers as the lowering goes.
 */

#include "drivers/gpu/nvrtx/compiler/sm75/lower.h"

#include <kern/kmem.h>
#include <uapi/errno.h>

#include <stddef.h>

/*
 * The registers a program may use without counting against it: the
 * register allocation spreads values over this many before it needs more,
 * so a register is reused as late as possible and the reordering is not
 * held back by a write after a read or a write.  62 and the hardware's two
 * are 64, which still lets a Turing SM keep its most threads resident
 * (65536 registers for 1024 threads).
 */
#define NVRTX_SM75_GPR_POOL		62U

/*
 * Lists the values an instruction reads; reports their number, or -1 for
 * an operation the compiler does not cover.
 */
int
nvrtx_sm75_sources(
	const struct drv_gpu_shader_ir_inst *instruction,
	uint32_t *sources)
{
	int count;

	/* The sources by the operation. */
	switch (instruction->op) {
	case DRV_GPU_IR_NOP:
	case DRV_GPU_IR_SKIP_BEGIN:
	case DRV_GPU_IR_SKIP_END:
	case DRV_GPU_IR_CONST:
	case DRV_GPU_IR_ICONST:
	case DRV_GPU_IR_BOOL:
	case DRV_GPU_IR_LOAD_PUSH:
	case DRV_GPU_IR_LOAD_UBO:
	case DRV_GPU_IR_LOAD_SYSTEM:
	case DRV_GPU_IR_STORAGE_SIZE:
		return 0;
	case DRV_GPU_IR_FNEG:
	case DRV_GPU_IR_FABS:
	case DRV_GPU_IR_RSQ:
	case DRV_GPU_IR_RCP:
	case DRV_GPU_IR_SQRT:
	case DRV_GPU_IR_EXP2:
	case DRV_GPU_IR_LOG2:
	case DRV_GPU_IR_SIN:
	case DRV_GPU_IR_COS:
	case DRV_GPU_IR_FLOOR:
	case DRV_GPU_IR_FRACT:
	case DRV_GPU_IR_FTRUNC:
	case DRV_GPU_IR_FROUND_EVEN:
	case DRV_GPU_IR_NOT:
	case DRV_GPU_IR_INEG:
	case DRV_GPU_IR_INOT:
	case DRV_GPU_IR_I2F:
	case DRV_GPU_IR_U2F:
	case DRV_GPU_IR_F2I:
	case DRV_GPU_IR_F2U:
	case DRV_GPU_IR_MOVE:
		sources[0] = instruction->src[0];
		return 1;
	case DRV_GPU_IR_FADD:
	case DRV_GPU_IR_FSUB:
	case DRV_GPU_IR_FMUL:
	case DRV_GPU_IR_FMIN:
	case DRV_GPU_IR_FMAX:
	case DRV_GPU_IR_FLT:
	case DRV_GPU_IR_FGE:
	case DRV_GPU_IR_FEQ:
	case DRV_GPU_IR_FNEU:
	case DRV_GPU_IR_AND:
	case DRV_GPU_IR_OR:
	case DRV_GPU_IR_IADD:
	case DRV_GPU_IR_ISUB:
	case DRV_GPU_IR_IMUL:
	case DRV_GPU_IR_IAND:
	case DRV_GPU_IR_IOR:
	case DRV_GPU_IR_IXOR:
	case DRV_GPU_IR_SHL:
	case DRV_GPU_IR_SHR:
	case DRV_GPU_IR_ASR:
	case DRV_GPU_IR_ILT:
	case DRV_GPU_IR_IGE:
	case DRV_GPU_IR_ULT:
	case DRV_GPU_IR_UGE:
	case DRV_GPU_IR_IEQ:
	case DRV_GPU_IR_INE:
		sources[0] = instruction->src[0];
		sources[1] = instruction->src[1];
		return 2;
	case DRV_GPU_IR_SELECT:
		sources[0] = instruction->src[0];
		sources[1] = instruction->src[1];
		sources[2] = instruction->src[2];
		return 3;
	case DRV_GPU_IR_LOAD_STORAGE:
		sources[0] = instruction->src[0];
		if (instruction->component == 0U)
			return 1;
		sources[1] = instruction->src[1];
		return 2;
	case DRV_GPU_IR_STORE_STORAGE:
	case DRV_GPU_IR_STORE_SHARED:
		sources[0] = instruction->src[0];
		sources[1] = instruction->src[1];
		if (instruction->component == 0U)
			return 2;
		sources[2] = instruction->src[2];
		return 3;
	case DRV_GPU_IR_LOAD_SHARED:
		sources[0] = instruction->src[0];
		if (instruction->component == 0U)
			return 1;
		sources[1] = instruction->src[1];
		return 2;
	case DRV_GPU_IR_LOOP_BEGIN:
	case DRV_GPU_IR_BARRIER:
	case DRV_GPU_IR_FENCE:
		return 0;
	case DRV_GPU_IR_LOOP_END:
	case DRV_GPU_IR_STORE_OUTPUT:
	case DRV_GPU_IR_KILL:
		sources[0] = instruction->src[0];
		return 1;
	case DRV_GPU_IR_LOAD_INPUT:
		return 0;
	case DRV_GPU_IR_SAMPLE:
		sources[0] = instruction->src[0];
		sources[1] = instruction->src[1];
		return 2;
	case DRV_GPU_IR_SAMPLE_LOD:
	case DRV_GPU_IR_SAMPLE_BIAS:
		sources[0] = instruction->src[0];
		sources[1] = instruction->src[1];
		sources[2] = instruction->src[2];
		return 3;
	case DRV_GPU_IR_TEXTURE:
		/* The message's parameters: src[1] consecutive values from src[0]. */
		if (instruction->src[1] == 0U || instruction->src[1] > NVRTX_SM75_MAX_SOURCES)
			break;
		for (count = 0; count < (int)instruction->src[1]; count++)
			sources[count] = instruction->src[0] + (uint32_t)count;
		return count;
	case DRV_GPU_IR_UDIV:
	case DRV_GPU_IR_UMOD:
	case DRV_GPU_IR_IDIV:
	case DRV_GPU_IR_IREM:
		sources[0] = instruction->src[0];
		sources[1] = instruction->src[1];
		return 2;
	case DRV_GPU_IR_ATOMIC:
		/* The offset and the value, the compare value of a compare and exchange, then the predicate. */
		sources[0] = instruction->src[0];
		sources[1] = instruction->src[1];
		count = 2;

		/* A compare and exchange reads the value it compares with. */
		if (instruction->immediate == DRV_GPU_IR_ATOMIC_CMPXCHG) {
			sources[count] = instruction->src[count];
			count++;
		}

		/* A predicated one reads its predicate last. */
		if (instruction->component == DRV_GPU_IR_ATOMIC_PREDICATED) {
			sources[count] = instruction->src[count];
			count++;
		}

		/* Succeeded: the atomic's sources. */
		return count;
	default:
		break;
	}

	/* Succeeded in finding the operation the compiler does not cover. */
	return -1;
}

/*
 * Reports nonzero when an instruction defines its destination value.
 */
int
nvrtx_sm75_defines(
	const struct drv_gpu_shader_ir_inst *instruction)
{
	/* The operations without a result: every skippable block is run, so its markers are nothing. */
	if (instruction->op == DRV_GPU_IR_NOP)
		return 0;
	if (instruction->op == DRV_GPU_IR_SKIP_BEGIN)
		return 0;
	if (instruction->op == DRV_GPU_IR_SKIP_END)
		return 0;
	if (instruction->op == DRV_GPU_IR_STORE_SHARED)
		return 0;
	if (instruction->op == DRV_GPU_IR_LOOP_BEGIN)
		return 0;
	if (instruction->op == DRV_GPU_IR_LOOP_END)
		return 0;
	if (instruction->op == DRV_GPU_IR_BARRIER)
		return 0;
	if (instruction->op == DRV_GPU_IR_FENCE)
		return 0;
	if (instruction->op == DRV_GPU_IR_STORE_OUTPUT)
		return 0;
	if (instruction->op == DRV_GPU_IR_KILL)
		return 0;

	/* A sample defines four values, which its lowering gives registers. */
	if (instruction->op == DRV_GPU_IR_SAMPLE)
		return 0;
	if (instruction->op == DRV_GPU_IR_SAMPLE_LOD)
		return 0;
	if (instruction->op == DRV_GPU_IR_SAMPLE_BIAS)
		return 0;
	if (instruction->op == DRV_GPU_IR_TEXTURE)
		return 0;
	if (instruction->op == DRV_GPU_IR_STORE_STORAGE)
		return 0;

	/* Succeeded: every other covered operation has one. */
	return 1;
}

/*
 * Makes every value a loop reads but that was made before the loop live
 * to the loop's end, since the next pass reads it again.  Checks that the
 * loops are nested properly.
 */
int
nvrtx_sm75_extend_loops(
	struct nvrtx_sm75_lowering *lowering)
{
	const struct drv_gpu_shader_ir *ir;
	const struct drv_gpu_shader_ir_inst *instruction;
	uint32_t *first_def;
	uint32_t begins[NVRTX_SM75_LOOP_DEPTH];
	uint32_t sources[NVRTX_SM75_MAX_SOURCES];
	uint32_t depth;
	uint32_t begin;
	uint32_t index;
	uint32_t inner;
	uint32_t source;
	int defines;
	int count;
	int error;

	/* The program. */
	ir = lowering->ir;

	/* Each value's first definition. */
	first_def = kern_calloc(ir->value_count + 1U, sizeof(*first_def));
	if (first_def == NULL)
		return ENOMEM;

	/* No value is defined before the program starts. */
	for (index = 0U; index <= ir->value_count; index++)
		first_def[index] = NVRTX_SM75_NOT_DEFINED;

	/* The first instruction that defines each value. */
	for (index = 0U; index < ir->instruction_count; index++) {
		instruction = &ir->instructions[index];
		defines = nvrtx_sm75_defines(instruction);
		if (defines != 0 && first_def[instruction->dst] == NVRTX_SM75_NOT_DEFINED)
			first_def[instruction->dst] = index;
	}

	/* Each loop, when its end is reached: the earlier values its body reads live to the end. */
	depth = 0U;
	for (index = 0U; index < ir->instruction_count; index++) {
		instruction = &ir->instructions[index];

		/* A loop's beginning goes one level deeper, as deep as the convergence barriers allow. */
		if (instruction->op == DRV_GPU_IR_LOOP_BEGIN) {
			if (depth == NVRTX_SM75_LOOP_DEPTH) {
				kern_free(first_def);
				lowering->index = index;
				error = nvrtx_sm75_refuse(lowering, ENOTSUP, "loops nested deeper than the convergence barriers");
				return error;
			}

			/* The loop begins here. */
			begins[depth] = index;
			depth++;
			continue;
		}

		/* Only a loop's end has more to do. */
		if (instruction->op != DRV_GPU_IR_LOOP_END)
			continue;

		/* A loop's end closes the innermost loop, which must have begun. */
		if (depth == 0U) {
			kern_free(first_def);
			lowering->index = index;
			error = nvrtx_sm75_refuse(lowering, EINVAL, "a loop's end without its beginning");
			return error;
		}

		/* The loop being closed. */
		depth--;
		begin = begins[depth];

		/* Each instruction of its body. */
		for (inner = begin + 1U; inner <= index; inner++) {
			/* Each value the instruction reads that was defined before the loop lives to the loop's end. */
			count = nvrtx_sm75_sources(&ir->instructions[inner], sources);
			for (source = 0U; count > 0 && source < (uint32_t)count; source++) {
				if (first_def[sources[source]] >= begin)
					continue;
				if (lowering->last_use[sources[source]] < index + 1U)
					lowering->last_use[sources[source]] = index + 1U;
			}
		}
	}

	/* The first definitions are only this pass's. */
	kern_free(first_def);

	/* Every loop must end. */
	if (depth != 0U) {
		error = nvrtx_sm75_refuse(lowering, EINVAL, "a loop without its end");
		return error;
	}

	/* Succeeded: the values live through their loops. */
	return 0;
}

/*
 * Takes the free register given back longest ago, among the first
 * NVRTX_SM75_GPR_POOL or the ones in use so far, whichever are more; when
 * none of them is free, the lowest free one.
 */
int
nvrtx_sm75_take(
	struct nvrtx_sm75_lowering *lowering,
	uint32_t *reg)
{
	uint32_t index;
	uint32_t limit;
	uint32_t best;
	int error;

	/* The pool. */
	limit = lowering->highest;
	if (limit < NVRTX_SM75_GPR_POOL)
		limit = NVRTX_SM75_GPR_POOL;
	if (limit > NVRTX_SM75_GPR_USABLE)
		limit = NVRTX_SM75_GPR_USABLE;

	/* The free one of the pool given back longest ago, else the first free one at all. */
	best = NVRTX_SM75_GPR_USABLE;
	for (index = 0U; index < NVRTX_SM75_GPR_USABLE; index++) {
		if (lowering->taken[index] != 0U)
			continue;
		if (index >= limit && best != NVRTX_SM75_GPR_USABLE)
			break;
		if (best == NVRTX_SM75_GPR_USABLE || lowering->freed_at[index] < lowering->freed_at[best])
			best = index;
		if (index >= limit)
			break;
	}

	/* None is free: the compiler does not spill. */
	if (best == NVRTX_SM75_GPR_USABLE) {
		error = nvrtx_sm75_refuse(lowering, ENOTSUP, "the program needs more registers than there are (no spilling yet)");
		return error;
	}

	/* It is taken from here on. */
	lowering->taken[best] = 1U;
	if (best + 1U > lowering->highest)
		lowering->highest = best + 1U;
	*reg = best;

	/* Succeeded: the register is the caller's. */
	return 0;
}

/*
 * Takes an even register whose odd neighbour is free too, the pair given back longest ago, as nvrtx_sm75_take() does.
 */
int
nvrtx_sm75_take_pair(
	struct nvrtx_sm75_lowering *lowering,
	uint32_t *reg)
{
	uint32_t index;
	uint32_t limit;
	uint32_t best;
	uint32_t age;
	uint32_t best_age;
	int error;

	/* The pool. */
	limit = lowering->highest;
	if (limit < NVRTX_SM75_GPR_POOL)
		limit = NVRTX_SM75_GPR_POOL;
	if (limit > NVRTX_SM75_GPR_USABLE)
		limit = NVRTX_SM75_GPR_USABLE;

	/* The free pair of the pool given back longest ago, else the first free pair at all. */
	best = NVRTX_SM75_GPR_USABLE;
	best_age = 0U;
	for (index = 0U; index + 1U < NVRTX_SM75_GPR_USABLE; index += 2U) {
		if (lowering->taken[index] != 0U)
			continue;
		if (lowering->taken[index + 1U] != 0U)
			continue;
		if (index >= limit && best != NVRTX_SM75_GPR_USABLE)
			break;

		/* The later of the two give-backs. */
		age = lowering->freed_at[index];
		if (lowering->freed_at[index + 1U] > age)
			age = lowering->freed_at[index + 1U];
		if (best == NVRTX_SM75_GPR_USABLE || age < best_age) {
			best = index;
			best_age = age;
		}

		/* Past the pool, the first free pair is enough. */
		if (index >= limit)
			break;
	}

	/* None is free: the compiler does not spill. */
	if (best == NVRTX_SM75_GPR_USABLE) {
		error = nvrtx_sm75_refuse(lowering, ENOTSUP, "the program needs more registers than there are (no spilling yet)");
		return error;
	}

	/* Both are taken from here on. */
	lowering->taken[best] = 1U;
	lowering->taken[best + 1U] = 1U;
	if (best + 2U > lowering->highest)
		lowering->highest = best + 2U;
	*reg = best;

	/* Succeeded: the pair is the caller's. */
	return 0;
}

/*
 * Gives a register back.
 */
void
nvrtx_sm75_give(
	struct nvrtx_sm75_lowering *lowering,
	uint32_t reg)
{
	/* RZ and out-of-range numbers are never taken. */
	if (reg >= NVRTX_SM75_GPR_USABLE)
		return;

	/* Free from here on, given back now. */
	lowering->taken[reg] = 0U;
	lowering->freed_at[reg] = lowering->count + 1U;
}

/*
 * Gives back the registers of the values an instruction read for the last
 * time, and of its result when nothing reads it.
 */
void
nvrtx_sm75_retire(
	struct nvrtx_sm75_lowering *lowering,
	const struct drv_gpu_shader_ir_inst *instruction)
{
	uint32_t sources[NVRTX_SM75_MAX_SOURCES];
	uint32_t value;
	uint32_t source;
	int is_sample;
	int defines;
	int count;

	/* The sources read for the last time. */
	defines = nvrtx_sm75_defines(instruction);
	count = nvrtx_sm75_sources(instruction, sources);
	for (source = 0U; count > 0 && source < (uint32_t)count; source++) {
		value = sources[source];

		/* A value read later, already given back, or the result itself stays. */
		if (lowering->last_use[value] != lowering->index + 1U)
			continue;
		if (lowering->reg[value] == NVRTX_SM75_NO_REG)
			continue;
		if (defines != 0 && value == instruction->dst)
			continue;

		/* Its register goes back. */
		nvrtx_sm75_give(lowering, lowering->reg[value]);
		lowering->reg[value] = NVRTX_SM75_NO_REG;
	}

	/* A sample's results nothing reads afterwards go back at once. */
	is_sample = nvrtx_sm75_is_sample(instruction->op);
	for (source = 0U; is_sample != 0 && source < 4U; source++) {
		value = instruction->dst + source;
		if (lowering->last_use[value] > lowering->index + 1U)
			continue;
		if (lowering->reg[value] == NVRTX_SM75_NO_REG)
			continue;

		/* Its register goes back. */
		nvrtx_sm75_give(lowering, lowering->reg[value]);
		lowering->reg[value] = NVRTX_SM75_NO_REG;
	}

	/* A result nothing reads afterwards goes back at once. */
	if (defines == 0)
		return;
	value = instruction->dst;
	if (lowering->last_use[value] > lowering->index + 1U)
		return;
	if (lowering->reg[value] == NVRTX_SM75_NO_REG)
		return;

	/* Its register goes back. */
	nvrtx_sm75_give(lowering, lowering->reg[value]);
	lowering->reg[value] = NVRTX_SM75_NO_REG;
}

/*
 * Reports a value's register as a source operand.
 */
struct nvrtx_sm75_src
nvrtx_sm75_value(
	const struct nvrtx_sm75_lowering *lowering,
	uint32_t value)
{
	struct nvrtx_sm75_src operand;

	/* The register the value lives in; a value never defined reads as RZ. */
	operand = nvrtx_sm75_reg(NVRTX_SM75_RZ);
	if (lowering->reg[value] != NVRTX_SM75_NO_REG)
		operand = nvrtx_sm75_reg(lowering->reg[value]);

	/* Succeeded: the operand. */
	return operand;
}
