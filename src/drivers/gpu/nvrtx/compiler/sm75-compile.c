/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The nvrtx shader compiler, stage A (see sm75-compile.h).
 *
 * One pass over the scalar IR chooses the SM75 instructions of each IR
 * instruction and assigns registers as it goes: a value gets a register
 * when it is defined and gives it back after the last instruction that
 * reads it (the IR of stage A is straight-line).  Addresses take an even
 * pair.  A Boolean of the IR is a register of all ones or zero; a
 * comparison sets P0 and turns it into a Boolean at once, and a selection
 * or a guarded access turns the Boolean back into P0.
 *
 * The dependences are marked serially, which is always correct and never
 * fast: an instruction of fixed latency holds the next one back for the
 * longest latency its kind of result has (6 cycles for a register, 13 for a
 * predicate, Turing's table); an instruction of variable latency releases
 * scoreboard 0 when its result is written and scoreboard 1 when its
 * sources are read, and the next instruction waits for both.  EXIT waits
 * for every scoreboard.
 */

#include "sm75-compile.h"
#include "sm75-encode.h"
#include "sm75-isa.h"

#include "drivers/gpu/compiler/optimize.h"
#include "drivers/gpu/compiler/spirv.h"

#include <kern/kcrt.h>
#include <kern/kmem.h>
#include <uapi/errno.h>

/* A value without a register. */
#define NVRTX_SM75_NO_REG		0xffffU

/* The most SM75 instructions one IR instruction becomes, and the end of the program (EXIT, BRA and the padding). */
#define NVRTX_SM75_EXPANSION		40U
#define NVRTX_SM75_TAIL			10U

/* The instructions in one 128-byte line of code, to which the program is padded. */
#define NVRTX_SM75_LINE_INSTRUCTIONS	8U

/* How the next instruction must wait for one just emitted. */
#define NVRTX_SM75_FIXED		0
#define NVRTX_SM75_FIXED_PREDICATE	1
#define NVRTX_SM75_VARIABLE_WRITE	2
#define NVRTX_SM75_VARIABLE_STORE	3
#define NVRTX_SM75_VARIABLE_SPECIAL	4
#define NVRTX_SM75_CONTROL		5
#define NVRTX_SM75_SYNC			6

/* The delay of a barrier or a memory barrier (its execution takes 5 cycles), and of the NOP after it. */
#define NVRTX_SM75_DELAY_SYNC		6U
#define NVRTX_SM75_DELAY_AFTER_SYNC	2U

/* The deepest loop nest (one convergence barrier each), and a value not yet defined. */
#define NVRTX_SM75_LOOP_DEPTH		16U
#define NVRTX_SM75_NOT_DEFINED		0xffffffffU

/* 2^32 less 512 as a float (0x4f7ffffe), which scales a reciprocal to a 32-bit fixed-point one. */
#define NVRTX_SM75_RECIPROCAL_SCALE	0x4f7ffffeU

/* The delays of the fixed-latency results, and of a variable-latency instruction itself. */
#define NVRTX_SM75_DELAY_GPR		6U
#define NVRTX_SM75_DELAY_PREDICATE	13U
#define NVRTX_SM75_DELAY_VARIABLE	1U
#define NVRTX_SM75_DELAY_EXIT		5U

/* The scoreboards of the serial marking: the result, and the read of the sources. */
#define NVRTX_SM75_BAR_RESULT		0U
#define NVRTX_SM75_BAR_SOURCES		1U

/* All ones, the Boolean true; the sign bit; and 1 / (2 pi) as a float, which the sine and cosine units take. */
#define NVRTX_SM75_TRUE			0xffffffffU
#define NVRTX_SM75_SIGN			0x80000000U
#define NVRTX_SM75_INV_TWO_PI		0x3e22f983U

/* The truth tables of LOP3 used: a and b, a or b, a xor b, not a. */
#define NVRTX_SM75_LUT_AND		0xc0U
#define NVRTX_SM75_LUT_OR		0xfcU
#define NVRTX_SM75_LUT_XOR		0x3cU
#define NVRTX_SM75_LUT_NOT		0x0fU

/* What a uniform of the IR maps to: no slot. */
#define NVRTX_SM75_NO_SLOT		0xffffffffU

/*
 * The state of one compilation.
 *
 * Lives on the stack of nvrtx_sm75_compile_ir(); its arrays are allocated
 * there and freed before it returns.
 */
struct nvrtx_sm75_lowering {
	/* The program, the result being built, and where a refusal is described. */
	const struct drv_gpu_shader_ir *ir;
	struct nvrtx_sm75_binary *binary;
	struct nvrtx_sm75_diagnostic *diagnostic;

	/* Each value's register (NVRTX_SM75_NO_REG while it has none), and the IR instruction that last reads it, plus one (0: none). */
	uint16_t *reg;
	uint32_t *last_use;

	/* Which registers are taken, and one past the highest ever taken. */
	uint8_t taken[NVRTX_SM75_GPR_USABLE];
	uint32_t highest;

	/* Each uniform's storage slot or constant buffer, NVRTX_SM75_NO_SLOT for neither. */
	uint32_t *slot;

	/* The code: room in instructions, instructions emitted, and the scoreboards the next one waits for. */
	uint32_t capacity;
	uint32_t count;
	uint32_t pending;

	/* The IR instruction being lowered. */
	uint32_t index;

	/* The loops being lowered: each one's BSSY and first instruction, innermost last. */
	uint32_t loop_bssy[NVRTX_SM75_LOOP_DEPTH];
	uint32_t loop_start[NVRTX_SM75_LOOP_DEPTH];
	uint32_t loop_depth;
};

static int nvrtx_sm75_refuse(struct nvrtx_sm75_lowering *lowering, int error, const char *reason);
static int nvrtx_sm75_prepare(struct nvrtx_sm75_lowering *lowering);
static int nvrtx_sm75_sources(const struct drv_gpu_shader_ir_inst *instruction, uint32_t *sources);
static int nvrtx_sm75_defines(const struct drv_gpu_shader_ir_inst *instruction);
static int nvrtx_sm75_map_uniforms(struct nvrtx_sm75_lowering *lowering);
static int nvrtx_sm75_lower(struct nvrtx_sm75_lowering *lowering, const struct drv_gpu_shader_ir_inst *instruction, uint32_t dst);
static int nvrtx_sm75_lower_alu(struct nvrtx_sm75_lowering *lowering, const struct drv_gpu_shader_ir_inst *instruction, uint32_t dst);
static int nvrtx_sm75_lower_compare(struct nvrtx_sm75_lowering *lowering, const struct drv_gpu_shader_ir_inst *instruction, uint32_t dst);
static int nvrtx_sm75_lower_unit(struct nvrtx_sm75_lowering *lowering, const struct drv_gpu_shader_ir_inst *instruction, uint32_t dst);
static int nvrtx_sm75_lower_system(struct nvrtx_sm75_lowering *lowering, const struct drv_gpu_shader_ir_inst *instruction, uint32_t dst);
static int nvrtx_sm75_lower_storage(struct nvrtx_sm75_lowering *lowering, const struct drv_gpu_shader_ir_inst *instruction, uint32_t dst);
static int nvrtx_sm75_lower_loop(struct nvrtx_sm75_lowering *lowering, const struct drv_gpu_shader_ir_inst *instruction);
static int nvrtx_sm75_lower_divide(struct nvrtx_sm75_lowering *lowering, const struct drv_gpu_shader_ir_inst *instruction, uint32_t dst);
static int nvrtx_sm75_divide_unsigned(struct nvrtx_sm75_lowering *lowering, uint32_t numerator, uint32_t denominator, uint32_t quotient, uint32_t remainder);
static int nvrtx_sm75_absolute(struct nvrtx_sm75_lowering *lowering, uint32_t value, uint32_t result);
static int nvrtx_sm75_negate_if_p0(struct nvrtx_sm75_lowering *lowering, uint32_t value);
static int nvrtx_sm75_lower_atomic(struct nvrtx_sm75_lowering *lowering, const struct drv_gpu_shader_ir_inst *instruction, uint32_t dst);
static int nvrtx_sm75_lower_shared(struct nvrtx_sm75_lowering *lowering, const struct drv_gpu_shader_ir_inst *instruction, uint32_t dst);
static int nvrtx_sm75_lower_sync(struct nvrtx_sm75_lowering *lowering, const struct drv_gpu_shader_ir_inst *instruction);
static int nvrtx_sm75_predicate(struct nvrtx_sm75_lowering *lowering, uint32_t value);
static int nvrtx_sm75_extend_loops(struct nvrtx_sm75_lowering *lowering);
static int nvrtx_sm75_storage_address(struct nvrtx_sm75_lowering *lowering, uint32_t slot, uint32_t offset, uint32_t *address);
static int nvrtx_sm75_emit(struct nvrtx_sm75_lowering *lowering, struct nvrtx_sm75_word *word, int kind);
static int nvrtx_sm75_finish(struct nvrtx_sm75_lowering *lowering);
static int nvrtx_sm75_take(struct nvrtx_sm75_lowering *lowering, uint32_t *reg);
static int nvrtx_sm75_take_pair(struct nvrtx_sm75_lowering *lowering, uint32_t *reg);
static void nvrtx_sm75_give(struct nvrtx_sm75_lowering *lowering, uint32_t reg);
static void nvrtx_sm75_retire(struct nvrtx_sm75_lowering *lowering, const struct drv_gpu_shader_ir_inst *instruction);
static struct nvrtx_sm75_src nvrtx_sm75_value(const struct nvrtx_sm75_lowering *lowering, uint32_t value);

/*
 * Compiles a SPIR-V compute module.
 *
 * The module is parsed by the shared frontend, optimized, and lowered; a
 * program the lowering refuses after the optimizer rewrote it is lowered
 * again from the parsed one, which keeps the parsed program's outcome.
 * Returns 0 with the program, or the frontend's or the lowering's error
 * with the diagnostic filled.
 */
int
nvrtx_sm75_compile(
	const uint32_t *words,
	size_t word_count,
	struct nvrtx_sm75_binary **binary,
	struct nvrtx_sm75_diagnostic *diagnostic)
{
	struct drv_gpu_shader_ir *ir;
	struct drv_gpu_shader_ir optimized;
	struct drv_gpu_shader_optimize_options options;
	struct drv_gpu_compile_diagnostic parse_diagnostic;
	int error;

	/* A refused compile never leaves an old output pointer live. */
	if (binary == NULL)
		return EINVAL;
	*binary = NULL;

	/* Starts with no refusal. */
	if (diagnostic != NULL)
		kern_memset(diagnostic, 0, sizeof(*diagnostic));

	/* A module without words is refused. */
	if (words == NULL)
		return EINVAL;

	/* Parses the compute stage. */
	ir = NULL;
	parse_diagnostic.reason = NULL;
	error = drv_gpu_shader_parse(words, word_count, DRV_GPU_STAGE_COMPUTE, &ir, &parse_diagnostic);
	if (error != 0) {
		if (diagnostic != NULL) {
			diagnostic->instruction = parse_diagnostic.word_offset;
			diagnostic->reason = parse_diagnostic.reason;
		}
		return error;
	}

	/* Optimizes a private copy: every block runs (there are no skipped blocks), and the known bits fold. */
	kern_memset(&options, 0, sizeof(options));
	options.runs_every_block = 1U;
	options.simplify = 1U;
	kern_memset(options.needed, 1, sizeof(options.needed));
	error = drv_gpu_shader_optimize(ir, &options, &optimized);
	if (error != 0) {
		drv_gpu_shader_ir_free(ir);
		return error;
	}

	/* Lowers the rewritten program, or the parsed one when the optimizer left it alone. */
	if (optimized.instructions != NULL) {
		error = nvrtx_sm75_compile_ir(&optimized, binary, diagnostic);
	} else {
		error = nvrtx_sm75_compile_ir(ir, binary, diagnostic);
	}

	/* A rewritten program refused for any reason but memory is lowered from the parsed one. */
	if (optimized.instructions != NULL && error != 0 && error != ENOMEM) {
		if (diagnostic != NULL)
			kern_memset(diagnostic, 0, sizeof(*diagnostic));
		error = nvrtx_sm75_compile_ir(ir, binary, diagnostic);
	}

	/* The IR retires before the program is handed over. */
	kern_free(optimized.instructions);
	drv_gpu_shader_ir_free(ir);

	/* Reports the lowering's refusal. */
	if (error != 0)
		return error;

	/* Succeeded: the caller owns the program. */
	return 0;
}

/*
 * Compiles a parsed compute program.
 *
 * Returns 0 with the program in *binary, EINVAL for a malformed program,
 * ENOTSUP for one stage A does not cover (both with the diagnostic), or
 * ENOMEM.
 */
int
nvrtx_sm75_compile_ir(
	const struct drv_gpu_shader_ir *ir,
	struct nvrtx_sm75_binary **binary,
	struct nvrtx_sm75_diagnostic *diagnostic)
{
	struct nvrtx_sm75_lowering lowering;
	const struct drv_gpu_shader_ir_inst *instruction;
	uint32_t dst;
	uint32_t index;
	int defines;
	int error;

	/* A refused compile never leaves an old output pointer live. */
	if (binary == NULL || ir == NULL)
		return EINVAL;
	*binary = NULL;

	/* The state, empty. */
	kern_memset(&lowering, 0, sizeof(lowering));
	lowering.ir = ir;
	lowering.diagnostic = diagnostic;

	/* The result. */
	lowering.binary = kern_calloc(1U, sizeof(*lowering.binary));
	if (lowering.binary == NULL)
		return ENOMEM;

	/* Checks the program, finds the last reads, maps the uniforms and makes room for the code. */
	error = nvrtx_sm75_prepare(&lowering);

	/* Lowers every instruction in order. */
	for (index = 0U; error == 0 && index < ir->instruction_count; index++) {
		instruction = &ir->instructions[index];
		lowering.index = index;

		/* The destination's register, a value redefined by a MOVE keeping its own. */
		dst = NVRTX_SM75_RZ;
		defines = nvrtx_sm75_defines(instruction);
		if (defines != 0) {
			if (lowering.reg[instruction->dst] == NVRTX_SM75_NO_REG) {
				error = nvrtx_sm75_take(&lowering, &dst);
				if (error != 0)
					break;
				lowering.reg[instruction->dst] = (uint16_t)dst;
			}
			dst = lowering.reg[instruction->dst];
		}

		/* The instruction's SM75 instructions. */
		error = nvrtx_sm75_lower(&lowering, instruction, dst);
		if (error != 0)
			break;

		/* Registers of values no later instruction reads go back. */
		nvrtx_sm75_retire(&lowering, instruction);
	}

	/* The end of the program. */
	if (error == 0)
		error = nvrtx_sm75_finish(&lowering);

	/* The per-value and per-uniform arrays are only the compilation's. */
	kern_free(lowering.reg);
	kern_free(lowering.last_use);
	kern_free(lowering.slot);

	/* A refused program is never handed over. */
	if (error != 0) {
		nvrtx_sm75_binary_free(lowering.binary);
		return error;
	}

	/* Succeeded: the caller owns the program. */
	*binary = lowering.binary;
	return 0;
}

/* Frees a compiled program. */
void
nvrtx_sm75_binary_free(
	struct nvrtx_sm75_binary *binary)
{
	/* Nothing to free. */
	if (binary == NULL)
		return;

	/* The code, then the record. */
	kern_free(binary->code);
	kern_free(binary);
}

/* Records a refusal of the current instruction and reports its error. */
static int
nvrtx_sm75_refuse(
	struct nvrtx_sm75_lowering *lowering,
	int error,
	const char *reason)
{
	/* The instruction and the reason, when the caller asked for them. */
	if (lowering->diagnostic != NULL) {
		lowering->diagnostic->instruction = lowering->index;
		lowering->diagnostic->reason = reason;
	}

	/* Succeeded in describing the refusal; the error goes back. */
	return error;
}

/*
 * Checks that the program is a compute program stage A covers, finds each
 * value's last read, maps the uniforms, and allocates the per-value arrays
 * and the code.
 */
static int
nvrtx_sm75_prepare(
	struct nvrtx_sm75_lowering *lowering)
{
	const struct drv_gpu_shader_ir *ir;
	const struct drv_gpu_shader_ir_inst *instruction;
	struct nvrtx_sm75_binary *binary;
	uint32_t sources[4];
	uint32_t source_count;
	uint32_t index;
	uint32_t source;
	int defines;
	int count;
	int error;

	ir = lowering->ir;
	binary = lowering->binary;

	/* Only a compute program is compiled. */
	if (ir->stage != DRV_GPU_STAGE_COMPUTE)
		return nvrtx_sm75_refuse(lowering, ENOTSUP, "only compute shaders are compiled (stage A)");

	/* The per-value arrays. */
	lowering->reg = kern_calloc(ir->value_count + 1U, sizeof(*lowering->reg));
	if (lowering->reg == NULL)
		return ENOMEM;

	lowering->last_use = kern_calloc(ir->value_count + 1U, sizeof(*lowering->last_use));
	if (lowering->last_use == NULL)
		return ENOMEM;

	/* No value has a register yet. */
	for (index = 0U; index <= ir->value_count; index++)
		lowering->reg[index] = NVRTX_SM75_NO_REG;

	/* Each instruction: covered, its values in range, its sources' last reads. */
	for (index = 0U; index < ir->instruction_count; index++) {
		instruction = &ir->instructions[index];
		lowering->index = index;

		/* The sources of an operation stage A covers. */
		count = nvrtx_sm75_sources(instruction, sources);
		if (count < 0)
			return nvrtx_sm75_refuse(lowering, ENOTSUP, "the operation is not compiled yet (stage A)");

		/* The destination is a value of the program. */
		defines = nvrtx_sm75_defines(instruction);
		if (defines != 0 && instruction->dst >= ir->value_count)
			return nvrtx_sm75_refuse(lowering, EINVAL, "a destination is not a value of the program");

		/* Each source is a value of the program, read last here so far. */
		source_count = (uint32_t)count;
		for (source = 0U; source < source_count; source++) {
			if (sources[source] >= ir->value_count)
				return nvrtx_sm75_refuse(lowering, EINVAL, "a source is not a value of the program");
			lowering->last_use[sources[source]] = index + 1U;
		}
	}

	/* Values read inside a loop that were made before it live to the loop's end. */
	error = nvrtx_sm75_extend_loops(lowering);
	if (error != 0)
		return error;

	/* The uniforms' slots. */
	error = nvrtx_sm75_map_uniforms(lowering);
	if (error != 0)
		return error;

	/* The code: every instruction's largest expansion and the end. */
	lowering->capacity = ir->instruction_count * NVRTX_SM75_EXPANSION + NVRTX_SM75_TAIL;
	binary->code = kern_calloc(lowering->capacity, NVRTX_SM75_INSTRUCTION_BYTES);
	if (binary->code == NULL)
		return ENOMEM;

	/* The program's interface. */
	binary->local_size[0] = ir->local_size[0];
	binary->local_size[1] = ir->local_size[1];
	binary->local_size[2] = ir->local_size[2];
	binary->shared_bytes = ir->shared_bytes;
	binary->barrier_count = 0U;
	if (ir->uses_barrier != 0U)
		binary->barrier_count = 1U;

	/* Succeeded: the program can be lowered. */
	lowering->index = 0U;
	return 0;
}

/*
 * Lists the values an instruction reads; reports their number, or -1 for
 * an operation stage A does not cover.
 */
static int
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
		sources[0] = instruction->src[0];
		return 1;
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
		if (instruction->immediate == DRV_GPU_IR_ATOMIC_CMPXCHG) {
			sources[count] = instruction->src[count];
			count++;
		}
		if (instruction->component == DRV_GPU_IR_ATOMIC_PREDICATED) {
			sources[count] = instruction->src[count];
			count++;
		}
		return count;
	default:
		break;
	}

	/* Succeeded in finding the operation outside stage A. */
	return -1;
}

/* Reports nonzero when an instruction defines its destination value. */
static int
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
	if (instruction->op == DRV_GPU_IR_STORE_STORAGE)
		return 0;

	/* Succeeded: every other covered operation has one. */
	return 1;
}

/*
 * Gives each storage buffer of the uniform list a slot of the storage table
 * and each uniform buffer a constant buffer, in the list's order, and
 * records them in the result.
 */
static int
nvrtx_sm75_map_uniforms(
	struct nvrtx_sm75_lowering *lowering)
{
	const struct drv_gpu_shader_ir *ir;
	const struct drv_gpu_shader_ir_uniform *uniform;
	struct nvrtx_sm75_binary *binary;
	uint32_t index;

	ir = lowering->ir;
	binary = lowering->binary;

	/* One slot per uniform. */
	lowering->slot = kern_calloc(ir->uniform_count + 1U, sizeof(*lowering->slot));
	if (lowering->slot == NULL)
		return ENOMEM;

	/* Each uniform in order. */
	for (index = 0U; index < ir->uniform_count; index++) {
		uniform = &ir->uniforms[index];
		lowering->slot[index] = NVRTX_SM75_NO_SLOT;

		/* A storage buffer takes the next entry of the table. */
		if (uniform->kind == DRV_GPU_IR_UNIFORM_STORAGE) {
			if (binary->storage_count == NVRTX_SM75_CB_STORAGE_MAX)
				return nvrtx_sm75_refuse(lowering, ENOTSUP, "more storage buffers than the table holds");
			lowering->slot[index] = binary->storage_count;
			binary->storage[binary->storage_count].set = uniform->set;
			binary->storage[binary->storage_count].binding = uniform->binding;
			binary->storage_count++;
		} else if (uniform->kind == DRV_GPU_IR_UNIFORM_BLOCK) {
			/* A uniform buffer takes the next constant buffer. */
			if (binary->uniform_count == NVRTX_SM75_CB_UNIFORM_MAX)
				return nvrtx_sm75_refuse(lowering, ENOTSUP, "more uniform buffers than constant buffers");
			lowering->slot[index] = NVRTX_SM75_CB_UNIFORM_FIRST + binary->uniform_count;
			binary->uniforms[binary->uniform_count].set = uniform->set;
			binary->uniforms[binary->uniform_count].binding = uniform->binding;
			binary->uniforms[binary->uniform_count].size = uniform->offset + uniform->size;
			binary->uniform_count++;
		}
	}

	/* Succeeded: every buffer has its place. */
	return 0;
}

/* Lowers one IR instruction into dst (RZ when it has no result). */
static int
nvrtx_sm75_lower(
	struct nvrtx_sm75_lowering *lowering,
	const struct drv_gpu_shader_ir_inst *instruction,
	uint32_t dst)
{
	struct nvrtx_sm75_word word;
	struct nvrtx_sm75_src source;
	uint32_t end;
	int error;

	/* The operations of their own families, then the moves of a value. */
	switch (instruction->op) {
	case DRV_GPU_IR_NOP:
	case DRV_GPU_IR_SKIP_BEGIN:
	case DRV_GPU_IR_SKIP_END:
		return 0;
	case DRV_GPU_IR_FLT:
	case DRV_GPU_IR_FGE:
	case DRV_GPU_IR_FEQ:
	case DRV_GPU_IR_FNEU:
	case DRV_GPU_IR_ILT:
	case DRV_GPU_IR_IGE:
	case DRV_GPU_IR_ULT:
	case DRV_GPU_IR_UGE:
	case DRV_GPU_IR_IEQ:
	case DRV_GPU_IR_INE:
	case DRV_GPU_IR_SELECT:
		error = nvrtx_sm75_lower_compare(lowering, instruction, dst);
		return error;
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
	case DRV_GPU_IR_I2F:
	case DRV_GPU_IR_U2F:
	case DRV_GPU_IR_F2I:
	case DRV_GPU_IR_F2U:
		error = nvrtx_sm75_lower_unit(lowering, instruction, dst);
		return error;
	case DRV_GPU_IR_LOAD_SYSTEM:
		error = nvrtx_sm75_lower_system(lowering, instruction, dst);
		return error;
	case DRV_GPU_IR_LOAD_STORAGE:
	case DRV_GPU_IR_STORE_STORAGE:
	case DRV_GPU_IR_STORAGE_SIZE:
		error = nvrtx_sm75_lower_storage(lowering, instruction, dst);
		return error;
	case DRV_GPU_IR_LOOP_BEGIN:
	case DRV_GPU_IR_LOOP_END:
		error = nvrtx_sm75_lower_loop(lowering, instruction);
		return error;
	case DRV_GPU_IR_UDIV:
	case DRV_GPU_IR_UMOD:
	case DRV_GPU_IR_IDIV:
	case DRV_GPU_IR_IREM:
		error = nvrtx_sm75_lower_divide(lowering, instruction, dst);
		return error;
	case DRV_GPU_IR_ATOMIC:
		error = nvrtx_sm75_lower_atomic(lowering, instruction, dst);
		return error;
	case DRV_GPU_IR_LOAD_SHARED:
	case DRV_GPU_IR_STORE_SHARED:
		error = nvrtx_sm75_lower_shared(lowering, instruction, dst);
		return error;
	case DRV_GPU_IR_BARRIER:
	case DRV_GPU_IR_FENCE:
		error = nvrtx_sm75_lower_sync(lowering, instruction);
		return error;
	case DRV_GPU_IR_CONST:
	case DRV_GPU_IR_ICONST:
	case DRV_GPU_IR_BOOL:
		source = nvrtx_sm75_imm(instruction->immediate);
		break;
	case DRV_GPU_IR_LOAD_PUSH:
		/* A push constant word lies in the first 256 bytes of constant buffer 0. */
		end = instruction->immediate + 4U;
		if (end > NVRTX_SM75_CB_PUSH_BYTES || end < instruction->immediate || (instruction->immediate & 3U) != 0U)
			return nvrtx_sm75_refuse(lowering, ENOTSUP, "a push constant beyond the 256 bytes of the root buffer");
		if (end > lowering->binary->push_bytes)
			lowering->binary->push_bytes = end;
		source = nvrtx_sm75_cb(NVRTX_SM75_CB_ROOT, NVRTX_SM75_CB_PUSH + instruction->immediate);
		break;
	case DRV_GPU_IR_LOAD_UBO:
		/* A uniform buffer word at an offset the constant buffer field holds. */
		if (instruction->location >= lowering->ir->uniform_count || lowering->ir->uniforms[instruction->location].kind != DRV_GPU_IR_UNIFORM_BLOCK)
			return nvrtx_sm75_refuse(lowering, EINVAL, "a uniform load names no uniform buffer");
		if (instruction->immediate > 0xfffcU || (instruction->immediate & 3U) != 0U)
			return nvrtx_sm75_refuse(lowering, ENOTSUP, "a uniform buffer offset beyond 64 KiB");
		source = nvrtx_sm75_cb(lowering->slot[instruction->location], instruction->immediate);
		break;
	case DRV_GPU_IR_MOVE:
		source = nvrtx_sm75_value(lowering, instruction->src[0]);
		break;
	default:
		error = nvrtx_sm75_lower_alu(lowering, instruction, dst);
		return error;
	}

	/* A value no instruction reads needs no move. */
	if (dst == NVRTX_SM75_RZ)
		return 0;

	/* MOV dst, the source. */
	nvrtx_sm75_begin(&word);
	nvrtx_sm75_mov(&word, dst, &source);
	error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_FIXED);
	if (error != 0)
		return error;

	/* Succeeded: the value is in its register. */
	return 0;
}

/* Lowers the float and integer arithmetic and the bitwise operations. */
static int
nvrtx_sm75_lower_alu(
	struct nvrtx_sm75_lowering *lowering,
	const struct drv_gpu_shader_ir_inst *instruction,
	uint32_t dst)
{
	struct nvrtx_sm75_word word;
	struct nvrtx_sm75_src a;
	struct nvrtx_sm75_src b;
	struct nvrtx_sm75_src zero;
	struct nvrtx_sm75_src mask;
	int error;

	nvrtx_sm75_begin(&word);
	zero = nvrtx_sm75_reg(NVRTX_SM75_RZ);
	a = nvrtx_sm75_value(lowering, instruction->src[0]);
	b = zero;

	/* The second source of the binary operations. */
	if (instruction->op != DRV_GPU_IR_FNEG &&
	    instruction->op != DRV_GPU_IR_FABS &&
	    instruction->op != DRV_GPU_IR_NOT &&
	    instruction->op != DRV_GPU_IR_INOT &&
	    instruction->op != DRV_GPU_IR_INEG)
		b = nvrtx_sm75_value(lowering, instruction->src[1]);

	/* The SM75 instruction of the operation. */
	switch (instruction->op) {
	case DRV_GPU_IR_FADD:
		nvrtx_sm75_fadd(&word, dst, &a, &b);
		break;
	case DRV_GPU_IR_FSUB:
		b.neg = 1;
		nvrtx_sm75_fadd(&word, dst, &a, &b);
		break;
	case DRV_GPU_IR_FMUL:
		nvrtx_sm75_fmul(&word, dst, &a, &b);
		break;
	case DRV_GPU_IR_FMIN:
		nvrtx_sm75_fmnmx(&word, dst, &a, &b, 1);
		break;
	case DRV_GPU_IR_FMAX:
		nvrtx_sm75_fmnmx(&word, dst, &a, &b, 0);
		break;
	case DRV_GPU_IR_FNEG:
		/* The sign bit flipped, exactly (also for zero and NaN). */
		mask = nvrtx_sm75_imm(NVRTX_SM75_SIGN);
		nvrtx_sm75_lop3(&word, dst, &a, &mask, &zero, NVRTX_SM75_LUT_XOR);
		break;
	case DRV_GPU_IR_FABS:
		/* The sign bit cleared. */
		mask = nvrtx_sm75_imm(~NVRTX_SM75_SIGN);
		nvrtx_sm75_lop3(&word, dst, &a, &mask, &zero, NVRTX_SM75_LUT_AND);
		break;
	case DRV_GPU_IR_IADD:
		nvrtx_sm75_iadd3(&word, dst, &a, &b, &zero);
		break;
	case DRV_GPU_IR_ISUB:
		b.neg = 1;
		nvrtx_sm75_iadd3(&word, dst, &a, &b, &zero);
		break;
	case DRV_GPU_IR_INEG:
		a.neg = 1;
		nvrtx_sm75_iadd3(&word, dst, &a, &zero, &zero);
		break;
	case DRV_GPU_IR_IMUL:
		nvrtx_sm75_imad(&word, dst, &a, &b, &zero, 0);
		break;
	case DRV_GPU_IR_AND:
	case DRV_GPU_IR_IAND:
		nvrtx_sm75_lop3(&word, dst, &a, &b, &zero, NVRTX_SM75_LUT_AND);
		break;
	case DRV_GPU_IR_OR:
	case DRV_GPU_IR_IOR:
		nvrtx_sm75_lop3(&word, dst, &a, &b, &zero, NVRTX_SM75_LUT_OR);
		break;
	case DRV_GPU_IR_IXOR:
		nvrtx_sm75_lop3(&word, dst, &a, &b, &zero, NVRTX_SM75_LUT_XOR);
		break;
	case DRV_GPU_IR_NOT:
	case DRV_GPU_IR_INOT:
		nvrtx_sm75_lop3(&word, dst, &a, &zero, &zero, NVRTX_SM75_LUT_NOT);
		break;
	case DRV_GPU_IR_SHL:
		nvrtx_sm75_shift(&word, dst, a.value, &b, 0, 0);
		break;
	case DRV_GPU_IR_SHR:
		nvrtx_sm75_shift(&word, dst, a.value, &b, 1, 0);
		break;
	case DRV_GPU_IR_ASR:
		nvrtx_sm75_shift(&word, dst, a.value, &b, 1, 1);
		break;
	default:
		return nvrtx_sm75_refuse(lowering, ENOTSUP, "the operation is not compiled yet (stage A)");
	}

	/* One instruction of fixed latency. */
	error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_FIXED);
	if (error != 0)
		return error;

	/* Succeeded: the result is in dst. */
	return 0;
}

/*
 * Lowers a comparison into P0 and then a Boolean, and a selection by a
 * Boolean.
 */
static int
nvrtx_sm75_lower_compare(
	struct nvrtx_sm75_lowering *lowering,
	const struct drv_gpu_shader_ir_inst *instruction,
	uint32_t dst)
{
	struct nvrtx_sm75_word word;
	struct nvrtx_sm75_src a;
	struct nvrtx_sm75_src b;
	struct nvrtx_sm75_src zero;
	struct nvrtx_sm75_src truth;
	int error;

	zero = nvrtx_sm75_reg(NVRTX_SM75_RZ);
	truth = nvrtx_sm75_imm(NVRTX_SM75_TRUE);
	a = nvrtx_sm75_value(lowering, instruction->src[0]);
	b = nvrtx_sm75_value(lowering, instruction->src[1]);

	/* P0 from the comparison, or from the selection's condition. */
	nvrtx_sm75_begin(&word);
	switch (instruction->op) {
	case DRV_GPU_IR_FLT:
		nvrtx_sm75_fsetp(&word, NVRTX_SM75_P0, NVRTX_SM75_FCMP_LT, &a, &b);
		break;
	case DRV_GPU_IR_FGE:
		nvrtx_sm75_fsetp(&word, NVRTX_SM75_P0, NVRTX_SM75_FCMP_GE, &a, &b);
		break;
	case DRV_GPU_IR_FEQ:
		nvrtx_sm75_fsetp(&word, NVRTX_SM75_P0, NVRTX_SM75_FCMP_EQ, &a, &b);
		break;
	case DRV_GPU_IR_FNEU:
		nvrtx_sm75_fsetp(&word, NVRTX_SM75_P0, NVRTX_SM75_FCMP_UNORD_NE, &a, &b);
		break;
	case DRV_GPU_IR_ILT:
		nvrtx_sm75_isetp(&word, NVRTX_SM75_P0, NVRTX_SM75_ICMP_LT, 1, &a, &b);
		break;
	case DRV_GPU_IR_IGE:
		nvrtx_sm75_isetp(&word, NVRTX_SM75_P0, NVRTX_SM75_ICMP_GE, 1, &a, &b);
		break;
	case DRV_GPU_IR_ULT:
		nvrtx_sm75_isetp(&word, NVRTX_SM75_P0, NVRTX_SM75_ICMP_LT, 0, &a, &b);
		break;
	case DRV_GPU_IR_UGE:
		nvrtx_sm75_isetp(&word, NVRTX_SM75_P0, NVRTX_SM75_ICMP_GE, 0, &a, &b);
		break;
	case DRV_GPU_IR_IEQ:
		nvrtx_sm75_isetp(&word, NVRTX_SM75_P0, NVRTX_SM75_ICMP_EQ, 0, &a, &b);
		break;
	case DRV_GPU_IR_INE:
		nvrtx_sm75_isetp(&word, NVRTX_SM75_P0, NVRTX_SM75_ICMP_NE, 0, &a, &b);
		break;
	default:
		/* SELECT: P0 is the condition (src[0]) being nonzero. */
		nvrtx_sm75_isetp(&word, NVRTX_SM75_P0, NVRTX_SM75_ICMP_NE, 0, &a, &zero);
		break;
	}

	error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_FIXED_PREDICATE);
	if (error != 0)
		return error;

	/* A result no instruction reads is not made. */
	if (dst == NVRTX_SM75_RZ)
		return 0;

	/* The result: the selected source, or the Boolean of P0. */
	nvrtx_sm75_begin(&word);
	if (instruction->op == DRV_GPU_IR_SELECT) {
		a = nvrtx_sm75_value(lowering, instruction->src[1]);
		b = nvrtx_sm75_value(lowering, instruction->src[2]);
		nvrtx_sm75_sel(&word, dst, &a, &b, NVRTX_SM75_P0, 0);
	} else {
		nvrtx_sm75_sel(&word, dst, &zero, &truth, NVRTX_SM75_P0, 1);
	}

	error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_FIXED);
	if (error != 0)
		return error;

	/* Succeeded: the result is in dst. */
	return 0;
}

/*
 * Lowers the operations of the units of variable latency: the special
 * functions (sine and cosine take the angle in turns), the conversions and
 * the roundings (the fraction is x - floor(x)).
 */
static int
nvrtx_sm75_lower_unit(
	struct nvrtx_sm75_lowering *lowering,
	const struct drv_gpu_shader_ir_inst *instruction,
	uint32_t dst)
{
	struct nvrtx_sm75_word word;
	struct nvrtx_sm75_src a;
	struct nvrtx_sm75_src b;
	struct nvrtx_sm75_src turns;
	uint32_t source;
	uint32_t temporary;
	uint32_t function;
	int error;

	source = lowering->reg[instruction->src[0]];
	temporary = NVRTX_SM75_RZ;

	/* Sine and cosine first scale the angle into turns. */
	if (instruction->op == DRV_GPU_IR_SIN || instruction->op == DRV_GPU_IR_COS) {
		error = nvrtx_sm75_take(lowering, &temporary);
		if (error != 0)
			return error;

		a = nvrtx_sm75_reg(source);
		turns = nvrtx_sm75_imm(NVRTX_SM75_INV_TWO_PI);
		nvrtx_sm75_begin(&word);
		nvrtx_sm75_fmul(&word, temporary, &a, &turns);
		error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_FIXED);
		if (error != 0) {
			nvrtx_sm75_give(lowering, temporary);
			return error;
		}
		source = temporary;
	}

	/* The fraction rounds into a temporary first. */
	if (instruction->op == DRV_GPU_IR_FRACT) {
		error = nvrtx_sm75_take(lowering, &temporary);
		if (error != 0)
			return error;
	}

	/* The unit's instruction. */
	nvrtx_sm75_begin(&word);
	function = NVRTX_SM75_MUFU_RCP;
	switch (instruction->op) {
	case DRV_GPU_IR_RSQ:
		function = NVRTX_SM75_MUFU_RSQ;
		break;
	case DRV_GPU_IR_SQRT:
		function = NVRTX_SM75_MUFU_SQRT;
		break;
	case DRV_GPU_IR_EXP2:
		function = NVRTX_SM75_MUFU_EX2;
		break;
	case DRV_GPU_IR_LOG2:
		function = NVRTX_SM75_MUFU_LG2;
		break;
	case DRV_GPU_IR_SIN:
		function = NVRTX_SM75_MUFU_SIN;
		break;
	case DRV_GPU_IR_COS:
		function = NVRTX_SM75_MUFU_COS;
		break;
	default:
		break;
	}

	/* The conversions and the roundings; the special functions otherwise. */
	switch (instruction->op) {
	case DRV_GPU_IR_I2F:
		nvrtx_sm75_i2f(&word, dst, source, 1);
		break;
	case DRV_GPU_IR_U2F:
		nvrtx_sm75_i2f(&word, dst, source, 0);
		break;
	case DRV_GPU_IR_F2I:
		nvrtx_sm75_f2i(&word, dst, source, 1);
		break;
	case DRV_GPU_IR_F2U:
		nvrtx_sm75_f2i(&word, dst, source, 0);
		break;
	case DRV_GPU_IR_FLOOR:
		nvrtx_sm75_frnd(&word, dst, source, NVRTX_SM75_ROUND_DOWN);
		break;
	case DRV_GPU_IR_FTRUNC:
		nvrtx_sm75_frnd(&word, dst, source, NVRTX_SM75_ROUND_ZERO);
		break;
	case DRV_GPU_IR_FROUND_EVEN:
		nvrtx_sm75_frnd(&word, dst, source, NVRTX_SM75_ROUND_NEAREST);
		break;
	case DRV_GPU_IR_FRACT:
		nvrtx_sm75_frnd(&word, temporary, source, NVRTX_SM75_ROUND_DOWN);
		break;
	default:
		nvrtx_sm75_mufu(&word, dst, function, source);
		break;
	}

	error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_VARIABLE_WRITE);

	/* The fraction subtracts the rounded value. */
	if (error == 0 && instruction->op == DRV_GPU_IR_FRACT) {
		a = nvrtx_sm75_reg(source);
		b = nvrtx_sm75_reg(temporary);
		b.neg = 1;
		nvrtx_sm75_begin(&word);
		nvrtx_sm75_fadd(&word, dst, &a, &b);
		error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_FIXED);
	}

	/* The temporary goes back. */
	if (temporary != NVRTX_SM75_RZ)
		nvrtx_sm75_give(lowering, temporary);

	/* Reports an emission past the code's room. */
	if (error != 0)
		return error;

	/* Succeeded: the result is in dst. */
	return 0;
}

/*
 * Lowers a built-in value of the compute invocation: its place in its
 * group, the group's place in the grid, and its linear index in the group
 * (x + width * (y + height * z)).
 */
static int
nvrtx_sm75_lower_system(
	struct nvrtx_sm75_lowering *lowering,
	const struct drv_gpu_shader_ir_inst *instruction,
	uint32_t dst)
{
	struct nvrtx_sm75_word word;
	struct nvrtx_sm75_src a;
	struct nvrtx_sm75_src b;
	struct nvrtx_sm75_src c;
	uint32_t ids[3];
	uint32_t axis;
	uint32_t special;
	int error;

	/* The register of a single built-in. */
	special = 0U;
	switch (instruction->component) {
	case DRV_GPU_IR_SYSTEM_LOCAL_ID_X:
		special = NVRTX_SM75_SR_TID_X;
		break;
	case DRV_GPU_IR_SYSTEM_LOCAL_ID_Y:
		special = NVRTX_SM75_SR_TID_Y;
		break;
	case DRV_GPU_IR_SYSTEM_LOCAL_ID_Z:
		special = NVRTX_SM75_SR_TID_Z;
		break;
	case DRV_GPU_IR_SYSTEM_GROUP_ID_X:
		special = NVRTX_SM75_SR_CTAID_X;
		break;
	case DRV_GPU_IR_SYSTEM_GROUP_ID_Y:
		special = NVRTX_SM75_SR_CTAID_Y;
		break;
	case DRV_GPU_IR_SYSTEM_GROUP_ID_Z:
		special = NVRTX_SM75_SR_CTAID_Z;
		break;
	case DRV_GPU_IR_SYSTEM_LOCAL_INDEX:
		break;
	default:
		return nvrtx_sm75_refuse(lowering, ENOTSUP, "a built-in value a compute shader does not have");
	}

	/* A single built-in is one read of its special register. */
	if (special != 0U) {
		nvrtx_sm75_begin(&word);
		nvrtx_sm75_s2r(&word, dst, special);
		error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_VARIABLE_SPECIAL);
		if (error != 0)
			return error;
		return 0;
	}

	/* The linear index: the three local ids into temporaries. */
	for (axis = 0U; axis < 3U; axis++) {
		error = nvrtx_sm75_take(lowering, &ids[axis]);
		if (error != 0) {
			while (axis > 0U) {
				axis--;
				nvrtx_sm75_give(lowering, ids[axis]);
			}
			return error;
		}

		nvrtx_sm75_begin(&word);
		nvrtx_sm75_s2r(&word, ids[axis], NVRTX_SM75_SR_TID_X + axis);
		error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_VARIABLE_SPECIAL);
		if (error != 0)
			break;
	}

	/* z * height + y into the z temporary, then that * width + x into dst. */
	if (error == 0) {
		a = nvrtx_sm75_reg(ids[2]);
		b = nvrtx_sm75_imm(lowering->ir->local_size[1]);
		c = nvrtx_sm75_reg(ids[1]);
		nvrtx_sm75_begin(&word);
		nvrtx_sm75_imad(&word, ids[2], &a, &b, &c, 0);
		error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_FIXED);
	}
	if (error == 0) {
		a = nvrtx_sm75_reg(ids[2]);
		b = nvrtx_sm75_imm(lowering->ir->local_size[0]);
		c = nvrtx_sm75_reg(ids[0]);
		nvrtx_sm75_begin(&word);
		nvrtx_sm75_imad(&word, dst, &a, &b, &c, 0);
		error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_FIXED);
	}

	/* The temporaries go back. */
	for (axis = 0U; axis < 3U; axis++)
		nvrtx_sm75_give(lowering, ids[axis]);

	/* Reports an emission past the code's room. */
	if (error != 0)
		return error;

	/* Succeeded: the index is in dst. */
	return 0;
}

/*
 * Lowers a storage buffer access: the buffer's address from the root
 * constant buffer plus the byte offset, then a global load or store, under
 * P0 when the access is predicated; or the buffer's size.
 */
static int
nvrtx_sm75_lower_storage(
	struct nvrtx_sm75_lowering *lowering,
	const struct drv_gpu_shader_ir_inst *instruction,
	uint32_t dst)
{
	struct nvrtx_sm75_word word;
	struct nvrtx_sm75_src a;
	struct nvrtx_sm75_src zero;
	struct nvrtx_sm75_src size;
	uint32_t slot;
	uint32_t address;
	uint32_t guard;
	int error;

	/* The access names a storage buffer of the list. */
	if (instruction->location >= lowering->ir->uniform_count || lowering->ir->uniforms[instruction->location].kind != DRV_GPU_IR_UNIFORM_STORAGE)
		return nvrtx_sm75_refuse(lowering, EINVAL, "a storage access names no storage buffer");
	slot = lowering->slot[instruction->location];

	/* The size is a word of the buffer's entry. */
	if (instruction->op == DRV_GPU_IR_STORAGE_SIZE) {
		size = nvrtx_sm75_cb(NVRTX_SM75_CB_ROOT, NVRTX_SM75_CB_STORAGE + slot * NVRTX_SM75_CB_STORAGE_ENTRY + NVRTX_SM75_CB_STORAGE_SIZE);
		nvrtx_sm75_begin(&word);
		nvrtx_sm75_mov(&word, dst, &size);
		error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_FIXED);
		if (error != 0)
			return error;
		return 0;
	}

	/* The address of the word. */
	error = nvrtx_sm75_storage_address(lowering, slot, lowering->reg[instruction->src[0]], &address);
	if (error != 0)
		return error;

	/* P0 from the predicate of a predicated access. */
	guard = NVRTX_SM75_PT;
	if (instruction->component != 0U) {
		guard = instruction->src[1];
		if (instruction->op == DRV_GPU_IR_STORE_STORAGE)
			guard = instruction->src[2];
		a = nvrtx_sm75_value(lowering, guard);
		zero = nvrtx_sm75_reg(NVRTX_SM75_RZ);
		nvrtx_sm75_begin(&word);
		nvrtx_sm75_isetp(&word, NVRTX_SM75_P0, NVRTX_SM75_ICMP_NE, 0, &a, &zero);
		error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_FIXED_PREDICATE);
		guard = NVRTX_SM75_P0;
	}

	/* The load or the store, guarded. */
	if (error == 0) {
		nvrtx_sm75_begin(&word);
		if (instruction->op == DRV_GPU_IR_LOAD_STORAGE) {
			nvrtx_sm75_ldg(&word, dst, address);
		} else {
			nvrtx_sm75_stg(&word, address, lowering->reg[instruction->src[1]]);
		}
		nvrtx_sm75_guard(&word, guard, 0);

		/* A load writes a result; a store only reads its sources. */
		if (instruction->op == DRV_GPU_IR_LOAD_STORAGE) {
			error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_VARIABLE_WRITE);
		} else {
			error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_VARIABLE_STORE);
		}
	}

	/* The address pair goes back. */
	nvrtx_sm75_give(lowering, address);
	nvrtx_sm75_give(lowering, address + 1U);

	/* Reports an emission past the code's room. */
	if (error != 0)
		return error;

	/* Succeeded: the access is made. */
	return 0;
}

/*
 * Lowers a loop's ends.  The threads of a warp may leave a loop at
 * different passes, so the loop is a convergence region: LOOP_BEGIN is a
 * BSSY whose barrier collects the threads at the BSYNC after the loop, and
 * LOOP_END branches back to the loop's first instruction for the threads
 * whose Boolean holds; the others go on to the BSYNC and wait there.
 */
static int
nvrtx_sm75_lower_loop(
	struct nvrtx_sm75_lowering *lowering,
	const struct drv_gpu_shader_ir_inst *instruction)
{
	struct nvrtx_sm75_word word;
	uint32_t *slot;
	uint32_t depth;
	uint32_t bssy;
	int32_t back;
	int error;

	/* LOOP_BEGIN: the BSSY (its target is set at the loop's end), and the loop starts after it. */
	if (instruction->op == DRV_GPU_IR_LOOP_BEGIN) {
		depth = lowering->loop_depth;
		if (depth == NVRTX_SM75_LOOP_DEPTH)
			return nvrtx_sm75_refuse(lowering, ENOTSUP, "loops nested deeper than the convergence barriers");

		nvrtx_sm75_begin(&word);
		nvrtx_sm75_bssy(&word, depth, 0);
		lowering->loop_bssy[depth] = lowering->count;
		error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_CONTROL);
		if (error != 0)
			return error;

		lowering->loop_start[depth] = lowering->count;
		lowering->loop_depth++;
		return 0;
	}

	/* LOOP_END closes the innermost loop. */
	if (lowering->loop_depth == 0U)
		return nvrtx_sm75_refuse(lowering, EINVAL, "a loop's end without its beginning");
	lowering->loop_depth--;
	depth = lowering->loop_depth;

	/* P0: the thread runs the body again. */
	error = nvrtx_sm75_predicate(lowering, instruction->src[0]);
	if (error != 0)
		return error;

	/* Back to the loop's first instruction where P0 holds. */
	back = (int32_t)lowering->loop_start[depth] - (int32_t)(lowering->count + 1U);
	nvrtx_sm75_begin(&word);
	nvrtx_sm75_bra(&word, back);
	nvrtx_sm75_bra_if(&word, NVRTX_SM75_P0, 0);
	error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_CONTROL);
	if (error != 0)
		return error;

	/* The BSYNC the loop's threads meet at, and the BSSY's target made its address. */
	bssy = lowering->loop_bssy[depth];
	nvrtx_sm75_begin(&word);
	nvrtx_sm75_bsync(&word, depth);
	slot = lowering->binary->code + (size_t)bssy * NVRTX_SM75_INSTRUCTION_WORDS;
	error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_CONTROL);
	if (error != 0)
		return error;

	kern_memcpy(word.w, slot, sizeof(word.w));
	nvrtx_sm75_set(&word, NVRTX_SM75_BSSY_OFFSET_LOW, NVRTX_SM75_BSSY_OFFSET_BITS, (uint64_t)((int64_t)(lowering->count - 1U - (bssy + 1U)) * NVRTX_SM75_INSTRUCTION_WORDS));
	kern_memcpy(slot, word.w, sizeof(word.w));

	/* Succeeded: the loop is closed. */
	return 0;
}

/*
 * Lowers the integer division and remainder.  The unsigned ones use the
 * float reciprocal of the divisor scaled to 32-bit fixed point, refined
 * once, then a quotient corrected at most twice; the signed ones divide the
 * magnitudes and give the quotient the sign of the operands' product and
 * the remainder the dividend's sign (SPIR-V OpSDiv and OpSRem).
 */
static int
nvrtx_sm75_lower_divide(
	struct nvrtx_sm75_lowering *lowering,
	const struct drv_gpu_shader_ir_inst *instruction,
	uint32_t dst)
{
	struct nvrtx_sm75_word word;
	struct nvrtx_sm75_src a;
	struct nvrtx_sm75_src b;
	struct nvrtx_sm75_src zero;
	uint32_t numerator;
	uint32_t denominator;
	uint32_t magnitude_n;
	uint32_t magnitude_d;
	uint32_t other;
	uint32_t sign;
	int is_signed;
	int error;

	numerator = lowering->reg[instruction->src[0]];
	denominator = lowering->reg[instruction->src[1]];
	is_signed = 0;
	if (instruction->op == DRV_GPU_IR_IDIV || instruction->op == DRV_GPU_IR_IREM)
		is_signed = 1;

	/* The unsigned ones: the quotient or the remainder into dst, the other into a temporary. */
	if (is_signed == 0) {
		error = nvrtx_sm75_take(lowering, &other);
		if (error != 0)
			return error;
		if (instruction->op == DRV_GPU_IR_UDIV) {
			error = nvrtx_sm75_divide_unsigned(lowering, numerator, denominator, dst, other);
		} else {
			error = nvrtx_sm75_divide_unsigned(lowering, numerator, denominator, other, dst);
		}
		nvrtx_sm75_give(lowering, other);
		if (error != 0)
			return error;
		return 0;
	}

	/* The signed ones: the magnitudes first. */
	error = nvrtx_sm75_take(lowering, &magnitude_n);
	if (error != 0)
		return error;
	error = nvrtx_sm75_take(lowering, &magnitude_d);
	if (error != 0) {
		nvrtx_sm75_give(lowering, magnitude_n);
		return error;
	}
	error = nvrtx_sm75_take(lowering, &other);
	if (error != 0) {
		nvrtx_sm75_give(lowering, magnitude_n);
		nvrtx_sm75_give(lowering, magnitude_d);
		return error;
	}

	error = nvrtx_sm75_absolute(lowering, numerator, magnitude_n);
	if (error == 0)
		error = nvrtx_sm75_absolute(lowering, denominator, magnitude_d);

	/* The unsigned division of the magnitudes. */
	if (error == 0 && instruction->op == DRV_GPU_IR_IDIV)
		error = nvrtx_sm75_divide_unsigned(lowering, magnitude_n, magnitude_d, dst, other);
	if (error == 0 && instruction->op == DRV_GPU_IR_IREM)
		error = nvrtx_sm75_divide_unsigned(lowering, magnitude_n, magnitude_d, other, dst);

	/* P0: the result is negative (the operands' signs differ for the quotient, the dividend is negative for the remainder). */
	zero = nvrtx_sm75_reg(NVRTX_SM75_RZ);
	sign = numerator;
	if (error == 0 && instruction->op == DRV_GPU_IR_IDIV) {
		a = nvrtx_sm75_reg(numerator);
		b = nvrtx_sm75_reg(denominator);
		nvrtx_sm75_begin(&word);
		nvrtx_sm75_lop3(&word, other, &a, &b, &zero, NVRTX_SM75_LUT_XOR);
		error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_FIXED);
		sign = other;
	}
	if (error == 0) {
		a = nvrtx_sm75_reg(sign);
		nvrtx_sm75_begin(&word);
		nvrtx_sm75_isetp(&word, NVRTX_SM75_P0, NVRTX_SM75_ICMP_LT, 1, &a, &zero);
		error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_FIXED_PREDICATE);
	}

	/* The result negated where P0 holds. */
	if (error == 0)
		error = nvrtx_sm75_negate_if_p0(lowering, dst);

	/* The temporaries go back. */
	nvrtx_sm75_give(lowering, magnitude_n);
	nvrtx_sm75_give(lowering, magnitude_d);
	nvrtx_sm75_give(lowering, other);

	/* Reports an emission past the code's room. */
	if (error != 0)
		return error;

	/* Succeeded: the result is in dst. */
	return 0;
}

/*
 * Divides two unsigned 32-bit registers: the quotient and the remainder
 * into their registers (both written, neither a source):
 *
 *   r = f2u(rcp(u2f(d)) * (2^32 - 512))          about 2^32 / d, below it
 *   r = r + hi(r * (-d * r))                      one refinement
 *   q = hi(n * r);  m = n - q * d
 *   twice: if (m >= d) { q = q + 1; m = m - d; }
 */
static int
nvrtx_sm75_divide_unsigned(
	struct nvrtx_sm75_lowering *lowering,
	uint32_t numerator,
	uint32_t denominator,
	uint32_t quotient,
	uint32_t remainder)
{
	struct nvrtx_sm75_word word;
	struct nvrtx_sm75_src a;
	struct nvrtx_sm75_src b;
	struct nvrtx_sm75_src c;
	struct nvrtx_sm75_src zero;
	uint32_t reciprocal;
	uint32_t scratch;
	uint32_t pair;
	unsigned step;
	int error;

	zero = nvrtx_sm75_reg(NVRTX_SM75_RZ);

	/* A register for the reciprocal, one for the products, and a pair for the high halves. */
	error = nvrtx_sm75_take(lowering, &reciprocal);
	if (error != 0)
		return error;
	error = nvrtx_sm75_take(lowering, &scratch);
	if (error != 0) {
		nvrtx_sm75_give(lowering, reciprocal);
		return error;
	}
	error = nvrtx_sm75_take_pair(lowering, &pair);
	if (error != 0) {
		nvrtx_sm75_give(lowering, reciprocal);
		nvrtx_sm75_give(lowering, scratch);
		return error;
	}

	/* The float reciprocal of the divisor, scaled and made an integer. */
	nvrtx_sm75_begin(&word);
	nvrtx_sm75_i2f(&word, scratch, denominator, 0);
	error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_VARIABLE_WRITE);
	if (error == 0) {
		nvrtx_sm75_begin(&word);
		nvrtx_sm75_mufu(&word, scratch, NVRTX_SM75_MUFU_RCP, scratch);
		error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_VARIABLE_WRITE);
	}
	if (error == 0) {
		a = nvrtx_sm75_reg(scratch);
		b = nvrtx_sm75_imm(NVRTX_SM75_RECIPROCAL_SCALE);
		nvrtx_sm75_begin(&word);
		nvrtx_sm75_fmul(&word, scratch, &a, &b);
		error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_FIXED);
	}
	if (error == 0) {
		nvrtx_sm75_begin(&word);
		nvrtx_sm75_f2i(&word, reciprocal, scratch, 0);
		error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_VARIABLE_WRITE);
	}

	/* The refinement: -d * r, its product's high half with r, added to r. */
	if (error == 0) {
		a = nvrtx_sm75_reg(denominator);
		a.neg = 1;
		nvrtx_sm75_begin(&word);
		nvrtx_sm75_iadd3(&word, scratch, &a, &zero, &zero);
		error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_FIXED);
	}
	if (error == 0) {
		a = nvrtx_sm75_reg(scratch);
		b = nvrtx_sm75_reg(reciprocal);
		nvrtx_sm75_begin(&word);
		nvrtx_sm75_imad(&word, scratch, &a, &b, &zero, 0);
		error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_FIXED);
	}
	if (error == 0) {
		a = nvrtx_sm75_reg(reciprocal);
		b = nvrtx_sm75_reg(scratch);
		nvrtx_sm75_begin(&word);
		nvrtx_sm75_imad_wide(&word, pair, &a, &b, &zero);
		error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_FIXED);
	}
	if (error == 0) {
		a = nvrtx_sm75_reg(reciprocal);
		b = nvrtx_sm75_reg(pair + 1U);
		nvrtx_sm75_begin(&word);
		nvrtx_sm75_iadd3(&word, reciprocal, &a, &b, &zero);
		error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_FIXED);
	}

	/* The quotient: the high half of n * r. */
	if (error == 0) {
		a = nvrtx_sm75_reg(numerator);
		b = nvrtx_sm75_reg(reciprocal);
		nvrtx_sm75_begin(&word);
		nvrtx_sm75_imad_wide(&word, pair, &a, &b, &zero);
		error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_FIXED);
	}
	if (error == 0) {
		b = nvrtx_sm75_reg(pair + 1U);
		nvrtx_sm75_begin(&word);
		nvrtx_sm75_mov(&word, quotient, &b);
		error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_FIXED);
	}

	/* The remainder: n - q * d. */
	if (error == 0) {
		a = nvrtx_sm75_reg(quotient);
		b = nvrtx_sm75_reg(denominator);
		nvrtx_sm75_begin(&word);
		nvrtx_sm75_imad(&word, scratch, &a, &b, &zero, 0);
		error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_FIXED);
	}
	if (error == 0) {
		a = nvrtx_sm75_reg(numerator);
		b = nvrtx_sm75_reg(scratch);
		b.neg = 1;
		nvrtx_sm75_begin(&word);
		nvrtx_sm75_iadd3(&word, remainder, &a, &b, &zero);
		error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_FIXED);
	}

	/* The two corrections, each where the remainder is still the divisor or more. */
	for (step = 0U; error == 0 && step < 2U; step++) {
		a = nvrtx_sm75_reg(remainder);
		b = nvrtx_sm75_reg(denominator);
		nvrtx_sm75_begin(&word);
		nvrtx_sm75_isetp(&word, NVRTX_SM75_P0, NVRTX_SM75_ICMP_GE, 0, &a, &b);
		error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_FIXED_PREDICATE);
		if (error != 0)
			break;

		a = nvrtx_sm75_reg(quotient);
		c = nvrtx_sm75_imm(1U);
		nvrtx_sm75_begin(&word);
		nvrtx_sm75_iadd3(&word, quotient, &a, &c, &zero);
		nvrtx_sm75_guard(&word, NVRTX_SM75_P0, 0);
		error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_FIXED);
		if (error != 0)
			break;

		a = nvrtx_sm75_reg(remainder);
		b = nvrtx_sm75_reg(denominator);
		b.neg = 1;
		nvrtx_sm75_begin(&word);
		nvrtx_sm75_iadd3(&word, remainder, &a, &b, &zero);
		nvrtx_sm75_guard(&word, NVRTX_SM75_P0, 0);
		error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_FIXED);
	}

	/* The temporaries go back. */
	nvrtx_sm75_give(lowering, reciprocal);
	nvrtx_sm75_give(lowering, scratch);
	nvrtx_sm75_give(lowering, pair);
	nvrtx_sm75_give(lowering, pair + 1U);

	/* Reports an emission past the code's room. */
	if (error != 0)
		return error;

	/* Succeeded: the quotient and the remainder are in their registers. */
	return 0;
}

/* Writes the magnitude of a signed register into another: -value where it is negative. */
static int
nvrtx_sm75_absolute(
	struct nvrtx_sm75_lowering *lowering,
	uint32_t value,
	uint32_t result)
{
	struct nvrtx_sm75_word word;
	struct nvrtx_sm75_src a;
	struct nvrtx_sm75_src zero;
	int error;

	zero = nvrtx_sm75_reg(NVRTX_SM75_RZ);

	/* P0: the value is negative. */
	a = nvrtx_sm75_reg(value);
	nvrtx_sm75_begin(&word);
	nvrtx_sm75_isetp(&word, NVRTX_SM75_P0, NVRTX_SM75_ICMP_LT, 1, &a, &zero);
	error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_FIXED_PREDICATE);
	if (error != 0)
		return error;

	/* The value, then its negation where P0 holds. */
	nvrtx_sm75_begin(&word);
	nvrtx_sm75_mov(&word, result, &a);
	error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_FIXED);
	if (error != 0)
		return error;

	error = nvrtx_sm75_negate_if_p0(lowering, result);
	if (error != 0)
		return error;

	/* Succeeded: the magnitude is in result. */
	return 0;
}

/* Negates a register in place where P0 holds. */
static int
nvrtx_sm75_negate_if_p0(
	struct nvrtx_sm75_lowering *lowering,
	uint32_t value)
{
	struct nvrtx_sm75_word word;
	struct nvrtx_sm75_src a;
	struct nvrtx_sm75_src zero;
	int error;

	zero = nvrtx_sm75_reg(NVRTX_SM75_RZ);

	/* @P0 IADD3 value, -value, RZ, RZ. */
	a = nvrtx_sm75_reg(value);
	a.neg = 1;
	nvrtx_sm75_begin(&word);
	nvrtx_sm75_iadd3(&word, value, &a, &zero, &zero);
	nvrtx_sm75_guard(&word, NVRTX_SM75_P0, 0);
	error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_FIXED);
	if (error != 0)
		return error;

	/* Succeeded: the register is negated where P0 holds. */
	return 0;
}

/*
 * Lowers an atomic on a storage buffer word (its address from the table)
 * or on a shared memory word (its offset), returning the old value; a
 * subtraction adds the negated value, a compare and exchange writes src[1]
 * where the word equals src[2].  A predicated one runs under P0.
 */
static int
nvrtx_sm75_lower_atomic(
	struct nvrtx_sm75_lowering *lowering,
	const struct drv_gpu_shader_ir_inst *instruction,
	uint32_t dst)
{
	struct nvrtx_sm75_word word;
	struct nvrtx_sm75_src a;
	struct nvrtx_sm75_src zero;
	uint32_t address;
	uint32_t data;
	uint32_t negated;
	uint32_t operation;
	uint32_t predicate;
	int is_signed;
	int shared;
	int error;

	/* The operation and its type. */
	is_signed = 0;
	switch (instruction->immediate) {
	case DRV_GPU_IR_ATOMIC_ADD:
	case DRV_GPU_IR_ATOMIC_SUB:
		operation = NVRTX_SM75_ATOM_ADD;
		break;
	case DRV_GPU_IR_ATOMIC_AND:
		operation = NVRTX_SM75_ATOM_AND;
		break;
	case DRV_GPU_IR_ATOMIC_OR:
		operation = NVRTX_SM75_ATOM_OR;
		break;
	case DRV_GPU_IR_ATOMIC_XOR:
		operation = NVRTX_SM75_ATOM_XOR;
		break;
	case DRV_GPU_IR_ATOMIC_XCHG:
	case DRV_GPU_IR_ATOMIC_CMPXCHG:
		operation = NVRTX_SM75_ATOM_EXCH;
		break;
	case DRV_GPU_IR_ATOMIC_SMIN:
		operation = NVRTX_SM75_ATOM_MIN;
		is_signed = 1;
		break;
	case DRV_GPU_IR_ATOMIC_SMAX:
		operation = NVRTX_SM75_ATOM_MAX;
		is_signed = 1;
		break;
	case DRV_GPU_IR_ATOMIC_UMIN:
		operation = NVRTX_SM75_ATOM_MIN;
		break;
	case DRV_GPU_IR_ATOMIC_UMAX:
		operation = NVRTX_SM75_ATOM_MAX;
		break;
	default:
		return nvrtx_sm75_refuse(lowering, EINVAL, "an atomic operation the IR does not define");
	}

	/* Where the word is: shared memory by its offset, or a storage buffer's word by its address. */
	shared = 0;
	if (instruction->location == DRV_GPU_IR_LOCATION_SHARED)
		shared = 1;
	address = lowering->reg[instruction->src[0]];
	if (shared == 0) {
		if (instruction->location >= lowering->ir->uniform_count || lowering->ir->uniforms[instruction->location].kind != DRV_GPU_IR_UNIFORM_STORAGE)
			return nvrtx_sm75_refuse(lowering, EINVAL, "an atomic names no storage buffer");
		error = nvrtx_sm75_storage_address(lowering, lowering->slot[instruction->location], address, &address);
		if (error != 0)
			return error;
	}

	/* A subtraction adds the negated value. */
	data = lowering->reg[instruction->src[1]];
	negated = NVRTX_SM75_RZ;
	error = 0;
	if (instruction->immediate == DRV_GPU_IR_ATOMIC_SUB) {
		error = nvrtx_sm75_take(lowering, &negated);
		if (error == 0) {
			a = nvrtx_sm75_reg(data);
			a.neg = 1;
			zero = nvrtx_sm75_reg(NVRTX_SM75_RZ);
			nvrtx_sm75_begin(&word);
			nvrtx_sm75_iadd3(&word, negated, &a, &zero, &zero);
			error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_FIXED);
			data = negated;
		}
	}

	/* P0 from the predicate, which follows the values. */
	if (error == 0 && instruction->component == DRV_GPU_IR_ATOMIC_PREDICATED) {
		predicate = instruction->src[2];
		if (instruction->immediate == DRV_GPU_IR_ATOMIC_CMPXCHG)
			predicate = instruction->src[3];
		error = nvrtx_sm75_predicate(lowering, predicate);
	}

	/* The atomic. */
	if (error == 0) {
		nvrtx_sm75_begin(&word);
		if (instruction->immediate == DRV_GPU_IR_ATOMIC_CMPXCHG) {
			nvrtx_sm75_atomic_cas(&word, shared, dst, address, lowering->reg[instruction->src[2]], data);
		} else {
			nvrtx_sm75_atomic(&word, shared, dst, address, data, operation, is_signed);
		}
		if (instruction->component == DRV_GPU_IR_ATOMIC_PREDICATED)
			nvrtx_sm75_guard(&word, NVRTX_SM75_P0, 0);
		error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_VARIABLE_WRITE);
	}

	/* The temporaries go back. */
	if (negated != NVRTX_SM75_RZ)
		nvrtx_sm75_give(lowering, negated);
	if (shared == 0) {
		nvrtx_sm75_give(lowering, address);
		nvrtx_sm75_give(lowering, address + 1U);
	}

	/* Reports an emission past the code's room. */
	if (error != 0)
		return error;

	/* Succeeded: the old value is in dst. */
	return 0;
}

/* Lowers a load or a store of a shared memory word, under P0 when predicated. */
static int
nvrtx_sm75_lower_shared(
	struct nvrtx_sm75_lowering *lowering,
	const struct drv_gpu_shader_ir_inst *instruction,
	uint32_t dst)
{
	struct nvrtx_sm75_word word;
	uint32_t predicate;
	uint32_t offset;
	int error;

	offset = lowering->reg[instruction->src[0]];

	/* P0 from the predicate of a predicated access. */
	if (instruction->component != 0U) {
		predicate = instruction->src[1];
		if (instruction->op == DRV_GPU_IR_STORE_SHARED)
			predicate = instruction->src[2];
		error = nvrtx_sm75_predicate(lowering, predicate);
		if (error != 0)
			return error;
	}

	/* The load or the store. */
	nvrtx_sm75_begin(&word);
	if (instruction->op == DRV_GPU_IR_LOAD_SHARED) {
		nvrtx_sm75_lds(&word, dst, offset);
	} else {
		nvrtx_sm75_sts(&word, offset, lowering->reg[instruction->src[1]]);
	}
	if (instruction->component != 0U)
		nvrtx_sm75_guard(&word, NVRTX_SM75_P0, 0);

	/* A load writes a result; a store only reads its sources. */
	if (instruction->op == DRV_GPU_IR_LOAD_SHARED) {
		error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_VARIABLE_WRITE);
	} else {
		error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_VARIABLE_STORE);
	}
	if (error != 0)
		return error;

	/* Succeeded: the access is made. */
	return 0;
}

/*
 * Lowers a workgroup barrier (after the memory barrier of global memory it
 * names; BAR.SYNC itself orders the workgroup's shared memory) and a memory
 * fence (MEMBAR at the GPU's scope for global memory, the workgroup's
 * otherwise).  Each is followed by a NOP of two cycles, as the CUDA
 * compiler does after an instruction with an execution latency.
 */
static int
nvrtx_sm75_lower_sync(
	struct nvrtx_sm75_lowering *lowering,
	const struct drv_gpu_shader_ir_inst *instruction)
{
	struct nvrtx_sm75_word word;
	uint32_t scope;
	int membar;
	int error;

	/* Whether a MEMBAR comes, and its scope. */
	scope = NVRTX_SM75_MEMBAR_CTA;
	if ((instruction->immediate & DRV_GPU_IR_FENCE_GLOBAL) != 0U)
		scope = NVRTX_SM75_MEMBAR_GPU;
	membar = 1;
	if (instruction->op == DRV_GPU_IR_BARRIER && scope != NVRTX_SM75_MEMBAR_GPU)
		membar = 0;

	/* The MEMBAR and its NOP. */
	if (membar != 0) {
		nvrtx_sm75_begin(&word);
		nvrtx_sm75_membar(&word, scope);
		error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_SYNC);
		if (error != 0)
			return error;

		nvrtx_sm75_begin(&word);
		nvrtx_sm75_nop(&word);
		error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_FIXED);
		if (error != 0)
			return error;
	}

	/* A fence is only the MEMBAR. */
	if (instruction->op == DRV_GPU_IR_FENCE)
		return 0;

	/* The barrier and its NOP. */
	nvrtx_sm75_begin(&word);
	nvrtx_sm75_bar(&word);
	error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_SYNC);
	if (error != 0)
		return error;

	nvrtx_sm75_begin(&word);
	nvrtx_sm75_nop(&word);
	error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_FIXED);
	if (error != 0)
		return error;

	/* Succeeded: the workgroup has met. */
	return 0;
}

/* Sets P0 from a Boolean value: it is not zero. */
static int
nvrtx_sm75_predicate(
	struct nvrtx_sm75_lowering *lowering,
	uint32_t value)
{
	struct nvrtx_sm75_word word;
	struct nvrtx_sm75_src a;
	struct nvrtx_sm75_src zero;
	int error;

	a = nvrtx_sm75_value(lowering, value);
	zero = nvrtx_sm75_reg(NVRTX_SM75_RZ);

	/* ISETP.NE.U32.AND P0, PT, value, RZ, PT. */
	nvrtx_sm75_begin(&word);
	nvrtx_sm75_isetp(&word, NVRTX_SM75_P0, NVRTX_SM75_ICMP_NE, 0, &a, &zero);
	error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_FIXED_PREDICATE);
	if (error != 0)
		return error;

	/* Succeeded: P0 holds the Boolean. */
	return 0;
}

/*
 * Makes every value a loop reads but that was made before the loop live
 * to the loop's end, since the next pass reads it again.  Checks that the
 * loops are nested properly.
 */
static int
nvrtx_sm75_extend_loops(
	struct nvrtx_sm75_lowering *lowering)
{
	const struct drv_gpu_shader_ir *ir;
	const struct drv_gpu_shader_ir_inst *instruction;
	uint32_t *first_def;
	uint32_t begins[NVRTX_SM75_LOOP_DEPTH];
	uint32_t sources[4];
	uint32_t depth;
	uint32_t begin;
	uint32_t index;
	uint32_t inner;
	uint32_t source;
	int defines;
	int count;

	ir = lowering->ir;

	/* Each value's first definition. */
	first_def = kern_calloc(ir->value_count + 1U, sizeof(*first_def));
	if (first_def == NULL)
		return ENOMEM;
	for (index = 0U; index <= ir->value_count; index++)
		first_def[index] = NVRTX_SM75_NOT_DEFINED;
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
		if (instruction->op == DRV_GPU_IR_LOOP_BEGIN) {
			if (depth == NVRTX_SM75_LOOP_DEPTH) {
				kern_free(first_def);
				lowering->index = index;
				return nvrtx_sm75_refuse(lowering, ENOTSUP, "loops nested deeper than the convergence barriers");
			}
			begins[depth] = index;
			depth++;
			continue;
		}
		if (instruction->op != DRV_GPU_IR_LOOP_END)
			continue;
		if (depth == 0U) {
			kern_free(first_def);
			lowering->index = index;
			return nvrtx_sm75_refuse(lowering, EINVAL, "a loop's end without its beginning");
		}
		depth--;
		begin = begins[depth];

		for (inner = begin + 1U; inner <= index; inner++) {
			count = nvrtx_sm75_sources(&ir->instructions[inner], sources);
			for (source = 0U; count > 0 && source < (uint32_t)count; source++) {
				if (first_def[sources[source]] >= begin)
					continue;
				if (lowering->last_use[sources[source]] < index + 1U)
					lowering->last_use[sources[source]] = index + 1U;
			}
		}
	}
	kern_free(first_def);

	/* Every loop must end. */
	if (depth != 0U)
		return nvrtx_sm75_refuse(lowering, EINVAL, "a loop without its end");

	/* Succeeded: the values live through their loops. */
	return 0;
}

/*
 * Computes a storage word's address into a new register pair: the
 * buffer's 64-bit address (LDC.64 from the table) plus the 32-bit byte
 * offset (IMAD.WIDE.U32 offset * 1 + address).
 */
static int
nvrtx_sm75_storage_address(
	struct nvrtx_sm75_lowering *lowering,
	uint32_t slot,
	uint32_t offset,
	uint32_t *address)
{
	struct nvrtx_sm75_word word;
	struct nvrtx_sm75_src a;
	struct nvrtx_sm75_src one;
	struct nvrtx_sm75_src base_pair;
	uint32_t base;
	int error;

	/* The pair the buffer's address is loaded into. */
	error = nvrtx_sm75_take_pair(lowering, &base);
	if (error != 0)
		return error;

	/* The pair the word's address goes into. */
	error = nvrtx_sm75_take_pair(lowering, address);
	if (error != 0) {
		nvrtx_sm75_give(lowering, base);
		nvrtx_sm75_give(lowering, base + 1U);
		return error;
	}

	/* The buffer's address from its entry. */
	nvrtx_sm75_begin(&word);
	nvrtx_sm75_ldc(&word, base, NVRTX_SM75_CB_ROOT, NVRTX_SM75_CB_STORAGE + slot * NVRTX_SM75_CB_STORAGE_ENTRY, 1);
	error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_VARIABLE_WRITE);

	/* The address plus the offset. */
	if (error == 0) {
		a = nvrtx_sm75_reg(offset);
		one = nvrtx_sm75_imm(1U);
		base_pair = nvrtx_sm75_reg(base);
		nvrtx_sm75_begin(&word);
		nvrtx_sm75_imad_wide(&word, *address, &a, &one, &base_pair);
		error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_FIXED);
	}

	/* The buffer's address is no longer needed. */
	nvrtx_sm75_give(lowering, base);
	nvrtx_sm75_give(lowering, base + 1U);

	/* An emission past the code's room gives the word's pair back too. */
	if (error != 0) {
		nvrtx_sm75_give(lowering, *address);
		nvrtx_sm75_give(lowering, *address + 1U);
		return error;
	}

	/* Succeeded: the pair holds the word's address. */
	return 0;
}

/*
 * Appends an instruction with its serial dependences: it waits for what
 * the instruction before left pending, and a variable-latency one leaves
 * its own scoreboards pending for the next.
 */
static int
nvrtx_sm75_emit(
	struct nvrtx_sm75_lowering *lowering,
	struct nvrtx_sm75_word *word,
	int kind)
{
	uint32_t *slot;
	unsigned write_bar;
	unsigned read_bar;
	unsigned delay;
	unsigned left;
	unsigned wait;
	int yield;

	/* The code has room for every expansion; running out is a bug of the lowering. */
	if (lowering->count >= lowering->capacity)
		return nvrtx_sm75_refuse(lowering, EINVAL, "the code outgrew its room");

	/* The control by the kind. */
	write_bar = NVRTX_SM75_BAR_NONE;
	read_bar = NVRTX_SM75_BAR_NONE;
	delay = NVRTX_SM75_DELAY_GPR;
	left = 0U;
	wait = 0U;
	yield = 0;
	if (kind == NVRTX_SM75_FIXED_PREDICATE) {
		delay = NVRTX_SM75_DELAY_PREDICATE;
	} else if (kind == NVRTX_SM75_VARIABLE_WRITE) {
		/* The result and the sources each release a scoreboard. */
		write_bar = NVRTX_SM75_BAR_RESULT;
		read_bar = NVRTX_SM75_BAR_SOURCES;
		delay = NVRTX_SM75_DELAY_VARIABLE;
		left = (1U << NVRTX_SM75_BAR_RESULT) | (1U << NVRTX_SM75_BAR_SOURCES);
	} else if (kind == NVRTX_SM75_VARIABLE_STORE) {
		/* Only the sources are read later. */
		read_bar = NVRTX_SM75_BAR_SOURCES;
		delay = NVRTX_SM75_DELAY_VARIABLE;
		left = 1U << NVRTX_SM75_BAR_SOURCES;
	} else if (kind == NVRTX_SM75_VARIABLE_SPECIAL) {
		/* Only the result is written later. */
		write_bar = NVRTX_SM75_BAR_RESULT;
		delay = NVRTX_SM75_DELAY_VARIABLE;
		left = 1U << NVRTX_SM75_BAR_RESULT;
	} else if (kind == NVRTX_SM75_CONTROL || kind == NVRTX_SM75_SYNC) {
		/* A branch, a convergence barrier or a barrier waits for everything and yields. */
		wait = NVRTX_SM75_WAIT_ALL;
		yield = 1;
		if (kind == NVRTX_SM75_SYNC)
			delay = NVRTX_SM75_DELAY_SYNC;
	}

	nvrtx_sm75_control(word, delay, yield, write_bar, read_bar, lowering->pending | wait);
	lowering->pending = left;

	/* The four words into the code. */
	slot = lowering->binary->code + (size_t)lowering->count * NVRTX_SM75_INSTRUCTION_WORDS;
	slot[0] = word->w[0];
	slot[1] = word->w[1];
	slot[2] = word->w[2];
	slot[3] = word->w[3];
	lowering->count++;

	/* Succeeded: the instruction is in the code. */
	return 0;
}

/*
 * Ends the program: EXIT after every scoreboard, a branch to itself (the
 * fetch ahead never runs past the end), and NOPs to a whole 128-byte line.
 * Records the code's size and the register count.
 */
static int
nvrtx_sm75_finish(
	struct nvrtx_sm75_lowering *lowering)
{
	struct nvrtx_sm75_word word;
	uint32_t *slot;
	int error;

	/* EXIT. */
	nvrtx_sm75_begin(&word);
	nvrtx_sm75_exit(&word);
	error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_FIXED);
	if (error != 0)
		return error;

	/* EXIT waits for everything and yields, as the CUDA compiler writes it. */
	slot = lowering->binary->code + (size_t)(lowering->count - 1U) * NVRTX_SM75_INSTRUCTION_WORDS;
	word.w[0] = slot[0];
	word.w[1] = slot[1];
	word.w[2] = slot[2];
	word.w[3] = slot[3];
	nvrtx_sm75_control(&word, NVRTX_SM75_DELAY_EXIT, 1, NVRTX_SM75_BAR_NONE, NVRTX_SM75_BAR_NONE, NVRTX_SM75_WAIT_ALL);
	slot[2] = word.w[2];
	slot[3] = word.w[3];
	lowering->pending = 0U;

	/* The branch to itself. */
	nvrtx_sm75_begin(&word);
	nvrtx_sm75_bra(&word, -1);
	error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_FIXED);
	if (error != 0)
		return error;

	/* The padding. */
	while ((lowering->count % NVRTX_SM75_LINE_INSTRUCTIONS) != 0U) {
		nvrtx_sm75_begin(&word);
		nvrtx_sm75_nop(&word);
		error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_FIXED);
		if (error != 0)
			return error;
	}

	/* The size and the registers (at least four, the hardware's two included). */
	lowering->binary->code_words = lowering->count * NVRTX_SM75_INSTRUCTION_WORDS;
	lowering->binary->gpr_count = lowering->highest + NVRTX_SM75_GPR_RESERVED;
	if (lowering->binary->gpr_count < NVRTX_SM75_GPR_MINIMUM)
		lowering->binary->gpr_count = NVRTX_SM75_GPR_MINIMUM;

	/* Succeeded: the program is complete. */
	return 0;
}

/* Takes the lowest free register. */
static int
nvrtx_sm75_take(
	struct nvrtx_sm75_lowering *lowering,
	uint32_t *reg)
{
	uint32_t index;

	/* The first free one. */
	for (index = 0U; index < NVRTX_SM75_GPR_USABLE; index++) {
		if (lowering->taken[index] != 0U)
			continue;

		/* It is taken from here on. */
		lowering->taken[index] = 1U;
		if (index + 1U > lowering->highest)
			lowering->highest = index + 1U;
		*reg = index;
		return 0;
	}

	/* Succeeded in finding none: stage A does not spill. */
	return nvrtx_sm75_refuse(lowering, ENOTSUP, "the program needs more registers than there are (no spilling yet)");
}

/* Takes the lowest free even register whose odd neighbour is free too. */
static int
nvrtx_sm75_take_pair(
	struct nvrtx_sm75_lowering *lowering,
	uint32_t *reg)
{
	uint32_t index;

	/* The first free pair. */
	for (index = 0U; index + 1U < NVRTX_SM75_GPR_USABLE; index += 2U) {
		if (lowering->taken[index] != 0U)
			continue;
		if (lowering->taken[index + 1U] != 0U)
			continue;

		/* Both are taken from here on. */
		lowering->taken[index] = 1U;
		lowering->taken[index + 1U] = 1U;
		if (index + 2U > lowering->highest)
			lowering->highest = index + 2U;
		*reg = index;
		return 0;
	}

	/* Succeeded in finding none: stage A does not spill. */
	return nvrtx_sm75_refuse(lowering, ENOTSUP, "the program needs more registers than there are (no spilling yet)");
}

/* Gives a register back. */
static void
nvrtx_sm75_give(
	struct nvrtx_sm75_lowering *lowering,
	uint32_t reg)
{
	/* RZ and out-of-range numbers are never taken. */
	if (reg >= NVRTX_SM75_GPR_USABLE)
		return;

	/* Free from here on. */
	lowering->taken[reg] = 0U;
}

/*
 * Gives back the registers of the values an instruction read for the last
 * time, and of its result when nothing reads it.
 */
static void
nvrtx_sm75_retire(
	struct nvrtx_sm75_lowering *lowering,
	const struct drv_gpu_shader_ir_inst *instruction)
{
	uint32_t sources[4];
	uint32_t value;
	uint32_t source;
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

	nvrtx_sm75_give(lowering, lowering->reg[value]);
	lowering->reg[value] = NVRTX_SM75_NO_REG;
}

/* Reports a value's register as a source operand. */
static struct nvrtx_sm75_src
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
