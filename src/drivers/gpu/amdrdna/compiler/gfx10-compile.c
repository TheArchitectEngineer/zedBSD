/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The amdrdna shader compiler's lowering (gfx10-compile.h): from the
 * shared scalar IR to a list of GFX10 instructions, which gfx10-wait.c
 * then gives its waits and writes as code.
 *
 * Every IR value lives in a VGPR, one 32-bit lane per invocation of the
 * wave.  A value gets the lowest free VGPR when it is defined and gives it
 * back after the last instruction that reads it; a value read inside a
 * loop that was made before the loop lives to the loop's end, and a loop
 * variable keeps its VGPR through every MOVE into it.  A Boolean is a VGPR
 * of all ones or zero: a comparison writes the lane mask VCC_LO and a
 * selection turns it into the Boolean at once; a selection, a loop's end
 * and a guarded access turn the Boolean back into VCC_LO.
 *
 * What is the same for every lane (push constants, uniform buffer words,
 * descriptors, the workgroup id) is read into SGPRs.  The descriptors are
 * loaded once at the start; a uniform word is loaded into one of a few
 * scalar temporaries and copied into the value's VGPR.
 *
 * A loop narrows the lane mask: each pass ends by dropping the lanes whose
 * condition is false and branching back while any lane is left; the mask
 * saved at the loop's start comes back after it.  Lanes that left early
 * keep the values they had, which is what the IR's loop variables say.
 */

#include "gfx10-compile.h"

#include "gfx10-encode.h"
#include "gfx10-wait.h"

#include "drivers/gpu/compiler/optimize.h"
#include "drivers/gpu/compiler/spirv.h"

#include <kern/kcrt.h>
#include <kern/kmem.h>
#include <uapi/errno.h>

#include <stddef.h>
#include <stdint.h>

/* A value without a VGPR. */
#define GFX10_NO_REG			0xffffU

/* A value not yet defined, and an IR instruction that reads no value. */
#define GFX10_NOT_DEFINED		0xffffffffU

/* The most values one IR instruction reads. */
#define GFX10_MAX_SOURCES		DRV_GPU_IR_TEXTURE_MAX_PARAMS

/* The deepest loop nest. */
#define GFX10_LOOP_DEPTH		16U

/* The scalar temporaries uniform words are loaded into, in turn. */
#define GFX10_SCALAR_TEMPS		8U

/* The instructions the list grows by when it is full. */
#define GFX10_LIST_GROWTH		256U

/* What a uniform of the IR maps to: no slot. */
#define GFX10_NO_SLOT			0xffffffffU

/*
 * The bits of a float's sign, and of everything but the sign; all ones,
 * the Boolean true.
 */
#define GFX10_SIGN_BIT			0x80000000U
#define GFX10_MAGNITUDE_BITS		0x7fffffffU
#define GFX10_TRUE			0xffffffffU

/*
 * 2^32 less 512 as a float (4294966784.0): the reciprocal of a divisor
 * scaled by it and truncated is a 32-bit fixed-point reciprocal a little
 * below the true one.
 *
 * Value referenced: Mesa 25.0.7 src/compiler/nir/nir_lower_idiv.c
 * (emit_udiv, the scale 4294966784.0).
 */
#define GFX10_RECIPROCAL_SCALE		0x4f7ffffeU

/* The first SGPR the compiler hands out for itself (after the user and the system SGPRs), a multiple of four. */
#define GFX10_SGPR_FIRST_FREE		8U

/* The VGPRs a compute program's local invocation id x, y and z arrive in. */
#define GFX10_CS_LOCAL_ID_X		0U
#define GFX10_CS_LOCAL_ID_Y		1U
#define GFX10_CS_LOCAL_ID_Z		2U
#define GFX10_CS_INPUT_VGPRS		3U

/* The SGPR a compute program's workgroup id x arrives in; y and z follow. */
#define GFX10_CS_GROUP_ID_X		2U

/*
 * The state of one compilation.
 *
 * Lives on the stack of amdrdna_gfx10_compile_ir(); its arrays are
 * allocated there and freed before it returns.
 */
struct gfx10_lowering {
	/* The program, the result being built, and where a refusal is described. */
	const struct drv_gpu_shader_ir *ir;
	struct amdrdna_gfx10_program *program;
	struct amdrdna_gfx10_diagnostic *diagnostic;

	/* Each value's VGPR (GFX10_NO_REG while it has none), and the IR instruction that last reads it, plus one (0: none). */
	uint16_t *vgpr;
	uint32_t *last_use;

	/* Which VGPRs are taken, the first one the allocator hands out, and one past the highest ever taken. */
	uint8_t taken[AMDRDNA_GFX10_VGPR_COUNT];
	uint32_t vgpr_first;
	uint32_t vgpr_highest;

	/* Each uniform's slot: its storage, uniform buffer or texture number; GFX10_NO_SLOT for none. */
	uint32_t *slot;

	/* The first SGPR of each storage buffer's and each uniform buffer's descriptor. */
	uint32_t storage_sgpr[AMDRDNA_GFX10_STORAGE_MAX];
	uint32_t uniform_sgpr[AMDRDNA_GFX10_UNIFORM_MAX];

	/* The SGPR each loop depth saves the lane mask in, the one a guarded access saves it in, and the scalar temporaries. */
	uint32_t loop_save[GFX10_LOOP_DEPTH];
	uint32_t guard_save;
	uint32_t scalar_temp[GFX10_SCALAR_TEMPS];
	uint32_t scalar_temp_next;

	/* One past the highest SGPR the program uses. */
	uint32_t sgpr_highest;

	/* The instruction list: entries, room, and the next label number. */
	struct amdrdna_gfx10_inst *list;
	uint32_t count;
	uint32_t capacity;
	uint32_t next_label;

	/* Nonzero when the list could not grow. */
	int out_of_memory;

	/* The loops being lowered: each one's label at its start, innermost last. */
	uint32_t loop_label[GFX10_LOOP_DEPTH];
	uint32_t loop_depth;

	/* The IR instruction being lowered. */
	uint32_t index;
};

static int refuse(struct gfx10_lowering *lowering, int error, const char *reason);
static int ir_sources(const struct drv_gpu_shader_ir_inst *instruction, uint32_t *sources);
static int ir_defines(const struct drv_gpu_shader_ir_inst *instruction);
static int prepare(struct gfx10_lowering *lowering);
static int extend_loops(struct gfx10_lowering *lowering);
static int map_uniforms(struct gfx10_lowering *lowering);
static int plan_registers(struct gfx10_lowering *lowering);
static int prologue(struct gfx10_lowering *lowering);
static int lower(struct gfx10_lowering *lowering, const struct drv_gpu_shader_ir_inst *instruction, uint32_t dst);
static int lower_float(struct gfx10_lowering *lowering, const struct drv_gpu_shader_ir_inst *instruction, uint32_t dst);
static int lower_integer(struct gfx10_lowering *lowering, const struct drv_gpu_shader_ir_inst *instruction, uint32_t dst);
static int lower_compare(struct gfx10_lowering *lowering, const struct drv_gpu_shader_ir_inst *instruction, uint32_t dst);
static int lower_uniform(struct gfx10_lowering *lowering, const struct drv_gpu_shader_ir_inst *instruction, uint32_t dst);
static int lower_system(struct gfx10_lowering *lowering, const struct drv_gpu_shader_ir_inst *instruction, uint32_t dst);
static int lower_storage(struct gfx10_lowering *lowering, const struct drv_gpu_shader_ir_inst *instruction, uint32_t dst);
static int lower_loop(struct gfx10_lowering *lowering, const struct drv_gpu_shader_ir_inst *instruction);
static int lower_divide(struct gfx10_lowering *lowering, const struct drv_gpu_shader_ir_inst *instruction, uint32_t dst);
static void divide_unsigned(struct gfx10_lowering *lowering, unsigned numerator, unsigned denominator, uint32_t quotient, uint32_t remainder, uint32_t temp);
static int lower_atomic(struct gfx10_lowering *lowering, const struct drv_gpu_shader_ir_inst *instruction, uint32_t dst);
static int lower_shared(struct gfx10_lowering *lowering, const struct drv_gpu_shader_ir_inst *instruction, uint32_t dst);
static void lower_sync(struct gfx10_lowering *lowering, const struct drv_gpu_shader_ir_inst *instruction);
static void emit_waits(struct gfx10_lowering *lowering, uint32_t fences);
static int finish(struct gfx10_lowering *lowering);
static void guard_begin(struct gfx10_lowering *lowering, uint32_t predicate);
static void guard_end(struct gfx10_lowering *lowering);
static void condition_from_boolean(struct gfx10_lowering *lowering, uint32_t value);
static void boolean_from_condition(struct gfx10_lowering *lowering, uint32_t dst);
static void emit(struct gfx10_lowering *lowering, const struct amdrdna_gfx10_inst *inst);
static void emit_vector(struct gfx10_lowering *lowering, enum amdrdna_gfx10_kind kind, unsigned opcode, unsigned dst, unsigned src0, unsigned src1, unsigned src2);
static void emit_vector_literal(struct gfx10_lowering *lowering, enum amdrdna_gfx10_kind kind, unsigned opcode, unsigned dst, unsigned src0, unsigned src1, uint32_t literal);
static void emit_multiply_add(struct gfx10_lowering *lowering, unsigned dst, unsigned factor, uint32_t constant, unsigned addend);
static void emit_scalar(struct gfx10_lowering *lowering, enum amdrdna_gfx10_kind kind, unsigned opcode, unsigned dst, unsigned src0, unsigned src1);
static void emit_scalar_load(struct gfx10_lowering *lowering, unsigned opcode, unsigned dst, unsigned base, uint32_t offset, unsigned count);
static void emit_label(struct gfx10_lowering *lowering, uint32_t label);
static void emit_branch(struct gfx10_lowering *lowering, unsigned opcode, uint32_t label);
static unsigned constant_operand(uint32_t bits);
static unsigned value_operand(const struct gfx10_lowering *lowering, uint32_t value);
static unsigned next_scalar_temp(struct gfx10_lowering *lowering);
static int take_vgpr(struct gfx10_lowering *lowering, uint32_t *reg);
static int take_vgpr_pair(struct gfx10_lowering *lowering, uint32_t *reg);
static void give_vgpr(struct gfx10_lowering *lowering, uint32_t reg);
static void retire(struct gfx10_lowering *lowering, const struct drv_gpu_shader_ir_inst *instruction);

/*
 * Compiles a SPIR-V module's stage.
 *
 * The module is parsed by the shared frontend, optimized, and lowered; a
 * program the lowering refuses after the optimizer rewrote it is lowered
 * again from the parsed one, which keeps the parsed program's outcome.
 * Returns 0 with the program, or the frontend's or the lowering's error
 * with the diagnostic filled.
 */
int
amdrdna_gfx10_compile(
	const uint32_t *words,
	size_t word_count,
	enum drv_gpu_shader_stage stage,
	struct amdrdna_gfx10_program **program,
	struct amdrdna_gfx10_diagnostic *diagnostic)
{
	struct drv_gpu_shader_ir *ir;
	struct drv_gpu_shader_ir optimized;
	struct drv_gpu_shader_optimize_options options;
	struct drv_gpu_compile_diagnostic parse_diagnostic;
	int error;

	/* A refused compile never leaves an old output pointer live. */
	if (program == NULL)
		return EINVAL;
	*program = NULL;

	/* Starts with no refusal. */
	if (diagnostic != NULL)
		kern_memset(diagnostic, 0, sizeof(*diagnostic));

	/* A module without words is refused. */
	if (words == NULL)
		return EINVAL;

	/* Parses the stage. */
	ir = NULL;
	parse_diagnostic.word_offset = 0U;
	parse_diagnostic.reason = NULL;
	error = drv_gpu_shader_parse(words, word_count, stage, &ir, &parse_diagnostic);
	if (error != 0) {
		if (diagnostic != NULL) {
			diagnostic->instruction = parse_diagnostic.word_offset;
			diagnostic->reason = parse_diagnostic.reason;
		}
		return error;
	}

	/* Optimizes a private copy: every block runs (nothing is skipped), and the known bits fold. */
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
		error = amdrdna_gfx10_compile_ir(&optimized, program, diagnostic);
	} else {
		error = amdrdna_gfx10_compile_ir(ir, program, diagnostic);
	}

	/* A rewritten program refused for any reason but memory is lowered from the parsed one. */
	if (optimized.instructions != NULL && error != 0 && error != ENOMEM) {
		if (diagnostic != NULL)
			kern_memset(diagnostic, 0, sizeof(*diagnostic));
		error = amdrdna_gfx10_compile_ir(ir, program, diagnostic);
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
 * Compiles a parsed program.
 *
 * Returns 0 with the program in *program, EINVAL for a malformed program,
 * ENOTSUP for one the compiler does not cover (both with the diagnostic),
 * or ENOMEM.
 */
int
amdrdna_gfx10_compile_ir(
	const struct drv_gpu_shader_ir *ir,
	struct amdrdna_gfx10_program **program,
	struct amdrdna_gfx10_diagnostic *diagnostic)
{
	struct gfx10_lowering lowering;
	const struct drv_gpu_shader_ir_inst *instruction;
	uint32_t dst;
	uint32_t index;
	int defines;
	int error;

	/* A refused compile never leaves an old output pointer live. */
	if (program == NULL || ir == NULL)
		return EINVAL;
	*program = NULL;

	/* The state, empty. */
	kern_memset(&lowering, 0, sizeof(lowering));
	lowering.ir = ir;
	lowering.diagnostic = diagnostic;

	/* The result. */
	lowering.program = kern_calloc(1U, sizeof(*lowering.program));
	if (lowering.program == NULL)
		return ENOMEM;
	lowering.program->stage = ir->stage;

	/* Checks the program, finds the last reads, maps the uniforms and places the registers. */
	error = prepare(&lowering);

	/* Loads what every lane shares. */
	if (error == 0)
		error = prologue(&lowering);

	/* Lowers every instruction in order. */
	for (index = 0U; error == 0 && index < ir->instruction_count; index++) {
		instruction = &ir->instructions[index];
		lowering.index = index;

		/* The destination's VGPR, a value redefined by a MOVE keeping its own. */
		dst = GFX10_NO_REG;
		defines = ir_defines(instruction);
		if (defines != 0) {
			if (lowering.vgpr[instruction->dst] == GFX10_NO_REG) {
				error = take_vgpr(&lowering, &dst);
				if (error != 0)
					break;
				lowering.vgpr[instruction->dst] = (uint16_t)dst;
			}
			dst = lowering.vgpr[instruction->dst];
		}

		/* The instruction's GFX10 instructions. */
		error = lower(&lowering, instruction, dst);
		if (error != 0)
			break;

		/* A list that could not grow fails the compile. */
		if (lowering.out_of_memory != 0) {
			error = ENOMEM;
			break;
		}

		/* VGPRs of values no later instruction reads go back. */
		retire(&lowering, instruction);
	}

	/* The end of the program, its waits and its code. */
	if (error == 0)
		error = finish(&lowering);

	/* The per-value and per-uniform arrays and the list are only the compilation's. */
	kern_free(lowering.vgpr);
	kern_free(lowering.last_use);
	kern_free(lowering.slot);
	kern_free(lowering.list);

	/* A refused program is never handed over. */
	if (error != 0) {
		amdrdna_gfx10_program_free(lowering.program);
		return error;
	}

	/* Succeeded: the caller owns the program. */
	*program = lowering.program;
	return 0;
}

/* Frees a compiled program. */
void
amdrdna_gfx10_program_free(
	struct amdrdna_gfx10_program *program)
{
	/* Nothing to free. */
	if (program == NULL)
		return;

	/* The code, then the record. */
	kern_free(program->code);
	kern_free(program);
}

/* Describes a refusal at the instruction being lowered and returns its error. */
static int
refuse(
	struct gfx10_lowering *lowering,
	int error,
	const char *reason)
{
	/* The diagnostic is optional. */
	if (lowering->diagnostic != NULL) {
		lowering->diagnostic->instruction = lowering->index;
		lowering->diagnostic->reason = reason;
	}

	/* Reports the error. */
	return error;
}

/*
 * Lists the values an IR instruction reads.
 *
 * Returns how many it put in sources[], or -1 for an operation the
 * compiler does not lower.
 */
static int
ir_sources(
	const struct drv_gpu_shader_ir_inst *instruction,
	uint32_t *sources)
{
	uint32_t count;
	uint32_t index;

	/* The number of values read, by the operation. */
	count = 0U;
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
	case DRV_GPU_IR_LOOP_BEGIN:
		count = 0U;
		break;
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
	case DRV_GPU_IR_UNPACK_HALF:
	case DRV_GPU_IR_LOOP_END:
		count = 1U;
		break;
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
	case DRV_GPU_IR_PACK_HALF:
	case DRV_GPU_IR_UDIV:
	case DRV_GPU_IR_UMOD:
	case DRV_GPU_IR_IDIV:
	case DRV_GPU_IR_IREM:
		count = 2U;
		break;
	case DRV_GPU_IR_BARRIER:
	case DRV_GPU_IR_FENCE:
		count = 0U;
		break;
	case DRV_GPU_IR_ATOMIC:
		/* The offset and the value, the compare of a compare-and-exchange, then the predicate. */
		count = 2U;
		if (instruction->immediate == DRV_GPU_IR_ATOMIC_CMPXCHG)
			count++;
		if (instruction->component == DRV_GPU_IR_ATOMIC_PREDICATED)
			count++;
		break;
	case DRV_GPU_IR_SELECT:
		count = 3U;
		break;
	case DRV_GPU_IR_LOAD_STORAGE:
	case DRV_GPU_IR_LOAD_SHARED:
		/* The offset, and the predicate of a guarded load. */
		count = 1U;
		if (instruction->component != 0U)
			count = 2U;
		break;
	case DRV_GPU_IR_STORE_STORAGE:
	case DRV_GPU_IR_STORE_SHARED:
		/* The offset and the data, and the predicate of a guarded store. */
		count = 2U;
		if (instruction->component != 0U)
			count = 3U;
		break;
	default:
		/* An operation the compiler does not lower yet. */
		return -1;
	}

	/* The sources are the first ones of the instruction. */
	for (index = 0U; index < count; index++)
		sources[index] = instruction->src[index];

	/* Reports the count. */
	return (int)count;
}

/* Reports whether an IR instruction defines its destination value. */
static int
ir_defines(
	const struct drv_gpu_shader_ir_inst *instruction)
{
	/* The operations that define nothing. */
	switch (instruction->op) {
	case DRV_GPU_IR_NOP:
	case DRV_GPU_IR_SKIP_BEGIN:
	case DRV_GPU_IR_SKIP_END:
	case DRV_GPU_IR_STORE_STORAGE:
	case DRV_GPU_IR_STORE_SHARED:
	case DRV_GPU_IR_BARRIER:
	case DRV_GPU_IR_FENCE:
	case DRV_GPU_IR_LOOP_BEGIN:
	case DRV_GPU_IR_LOOP_END:
		return 0;
	default:
		return 1;
	}
}

/*
 * Checks the program and prepares its lowering: the last read of every
 * value, the loops' lifetimes, the uniforms' slots and the registers.
 */
static int
prepare(
	struct gfx10_lowering *lowering)
{
	const struct drv_gpu_shader_ir *ir;
	const struct drv_gpu_shader_ir_inst *instruction;
	uint32_t sources[GFX10_MAX_SOURCES];
	uint32_t index;
	uint32_t source;
	int count;
	int defines;
	int error;

	ir = lowering->ir;

	/* Compute programs are compiled; the graphics stages come later. */
	if (ir->stage != DRV_GPU_STAGE_COMPUTE)
		return refuse(lowering, ENOTSUP, "only compute shaders are compiled yet");

	/* The per-value arrays. */
	lowering->vgpr = kern_calloc(ir->value_count + 1U, sizeof(*lowering->vgpr));
	if (lowering->vgpr == NULL)
		return ENOMEM;

	lowering->last_use = kern_calloc(ir->value_count + 1U, sizeof(*lowering->last_use));
	if (lowering->last_use == NULL)
		return ENOMEM;

	/* No value has a VGPR yet. */
	for (index = 0U; index <= ir->value_count; index++)
		lowering->vgpr[index] = GFX10_NO_REG;

	/* Each instruction: covered, its values in range, its sources' last reads. */
	for (index = 0U; index < ir->instruction_count; index++) {
		instruction = &ir->instructions[index];
		lowering->index = index;

		/* The sources of an operation the compiler covers. */
		count = ir_sources(instruction, sources);
		if (count < 0)
			return refuse(lowering, ENOTSUP, "the operation is not compiled yet");

		/* The destination is a value of the program. */
		defines = ir_defines(instruction);
		if (defines != 0 && instruction->dst >= ir->value_count)
			return refuse(lowering, EINVAL, "a destination is not a value of the program");

		/* Each source is a value of the program, read last here so far. */
		for (source = 0U; source < (uint32_t)count; source++) {
			if (sources[source] >= ir->value_count)
				return refuse(lowering, EINVAL, "a source is not a value of the program");
			lowering->last_use[sources[source]] = index + 1U;
		}
	}

	/* Values read inside a loop that were made before it live to the loop's end. */
	error = extend_loops(lowering);
	if (error != 0)
		return error;

	/* The uniforms' slots and the resource table. */
	error = map_uniforms(lowering);
	if (error != 0)
		return error;

	/* The registers the inputs, the descriptors and the lowering keep. */
	error = plan_registers(lowering);
	if (error != 0)
		return error;

	/* The program's interface. */
	lowering->program->local_size[0] = ir->local_size[0];
	lowering->program->local_size[1] = ir->local_size[1];
	lowering->program->local_size[2] = ir->local_size[2];
	lowering->program->shared_bytes = ir->shared_bytes;
	lowering->program->uses_barrier = ir->uses_barrier;

	/* Succeeded: the program can be lowered. */
	lowering->index = 0U;
	return 0;
}

/*
 * Makes every value a loop's body reads that was defined before the loop
 * live to the loop's end, so that the next pass still finds it.
 */
static int
extend_loops(
	struct gfx10_lowering *lowering)
{
	const struct drv_gpu_shader_ir *ir;
	const struct drv_gpu_shader_ir_inst *instruction;
	uint32_t *first_definition;
	uint32_t starts[GFX10_LOOP_DEPTH];
	uint32_t sources[GFX10_MAX_SOURCES];
	uint32_t depth;
	uint32_t start;
	uint32_t index;
	uint32_t inner;
	uint32_t source;
	int defines;
	int count;

	ir = lowering->ir;

	/* Each value's first definition. */
	first_definition = kern_calloc(ir->value_count + 1U, sizeof(*first_definition));
	if (first_definition == NULL)
		return ENOMEM;

	/* No value is defined before the instructions are read. */
	for (index = 0U; index <= ir->value_count; index++)
		first_definition[index] = GFX10_NOT_DEFINED;

	/* The first instruction that defines each value. */
	for (index = 0U; index < ir->instruction_count; index++) {
		instruction = &ir->instructions[index];
		defines = ir_defines(instruction);
		if (defines != 0 && first_definition[instruction->dst] == GFX10_NOT_DEFINED)
			first_definition[instruction->dst] = index;
	}

	/* Each loop, at its end: the values its body reads that came before it live to the end. */
	depth = 0U;
	for (index = 0U; index < ir->instruction_count; index++) {
		instruction = &ir->instructions[index];
		lowering->index = index;

		/* A loop's start is remembered. */
		if (instruction->op == DRV_GPU_IR_LOOP_BEGIN) {
			if (depth == GFX10_LOOP_DEPTH) {
				kern_free(first_definition);
				return refuse(lowering, ENOTSUP, "loops nested deeper than the compiler follows");
			}
			starts[depth] = index;
			depth++;
			continue;
		}

		/* Anything but a loop's end is passed. */
		if (instruction->op != DRV_GPU_IR_LOOP_END)
			continue;

		/* An end without a start is malformed. */
		if (depth == 0U) {
			kern_free(first_definition);
			return refuse(lowering, EINVAL, "a loop's end without its beginning");
		}
		depth--;
		start = starts[depth];

		/* The body's reads of earlier values. */
		for (inner = start + 1U; inner <= index; inner++) {
			count = ir_sources(&ir->instructions[inner], sources);
			for (source = 0U; count > 0 && source < (uint32_t)count; source++) {
				if (first_definition[sources[source]] >= start)
					continue;
				if (lowering->last_use[sources[source]] < index + 1U)
					lowering->last_use[sources[source]] = index + 1U;
			}
		}
	}
	kern_free(first_definition);

	/* Every loop must end. */
	if (depth != 0U)
		return refuse(lowering, EINVAL, "a loop without its end");

	/* Succeeded: the values live through their loops. */
	return 0;
}

/*
 * Gives each uniform its slot and lays out the resource table: the push
 * constants, then the storage buffers' descriptors, then the uniform
 * buffers'.
 */
static int
map_uniforms(
	struct gfx10_lowering *lowering)
{
	const struct drv_gpu_shader_ir *ir;
	const struct drv_gpu_shader_ir_uniform *uniform;
	struct amdrdna_gfx10_program *program;
	uint32_t table;
	uint32_t index;

	ir = lowering->ir;
	program = lowering->program;

	/* One slot per uniform. */
	lowering->slot = kern_calloc(ir->uniform_count + 1U, sizeof(*lowering->slot));
	if (lowering->slot == NULL)
		return ENOMEM;

	/* The push constants come first, rounded to a descriptor's 16 bytes. */
	program->push_bytes = ir->push_bytes;
	table = (ir->push_bytes + 15U) & ~15U;

	/* The storage buffers in order. */
	for (index = 0U; index < ir->uniform_count; index++) {
		uniform = &ir->uniforms[index];
		lowering->slot[index] = GFX10_NO_SLOT;
		if (uniform->kind != DRV_GPU_IR_UNIFORM_STORAGE)
			continue;

		/* The table holds a fixed number of storage buffers. */
		if (program->storage_count == AMDRDNA_GFX10_STORAGE_MAX)
			return refuse(lowering, ENOTSUP, "more storage buffers than the table holds");

		/* The buffer's descriptor takes the next 16 bytes. */
		lowering->slot[index] = program->storage_count;
		program->storage[program->storage_count].set = uniform->set;
		program->storage[program->storage_count].binding = uniform->binding;
		program->storage[program->storage_count].table_offset = table;
		program->storage[program->storage_count].size = 0U;
		program->storage_count++;
		table += AMDRDNA_GFX10_BUFFER_DESCRIPTOR_BYTES;
	}

	/* The uniform buffers in order. */
	for (index = 0U; index < ir->uniform_count; index++) {
		uniform = &ir->uniforms[index];
		if (uniform->kind != DRV_GPU_IR_UNIFORM_BLOCK)
			continue;

		/* The table holds a fixed number of uniform buffers. */
		if (program->uniform_count == AMDRDNA_GFX10_UNIFORM_MAX)
			return refuse(lowering, ENOTSUP, "more uniform buffers than the table holds");

		/* The buffer's descriptor takes the next 16 bytes. */
		lowering->slot[index] = program->uniform_count;
		program->uniforms[program->uniform_count].set = uniform->set;
		program->uniforms[program->uniform_count].binding = uniform->binding;
		program->uniforms[program->uniform_count].table_offset = table;
		program->uniforms[program->uniform_count].size = uniform->offset + uniform->size;
		program->uniform_count++;
		table += AMDRDNA_GFX10_BUFFER_DESCRIPTOR_BYTES;
	}

	/* A sampled image is not compiled yet. */
	for (index = 0U; index < ir->uniform_count; index++) {
		uniform = &ir->uniforms[index];
		if (uniform->kind == DRV_GPU_IR_UNIFORM_SAMPLED_IMAGE)
			return refuse(lowering, ENOTSUP, "sampled images are not compiled yet");
	}

	/* Succeeded: the table's size is known. */
	program->table_bytes = table;
	return 0;
}

/*
 * Places the registers the program keeps for its whole run: the inputs the
 * hardware delivers, the descriptors, the loop and guard mask saves and the
 * scalar temporaries.
 */
static int
plan_registers(
	struct gfx10_lowering *lowering)
{
	struct amdrdna_gfx10_program *program;
	const struct drv_gpu_shader_ir_inst *instruction;
	uint32_t sgpr;
	uint32_t index;
	uint32_t depth;
	uint32_t deepest;

	program = lowering->program;

	/* A compute program: the table address and the workgroup id in s0..s4, the local ids in v0..v2. */
	program->user_sgprs = AMDRDNA_GFX10_ABI_USER_SGPRS;
	lowering->vgpr_first = GFX10_CS_INPUT_VGPRS;
	lowering->vgpr_highest = GFX10_CS_INPUT_VGPRS;

	/* The descriptors, four SGPRs each from a multiple of four. */
	sgpr = GFX10_SGPR_FIRST_FREE;
	for (index = 0U; index < program->storage_count; index++) {
		lowering->storage_sgpr[index] = sgpr;
		sgpr += 4U;
	}
	for (index = 0U; index < program->uniform_count; index++) {
		lowering->uniform_sgpr[index] = sgpr;
		sgpr += 4U;
	}

	/* The deepest loop nest: one lane mask save per depth. */
	depth = 0U;
	deepest = 0U;
	for (index = 0U; index < lowering->ir->instruction_count; index++) {
		instruction = &lowering->ir->instructions[index];
		if (instruction->op == DRV_GPU_IR_LOOP_BEGIN) {
			depth++;
			if (depth > deepest)
				deepest = depth;
		} else if (instruction->op == DRV_GPU_IR_LOOP_END && depth != 0U) {
			depth--;
		}
	}
	for (index = 0U; index < deepest; index++) {
		lowering->loop_save[index] = sgpr;
		sgpr++;
	}

	/* The guard's lane mask save and the scalar temporaries. */
	lowering->guard_save = sgpr;
	sgpr++;
	for (index = 0U; index < GFX10_SCALAR_TEMPS; index++) {
		lowering->scalar_temp[index] = sgpr;
		sgpr++;
	}

	/* Everything fits below VCC. */
	if (sgpr > AMDRDNA_GFX10_SGPR_COUNT)
		return refuse(lowering, ENOTSUP, "the descriptors and the loops need more SGPRs than there are");

	/* Succeeded: the registers are placed. */
	lowering->sgpr_highest = sgpr;
	return 0;
}

/* Loads the descriptors of the buffers the program uses into their SGPRs. */
static int
prologue(
	struct gfx10_lowering *lowering)
{
	struct amdrdna_gfx10_program *program;
	uint32_t index;

	program = lowering->program;

	/* Each storage buffer's descriptor. */
	for (index = 0U; index < program->storage_count; index++) {
		emit_scalar_load(lowering,
				 AMDRDNA_GFX10_S_LOAD_DWORDX4,
				 lowering->storage_sgpr[index],
				 AMDRDNA_GFX10_ABI_TABLE,
				 program->storage[index].table_offset,
				 4U);
	}

	/* Each uniform buffer's descriptor. */
	for (index = 0U; index < program->uniform_count; index++) {
		emit_scalar_load(lowering,
				 AMDRDNA_GFX10_S_LOAD_DWORDX4,
				 lowering->uniform_sgpr[index],
				 AMDRDNA_GFX10_ABI_TABLE,
				 program->uniforms[index].table_offset,
				 4U);
	}

	/* A list that could not grow fails the compile. */
	if (lowering->out_of_memory != 0)
		return ENOMEM;

	/* Succeeded: the descriptors are on their way. */
	return 0;
}

/* Lowers one IR instruction into dst's VGPR (GFX10_NO_REG when it defines nothing). */
static int
lower(
	struct gfx10_lowering *lowering,
	const struct drv_gpu_shader_ir_inst *instruction,
	uint32_t dst)
{
	int error;

	/* Picks the lowering by the operation. */
	switch (instruction->op) {
	case DRV_GPU_IR_NOP:
	case DRV_GPU_IR_SKIP_BEGIN:
	case DRV_GPU_IR_SKIP_END:
		/* Every block runs, so a skippable block's markers do nothing. */
		error = 0;
		break;
	case DRV_GPU_IR_CONST:
	case DRV_GPU_IR_ICONST:
	case DRV_GPU_IR_BOOL:
		emit_vector_literal(lowering,
				    AMDRDNA_GFX10_KIND_VECTOR_ONE,
				    AMDRDNA_GFX10_V_MOV_B32,
				    AMDRDNA_GFX10_V(dst),
				    constant_operand(instruction->immediate),
				    0U,
				    instruction->immediate);
		error = 0;
		break;
	case DRV_GPU_IR_MOVE:
		emit_vector(lowering,
			    AMDRDNA_GFX10_KIND_VECTOR_ONE,
			    AMDRDNA_GFX10_V_MOV_B32,
			    AMDRDNA_GFX10_V(dst),
			    value_operand(lowering, instruction->src[0]),
			    0U,
			    0U);
		error = 0;
		break;
	case DRV_GPU_IR_FADD:
	case DRV_GPU_IR_FSUB:
	case DRV_GPU_IR_FMUL:
	case DRV_GPU_IR_FNEG:
	case DRV_GPU_IR_FABS:
	case DRV_GPU_IR_FMIN:
	case DRV_GPU_IR_FMAX:
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
	case DRV_GPU_IR_PACK_HALF:
	case DRV_GPU_IR_UNPACK_HALF:
		error = lower_float(lowering, instruction, dst);
		break;
	case DRV_GPU_IR_AND:
	case DRV_GPU_IR_OR:
	case DRV_GPU_IR_NOT:
	case DRV_GPU_IR_IADD:
	case DRV_GPU_IR_ISUB:
	case DRV_GPU_IR_IMUL:
	case DRV_GPU_IR_INEG:
	case DRV_GPU_IR_IAND:
	case DRV_GPU_IR_IOR:
	case DRV_GPU_IR_IXOR:
	case DRV_GPU_IR_INOT:
	case DRV_GPU_IR_SHL:
	case DRV_GPU_IR_SHR:
	case DRV_GPU_IR_ASR:
		error = lower_integer(lowering, instruction, dst);
		break;
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
		error = lower_compare(lowering, instruction, dst);
		break;
	case DRV_GPU_IR_LOAD_PUSH:
	case DRV_GPU_IR_LOAD_UBO:
		error = lower_uniform(lowering, instruction, dst);
		break;
	case DRV_GPU_IR_LOAD_SYSTEM:
		error = lower_system(lowering, instruction, dst);
		break;
	case DRV_GPU_IR_LOAD_STORAGE:
	case DRV_GPU_IR_STORE_STORAGE:
	case DRV_GPU_IR_STORAGE_SIZE:
		error = lower_storage(lowering, instruction, dst);
		break;
	case DRV_GPU_IR_LOOP_BEGIN:
	case DRV_GPU_IR_LOOP_END:
		error = lower_loop(lowering, instruction);
		break;
	case DRV_GPU_IR_UDIV:
	case DRV_GPU_IR_UMOD:
	case DRV_GPU_IR_IDIV:
	case DRV_GPU_IR_IREM:
		error = lower_divide(lowering, instruction, dst);
		break;
	case DRV_GPU_IR_ATOMIC:
		/* An atomic on shared memory, or on a storage buffer. */
		if (instruction->location == DRV_GPU_IR_LOCATION_SHARED) {
			error = lower_shared(lowering, instruction, dst);
		} else {
			error = lower_atomic(lowering, instruction, dst);
		}
		break;
	case DRV_GPU_IR_LOAD_SHARED:
	case DRV_GPU_IR_STORE_SHARED:
		error = lower_shared(lowering, instruction, dst);
		break;
	case DRV_GPU_IR_BARRIER:
	case DRV_GPU_IR_FENCE:
		lower_sync(lowering, instruction);
		error = 0;
		break;
	default:
		error = refuse(lowering, ENOTSUP, "the operation is not compiled yet");
		break;
	}

	/* Reports the lowering's refusal. */
	if (error != 0)
		return error;

	/* Succeeded: the instruction is lowered. */
	return 0;
}

/* Lowers a float operation, a conversion, or a half-float packing. */
static int
lower_float(
	struct gfx10_lowering *lowering,
	const struct drv_gpu_shader_ir_inst *instruction,
	uint32_t dst)
{
	unsigned a;
	unsigned b;
	unsigned d;
	unsigned opcode;
	uint32_t temp;
	int error;

	a = value_operand(lowering, instruction->src[0]);
	b = 0U;
	if (instruction->op == DRV_GPU_IR_FADD || instruction->op == DRV_GPU_IR_FSUB ||
	    instruction->op == DRV_GPU_IR_FMUL || instruction->op == DRV_GPU_IR_FMIN ||
	    instruction->op == DRV_GPU_IR_FMAX || instruction->op == DRV_GPU_IR_PACK_HALF)
		b = value_operand(lowering, instruction->src[1]);
	d = AMDRDNA_GFX10_V(dst);

	/* Picks the instructions by the operation. */
	switch (instruction->op) {
	case DRV_GPU_IR_FADD:
		emit_vector(lowering, AMDRDNA_GFX10_KIND_VECTOR_TWO, AMDRDNA_GFX10_V_ADD_F32, d, a, b, 0U);
		break;
	case DRV_GPU_IR_FSUB:
		emit_vector(lowering, AMDRDNA_GFX10_KIND_VECTOR_TWO, AMDRDNA_GFX10_V_SUB_F32, d, a, b, 0U);
		break;
	case DRV_GPU_IR_FMUL:
		emit_vector(lowering, AMDRDNA_GFX10_KIND_VECTOR_TWO, AMDRDNA_GFX10_V_MUL_F32, d, a, b, 0U);
		break;
	case DRV_GPU_IR_FMIN:
		emit_vector(lowering, AMDRDNA_GFX10_KIND_VECTOR_TWO, AMDRDNA_GFX10_V_MIN_F32, d, a, b, 0U);
		break;
	case DRV_GPU_IR_FMAX:
		emit_vector(lowering, AMDRDNA_GFX10_KIND_VECTOR_TWO, AMDRDNA_GFX10_V_MAX_F32, d, a, b, 0U);
		break;
	case DRV_GPU_IR_FNEG:
		/* The negation flips the sign bit and nothing else, a NaN included. */
		emit_vector_literal(lowering, AMDRDNA_GFX10_KIND_VECTOR_TWO, AMDRDNA_GFX10_V_XOR_B32, d, AMDRDNA_GFX10_LITERAL, a, GFX10_SIGN_BIT);
		break;
	case DRV_GPU_IR_FABS:
		/* The absolute value clears the sign bit and nothing else. */
		emit_vector_literal(lowering, AMDRDNA_GFX10_KIND_VECTOR_TWO, AMDRDNA_GFX10_V_AND_B32, d, AMDRDNA_GFX10_LITERAL, a, GFX10_MAGNITUDE_BITS);
		break;
	case DRV_GPU_IR_SIN:
	case DRV_GPU_IR_COS:
		/* The sine and cosine units take the angle in turns: the radians scaled by 1 / (2 pi) first. */
		emit_vector(lowering, AMDRDNA_GFX10_KIND_VECTOR_TWO, AMDRDNA_GFX10_V_MUL_F32, d, AMDRDNA_GFX10_INV_TWO_PI, a, 0U);
		opcode = AMDRDNA_GFX10_V_SIN_F32;
		if (instruction->op == DRV_GPU_IR_COS)
			opcode = AMDRDNA_GFX10_V_COS_F32;
		emit_vector(lowering, AMDRDNA_GFX10_KIND_VECTOR_ONE, opcode, d, d, 0U, 0U);
		break;
	case DRV_GPU_IR_FRACT:
		/* The fraction is x - floor(x), the floor taken first into the destination. */
		emit_vector(lowering, AMDRDNA_GFX10_KIND_VECTOR_ONE, AMDRDNA_GFX10_V_FLOOR_F32, d, a, 0U, 0U);
		emit_vector(lowering, AMDRDNA_GFX10_KIND_VECTOR_TWO, AMDRDNA_GFX10_V_SUB_F32, d, a, d, 0U);
		break;
	case DRV_GPU_IR_PACK_HALF:
		/* Each half is converted on its own, the high one into a temporary, then the two are packed. */
		error = take_vgpr(lowering, &temp);
		if (error != 0)
			return error;
		emit_vector(lowering, AMDRDNA_GFX10_KIND_VECTOR_ONE, AMDRDNA_GFX10_V_CVT_F16_F32, d, a, 0U, 0U);
		emit_vector(lowering, AMDRDNA_GFX10_KIND_VECTOR_ONE, AMDRDNA_GFX10_V_CVT_F16_F32, AMDRDNA_GFX10_V(temp), b, 0U, 0U);
		emit_vector(lowering, AMDRDNA_GFX10_KIND_VECTOR_WIDE, AMDRDNA_GFX10_V_PACK_B32_F16, d, d, AMDRDNA_GFX10_V(temp), 0U);
		give_vgpr(lowering, temp);
		break;
	case DRV_GPU_IR_UNPACK_HALF:
		/* The high half is shifted down first; the conversion reads the low 16 bits. */
		if (instruction->component != 0U) {
			emit_vector(lowering, AMDRDNA_GFX10_KIND_VECTOR_TWO, AMDRDNA_GFX10_V_LSHRREV_B32, d, constant_operand(16U), a, 0U);
			a = d;
		}
		emit_vector(lowering, AMDRDNA_GFX10_KIND_VECTOR_ONE, AMDRDNA_GFX10_V_CVT_F32_F16, d, a, 0U, 0U);
		break;
	default:
		/* The one-source operations each have one instruction. */
		switch (instruction->op) {
		case DRV_GPU_IR_RSQ:
			opcode = AMDRDNA_GFX10_V_RSQ_F32;
			break;
		case DRV_GPU_IR_RCP:
			opcode = AMDRDNA_GFX10_V_RCP_F32;
			break;
		case DRV_GPU_IR_SQRT:
			opcode = AMDRDNA_GFX10_V_SQRT_F32;
			break;
		case DRV_GPU_IR_EXP2:
			opcode = AMDRDNA_GFX10_V_EXP_F32;
			break;
		case DRV_GPU_IR_LOG2:
			opcode = AMDRDNA_GFX10_V_LOG_F32;
			break;
		case DRV_GPU_IR_FLOOR:
			opcode = AMDRDNA_GFX10_V_FLOOR_F32;
			break;
		case DRV_GPU_IR_FTRUNC:
			opcode = AMDRDNA_GFX10_V_TRUNC_F32;
			break;
		case DRV_GPU_IR_FROUND_EVEN:
			opcode = AMDRDNA_GFX10_V_RNDNE_F32;
			break;
		case DRV_GPU_IR_I2F:
			opcode = AMDRDNA_GFX10_V_CVT_F32_I32;
			break;
		case DRV_GPU_IR_U2F:
			opcode = AMDRDNA_GFX10_V_CVT_F32_U32;
			break;
		case DRV_GPU_IR_F2I:
			opcode = AMDRDNA_GFX10_V_CVT_I32_F32;
			break;
		default:
			opcode = AMDRDNA_GFX10_V_CVT_U32_F32;
			break;
		}
		emit_vector(lowering, AMDRDNA_GFX10_KIND_VECTOR_ONE, opcode, d, a, 0U, 0U);
		break;
	}

	/* Succeeded: the operation is lowered. */
	return 0;
}

/* Lowers an integer or a bitwise (Boolean included) operation. */
static int
lower_integer(
	struct gfx10_lowering *lowering,
	const struct drv_gpu_shader_ir_inst *instruction,
	uint32_t dst)
{
	unsigned a;
	unsigned b;
	unsigned d;

	a = value_operand(lowering, instruction->src[0]);
	b = 0U;
	if (instruction->op != DRV_GPU_IR_NOT && instruction->op != DRV_GPU_IR_INOT &&
	    instruction->op != DRV_GPU_IR_INEG)
		b = value_operand(lowering, instruction->src[1]);
	d = AMDRDNA_GFX10_V(dst);

	/* Picks the instruction by the operation. */
	switch (instruction->op) {
	case DRV_GPU_IR_AND:
	case DRV_GPU_IR_IAND:
		emit_vector(lowering, AMDRDNA_GFX10_KIND_VECTOR_TWO, AMDRDNA_GFX10_V_AND_B32, d, a, b, 0U);
		break;
	case DRV_GPU_IR_OR:
	case DRV_GPU_IR_IOR:
		emit_vector(lowering, AMDRDNA_GFX10_KIND_VECTOR_TWO, AMDRDNA_GFX10_V_OR_B32, d, a, b, 0U);
		break;
	case DRV_GPU_IR_IXOR:
		emit_vector(lowering, AMDRDNA_GFX10_KIND_VECTOR_TWO, AMDRDNA_GFX10_V_XOR_B32, d, a, b, 0U);
		break;
	case DRV_GPU_IR_NOT:
	case DRV_GPU_IR_INOT:
		emit_vector(lowering, AMDRDNA_GFX10_KIND_VECTOR_ONE, AMDRDNA_GFX10_V_NOT_B32, d, a, 0U, 0U);
		break;
	case DRV_GPU_IR_IADD:
		emit_vector(lowering, AMDRDNA_GFX10_KIND_VECTOR_TWO, AMDRDNA_GFX10_V_ADD_NC_U32, d, a, b, 0U);
		break;
	case DRV_GPU_IR_ISUB:
		emit_vector(lowering, AMDRDNA_GFX10_KIND_VECTOR_TWO, AMDRDNA_GFX10_V_SUB_NC_U32, d, a, b, 0U);
		break;
	case DRV_GPU_IR_INEG:
		/* The negation is zero minus the value. */
		emit_vector(lowering, AMDRDNA_GFX10_KIND_VECTOR_TWO, AMDRDNA_GFX10_V_SUB_NC_U32, d, AMDRDNA_GFX10_ZERO, a, 0U);
		break;
	case DRV_GPU_IR_IMUL:
		emit_vector(lowering, AMDRDNA_GFX10_KIND_VECTOR_WIDE, AMDRDNA_GFX10_V_MUL_LO_U32, d, a, b, 0U);
		break;
	case DRV_GPU_IR_SHL:
		/* The shifts take the count first and use its low five bits, which is the IR's modulo 32. */
		emit_vector(lowering, AMDRDNA_GFX10_KIND_VECTOR_TWO, AMDRDNA_GFX10_V_LSHLREV_B32, d, b, a, 0U);
		break;
	case DRV_GPU_IR_SHR:
		emit_vector(lowering, AMDRDNA_GFX10_KIND_VECTOR_TWO, AMDRDNA_GFX10_V_LSHRREV_B32, d, b, a, 0U);
		break;
	default:
		emit_vector(lowering, AMDRDNA_GFX10_KIND_VECTOR_TWO, AMDRDNA_GFX10_V_ASHRREV_I32, d, b, a, 0U);
		break;
	}

	/* Succeeded: the operation is lowered. */
	return 0;
}

/* Lowers a comparison into a Boolean, or a selection by a Boolean. */
static int
lower_compare(
	struct gfx10_lowering *lowering,
	const struct drv_gpu_shader_ir_inst *instruction,
	uint32_t dst)
{
	unsigned opcode;

	/* A selection: the condition into VCC_LO, then each lane takes the second source where it is set. */
	if (instruction->op == DRV_GPU_IR_SELECT) {
		condition_from_boolean(lowering, instruction->src[0]);
		emit_vector(lowering,
			    AMDRDNA_GFX10_KIND_VECTOR_TWO,
			    AMDRDNA_GFX10_V_CNDMASK_B32,
			    AMDRDNA_GFX10_V(dst),
			    value_operand(lowering, instruction->src[2]),
			    value_operand(lowering, instruction->src[1]),
			    AMDRDNA_GFX10_VCC_LO);
		return 0;
	}

	/* Picks the comparison by the operation. */
	switch (instruction->op) {
	case DRV_GPU_IR_FLT:
		opcode = AMDRDNA_GFX10_V_CMP_LT_F32;
		break;
	case DRV_GPU_IR_FGE:
		opcode = AMDRDNA_GFX10_V_CMP_GE_F32;
		break;
	case DRV_GPU_IR_FEQ:
		opcode = AMDRDNA_GFX10_V_CMP_EQ_F32;
		break;
	case DRV_GPU_IR_FNEU:
		/* "Not equal" in the unordered sense: true when either is NaN. */
		opcode = AMDRDNA_GFX10_V_CMP_NEQ_F32;
		break;
	case DRV_GPU_IR_ILT:
		opcode = AMDRDNA_GFX10_V_CMP_LT_I32;
		break;
	case DRV_GPU_IR_IGE:
		opcode = AMDRDNA_GFX10_V_CMP_GE_I32;
		break;
	case DRV_GPU_IR_ULT:
		opcode = AMDRDNA_GFX10_V_CMP_LT_U32;
		break;
	case DRV_GPU_IR_UGE:
		opcode = AMDRDNA_GFX10_V_CMP_GE_U32;
		break;
	case DRV_GPU_IR_IEQ:
		opcode = AMDRDNA_GFX10_V_CMP_EQ_U32;
		break;
	default:
		opcode = AMDRDNA_GFX10_V_CMP_NE_U32;
		break;
	}

	/* The comparison writes VCC_LO, which becomes the Boolean. */
	emit_vector(lowering,
		    AMDRDNA_GFX10_KIND_VECTOR_COMPARE,
		    opcode,
		    AMDRDNA_GFX10_VCC_LO,
		    value_operand(lowering, instruction->src[0]),
		    value_operand(lowering, instruction->src[1]),
		    0U);
	boolean_from_condition(lowering, dst);

	/* Succeeded: the comparison is lowered. */
	return 0;
}

/* Lowers the read of a push constant or uniform buffer word: a scalar load, then a copy to every lane. */
static int
lower_uniform(
	struct gfx10_lowering *lowering,
	const struct drv_gpu_shader_ir_inst *instruction,
	uint32_t dst)
{
	const struct drv_gpu_shader_ir_uniform *uniform;
	unsigned temp;
	uint32_t slot;

	temp = next_scalar_temp(lowering);

	/* A push constant is read from the table, at its offset. */
	if (instruction->op == DRV_GPU_IR_LOAD_PUSH) {
		/* The word lies inside the push constants. */
		if (instruction->immediate + 4U > lowering->ir->push_bytes)
			return refuse(lowering, EINVAL, "a push constant read past the push constants");

		emit_scalar_load(lowering, AMDRDNA_GFX10_S_LOAD_DWORD, temp, AMDRDNA_GFX10_ABI_TABLE, instruction->immediate, 1U);
	} else {
		/* The uniform is a uniform buffer of the program. */
		if (instruction->location >= lowering->ir->uniform_count)
			return refuse(lowering, EINVAL, "a uniform buffer read names no uniform");
		uniform = &lowering->ir->uniforms[instruction->location];
		if (uniform->kind != DRV_GPU_IR_UNIFORM_BLOCK)
			return refuse(lowering, EINVAL, "a uniform buffer read names another kind of uniform");

		/* The word is read through the buffer's descriptor. */
		slot = lowering->slot[instruction->location];
		emit_scalar_load(lowering,
				 AMDRDNA_GFX10_S_BUFFER_LOAD_DWORD,
				 temp,
				 lowering->uniform_sgpr[slot],
				 instruction->immediate,
				 1U);
	}

	/* Every lane gets the word. */
	emit_vector(lowering, AMDRDNA_GFX10_KIND_VECTOR_ONE, AMDRDNA_GFX10_V_MOV_B32, AMDRDNA_GFX10_V(dst), temp, 0U, 0U);

	/* Succeeded: the word is the value. */
	return 0;
}

/* Lowers the read of a compute invocation's built-in value. */
static int
lower_system(
	struct gfx10_lowering *lowering,
	const struct drv_gpu_shader_ir_inst *instruction,
	uint32_t dst)
{
	const struct drv_gpu_shader_ir *ir;
	unsigned d;
	unsigned source;

	ir = lowering->ir;
	d = AMDRDNA_GFX10_V(dst);

	/* Picks the input register by the value asked for. */
	switch (instruction->component) {
	case DRV_GPU_IR_SYSTEM_LOCAL_ID_X:
		source = AMDRDNA_GFX10_V(GFX10_CS_LOCAL_ID_X);
		break;
	case DRV_GPU_IR_SYSTEM_LOCAL_ID_Y:
		source = AMDRDNA_GFX10_V(GFX10_CS_LOCAL_ID_Y);
		break;
	case DRV_GPU_IR_SYSTEM_LOCAL_ID_Z:
		source = AMDRDNA_GFX10_V(GFX10_CS_LOCAL_ID_Z);
		break;
	case DRV_GPU_IR_SYSTEM_GROUP_ID_X:
		source = AMDRDNA_GFX10_S(GFX10_CS_GROUP_ID_X);
		break;
	case DRV_GPU_IR_SYSTEM_GROUP_ID_Y:
		source = AMDRDNA_GFX10_S(GFX10_CS_GROUP_ID_X + 1U);
		break;
	case DRV_GPU_IR_SYSTEM_GROUP_ID_Z:
		source = AMDRDNA_GFX10_S(GFX10_CS_GROUP_ID_X + 2U);
		break;
	case DRV_GPU_IR_SYSTEM_LOCAL_INDEX:
		/*
		 * The linear index is x + size_x * (y + size_y * z); the
		 * sizes are below 2^24, so the 24-bit multiply-adds are exact.
		 */
		emit_multiply_add(lowering,
				  d,
				  AMDRDNA_GFX10_V(GFX10_CS_LOCAL_ID_Z),
				  ir->local_size[1],
				  AMDRDNA_GFX10_V(GFX10_CS_LOCAL_ID_Y));
		emit_multiply_add(lowering,
				  d,
				  d,
				  ir->local_size[0],
				  AMDRDNA_GFX10_V(GFX10_CS_LOCAL_ID_X));
		return 0;
	default:
		return refuse(lowering, ENOTSUP, "the built-in value is not one a compute program has");
	}

	/* Copies the input to the value's VGPR. */
	emit_vector(lowering, AMDRDNA_GFX10_KIND_VECTOR_ONE, AMDRDNA_GFX10_V_MOV_B32, d, source, 0U, 0U);

	/* Succeeded: the built-in value is read. */
	return 0;
}

/* Lowers a storage buffer read, write or size query. */
static int
lower_storage(
	struct gfx10_lowering *lowering,
	const struct drv_gpu_shader_ir_inst *instruction,
	uint32_t dst)
{
	const struct drv_gpu_shader_ir_uniform *uniform;
	struct amdrdna_gfx10_inst inst;
	uint32_t slot;
	int guarded;

	/* The uniform is a storage buffer of the program. */
	if (instruction->location >= lowering->ir->uniform_count)
		return refuse(lowering, EINVAL, "a storage access names no uniform");
	uniform = &lowering->ir->uniforms[instruction->location];
	if (uniform->kind != DRV_GPU_IR_UNIFORM_STORAGE)
		return refuse(lowering, EINVAL, "a storage access names another kind of uniform");
	slot = lowering->slot[instruction->location];

	/* The size is the descriptor's record count, the buffer's bytes. */
	if (instruction->op == DRV_GPU_IR_STORAGE_SIZE) {
		emit_vector(lowering,
			    AMDRDNA_GFX10_KIND_VECTOR_ONE,
			    AMDRDNA_GFX10_V_MOV_B32,
			    AMDRDNA_GFX10_V(dst),
			    AMDRDNA_GFX10_S(lowering->storage_sgpr[slot] + 2U),
			    0U,
			    0U);
		return 0;
	}

	/* A guarded access runs only for the lanes whose predicate holds. */
	guarded = 0;
	if (instruction->op == DRV_GPU_IR_LOAD_STORAGE && instruction->component != 0U) {
		guard_begin(lowering, instruction->src[1]);
		guarded = 1;
	} else if (instruction->op == DRV_GPU_IR_STORE_STORAGE && instruction->component != 0U) {
		guard_begin(lowering, instruction->src[2]);
		guarded = 1;
	}

	/* The access: the byte offset in a VGPR, the descriptor, no scalar offset. */
	kern_memset(&inst, 0, sizeof(inst));
	inst.kind = AMDRDNA_GFX10_KIND_BUFFER;
	inst.src[0] = (uint16_t)value_operand(lowering, instruction->src[0]);
	inst.src[1] = (uint16_t)lowering->storage_sgpr[slot];
	inst.src[2] = AMDRDNA_GFX10_ZERO;
	inst.flags = AMDRDNA_GFX10_FLAG_OFFSET_ADDRESS;
	if (instruction->op == DRV_GPU_IR_LOAD_STORAGE) {
		/*
		 * A load writes the value's VGPR.  It goes past both caches (the
		 * coherent bits are set together), so a store of another
		 * workgroup is seen once its fence has run.
		 *
		 * Procedure referenced: Mesa 25.0.7 src/amd/compiler/README-ISA.md
		 * ("RDNA L0, L1 cache and DLC, GLC bits").
		 */
		inst.opcode = AMDRDNA_GFX10_BUFFER_LOAD_DWORD;
		inst.dst = (uint16_t)AMDRDNA_GFX10_V(dst);
		inst.count = 1U;
		inst.flags |= AMDRDNA_GFX10_FLAG_GLOBALLY_COHERENT;
		inst.flags |= AMDRDNA_GFX10_FLAG_DEVICE_COHERENT;
	} else {
		/* A store reads the data's VGPR. */
		inst.opcode = AMDRDNA_GFX10_BUFFER_STORE_DWORD;
		inst.src[3] = (uint16_t)value_operand(lowering, instruction->src[1]);
	}
	emit(lowering, &inst);

	/* The lanes come back after a guarded access. */
	if (guarded != 0)
		guard_end(lowering);

	/* Succeeded: the access is lowered. */
	return 0;
}

/*
 * Lowers a loop's start or end.
 *
 * At the start the lane mask is saved and the label of the next pass is
 * placed.  At the end the lanes whose condition is false are dropped, the
 * loop branches back while any lane is left, and the saved mask comes back.
 */
static int
lower_loop(
	struct gfx10_lowering *lowering,
	const struct drv_gpu_shader_ir_inst *instruction)
{
	uint32_t depth;
	uint32_t label;

	/* A loop's start. */
	if (instruction->op == DRV_GPU_IR_LOOP_BEGIN) {
		depth = lowering->loop_depth;
		label = lowering->next_label;
		lowering->next_label++;

		/* Saves the lanes that enter, and marks the start of a pass. */
		emit_scalar(lowering,
			    AMDRDNA_GFX10_KIND_SCALAR_ONE,
			    AMDRDNA_GFX10_S_MOV_B32,
			    lowering->loop_save[depth],
			    AMDRDNA_GFX10_EXEC_LO,
			    0U);
		emit_label(lowering, label);
		lowering->loop_label[depth] = label;
		lowering->loop_depth++;
		return 0;
	}

	/* A loop's end matches the innermost start. */
	if (lowering->loop_depth == 0U)
		return refuse(lowering, EINVAL, "a loop's end without its beginning");
	lowering->loop_depth--;
	depth = lowering->loop_depth;

	/* Drops the lanes whose condition is false. */
	condition_from_boolean(lowering, instruction->src[0]);
	emit_scalar(lowering,
		    AMDRDNA_GFX10_KIND_SCALAR_TWO,
		    AMDRDNA_GFX10_S_AND_B32,
		    AMDRDNA_GFX10_EXEC_LO,
		    AMDRDNA_GFX10_EXEC_LO,
		    AMDRDNA_GFX10_VCC_LO);

	/* Runs another pass while any lane is left. */
	emit_branch(lowering, AMDRDNA_GFX10_S_CBRANCH_EXECNZ, lowering->loop_label[depth]);

	/* The lanes that entered come back. */
	emit_scalar(lowering,
		    AMDRDNA_GFX10_KIND_SCALAR_ONE,
		    AMDRDNA_GFX10_S_MOV_B32,
		    AMDRDNA_GFX10_EXEC_LO,
		    lowering->loop_save[depth],
		    0U);

	/* Succeeded: the loop is closed. */
	return 0;
}

/*
 * Lowers an integer division or remainder, unsigned or signed.
 *
 * The signed forms divide the magnitudes and give the quotient the sign
 * of the operands' exclusive or, the remainder the sign of the numerator.
 */
static int
lower_divide(
	struct gfx10_lowering *lowering,
	const struct drv_gpu_shader_ir_inst *instruction,
	uint32_t dst)
{
	unsigned numerator;
	unsigned denominator;
	uint32_t quotient;
	uint32_t remainder;
	uint32_t temp;
	uint32_t sign;
	uint32_t magnitude;
	int is_signed;
	int error;

	numerator = value_operand(lowering, instruction->src[0]);
	denominator = value_operand(lowering, instruction->src[1]);
	is_signed = 0;
	if (instruction->op == DRV_GPU_IR_IDIV || instruction->op == DRV_GPU_IR_IREM)
		is_signed = 1;

	/* The quotient, the remainder and a scratch register. */
	error = take_vgpr(lowering, &quotient);
	if (error != 0)
		return error;
	error = take_vgpr(lowering, &remainder);
	if (error != 0)
		return error;
	error = take_vgpr(lowering, &temp);
	if (error != 0)
		return error;

	/*
	 * The signed forms divide the magnitudes: |x| is the larger of x and
	 * -x (the most negative integer stays itself, which read unsigned is
	 * its magnitude), |n| held in dst and |d| in a fourth register.
	 */
	if (is_signed != 0) {
		error = take_vgpr(lowering, &magnitude);
		if (error != 0)
			return error;
		emit_vector(lowering, AMDRDNA_GFX10_KIND_VECTOR_TWO, AMDRDNA_GFX10_V_SUB_NC_U32, AMDRDNA_GFX10_V(temp), AMDRDNA_GFX10_ZERO, numerator, 0U);
		emit_vector(lowering, AMDRDNA_GFX10_KIND_VECTOR_TWO, AMDRDNA_GFX10_V_MAX_I32, AMDRDNA_GFX10_V(dst), numerator, AMDRDNA_GFX10_V(temp), 0U);
		emit_vector(lowering, AMDRDNA_GFX10_KIND_VECTOR_TWO, AMDRDNA_GFX10_V_SUB_NC_U32, AMDRDNA_GFX10_V(temp), AMDRDNA_GFX10_ZERO, denominator, 0U);
		emit_vector(lowering, AMDRDNA_GFX10_KIND_VECTOR_TWO, AMDRDNA_GFX10_V_MAX_I32, AMDRDNA_GFX10_V(magnitude), denominator, AMDRDNA_GFX10_V(temp), 0U);
		divide_unsigned(lowering, AMDRDNA_GFX10_V(dst), AMDRDNA_GFX10_V(magnitude), quotient, remainder, temp);
		give_vgpr(lowering, magnitude);
	} else {
		divide_unsigned(lowering, numerator, denominator, quotient, remainder, temp);
	}

	/* Picks the result, giving a signed one its sign: (r ^ s) - s with s all ones for a negative result. */
	if (instruction->op == DRV_GPU_IR_UDIV) {
		emit_vector(lowering, AMDRDNA_GFX10_KIND_VECTOR_ONE, AMDRDNA_GFX10_V_MOV_B32, AMDRDNA_GFX10_V(dst), AMDRDNA_GFX10_V(quotient), 0U, 0U);
	} else if (instruction->op == DRV_GPU_IR_UMOD) {
		emit_vector(lowering, AMDRDNA_GFX10_KIND_VECTOR_ONE, AMDRDNA_GFX10_V_MOV_B32, AMDRDNA_GFX10_V(dst), AMDRDNA_GFX10_V(remainder), 0U, 0U);
	} else {
		/* The sign: of numerator ^ denominator for the quotient, of the numerator for the remainder. */
		sign = temp;
		if (instruction->op == DRV_GPU_IR_IDIV) {
			emit_vector(lowering, AMDRDNA_GFX10_KIND_VECTOR_TWO, AMDRDNA_GFX10_V_XOR_B32, AMDRDNA_GFX10_V(sign), numerator, denominator, 0U);
			emit_vector(lowering, AMDRDNA_GFX10_KIND_VECTOR_TWO, AMDRDNA_GFX10_V_ASHRREV_I32, AMDRDNA_GFX10_V(sign), constant_operand(31U), AMDRDNA_GFX10_V(sign), 0U);
			emit_vector(lowering, AMDRDNA_GFX10_KIND_VECTOR_TWO, AMDRDNA_GFX10_V_XOR_B32, AMDRDNA_GFX10_V(dst), AMDRDNA_GFX10_V(quotient), AMDRDNA_GFX10_V(sign), 0U);
		} else {
			emit_vector(lowering, AMDRDNA_GFX10_KIND_VECTOR_TWO, AMDRDNA_GFX10_V_ASHRREV_I32, AMDRDNA_GFX10_V(sign), constant_operand(31U), numerator, 0U);
			emit_vector(lowering, AMDRDNA_GFX10_KIND_VECTOR_TWO, AMDRDNA_GFX10_V_XOR_B32, AMDRDNA_GFX10_V(dst), AMDRDNA_GFX10_V(remainder), AMDRDNA_GFX10_V(sign), 0U);
		}
		emit_vector(lowering, AMDRDNA_GFX10_KIND_VECTOR_TWO, AMDRDNA_GFX10_V_SUB_NC_U32, AMDRDNA_GFX10_V(dst), AMDRDNA_GFX10_V(dst), AMDRDNA_GFX10_V(sign), 0U);
	}

	give_vgpr(lowering, temp);
	give_vgpr(lowering, remainder);
	give_vgpr(lowering, quotient);

	/* Succeeded: the division is lowered. */
	return 0;
}

/*
 * Divides two unsigned integers: the quotient and the remainder into their
 * VGPRs, temp used as scratch; none of the three is the numerator's or the
 * denominator's.
 *
 * A fixed-point reciprocal of the denominator from the float unit is
 * refined once by one Newton step; the quotient it gives is at most two
 * too small, and two corrections add the rest.
 *
 * Procedure referenced: Mesa 25.0.7 src/compiler/nir/nir_lower_idiv.c
 * (emit_udiv).
 */
static void
divide_unsigned(
	struct gfx10_lowering *lowering,
	unsigned numerator,
	unsigned denominator,
	uint32_t quotient,
	uint32_t remainder,
	uint32_t temp)
{
	unsigned q;
	unsigned r;
	unsigned t;
	uint32_t pass;

	q = AMDRDNA_GFX10_V(quotient);
	r = AMDRDNA_GFX10_V(remainder);
	t = AMDRDNA_GFX10_V(temp);

	/* The estimate e of 2^32 / d, in the quotient's VGPR: the float reciprocal scaled and truncated. */
	emit_vector(lowering, AMDRDNA_GFX10_KIND_VECTOR_ONE, AMDRDNA_GFX10_V_CVT_F32_U32, t, denominator, 0U, 0U);
	emit_vector(lowering, AMDRDNA_GFX10_KIND_VECTOR_ONE, AMDRDNA_GFX10_V_RCP_IFLAG_F32, t, t, 0U, 0U);
	emit_vector_literal(lowering, AMDRDNA_GFX10_KIND_VECTOR_TWO, AMDRDNA_GFX10_V_MUL_F32, t, AMDRDNA_GFX10_LITERAL, t, GFX10_RECIPROCAL_SCALE);
	emit_vector(lowering, AMDRDNA_GFX10_KIND_VECTOR_ONE, AMDRDNA_GFX10_V_CVT_U32_F32, t, t, 0U, 0U);

	/* One Newton step: e += high(e * (-d * e)), the error term kept in the remainder's VGPR. */
	emit_vector(lowering, AMDRDNA_GFX10_KIND_VECTOR_TWO, AMDRDNA_GFX10_V_SUB_NC_U32, q, AMDRDNA_GFX10_ZERO, denominator, 0U);
	emit_vector(lowering, AMDRDNA_GFX10_KIND_VECTOR_WIDE, AMDRDNA_GFX10_V_MUL_LO_U32, q, q, t, 0U);
	emit_vector(lowering, AMDRDNA_GFX10_KIND_VECTOR_WIDE, AMDRDNA_GFX10_V_MUL_HI_U32, q, t, q, 0U);
	emit_vector(lowering, AMDRDNA_GFX10_KIND_VECTOR_TWO, AMDRDNA_GFX10_V_ADD_NC_U32, t, t, q, 0U);

	/* The first quotient high(n * e), and its remainder n - q * d. */
	emit_vector(lowering, AMDRDNA_GFX10_KIND_VECTOR_WIDE, AMDRDNA_GFX10_V_MUL_HI_U32, q, numerator, t, 0U);
	emit_vector(lowering, AMDRDNA_GFX10_KIND_VECTOR_WIDE, AMDRDNA_GFX10_V_MUL_LO_U32, t, q, denominator, 0U);
	emit_vector(lowering, AMDRDNA_GFX10_KIND_VECTOR_TWO, AMDRDNA_GFX10_V_SUB_NC_U32, r, numerator, t, 0U);

	/* Twice: where the remainder is still at least d, the quotient grows by one and the remainder drops by d. */
	for (pass = 0U; pass < 2U; pass++) {
		emit_vector(lowering, AMDRDNA_GFX10_KIND_VECTOR_COMPARE, AMDRDNA_GFX10_V_CMP_GE_U32, AMDRDNA_GFX10_VCC_LO, r, denominator, 0U);
		emit_vector(lowering, AMDRDNA_GFX10_KIND_VECTOR_TWO, AMDRDNA_GFX10_V_ADD_NC_U32, t, constant_operand(1U), q, 0U);
		emit_vector(lowering, AMDRDNA_GFX10_KIND_VECTOR_TWO, AMDRDNA_GFX10_V_CNDMASK_B32, q, q, t, AMDRDNA_GFX10_VCC_LO);
		emit_vector(lowering, AMDRDNA_GFX10_KIND_VECTOR_TWO, AMDRDNA_GFX10_V_SUB_NC_U32, t, r, denominator, 0U);
		emit_vector(lowering, AMDRDNA_GFX10_KIND_VECTOR_TWO, AMDRDNA_GFX10_V_CNDMASK_B32, r, r, t, AMDRDNA_GFX10_VCC_LO);
	}
}

/*
 * Lowers an atomic on a storage buffer word.
 *
 * The buffer atomic writes the old value over its data register, so the
 * data is first copied into dst (or, for a compare-and-swap, into a pair
 * with the compare after it).  The old value is asked for only when
 * something reads it.
 */
static int
lower_atomic(
	struct gfx10_lowering *lowering,
	const struct drv_gpu_shader_ir_inst *instruction,
	uint32_t dst)
{
	const struct drv_gpu_shader_ir_uniform *uniform;
	struct amdrdna_gfx10_inst inst;
	uint32_t slot;
	uint32_t pair;
	uint32_t predicate;
	unsigned opcode;
	int returns;
	int compare_swap;
	int error;

	/* The uniform is a storage buffer of the program. */
	if (instruction->location >= lowering->ir->uniform_count)
		return refuse(lowering, EINVAL, "an atomic names no uniform");
	uniform = &lowering->ir->uniforms[instruction->location];
	if (uniform->kind != DRV_GPU_IR_UNIFORM_STORAGE)
		return refuse(lowering, EINVAL, "an atomic names another kind of uniform");
	slot = lowering->slot[instruction->location];

	/* Picks the buffer atomic by the operation. */
	compare_swap = 0;
	switch (instruction->immediate) {
	case DRV_GPU_IR_ATOMIC_AND:
		opcode = AMDRDNA_GFX10_BUFFER_ATOMIC_AND;
		break;
	case DRV_GPU_IR_ATOMIC_OR:
		opcode = AMDRDNA_GFX10_BUFFER_ATOMIC_OR;
		break;
	case DRV_GPU_IR_ATOMIC_XOR:
		opcode = AMDRDNA_GFX10_BUFFER_ATOMIC_XOR;
		break;
	case DRV_GPU_IR_ATOMIC_XCHG:
		opcode = AMDRDNA_GFX10_BUFFER_ATOMIC_SWAP;
		break;
	case DRV_GPU_IR_ATOMIC_ADD:
		opcode = AMDRDNA_GFX10_BUFFER_ATOMIC_ADD;
		break;
	case DRV_GPU_IR_ATOMIC_SUB:
		opcode = AMDRDNA_GFX10_BUFFER_ATOMIC_SUB;
		break;
	case DRV_GPU_IR_ATOMIC_SMAX:
		opcode = AMDRDNA_GFX10_BUFFER_ATOMIC_SMAX;
		break;
	case DRV_GPU_IR_ATOMIC_SMIN:
		opcode = AMDRDNA_GFX10_BUFFER_ATOMIC_SMIN;
		break;
	case DRV_GPU_IR_ATOMIC_UMAX:
		opcode = AMDRDNA_GFX10_BUFFER_ATOMIC_UMAX;
		break;
	case DRV_GPU_IR_ATOMIC_UMIN:
		opcode = AMDRDNA_GFX10_BUFFER_ATOMIC_UMIN;
		break;
	case DRV_GPU_IR_ATOMIC_CMPXCHG:
		opcode = AMDRDNA_GFX10_BUFFER_ATOMIC_CMPSWAP;
		compare_swap = 1;
		break;
	default:
		return refuse(lowering, EINVAL, "an atomic operation the IR does not have");
	}

	/* The old value is returned only when a later instruction reads it. */
	returns = 0;
	if (lowering->last_use[instruction->dst] > lowering->index + 1U)
		returns = 1;

	/* A compare-and-swap takes its data and compare in a pair of VGPRs. */
	pair = dst;
	if (compare_swap != 0) {
		error = take_vgpr_pair(lowering, &pair);
		if (error != 0)
			return error;
	}

	/* The data (and the compare) copied where the atomic writes the old value. */
	emit_vector(lowering, AMDRDNA_GFX10_KIND_VECTOR_ONE, AMDRDNA_GFX10_V_MOV_B32, AMDRDNA_GFX10_V(pair), value_operand(lowering, instruction->src[1]), 0U, 0U);
	if (compare_swap != 0)
		emit_vector(lowering, AMDRDNA_GFX10_KIND_VECTOR_ONE, AMDRDNA_GFX10_V_MOV_B32, AMDRDNA_GFX10_V(pair + 1U), value_operand(lowering, instruction->src[2]), 0U, 0U);

	/* A predicated atomic runs only for the lanes whose predicate holds. */
	if (instruction->component == DRV_GPU_IR_ATOMIC_PREDICATED) {
		predicate = instruction->src[2];
		if (compare_swap != 0)
			predicate = instruction->src[3];
		guard_begin(lowering, predicate);
	}

	/* The atomic: the byte offset in a VGPR, the descriptor, no scalar offset. */
	kern_memset(&inst, 0, sizeof(inst));
	inst.kind = AMDRDNA_GFX10_KIND_BUFFER;
	inst.opcode = (uint16_t)opcode;
	inst.src[0] = (uint16_t)value_operand(lowering, instruction->src[0]);
	inst.src[1] = (uint16_t)lowering->storage_sgpr[slot];
	inst.src[2] = AMDRDNA_GFX10_ZERO;
	inst.src[3] = (uint16_t)AMDRDNA_GFX10_V(pair);
	inst.flags = AMDRDNA_GFX10_FLAG_OFFSET_ADDRESS;
	if (returns != 0) {
		inst.dst = (uint16_t)AMDRDNA_GFX10_V(pair);
		inst.flags |= AMDRDNA_GFX10_FLAG_GLOBALLY_COHERENT;
	}
	emit(lowering, &inst);

	/* The lanes come back after a predicated atomic. */
	if (instruction->component == DRV_GPU_IR_ATOMIC_PREDICATED)
		guard_end(lowering);

	/* A compare-and-swap's old value moves from the pair to dst. */
	if (compare_swap != 0) {
		if (returns != 0)
			emit_vector(lowering, AMDRDNA_GFX10_KIND_VECTOR_ONE, AMDRDNA_GFX10_V_MOV_B32, AMDRDNA_GFX10_V(dst), AMDRDNA_GFX10_V(pair), 0U, 0U);
		give_vgpr(lowering, pair + 1U);
		give_vgpr(lowering, pair);
	}

	/* Succeeded: the atomic is lowered. */
	return 0;
}

/*
 * Lowers a read, a write or an atomic of the workgroup's shared memory.
 *
 * The shared memory accesses on GFX10 need no M0 (only the per-lane
 * address variants read it).
 *
 * Procedure referenced: Mesa 25.0.7 src/amd/compiler/README-ISA.md
 * ("m0 with LDS instructions on Vega and newer").
 */
static int
lower_shared(
	struct gfx10_lowering *lowering,
	const struct drv_gpu_shader_ir_inst *instruction,
	uint32_t dst)
{
	struct amdrdna_gfx10_inst inst;
	uint32_t predicate;
	int guarded;

	kern_memset(&inst, 0, sizeof(inst));
	inst.kind = AMDRDNA_GFX10_KIND_SHARED;
	inst.src[0] = (uint16_t)value_operand(lowering, instruction->src[0]);
	guarded = 0;
	predicate = 0U;

	/* Picks the access by the operation. */
	if (instruction->op == DRV_GPU_IR_LOAD_SHARED) {
		inst.opcode = AMDRDNA_GFX10_DS_READ_B32;
		inst.dst = (uint16_t)AMDRDNA_GFX10_V(dst);
		if (instruction->component != 0U) {
			guarded = 1;
			predicate = instruction->src[1];
		}
	} else if (instruction->op == DRV_GPU_IR_STORE_SHARED) {
		inst.opcode = AMDRDNA_GFX10_DS_WRITE_B32;
		inst.src[1] = (uint16_t)value_operand(lowering, instruction->src[1]);
		if (instruction->component != 0U) {
			guarded = 1;
			predicate = instruction->src[2];
		}
	} else {
		/* An atomic returns the old value; a compare-and-store takes the compare first. */
		inst.dst = (uint16_t)AMDRDNA_GFX10_V(dst);
		inst.src[1] = (uint16_t)value_operand(lowering, instruction->src[1]);
		switch (instruction->immediate) {
		case DRV_GPU_IR_ATOMIC_AND:
			inst.opcode = AMDRDNA_GFX10_DS_AND_RTN_B32;
			break;
		case DRV_GPU_IR_ATOMIC_OR:
			inst.opcode = AMDRDNA_GFX10_DS_OR_RTN_B32;
			break;
		case DRV_GPU_IR_ATOMIC_XOR:
			inst.opcode = AMDRDNA_GFX10_DS_XOR_RTN_B32;
			break;
		case DRV_GPU_IR_ATOMIC_XCHG:
			inst.opcode = AMDRDNA_GFX10_DS_WRXCHG_RTN_B32;
			break;
		case DRV_GPU_IR_ATOMIC_ADD:
			inst.opcode = AMDRDNA_GFX10_DS_ADD_RTN_U32;
			break;
		case DRV_GPU_IR_ATOMIC_SUB:
			inst.opcode = AMDRDNA_GFX10_DS_SUB_RTN_U32;
			break;
		case DRV_GPU_IR_ATOMIC_SMAX:
			inst.opcode = AMDRDNA_GFX10_DS_MAX_RTN_I32;
			break;
		case DRV_GPU_IR_ATOMIC_SMIN:
			inst.opcode = AMDRDNA_GFX10_DS_MIN_RTN_I32;
			break;
		case DRV_GPU_IR_ATOMIC_UMAX:
			inst.opcode = AMDRDNA_GFX10_DS_MAX_RTN_U32;
			break;
		case DRV_GPU_IR_ATOMIC_UMIN:
			inst.opcode = AMDRDNA_GFX10_DS_MIN_RTN_U32;
			break;
		case DRV_GPU_IR_ATOMIC_CMPXCHG:
			inst.opcode = AMDRDNA_GFX10_DS_CMPST_RTN_B32;
			inst.src[1] = (uint16_t)value_operand(lowering, instruction->src[2]);
			inst.src[2] = (uint16_t)value_operand(lowering, instruction->src[1]);
			break;
		default:
			return refuse(lowering, EINVAL, "an atomic operation the IR does not have");
		}

		/* A predicated atomic runs only where its predicate holds. */
		if (instruction->component == DRV_GPU_IR_ATOMIC_PREDICATED) {
			guarded = 1;
			predicate = instruction->src[2];
			if (instruction->immediate == DRV_GPU_IR_ATOMIC_CMPXCHG)
				predicate = instruction->src[3];
		}
	}

	/* The access, under its guard when it has one. */
	if (guarded != 0)
		guard_begin(lowering, predicate);
	emit(lowering, &inst);
	if (guarded != 0)
		guard_end(lowering);

	/* Succeeded: the access is lowered. */
	return 0;
}

/*
 * Lowers a fence or a workgroup barrier.
 *
 * A fence waits until the accesses of the memory it names are done (the
 * loads of storage go past the caches, so nothing is to be invalidated).
 * A barrier runs its fences, then waits for the other waves.
 */
static void
lower_sync(
	struct gfx10_lowering *lowering,
	const struct drv_gpu_shader_ir_inst *instruction)
{
	struct amdrdna_gfx10_inst inst;

	/* The fence's waits. */
	emit_waits(lowering, instruction->immediate);

	/* A barrier waits for every wave of the workgroup. */
	if (instruction->op == DRV_GPU_IR_BARRIER) {
		kern_memset(&inst, 0, sizeof(inst));
		inst.kind = AMDRDNA_GFX10_KIND_CONTROL;
		inst.opcode = AMDRDNA_GFX10_S_BARRIER;
		emit(lowering, &inst);
	}
}

/* Waits for the memory accesses a set of fences (DRV_GPU_IR_FENCE_*) names. */
static void
emit_waits(
	struct gfx10_lowering *lowering,
	uint32_t fences)
{
	struct amdrdna_gfx10_inst inst;
	uint32_t immediate;

	/* The loads and returning atomics of storage, and the shared memory accesses, by the counter wait. */
	immediate = AMDRDNA_GFX10_WAIT_NONE;
	if ((fences & DRV_GPU_IR_FENCE_GLOBAL) != 0U)
		immediate &= ~AMDRDNA_GFX10_WAIT_VECTOR_MASK;
	if ((fences & DRV_GPU_IR_FENCE_SHARED) != 0U)
		immediate &= ~AMDRDNA_GFX10_WAIT_SCALAR_MASK;
	if (immediate != AMDRDNA_GFX10_WAIT_NONE) {
		kern_memset(&inst, 0, sizeof(inst));
		inst.kind = AMDRDNA_GFX10_KIND_CONTROL;
		inst.opcode = AMDRDNA_GFX10_S_WAITCNT;
		inst.immediate = immediate;
		emit(lowering, &inst);
	}

	/* The stores and the other atomics of storage, by the store counter wait. */
	if ((fences & DRV_GPU_IR_FENCE_GLOBAL) != 0U) {
		kern_memset(&inst, 0, sizeof(inst));
		inst.kind = AMDRDNA_GFX10_KIND_SCALAR_CONSTANT;
		inst.opcode = AMDRDNA_GFX10_S_WAITCNT_VSCNT;
		inst.dst = AMDRDNA_GFX10_NULL;
		emit(lowering, &inst);
	}
}

/* Ends the program, places its waits and writes its code. */
static int
finish(
	struct gfx10_lowering *lowering)
{
	struct amdrdna_gfx10_program *program;
	struct amdrdna_gfx10_inst inst;
	struct amdrdna_gfx10_inst *waited;
	uint32_t waited_count;
	uint32_t failed;
	int error;

	program = lowering->program;

	/* The program ends. */
	kern_memset(&inst, 0, sizeof(inst));
	inst.kind = AMDRDNA_GFX10_KIND_CONTROL;
	inst.opcode = AMDRDNA_GFX10_S_ENDPGM;
	emit(lowering, &inst);
	if (lowering->out_of_memory != 0)
		return ENOMEM;

	/* The waits and the hazard breaks. */
	error = amdrdna_gfx10_place_waits(lowering->list, lowering->count, &waited, &waited_count);
	if (error != 0)
		return error;

	/* The code. */
	failed = 0U;
	error = amdrdna_gfx10_assemble(&waited, &waited_count, &program->code, &program->code_words, &failed);
	kern_free(waited);
	if (error == EINVAL)
		return refuse(lowering, EINVAL, "an instruction could not be encoded");
	if (error != 0)
		return error;

	/* The registers the program uses. */
	program->vgpr_count = lowering->vgpr_highest;
	program->sgpr_count = lowering->sgpr_highest;

	/* Succeeded: the program is complete. */
	return 0;
}

/* Narrows the lane mask to the lanes whose Boolean is true, saving the old mask. */
static void
guard_begin(
	struct gfx10_lowering *lowering,
	uint32_t predicate)
{
	/* The predicate into VCC_LO, then the lanes ANDed with it and the old mask saved. */
	condition_from_boolean(lowering, predicate);
	emit_scalar(lowering,
		    AMDRDNA_GFX10_KIND_SCALAR_ONE,
		    AMDRDNA_GFX10_S_AND_SAVEEXEC_B32,
		    lowering->guard_save,
		    AMDRDNA_GFX10_VCC_LO,
		    0U);
}

/* Restores the lane mask a guard saved. */
static void
guard_end(
	struct gfx10_lowering *lowering)
{
	/* The saved mask comes back. */
	emit_scalar(lowering,
		    AMDRDNA_GFX10_KIND_SCALAR_ONE,
		    AMDRDNA_GFX10_S_MOV_B32,
		    AMDRDNA_GFX10_EXEC_LO,
		    lowering->guard_save,
		    0U);
}

/* Sets VCC_LO for the lanes where a Boolean value is true. */
static void
condition_from_boolean(
	struct gfx10_lowering *lowering,
	uint32_t value)
{
	/* A lane's Boolean is true when it is not zero. */
	emit_vector(lowering,
		    AMDRDNA_GFX10_KIND_VECTOR_COMPARE,
		    AMDRDNA_GFX10_V_CMP_NE_U32,
		    AMDRDNA_GFX10_VCC_LO,
		    AMDRDNA_GFX10_ZERO,
		    value_operand(lowering, value),
		    0U);
}

/* Makes a Boolean value (all ones or zero) of VCC_LO. */
static void
boolean_from_condition(
	struct gfx10_lowering *lowering,
	uint32_t dst)
{
	/* Each lane takes -1 where VCC_LO is set and 0 elsewhere. */
	emit_vector(lowering,
		    AMDRDNA_GFX10_KIND_VECTOR_TWO,
		    AMDRDNA_GFX10_V_CNDMASK_B32,
		    AMDRDNA_GFX10_V(dst),
		    AMDRDNA_GFX10_ZERO,
		    constant_operand(GFX10_TRUE),
		    AMDRDNA_GFX10_VCC_LO);
}

/* Appends an instruction, growing the list when it is full. */
static void
emit(
	struct gfx10_lowering *lowering,
	const struct amdrdna_gfx10_inst *inst)
{
	struct amdrdna_gfx10_inst *grown;
	uint32_t capacity;

	/* Once the list could not grow, nothing more is added. */
	if (lowering->out_of_memory != 0)
		return;

	/* A full list is copied into a larger one. */
	if (lowering->count == lowering->capacity) {
		capacity = lowering->capacity + GFX10_LIST_GROWTH;
		grown = kern_calloc(capacity, sizeof(*grown));
		if (grown == NULL) {
			lowering->out_of_memory = 1;
			return;
		}

		/* The entries so far move over. */
		if (lowering->list != NULL)
			kern_memcpy(grown, lowering->list, lowering->count * sizeof(*grown));
		kern_free(lowering->list);
		lowering->list = grown;
		lowering->capacity = capacity;
	}

	/* Appends the instruction. */
	lowering->list[lowering->count] = *inst;
	lowering->count++;
}

/* Appends a vector ALU instruction. */
static void
emit_vector(
	struct gfx10_lowering *lowering,
	enum amdrdna_gfx10_kind kind,
	unsigned opcode,
	unsigned dst,
	unsigned src0,
	unsigned src1,
	unsigned src2)
{
	struct amdrdna_gfx10_inst inst;

	/* Builds the instruction. */
	kern_memset(&inst, 0, sizeof(inst));
	inst.kind = kind;
	inst.opcode = (uint16_t)opcode;
	inst.dst = (uint16_t)dst;
	inst.src[0] = (uint16_t)src0;
	inst.src[1] = (uint16_t)src1;
	inst.src[2] = (uint16_t)src2;

	/* Places it. */
	emit(lowering, &inst);
}

/* Appends a vector ALU instruction whose sources may name a literal. */
static void
emit_vector_literal(
	struct gfx10_lowering *lowering,
	enum amdrdna_gfx10_kind kind,
	unsigned opcode,
	unsigned dst,
	unsigned src0,
	unsigned src1,
	uint32_t literal)
{
	struct amdrdna_gfx10_inst inst;

	/* Builds the instruction with its literal. */
	kern_memset(&inst, 0, sizeof(inst));
	inst.kind = kind;
	inst.opcode = (uint16_t)opcode;
	inst.dst = (uint16_t)dst;
	inst.src[0] = (uint16_t)src0;
	inst.src[1] = (uint16_t)src1;
	inst.literal = literal;

	/* Places it. */
	emit(lowering, &inst);
}

/* Appends dst = factor * constant + addend, the 24-bit unsigned multiply-add. */
static void
emit_multiply_add(
	struct gfx10_lowering *lowering,
	unsigned dst,
	unsigned factor,
	uint32_t constant,
	unsigned addend)
{
	struct amdrdna_gfx10_inst inst;

	/* Builds the instruction, the constant inline or as the literal. */
	kern_memset(&inst, 0, sizeof(inst));
	inst.kind = AMDRDNA_GFX10_KIND_VECTOR_WIDE;
	inst.opcode = AMDRDNA_GFX10_V_MAD_U32_U24;
	inst.dst = (uint16_t)dst;
	inst.src[0] = (uint16_t)factor;
	inst.src[1] = (uint16_t)constant_operand(constant);
	inst.src[2] = (uint16_t)addend;
	inst.literal = constant;

	/* Places it. */
	emit(lowering, &inst);
}

/* Appends a scalar ALU instruction. */
static void
emit_scalar(
	struct gfx10_lowering *lowering,
	enum amdrdna_gfx10_kind kind,
	unsigned opcode,
	unsigned dst,
	unsigned src0,
	unsigned src1)
{
	struct amdrdna_gfx10_inst inst;

	/* Builds the instruction. */
	kern_memset(&inst, 0, sizeof(inst));
	inst.kind = kind;
	inst.opcode = (uint16_t)opcode;
	inst.dst = (uint16_t)dst;
	inst.src[0] = (uint16_t)src0;
	inst.src[1] = (uint16_t)src1;

	/* Places it. */
	emit(lowering, &inst);
}

/* Appends a scalar load of count words at a byte offset from a base (an address pair or a buffer descriptor). */
static void
emit_scalar_load(
	struct gfx10_lowering *lowering,
	unsigned opcode,
	unsigned dst,
	unsigned base,
	uint32_t offset,
	unsigned count)
{
	struct amdrdna_gfx10_inst inst;

	/* Builds the load. */
	kern_memset(&inst, 0, sizeof(inst));
	inst.kind = AMDRDNA_GFX10_KIND_SCALAR_LOAD;
	inst.opcode = (uint16_t)opcode;
	inst.dst = (uint16_t)dst;
	inst.src[0] = (uint16_t)base;
	inst.immediate = offset;
	inst.count = (uint8_t)count;

	/* Places it. */
	emit(lowering, &inst);
}

/* Appends a label. */
static void
emit_label(
	struct gfx10_lowering *lowering,
	uint32_t label)
{
	struct amdrdna_gfx10_inst inst;

	/* A label is a record of its own kind with its number. */
	kern_memset(&inst, 0, sizeof(inst));
	inst.kind = AMDRDNA_GFX10_KIND_LABEL;
	inst.label = label;

	/* Places it. */
	emit(lowering, &inst);
}

/* Appends a branch to a label. */
static void
emit_branch(
	struct gfx10_lowering *lowering,
	unsigned opcode,
	uint32_t label)
{
	struct amdrdna_gfx10_inst inst;

	/* The distance is resolved when the code is written. */
	kern_memset(&inst, 0, sizeof(inst));
	inst.kind = AMDRDNA_GFX10_KIND_BRANCH;
	inst.opcode = (uint16_t)opcode;
	inst.label = label;

	/* Places it. */
	emit(lowering, &inst);
}

/*
 * Reports the operand code of a 32-bit constant: an inline integer
 * (0 .. 64 and -16 .. -1), an inline float, or the literal (the caller
 * then puts the bits in the instruction's literal).
 */
static unsigned
constant_operand(
	uint32_t bits)
{
	/* The small non-negative integers. */
	if (bits <= 64U)
		return AMDRDNA_GFX10_INT_FIRST + bits;

	/* The small negative integers: -1 is 193, -16 is 208. */
	if (bits >= 0xfffffff0U)
		return AMDRDNA_GFX10_INT_LAST + (0U - bits);

	/* The inline floats. */
	switch (bits) {
	case AMDRDNA_GFX10_BITS_HALF:
		return AMDRDNA_GFX10_HALF;
	case AMDRDNA_GFX10_BITS_HALF | GFX10_SIGN_BIT:
		return AMDRDNA_GFX10_MINUS_HALF;
	case AMDRDNA_GFX10_BITS_ONE:
		return AMDRDNA_GFX10_ONE;
	case AMDRDNA_GFX10_BITS_ONE | GFX10_SIGN_BIT:
		return AMDRDNA_GFX10_MINUS_ONE;
	case AMDRDNA_GFX10_BITS_TWO:
		return AMDRDNA_GFX10_TWO;
	case AMDRDNA_GFX10_BITS_TWO | GFX10_SIGN_BIT:
		return AMDRDNA_GFX10_MINUS_TWO;
	case AMDRDNA_GFX10_BITS_FOUR:
		return AMDRDNA_GFX10_FOUR;
	case AMDRDNA_GFX10_BITS_FOUR | GFX10_SIGN_BIT:
		return AMDRDNA_GFX10_MINUS_FOUR;
	case AMDRDNA_GFX10_BITS_INV_TWO_PI:
		return AMDRDNA_GFX10_INV_TWO_PI;
	default:
		return AMDRDNA_GFX10_LITERAL;
	}
}

/* Reports the operand code of a value's VGPR. */
static unsigned
value_operand(
	const struct gfx10_lowering *lowering,
	uint32_t value)
{
	/* The value's VGPR as an operand. */
	return AMDRDNA_GFX10_V(lowering->vgpr[value]);
}

/* Reports the next scalar temporary, in turn. */
static unsigned
next_scalar_temp(
	struct gfx10_lowering *lowering)
{
	unsigned temp;

	/* Hands the temporaries out in a ring; the waits keep a reused one correct. */
	temp = lowering->scalar_temp[lowering->scalar_temp_next];
	lowering->scalar_temp_next = (lowering->scalar_temp_next + 1U) % GFX10_SCALAR_TEMPS;

	/* Reports the temporary. */
	return temp;
}

/* Takes the lowest free VGPR. */
static int
take_vgpr(
	struct gfx10_lowering *lowering,
	uint32_t *reg)
{
	uint32_t index;

	/* The lowest free one from the first the allocator hands out. */
	for (index = lowering->vgpr_first; index < AMDRDNA_GFX10_VGPR_COUNT; index++) {
		if (lowering->taken[index] == 0U)
			break;
	}

	/* No VGPR is free: the program needs more than there are. */
	if (index == AMDRDNA_GFX10_VGPR_COUNT)
		return refuse(lowering, ENOTSUP, "the program needs more VGPRs than there are");

	/* Takes it, and counts it among those the program uses. */
	lowering->taken[index] = 1U;
	if (index + 1U > lowering->vgpr_highest)
		lowering->vgpr_highest = index + 1U;

	/* Succeeded: the VGPR is the caller's. */
	*reg = index;
	return 0;
}

/* Takes the lowest two consecutive free VGPRs; *reg is the first. */
static int
take_vgpr_pair(
	struct gfx10_lowering *lowering,
	uint32_t *reg)
{
	uint32_t index;

	/* The lowest free one whose next is free too. */
	for (index = lowering->vgpr_first; index + 1U < AMDRDNA_GFX10_VGPR_COUNT; index++) {
		if (lowering->taken[index] == 0U && lowering->taken[index + 1U] == 0U)
			break;
	}

	/* No pair is free. */
	if (index + 1U >= AMDRDNA_GFX10_VGPR_COUNT)
		return refuse(lowering, ENOTSUP, "the program needs more VGPRs than there are");

	/* Takes both. */
	lowering->taken[index] = 1U;
	lowering->taken[index + 1U] = 1U;
	if (index + 2U > lowering->vgpr_highest)
		lowering->vgpr_highest = index + 2U;

	/* Succeeded: the pair is the caller's. */
	*reg = index;
	return 0;
}

/* Gives a VGPR back. */
static void
give_vgpr(
	struct gfx10_lowering *lowering,
	uint32_t reg)
{
	/* The VGPR is free again. */
	lowering->taken[reg] = 0U;
}

/* Gives back the VGPRs of the values no later instruction reads. */
static void
retire(
	struct gfx10_lowering *lowering,
	const struct drv_gpu_shader_ir_inst *instruction)
{
	uint32_t sources[GFX10_MAX_SOURCES];
	uint32_t source;
	uint32_t value;
	int count;
	int defines;

	/* Each source read here for the last time. */
	count = ir_sources(instruction, sources);
	for (source = 0U; count > 0 && source < (uint32_t)count; source++) {
		value = sources[source];
		if (lowering->last_use[value] != lowering->index + 1U)
			continue;
		if (lowering->vgpr[value] == GFX10_NO_REG)
			continue;
		give_vgpr(lowering, lowering->vgpr[value]);
		lowering->vgpr[value] = GFX10_NO_REG;
	}

	/* A destination no instruction reads goes back at once. */
	defines = ir_defines(instruction);
	if (defines != 0) {
		value = instruction->dst;
		if (lowering->last_use[value] <= lowering->index + 1U && lowering->vgpr[value] != GFX10_NO_REG) {
			give_vgpr(lowering, lowering->vgpr[value]);
			lowering->vgpr[value] = GFX10_NO_REG;
		}
	}
}
