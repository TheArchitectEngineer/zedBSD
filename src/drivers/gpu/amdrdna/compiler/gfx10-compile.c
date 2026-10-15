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
 * The VGPRs a vertex program's vertex number and instance number arrive in
 * (each without its base), with VGPR_COMP_CNT 3 so that v0 .. v3 are
 * delivered; and the SGPR a pixel program's primitive mask arrives in,
 * after its two user SGPRs.
 *
 * Values referenced: Mesa 25.0.7 src/amd/vulkan/radv_shader_args.c
 * (declare_vs_input_vgprs, the legacy VS on GFX10: vertex_id, then
 * instance_id as the fourth, lines 152-183; declare_ps_input_vgprs and the
 * prim_mask SGPR, line 855).
 */
#define GFX10_VS_VERTEX_ID		0U
#define GFX10_VS_INSTANCE_ID		3U
#define GFX10_VS_INPUT_VGPRS		4U
#define GFX10_PS_PRIM_MASK		2U

/*
 * The bits of the pixel input enable this compiler uses, and the VGPRs
 * each delivers in this order: the perspective barycentrics at the pixel
 * centre (2), the linear ones (2), the position's x, y, z, w (1 each) and
 * the face (1).  The hardware packs the enabled inputs from v0 in the
 * order of the bits.
 *
 * Values referenced: Mesa 25.0.7 src/amd/registers/gfx10.json
 * (SPI_PS_INPUT_ENA: PERSP_CENTER_ENA bit 1, LINEAR_CENTER_ENA bit 5,
 * POS_X_FLOAT_ENA .. POS_W_FLOAT_ENA bits 8 .. 11, FRONT_FACE_ENA bit 12)
 * and src/amd/vulkan/radv_shader_args.c (declare_ps_input_vgprs: the
 * VGPRs in that order, lines 265-276).
 */
#define GFX10_PS_PERSP_CENTER		(1U << 1)
#define GFX10_PS_LINEAR_CENTER		(1U << 5)
#define GFX10_PS_POSITION		(0xfU << 8)
#define GFX10_PS_FRONT_FACE		(1U << 12)

/* The colour targets a pixel program writes. */
#define GFX10_COLOR_TARGETS		8U

/* A value whose operand is its VGPR (it has no SGPR or inline constant of its own). */
#define GFX10_NO_OPERAND		0xffffU

/*
 * The most chunks of 16 uniform bytes (push constants or uniform buffer
 * words) loaded into SGPRs at the start, and the scratch VGPRs one IR
 * instruction may take for operands that must be VGPRs.
 */
#define GFX10_UNIFORM_CHUNKS		32U
#define GFX10_SCRATCH_VGPRS		8U

/*
 * The chunks of 16 uniform bytes kept, in turn, for the words that got no
 * SGPR of their own; the cache is forgotten at each loop's start and end,
 * where the code is reached from two places.
 */
#define GFX10_CHUNK_CACHE		4U

/*
 * The most distinct scalar values (SGPRs, the literal) one vector ALU
 * instruction may read on GFX10.
 *
 * Values referenced: Mesa 25.0.7 src/amd/compiler/aco_validate.cpp
 * (validate_ir, the VALU check: const_bus_limit 2 from GFX10 but for the
 * 64-bit shifts, lines 472-480).
 */
#define GFX10_CONSTANT_BUS_LIMIT	2U

/* The SGPRs of an image descriptor and of a sampler, and the byte offset of the sampler after the image descriptor in the table. */
#define GFX10_IMAGE_SGPRS		8U
#define GFX10_SAMPLER_SGPRS		4U

/*
 * The most address VGPRs a sample takes (an offset, a bias, u, v and a
 * level), and the bits of one channel of a packed texel offset: 6 bits,
 * the next channel 8 bits up.
 *
 * Values referenced: Mesa 25.0.7 src/amd/compiler/aco_instruction_selection.cpp
 * (visit_tex: each constant offset & 0x3F shifted by 8 * i, lines
 * 9150-9160).
 */
#define GFX10_SAMPLE_ADDRESSES		4U
#define GFX10_OFFSET_CHANNEL_MASK	0x3fU
#define GFX10_OFFSET_CHANNEL_SHIFT	8U

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

	/*
	 * Each value's operand when it is not a VGPR: an inline constant, or
	 * the SGPR a uniform word was loaded into at the start
	 * (GFX10_NO_OPERAND otherwise); and whether a MOVE writes the value,
	 * which then always lives in a VGPR.
	 */
	uint16_t *operand_code;
	uint8_t *moved;

	/*
	 * Each value stored to an output and read by nothing else, where the
	 * output is stored only once: the output's register, which the value
	 * is defined into so that the store needs no copy (GFX10_NO_REG for
	 * the others).  A sample's first channel stands for its four when all
	 * four go to one output in order.
	 */
	uint16_t *output_home;

	/*
	 * The chunks of 16 uniform bytes loaded at the start: the uniform
	 * buffer's slot (or GFX10_NO_SLOT for the push constants), the byte
	 * offset, and the first of the four SGPRs.
	 */
	uint32_t chunk_slot[GFX10_UNIFORM_CHUNKS];
	uint32_t chunk_offset[GFX10_UNIFORM_CHUNKS];
	uint32_t chunk_sgpr[GFX10_UNIFORM_CHUNKS];
	uint32_t chunk_count;

	/* The cache of uniform chunks: its first SGPR, each entry's buffer and offset and whether it holds one, the next to replace. */
	uint32_t cache_sgpr;
	uint32_t cache_slot[GFX10_CHUNK_CACHE];
	uint32_t cache_offset[GFX10_CHUNK_CACHE];
	uint8_t cache_valid[GFX10_CHUNK_CACHE];
	uint32_t cache_next;

	/* The scratch VGPRs taken for the IR instruction being lowered, given back after it. */
	uint32_t scratch[GFX10_SCRATCH_VGPRS];
	uint32_t scratch_count;

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

	/*
	 * A vertex program: the VGPRs of the vertex and instance numbers (with
	 * their bases), of each fetched location's four channels, of the
	 * position, the point size and each parameter's four channels; the
	 * first SGPR of each fetched location's descriptor.
	 */
	uint32_t vertex_index_vgpr;
	uint32_t instance_index_vgpr;
	uint32_t input_vgpr[AMDRDNA_GFX10_LOCATIONS];
	uint32_t vertex_sgpr[AMDRDNA_GFX10_LOCATIONS];
	uint32_t position_vgpr;
	uint32_t point_size_vgpr;
	uint32_t parameter_vgpr[AMDRDNA_GFX10_LOCATIONS];

	/*
	 * A pixel program: the first VGPR of the perspective and the linear
	 * barycentrics, of the position and of the face; the first VGPR of
	 * each colour's four channels; the SGPR of the lanes still alive, and
	 * the label of the early end a discard of every lane takes.
	 */
	uint32_t perspective_vgpr;
	uint32_t screen_linear_vgpr;
	uint32_t position_input_vgpr;
	uint32_t face_vgpr;
	uint32_t color_vgpr[GFX10_COLOR_TARGETS];
	uint32_t live_sgpr;
	uint32_t early_end_label;

	/*
	 * A pixel program that samples with derivatives runs whole quads:
	 * the helper lanes run with the alive ones, and only the alive lanes
	 * are exported.
	 */
	int whole_quads;

	/* The first SGPR of each sampled image's descriptor and of its sampler. */
	uint32_t image_sgpr[AMDRDNA_GFX10_TEXTURE_MAX];
	uint32_t sampler_sgpr[AMDRDNA_GFX10_TEXTURE_MAX];

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
static int prepare_graphics(struct gfx10_lowering *lowering);
static const struct drv_gpu_shader_ir_io *find_input(const struct drv_gpu_shader_ir *ir, uint32_t location);
static int plan_registers(struct gfx10_lowering *lowering);
static uint32_t reserve_vgprs(struct gfx10_lowering *lowering, uint32_t count);
static void prologue_vertex(struct gfx10_lowering *lowering);
static int lower_input(struct gfx10_lowering *lowering, const struct drv_gpu_shader_ir_inst *instruction, uint32_t dst);
static int lower_output(struct gfx10_lowering *lowering, const struct drv_gpu_shader_ir_inst *instruction);
static void lower_kill(struct gfx10_lowering *lowering, const struct drv_gpu_shader_ir_inst *instruction);
static int lower_sample(struct gfx10_lowering *lowering, const struct drv_gpu_shader_ir_inst *instruction, uint32_t dst);
static void restrict_to_alive(struct gfx10_lowering *lowering);
static void finish_exports(struct gfx10_lowering *lowering);
static void emit_export(struct gfx10_lowering *lowering, uint32_t target, uint32_t first, uint32_t mask, uint32_t flags);
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
static unsigned vgpr_operand(struct gfx10_lowering *lowering, uint32_t value);
static unsigned scratch_copy(struct gfx10_lowering *lowering, unsigned code);
static void give_scratch(struct gfx10_lowering *lowering);
static void legalize_scalars(struct gfx10_lowering *lowering, struct amdrdna_gfx10_inst *inst);
static unsigned vector_reads(const struct amdrdna_gfx10_inst *inst);
static void plan_uniforms(struct gfx10_lowering *lowering, uint32_t *sgpr);
static int has_operand(struct gfx10_lowering *lowering, const struct drv_gpu_shader_ir_inst *instruction);
static int plan_output_homes(struct gfx10_lowering *lowering);
static uint32_t output_register(const struct gfx10_lowering *lowering, const struct drv_gpu_shader_ir_inst *instruction);
static unsigned next_scalar_temp(struct gfx10_lowering *lowering);
static int take_vgpr(struct gfx10_lowering *lowering, uint32_t *reg);
static int take_vgpr_pair(struct gfx10_lowering *lowering, uint32_t *reg);
static int take_vgpr_block(struct gfx10_lowering *lowering, uint32_t count, uint32_t *reg);
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
	uint32_t block;
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

		/* A constant or a uniform word with an operand of its own needs no instruction. */
		defines = has_operand(&lowering, instruction);
		if (defines != 0)
			continue;

		/* The destination's VGPR, a value redefined by a MOVE keeping its own; a sample's four in a row. */
		dst = GFX10_NO_REG;
		defines = ir_defines(instruction);
		if (defines == 4) {
			/* A sample's channels written straight into an output's registers, or a block of their own. */
			if (lowering.output_home[instruction->dst] != GFX10_NO_REG) {
				dst = lowering.output_home[instruction->dst];
			} else {
				error = take_vgpr_block(&lowering, 4U, &dst);
				if (error != 0)
					break;
			}
			for (block = 0U; block < 4U; block++)
				lowering.vgpr[instruction->dst + block] = (uint16_t)(dst + block);
		} else if (defines != 0) {
			if (lowering.vgpr[instruction->dst] == GFX10_NO_REG && lowering.output_home[instruction->dst] != GFX10_NO_REG) {
				/* A value only stored to an output lives in the output's register. */
				lowering.vgpr[instruction->dst] = lowering.output_home[instruction->dst];
			} else if (lowering.vgpr[instruction->dst] == GFX10_NO_REG) {
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

		/* The scratch VGPRs, and the VGPRs of values no later instruction reads, go back. */
		give_scratch(&lowering);
		retire(&lowering, instruction);
	}

	/* The end of the program, its waits and its code. */
	if (error == 0)
		error = finish(&lowering);

	/* The per-value and per-uniform arrays and the list are only the compilation's. */
	kern_free(lowering.vgpr);
	kern_free(lowering.last_use);
	kern_free(lowering.operand_code);
	kern_free(lowering.moved);
	kern_free(lowering.output_home);
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
	case DRV_GPU_IR_LOAD_INPUT:
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
	case DRV_GPU_IR_STORE_OUTPUT:
	case DRV_GPU_IR_KILL:
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
	case DRV_GPU_IR_SAMPLE:
		count = 2U;
		break;
	case DRV_GPU_IR_SAMPLE_BIAS:
	case DRV_GPU_IR_SAMPLE_LOD:
		count = 3U;
		break;
	case DRV_GPU_IR_TEXTURE:
		/* The message's parameters are src[1] consecutive values from src[0]. */
		if (instruction->src[1] == 0U || instruction->src[1] > DRV_GPU_IR_TEXTURE_MAX_PARAMS)
			return -1;
		for (index = 0U; index < instruction->src[1]; index++)
			sources[index] = instruction->src[0] + index;
		return (int)instruction->src[1];
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

/* Reports how many values from its destination an IR instruction defines: 0, 1, or a sample's 4. */
static int
ir_defines(
	const struct drv_gpu_shader_ir_inst *instruction)
{
	/* The operations that define nothing, and the samples. */
	switch (instruction->op) {
	case DRV_GPU_IR_SAMPLE:
	case DRV_GPU_IR_SAMPLE_BIAS:
	case DRV_GPU_IR_SAMPLE_LOD:
	case DRV_GPU_IR_TEXTURE:
		return 4;
	case DRV_GPU_IR_NOP:
	case DRV_GPU_IR_SKIP_BEGIN:
	case DRV_GPU_IR_SKIP_END:
	case DRV_GPU_IR_STORE_STORAGE:
	case DRV_GPU_IR_STORE_SHARED:
	case DRV_GPU_IR_BARRIER:
	case DRV_GPU_IR_FENCE:
	case DRV_GPU_IR_LOOP_BEGIN:
	case DRV_GPU_IR_LOOP_END:
	case DRV_GPU_IR_STORE_OUTPUT:
	case DRV_GPU_IR_KILL:
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

	/* Compute, vertex and pixel programs are compiled. */
	if (ir->stage != DRV_GPU_STAGE_COMPUTE &&
	    ir->stage != DRV_GPU_STAGE_VERTEX &&
	    ir->stage != DRV_GPU_STAGE_FRAGMENT)
		return refuse(lowering, ENOTSUP, "only compute, vertex and fragment shaders are compiled");

	/* The per-value arrays. */
	lowering->vgpr = kern_calloc(ir->value_count + 1U, sizeof(*lowering->vgpr));
	if (lowering->vgpr == NULL)
		return ENOMEM;

	lowering->last_use = kern_calloc(ir->value_count + 1U, sizeof(*lowering->last_use));
	if (lowering->last_use == NULL)
		return ENOMEM;

	lowering->operand_code = kern_calloc(ir->value_count + 1U, sizeof(*lowering->operand_code));
	if (lowering->operand_code == NULL)
		return ENOMEM;

	lowering->moved = kern_calloc(ir->value_count + 1U, sizeof(*lowering->moved));
	if (lowering->moved == NULL)
		return ENOMEM;

	lowering->output_home = kern_calloc(ir->value_count + 1U, sizeof(*lowering->output_home));
	if (lowering->output_home == NULL)
		return ENOMEM;

	/* No value has a VGPR or an operand of its own yet. */
	for (index = 0U; index <= ir->value_count; index++) {
		lowering->vgpr[index] = GFX10_NO_REG;
		lowering->operand_code[index] = GFX10_NO_OPERAND;
		lowering->output_home[index] = GFX10_NO_REG;
	}

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
		if (defines != 0 && (instruction->dst >= ir->value_count || ir->value_count - instruction->dst < (uint32_t)defines))
			return refuse(lowering, EINVAL, "a destination is not a value of the program");

		/* A MOVE's destination always lives in a VGPR. */
		if (instruction->op == DRV_GPU_IR_MOVE)
			lowering->moved[instruction->dst] = 1U;

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

	/* A graphics program's inputs and outputs. */
	error = prepare_graphics(lowering);
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
		for (inner = 0U; inner < (uint32_t)defines; inner++) {
			if (first_definition[instruction->dst + inner] == GFX10_NOT_DEFINED)
				first_definition[instruction->dst + inner] = index;
		}
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
		if (program->storage_used == AMDRDNA_GFX10_STORAGE_MAX)
			return refuse(lowering, ENOTSUP, "more storage buffers than the table holds");

		/* The buffer's descriptor takes the next 16 bytes. */
		lowering->slot[index] = program->storage_used;
		program->storage[program->storage_used].set = uniform->set;
		program->storage[program->storage_used].binding = uniform->binding;
		program->storage[program->storage_used].table_offset = table;
		program->storage[program->storage_used].size = 0U;
		program->storage_used++;
		table += AMDRDNA_GFX10_BUFFER_DESCRIPTOR_BYTES;
	}

	/* The uniform buffers in order. */
	for (index = 0U; index < ir->uniform_count; index++) {
		uniform = &ir->uniforms[index];
		if (uniform->kind != DRV_GPU_IR_UNIFORM_BLOCK)
			continue;

		/* The table holds a fixed number of uniform buffers. */
		if (program->uniform_used == AMDRDNA_GFX10_UNIFORM_MAX)
			return refuse(lowering, ENOTSUP, "more uniform buffers than the table holds");

		/* The buffer's descriptor takes the next 16 bytes. */
		lowering->slot[index] = program->uniform_used;
		program->uniforms[program->uniform_used].set = uniform->set;
		program->uniforms[program->uniform_used].binding = uniform->binding;
		program->uniforms[program->uniform_used].table_offset = table;
		program->uniforms[program->uniform_used].size = uniform->offset + uniform->size;
		program->uniform_used++;
		table += AMDRDNA_GFX10_BUFFER_DESCRIPTOR_BYTES;
	}

	/* The sampled images in order: the image descriptor, then the sampler. */
	for (index = 0U; index < ir->uniform_count; index++) {
		uniform = &ir->uniforms[index];
		if (uniform->kind != DRV_GPU_IR_UNIFORM_SAMPLED_IMAGE)
			continue;

		/* The table holds a fixed number of sampled images. */
		if (program->texture_used == AMDRDNA_GFX10_TEXTURE_MAX)
			return refuse(lowering, ENOTSUP, "more sampled images than the table holds");

		/* The two descriptors take the next 48 bytes. */
		lowering->slot[index] = program->texture_used;
		program->textures[program->texture_used].set = uniform->set;
		program->textures[program->texture_used].binding = uniform->binding;
		program->textures[program->texture_used].table_offset = table;
		program->textures[program->texture_used].size = 0U;
		program->texture_used++;
		table += AMDRDNA_GFX10_IMAGE_DESCRIPTOR_BYTES + AMDRDNA_GFX10_SAMPLER_DESCRIPTOR_BYTES;
	}

	/* A vertex program's vertex buffer descriptors, one per location up to the highest fetched. */
	if (lowering->ir->stage == DRV_GPU_STAGE_VERTEX && program->fetched_locations != 0U) {
		program->vertex_table_offset = table;
		for (index = 0U; index < AMDRDNA_GFX10_LOCATIONS; index++) {
			if ((program->fetched_locations >> index) != 0U)
				table += AMDRDNA_GFX10_BUFFER_DESCRIPTOR_BYTES;
		}
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
	int error;

	program = lowering->program;
	error = 0;

	/* The inputs the hardware delivers, by the stage. */
	if (lowering->ir->stage == DRV_GPU_STAGE_COMPUTE) {
		/* The table address and the workgroup id in s0 .. s4, the local ids in v0 .. v2. */
		program->user_sgprs = AMDRDNA_GFX10_ABI_USER_SGPRS;
		lowering->vgpr_first = GFX10_CS_INPUT_VGPRS;
	} else if (lowering->ir->stage == DRV_GPU_STAGE_VERTEX) {
		/* The table, the base vertex, the first instance and the instance mask in s0 .. s4; v0 .. v3. */
		program->user_sgprs = AMDRDNA_GFX10_ABI_USER_SGPRS_VS;
		lowering->vgpr_first = GFX10_VS_INPUT_VGPRS;
	} else {
		/* The table in s0 .. s1, the primitive mask in s2; the enabled inputs packed from v0. */
		program->user_sgprs = AMDRDNA_GFX10_ABI_USER_SGPRS;
		lowering->vgpr_first = 0U;
		lowering->perspective_vgpr = reserve_vgprs(lowering, 2U);
		if ((program->pixel_inputs & GFX10_PS_LINEAR_CENTER) != 0U)
			lowering->screen_linear_vgpr = reserve_vgprs(lowering, 2U);
		if ((program->pixel_inputs & GFX10_PS_POSITION) != 0U)
			lowering->position_input_vgpr = reserve_vgprs(lowering, 4U);
		if ((program->pixel_inputs & GFX10_PS_FRONT_FACE) != 0U)
			lowering->face_vgpr = reserve_vgprs(lowering, 1U);
	}
	lowering->vgpr_highest = lowering->vgpr_first;

	/* A vertex program keeps its indices, its fetched locations, its position, point size and parameters. */
	if (lowering->ir->stage == DRV_GPU_STAGE_VERTEX) {
		lowering->vertex_index_vgpr = reserve_vgprs(lowering, 1U);
		lowering->instance_index_vgpr = reserve_vgprs(lowering, 1U);
		for (index = 0U; index < AMDRDNA_GFX10_LOCATIONS; index++) {
			if ((program->fetched_locations & (1U << index)) != 0U)
				lowering->input_vgpr[index] = reserve_vgprs(lowering, 4U);
			if ((program->parameter_outputs & (1U << index)) != 0U)
				lowering->parameter_vgpr[index] = reserve_vgprs(lowering, 4U);
		}
		lowering->position_vgpr = reserve_vgprs(lowering, 4U);
		if (program->writes_point_size != 0U)
			lowering->point_size_vgpr = reserve_vgprs(lowering, 1U);
	}

	/* A pixel program keeps the four channels of each colour it writes. */
	if (lowering->ir->stage == DRV_GPU_STAGE_FRAGMENT) {
		for (index = 0U; index < GFX10_COLOR_TARGETS; index++) {
			if ((program->color_outputs & (1U << index)) != 0U)
				lowering->color_vgpr[index] = reserve_vgprs(lowering, 4U);
		}
	}

	/* The descriptors, four SGPRs each from a multiple of four. */
	sgpr = GFX10_SGPR_FIRST_FREE;
	for (index = 0U; index < program->storage_used; index++) {
		lowering->storage_sgpr[index] = sgpr;
		sgpr += 4U;
	}
	for (index = 0U; index < program->uniform_used; index++) {
		lowering->uniform_sgpr[index] = sgpr;
		sgpr += 4U;
	}
	for (index = 0U; index < AMDRDNA_GFX10_LOCATIONS; index++) {
		if ((program->fetched_locations & (1U << index)) != 0U) {
			lowering->vertex_sgpr[index] = sgpr;
			sgpr += 4U;
		}
	}

	for (index = 0U; index < program->texture_used; index++) {
		lowering->image_sgpr[index] = sgpr;
		sgpr += GFX10_IMAGE_SGPRS;
		lowering->sampler_sgpr[index] = sgpr;
		sgpr += GFX10_SAMPLER_SGPRS;
	}

	/* A pixel program that discards or runs whole quads keeps the lanes still alive. */
	if (program->discards != 0U || lowering->whole_quads != 0) {
		lowering->live_sgpr = sgpr;
		sgpr++;
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

	/* The uniform words loaded once at the start, as many as the SGPRs left hold, and the cache for the others. */
	plan_uniforms(lowering, &sgpr);
	if (sgpr > AMDRDNA_GFX10_SGPR_COUNT)
		return refuse(lowering, ENOTSUP, "the descriptors and the loops leave no SGPRs for the uniform words");

	/* The values defined straight into their output's registers. */
	error = plan_output_homes(lowering);
	if (error != 0)
		return error;

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
	for (index = 0U; index < program->storage_used; index++) {
		emit_scalar_load(lowering,
				 AMDRDNA_GFX10_S_LOAD_DWORDX4,
				 lowering->storage_sgpr[index],
				 AMDRDNA_GFX10_ABI_TABLE,
				 program->storage[index].table_offset,
				 4U);
	}

	/* Each uniform buffer's descriptor. */
	for (index = 0U; index < program->uniform_used; index++) {
		emit_scalar_load(lowering,
				 AMDRDNA_GFX10_S_LOAD_DWORDX4,
				 lowering->uniform_sgpr[index],
				 AMDRDNA_GFX10_ABI_TABLE,
				 program->uniforms[index].table_offset,
				 4U);
	}

	/* The uniform words used, 16 bytes at a time: the push constants from the table, the others through their buffer. */
	for (index = 0U; index < lowering->chunk_count; index++) {
		if (lowering->chunk_slot[index] == GFX10_NO_SLOT) {
			emit_scalar_load(lowering,
					 AMDRDNA_GFX10_S_LOAD_DWORDX4,
					 lowering->chunk_sgpr[index],
					 AMDRDNA_GFX10_ABI_TABLE,
					 lowering->chunk_offset[index],
					 4U);
		} else {
			emit_scalar_load(lowering,
					 AMDRDNA_GFX10_S_BUFFER_LOAD_DWORDX4,
					 lowering->chunk_sgpr[index],
					 lowering->uniform_sgpr[lowering->chunk_slot[index]],
					 lowering->chunk_offset[index],
					 4U);
		}
	}

	/* Each sampled image's descriptor and sampler. */
	for (index = 0U; index < program->texture_used; index++) {
		emit_scalar_load(lowering,
				 AMDRDNA_GFX10_S_LOAD_DWORDX8,
				 lowering->image_sgpr[index],
				 AMDRDNA_GFX10_ABI_TABLE,
				 program->textures[index].table_offset,
				 GFX10_IMAGE_SGPRS);
		emit_scalar_load(lowering,
				 AMDRDNA_GFX10_S_LOAD_DWORDX4,
				 lowering->sampler_sgpr[index],
				 AMDRDNA_GFX10_ABI_TABLE,
				 program->textures[index].table_offset + AMDRDNA_GFX10_IMAGE_DESCRIPTOR_BYTES,
				 GFX10_SAMPLER_SGPRS);
	}

	/* A vertex program fetches its inputs; a pixel program points M0 at its primitive's attributes. */
	if (lowering->ir->stage == DRV_GPU_STAGE_VERTEX)
		prologue_vertex(lowering);
	if (lowering->ir->stage == DRV_GPU_STAGE_FRAGMENT) {
		emit_scalar(lowering,
			    AMDRDNA_GFX10_KIND_SCALAR_ONE,
			    AMDRDNA_GFX10_S_MOV_B32,
			    AMDRDNA_GFX10_M0,
			    AMDRDNA_GFX10_S(GFX10_PS_PRIM_MASK),
			    0U);

		/* The lanes alive at the start are those the wave runs. */
		if (program->discards != 0U || lowering->whole_quads != 0) {
			emit_scalar(lowering,
				    AMDRDNA_GFX10_KIND_SCALAR_ONE,
				    AMDRDNA_GFX10_S_MOV_B32,
				    lowering->live_sgpr,
				    AMDRDNA_GFX10_EXEC_LO,
				    0U);
		}

		/* A discard may end the wave early. */
		if (program->discards != 0U) {
			lowering->early_end_label = lowering->next_label;
			lowering->next_label++;
		}

		/* Whole quads: every quad with a lane alive runs all four, so the derivatives have their neighbours. */
		if (lowering->whole_quads != 0) {
			emit_scalar(lowering,
				    AMDRDNA_GFX10_KIND_SCALAR_ONE,
				    AMDRDNA_GFX10_S_WQM_B32,
				    AMDRDNA_GFX10_EXEC_LO,
				    AMDRDNA_GFX10_EXEC_LO,
				    0U);
		}
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
	case DRV_GPU_IR_LOAD_INPUT:
		error = lower_input(lowering, instruction, dst);
		break;
	case DRV_GPU_IR_STORE_OUTPUT:
		error = lower_output(lowering, instruction);
		break;
	case DRV_GPU_IR_KILL:
		lower_kill(lowering, instruction);
		error = 0;
		break;
	case DRV_GPU_IR_SAMPLE:
	case DRV_GPU_IR_SAMPLE_BIAS:
	case DRV_GPU_IR_SAMPLE_LOD:
	case DRV_GPU_IR_TEXTURE:
		error = lower_sample(lowering, instruction, dst);
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
	uint32_t slot;
	uint32_t offset;
	uint32_t entry;
	uint32_t found;
	uint32_t base;

	/* The buffer: the push constants, or a uniform buffer of the program. */
	slot = GFX10_NO_SLOT;
	if (instruction->op == DRV_GPU_IR_LOAD_PUSH) {
		/* The word lies inside the push constants. */
		if (instruction->immediate + 4U > lowering->ir->push_bytes)
			return refuse(lowering, EINVAL, "a push constant read past the push constants");
	} else {
		if (instruction->location >= lowering->ir->uniform_count)
			return refuse(lowering, EINVAL, "a uniform buffer read names no uniform");
		uniform = &lowering->ir->uniforms[instruction->location];
		if (uniform->kind != DRV_GPU_IR_UNIFORM_BLOCK)
			return refuse(lowering, EINVAL, "a uniform buffer read names another kind of uniform");
		slot = lowering->slot[instruction->location];
	}
	offset = instruction->immediate & ~15U;

	/* The 16 bytes around the word, from the cache of chunks or loaded into its next entry. */
	found = GFX10_NO_SLOT;
	for (entry = 0U; entry < GFX10_CHUNK_CACHE; entry++) {
		if (lowering->cache_valid[entry] != 0U &&
		    lowering->cache_slot[entry] == slot &&
		    lowering->cache_offset[entry] == offset)
			found = entry;
	}
	if (found == GFX10_NO_SLOT) {
		found = lowering->cache_next;
		lowering->cache_next = (lowering->cache_next + 1U) % GFX10_CHUNK_CACHE;
		base = lowering->cache_sgpr + 4U * found;
		if (slot == GFX10_NO_SLOT) {
			emit_scalar_load(lowering, AMDRDNA_GFX10_S_LOAD_DWORDX4, base, AMDRDNA_GFX10_ABI_TABLE, offset, 4U);
		} else {
			emit_scalar_load(lowering,
					 AMDRDNA_GFX10_S_BUFFER_LOAD_DWORDX4,
					 base,
					 lowering->uniform_sgpr[slot],
					 offset,
					 4U);
		}
		lowering->cache_valid[found] = 1U;
		lowering->cache_slot[found] = slot;
		lowering->cache_offset[found] = offset;
	}

	/* Every lane gets the word. */
	emit_vector(lowering,
		    AMDRDNA_GFX10_KIND_VECTOR_ONE,
		    AMDRDNA_GFX10_V_MOV_B32,
		    AMDRDNA_GFX10_V(dst),
		    AMDRDNA_GFX10_S(lowering->cache_sgpr + 4U * found + (instruction->immediate & 15U) / 4U),
		    0U,
		    0U);

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
	inst.src[0] = (uint16_t)vgpr_operand(lowering, instruction->src[0]);
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
		inst.src[3] = (uint16_t)vgpr_operand(lowering, instruction->src[1]);
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

	/* The code after a loop's start or end is reached from two places: the cached chunks are forgotten. */
	kern_memset(lowering->cache_valid, 0, sizeof(lowering->cache_valid));

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

	/* Except those a discard inside the loop killed. */
	if (lowering->program->discards != 0U)
		restrict_to_alive(lowering);

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
	inst.src[0] = (uint16_t)vgpr_operand(lowering, instruction->src[0]);
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
	inst.src[0] = (uint16_t)vgpr_operand(lowering, instruction->src[0]);
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
		inst.src[1] = (uint16_t)vgpr_operand(lowering, instruction->src[1]);
		if (instruction->component != 0U) {
			guarded = 1;
			predicate = instruction->src[2];
		}
	} else {
		/* An atomic returns the old value; a compare-and-store takes the compare first. */
		inst.dst = (uint16_t)AMDRDNA_GFX10_V(dst);
		inst.src[1] = (uint16_t)vgpr_operand(lowering, instruction->src[1]);
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
			inst.src[1] = (uint16_t)vgpr_operand(lowering, instruction->src[2]);
			inst.src[2] = (uint16_t)vgpr_operand(lowering, instruction->src[1]);
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

/*
 * Finds what a graphics program reads and writes: a vertex program's
 * fetched locations, parameters and point size; a pixel program's
 * attributes (flat or not), position, face, colours and discards.  It
 * decides the pixel inputs the hardware delivers.
 */
static int
prepare_graphics(
	struct gfx10_lowering *lowering)
{
	const struct drv_gpu_shader_ir *ir;
	const struct drv_gpu_shader_ir_inst *instruction;
	const struct drv_gpu_shader_ir_io *input;
	struct amdrdna_gfx10_program *program;
	uint32_t index;
	uint32_t location;
	int uses_linear;

	ir = lowering->ir;
	program = lowering->program;
	uses_linear = 0;

	/* A compute program has none of these. */
	if (ir->stage == DRV_GPU_STAGE_COMPUTE)
		return 0;

	/* Each input read and output written. */
	for (index = 0U; index < ir->instruction_count; index++) {
		instruction = &ir->instructions[index];
		lowering->index = index;
		location = instruction->location;

		/* A discard, in a pixel program only. */
		if (instruction->op == DRV_GPU_IR_KILL) {
			if (ir->stage != DRV_GPU_STAGE_FRAGMENT)
				return refuse(lowering, EINVAL, "a discard outside a fragment shader");
			program->discards = 1U;
			continue;
		}

		/* A pixel program's sample with derivatives runs whole quads. */
		if (ir->stage == DRV_GPU_STAGE_FRAGMENT) {
			if (instruction->op == DRV_GPU_IR_SAMPLE || instruction->op == DRV_GPU_IR_SAMPLE_BIAS) {
				lowering->whole_quads = 1;
			} else if (instruction->op == DRV_GPU_IR_TEXTURE &&
				   ((instruction->component & 0x1fU) == DRV_GPU_IR_TEXTURE_SAMPLE ||
				    (instruction->component & 0x1fU) == DRV_GPU_IR_TEXTURE_SAMPLE_BIAS)) {
				lowering->whole_quads = 1;
			}
		}

		/* Only the inputs and the outputs are looked at further. */
		if (instruction->op != DRV_GPU_IR_LOAD_INPUT && instruction->op != DRV_GPU_IR_STORE_OUTPUT)
			continue;

		/* Four channels to a location. */
		if (instruction->component > 3U)
			return refuse(lowering, EINVAL, "an input or output channel past the fourth");

		/* A vertex program's input: a fetched location, or the vertex or instance number. */
		if (ir->stage == DRV_GPU_STAGE_VERTEX && instruction->op == DRV_GPU_IR_LOAD_INPUT) {
			if (location < AMDRDNA_GFX10_LOCATIONS) {
				program->fetched_locations |= 1U << location;
			} else if (location != DRV_GPU_SHADER_LOCATION_VERTEX_INDEX &&
				   location != DRV_GPU_SHADER_LOCATION_INSTANCE_INDEX) {
				return refuse(lowering, ENOTSUP, "a vertex input that is not compiled yet");
			}
			continue;
		}

		/* A vertex program's output: the position, the point size, or a parameter. */
		if (ir->stage == DRV_GPU_STAGE_VERTEX) {
			if (location == DRV_GPU_IR_LOCATION_POINT_SIZE) {
				program->writes_point_size = 1U;
			} else if (location < AMDRDNA_GFX10_LOCATIONS) {
				program->parameter_outputs |= 1U << location;
			} else if (location != DRV_GPU_IR_LOCATION_POSITION) {
				return refuse(lowering, ENOTSUP, "a vertex output that is not compiled yet");
			}
			continue;
		}

		/* A pixel program's output: a colour. */
		if (instruction->op == DRV_GPU_IR_STORE_OUTPUT) {
			if (location >= GFX10_COLOR_TARGETS)
				return refuse(lowering, ENOTSUP, "a fragment output that is not compiled yet");
			program->color_outputs |= 1U << location;
			continue;
		}

		/* A pixel program's input: the position, the face, or an attribute. */
		if (location == DRV_GPU_SHADER_LOCATION_FRAG_COORD) {
			program->pixel_inputs |= GFX10_PS_POSITION;
		} else if (location == DRV_GPU_SHADER_LOCATION_FRONT_FACING) {
			program->pixel_inputs |= GFX10_PS_FRONT_FACE;
		} else if (location < AMDRDNA_GFX10_LOCATIONS) {
			program->attribute_inputs |= 1U << location;
			input = find_input(ir, location);
			if (input != NULL && input->flat != 0U) {
				program->flat_inputs |= 1U << location;
			} else if (input != NULL && input->noperspective != 0U) {
				uses_linear = 1;
			}
		} else {
			return refuse(lowering, ENOTSUP, "a fragment input that is not compiled yet");
		}
	}

	/* A pixel program that runs whole quads would write memory from its helper lanes. */
	if (lowering->whole_quads != 0) {
		for (index = 0U; index < ir->instruction_count; index++) {
			lowering->index = index;
			if (ir->instructions[index].op == DRV_GPU_IR_STORE_STORAGE || ir->instructions[index].op == DRV_GPU_IR_ATOMIC)
				return refuse(lowering, ENOTSUP, "storage writes in a fragment shader that samples with derivatives are not compiled yet");
		}
	}

	/* A pixel program always has the perspective barycentrics; the linear ones when an input needs them. */
	if (ir->stage == DRV_GPU_STAGE_FRAGMENT) {
		program->pixel_inputs |= GFX10_PS_PERSP_CENTER;
		if (uses_linear != 0)
			program->pixel_inputs |= GFX10_PS_LINEAR_CENTER;
	}

	/* Succeeded: the interface is known. */
	lowering->index = 0U;
	return 0;
}

/* Finds the interface record of an input location; NULL when the program has none. */
static const struct drv_gpu_shader_ir_io *
find_input(
	const struct drv_gpu_shader_ir *ir,
	uint32_t location)
{
	uint32_t index;

	/* Each input of the interface. */
	for (index = 0U; index < ir->input_count; index++) {
		if (ir->inputs[index].location == location)
			return &ir->inputs[index];
	}

	/* No record. */
	return NULL;
}

/* Reserves count VGPRs for the whole program after those already kept; returns the first. */
static uint32_t
reserve_vgprs(
	struct gfx10_lowering *lowering,
	uint32_t count)
{
	uint32_t first;

	/* The block grows past the registers the allocator hands out. */
	first = lowering->vgpr_first;
	lowering->vgpr_first += count;
	if (lowering->vgpr_first > lowering->vgpr_highest)
		lowering->vgpr_highest = lowering->vgpr_first;

	/* The first register of the block. */
	return first;
}

/*
 * Loads a vertex program's inputs: the vertex and instance numbers with
 * their bases, and the four channels of each fetched location, by the
 * vertex number or, where the instance mask has the location's bit, by
 * the instance number.
 */
static void
prologue_vertex(
	struct gfx10_lowering *lowering)
{
	struct amdrdna_gfx10_program *program;
	struct amdrdna_gfx10_inst inst;
	uint32_t location;
	unsigned temp;
	unsigned index_vgpr;

	program = lowering->program;

	/* The numbers: v0 plus the base vertex, v3 plus the first instance. */
	emit_vector(lowering,
		    AMDRDNA_GFX10_KIND_VECTOR_TWO,
		    AMDRDNA_GFX10_V_ADD_NC_U32,
		    AMDRDNA_GFX10_V(lowering->vertex_index_vgpr),
		    AMDRDNA_GFX10_S(AMDRDNA_GFX10_ABI_BASE_VERTEX),
		    AMDRDNA_GFX10_V(GFX10_VS_VERTEX_ID),
		    0U);
	emit_vector(lowering,
		    AMDRDNA_GFX10_KIND_VECTOR_TWO,
		    AMDRDNA_GFX10_V_ADD_NC_U32,
		    AMDRDNA_GFX10_V(lowering->instance_index_vgpr),
		    AMDRDNA_GFX10_S(AMDRDNA_GFX10_ABI_FIRST_INSTANCE),
		    AMDRDNA_GFX10_V(GFX10_VS_INSTANCE_ID),
		    0U);

	/* Each fetched location. */
	for (location = 0U; location < AMDRDNA_GFX10_LOCATIONS; location++) {
		if ((program->fetched_locations & (1U << location)) == 0U)
			continue;

		/* Its descriptor from the table. */
		emit_scalar_load(lowering,
				 AMDRDNA_GFX10_S_LOAD_DWORDX4,
				 lowering->vertex_sgpr[location],
				 AMDRDNA_GFX10_ABI_TABLE,
				 program->vertex_table_offset + AMDRDNA_GFX10_BUFFER_DESCRIPTOR_BYTES * location,
				 4U);

		/*
		 * Its record number, chosen for every lane: the instance mask's
		 * bit into SCC, SCC into a mask of all lanes or none, the mask
		 * selecting the instance number over the vertex number.  The
		 * selection is made into the location's first channel register,
		 * which the fetch then overwrites.
		 */
		temp = next_scalar_temp(lowering);
		index_vgpr = AMDRDNA_GFX10_V(lowering->input_vgpr[location]);
		emit_scalar(lowering,
			    AMDRDNA_GFX10_KIND_SCALAR_COMPARE,
			    AMDRDNA_GFX10_S_BITCMP1_B32,
			    0U,
			    AMDRDNA_GFX10_S(AMDRDNA_GFX10_ABI_INSTANCE_MASK),
			    constant_operand(location));
		emit_scalar(lowering,
			    AMDRDNA_GFX10_KIND_SCALAR_TWO,
			    AMDRDNA_GFX10_S_CSELECT_B32,
			    temp,
			    constant_operand(GFX10_TRUE),
			    AMDRDNA_GFX10_ZERO);
		emit_vector(lowering,
			    AMDRDNA_GFX10_KIND_VECTOR_TWO,
			    AMDRDNA_GFX10_V_CNDMASK_B32,
			    index_vgpr,
			    AMDRDNA_GFX10_V(lowering->vertex_index_vgpr),
			    AMDRDNA_GFX10_V(lowering->instance_index_vgpr),
			    temp);

		/* The fetch of the four channels, the format and the missing channels from the descriptor. */
		kern_memset(&inst, 0, sizeof(inst));
		inst.kind = AMDRDNA_GFX10_KIND_BUFFER;
		inst.opcode = AMDRDNA_GFX10_BUFFER_LOAD_FORMAT_XYZW;
		inst.dst = (uint16_t)index_vgpr;
		inst.src[0] = (uint16_t)index_vgpr;
		inst.src[1] = (uint16_t)lowering->vertex_sgpr[location];
		inst.src[2] = AMDRDNA_GFX10_ZERO;
		inst.flags = AMDRDNA_GFX10_FLAG_INDEX_ADDRESS;
		inst.count = 4U;
		emit(lowering, &inst);
	}
}

/* Lowers the read of a vertex or pixel program's input. */
static int
lower_input(
	struct gfx10_lowering *lowering,
	const struct drv_gpu_shader_ir_inst *instruction,
	uint32_t dst)
{
	const struct drv_gpu_shader_ir_io *input;
	struct amdrdna_gfx10_inst inst;
	uint32_t location;
	uint32_t barycentric;
	unsigned source;

	location = instruction->location;

	/* A vertex program copies the fetched channel or the number. */
	if (lowering->ir->stage == DRV_GPU_STAGE_VERTEX) {
		if (location == DRV_GPU_SHADER_LOCATION_VERTEX_INDEX) {
			source = AMDRDNA_GFX10_V(lowering->vertex_index_vgpr);
		} else if (location == DRV_GPU_SHADER_LOCATION_INSTANCE_INDEX) {
			source = AMDRDNA_GFX10_V(lowering->instance_index_vgpr);
		} else {
			source = AMDRDNA_GFX10_V(lowering->input_vgpr[location] + instruction->component);
		}
		emit_vector(lowering, AMDRDNA_GFX10_KIND_VECTOR_ONE, AMDRDNA_GFX10_V_MOV_B32, AMDRDNA_GFX10_V(dst), source, 0U, 0U);
		return 0;
	}

	/* A pixel program's input. */
	if (lowering->ir->stage != DRV_GPU_STAGE_FRAGMENT)
		return refuse(lowering, ENOTSUP, "inputs of this stage are not compiled");

	/*
	 * The position: x, y and z as delivered, w as 1 / w (the hardware
	 * delivers w).
	 *
	 * Procedure referenced: Mesa 25.0.7
	 * src/amd/vulkan/radv_pipeline_graphics.c (nir_lower_fragcoord_wtrans,
	 * line 2710).
	 */
	if (location == DRV_GPU_SHADER_LOCATION_FRAG_COORD) {
		source = AMDRDNA_GFX10_V(lowering->position_input_vgpr + instruction->component);
		if (instruction->component == 3U) {
			emit_vector(lowering, AMDRDNA_GFX10_KIND_VECTOR_ONE, AMDRDNA_GFX10_V_RCP_F32, AMDRDNA_GFX10_V(dst), source, 0U, 0U);
		} else {
			emit_vector(lowering, AMDRDNA_GFX10_KIND_VECTOR_ONE, AMDRDNA_GFX10_V_MOV_B32, AMDRDNA_GFX10_V(dst), source, 0U, 0U);
		}
		return 0;
	}

	/*
	 * The face: the front face where the delivered float is above zero.
	 *
	 * Procedure referenced: Mesa 25.0.7
	 * src/amd/common/nir/ac_nir_lower_intrinsics_to_args.c
	 * (load_front_face: the argument greater than 0, line 282).
	 */
	if (location == DRV_GPU_SHADER_LOCATION_FRONT_FACING) {
		emit_vector(lowering,
			    AMDRDNA_GFX10_KIND_VECTOR_COMPARE,
			    AMDRDNA_GFX10_V_CMP_LT_F32,
			    AMDRDNA_GFX10_VCC_LO,
			    AMDRDNA_GFX10_ZERO,
			    AMDRDNA_GFX10_V(lowering->face_vgpr),
			    0U);
		boolean_from_condition(lowering, dst);
		return 0;
	}

	/* An attribute: flat from the provoking vertex, or interpolated in two steps. */
	kern_memset(&inst, 0, sizeof(inst));
	inst.kind = AMDRDNA_GFX10_KIND_INTERPOLATE;
	inst.dst = (uint16_t)AMDRDNA_GFX10_V(dst);
	inst.target = (uint8_t)location;
	inst.channel = (uint8_t)instruction->component;
	if ((lowering->program->flat_inputs & (1U << location)) != 0U) {
		inst.opcode = AMDRDNA_GFX10_V_INTERP_MOV_F32;
		inst.src[0] = AMDRDNA_GFX10_INTERP_P0;
		emit(lowering, &inst);
		return 0;
	}

	/* The barycentrics: linear for a noperspective input, perspective otherwise. */
	barycentric = lowering->perspective_vgpr;
	input = find_input(lowering->ir, location);
	if (input != NULL && input->noperspective != 0U)
		barycentric = lowering->screen_linear_vgpr;

	/* The first step with i, the second adding j. */
	inst.opcode = AMDRDNA_GFX10_V_INTERP_P1_F32;
	inst.src[0] = (uint16_t)AMDRDNA_GFX10_V(barycentric);
	emit(lowering, &inst);
	inst.opcode = AMDRDNA_GFX10_V_INTERP_P2_F32;
	inst.src[0] = (uint16_t)AMDRDNA_GFX10_V(barycentric + 1U);
	emit(lowering, &inst);

	/* Succeeded: the input is in dst. */
	return 0;
}

/* Lowers the write of an output: into the register the export at the end reads. */
static int
lower_output(
	struct gfx10_lowering *lowering,
	const struct drv_gpu_shader_ir_inst *instruction)
{
	uint32_t location;
	uint32_t target;

	location = instruction->location;

	/* Picks the register by the stage and the location. */
	if (lowering->ir->stage == DRV_GPU_STAGE_FRAGMENT) {
		target = lowering->color_vgpr[location] + instruction->component;
	} else if (lowering->ir->stage != DRV_GPU_STAGE_VERTEX) {
		return refuse(lowering, ENOTSUP, "outputs of this stage are not compiled");
	} else if (location == DRV_GPU_IR_LOCATION_POSITION) {
		target = lowering->position_vgpr + instruction->component;
	} else if (location == DRV_GPU_IR_LOCATION_POINT_SIZE) {
		target = lowering->point_size_vgpr;
	} else {
		target = lowering->parameter_vgpr[location] + instruction->component;
	}

	/* The value into the register, unless it was defined there. */
	if (value_operand(lowering, instruction->src[0]) == AMDRDNA_GFX10_V(target))
		return 0;
	emit_vector(lowering,
		    AMDRDNA_GFX10_KIND_VECTOR_ONE,
		    AMDRDNA_GFX10_V_MOV_B32,
		    AMDRDNA_GFX10_V(target),
		    value_operand(lowering, instruction->src[0]),
		    0U,
		    0U);

	/* Succeeded: the output waits for the export. */
	return 0;
}

/*
 * Lowers a discard: the lanes whose Boolean is true leave the alive mask
 * and the lanes the wave runs; when none is alive the wave takes the early
 * end.
 */
static void
lower_kill(
	struct gfx10_lowering *lowering,
	const struct drv_gpu_shader_ir_inst *instruction)
{
	/* The lanes to discard into VCC_LO, then out of the alive mask (SCC says whether any is left). */
	condition_from_boolean(lowering, instruction->src[0]);
	emit_scalar(lowering,
		    AMDRDNA_GFX10_KIND_SCALAR_TWO,
		    AMDRDNA_GFX10_S_ANDN2_B32,
		    lowering->live_sgpr,
		    lowering->live_sgpr,
		    AMDRDNA_GFX10_VCC_LO);

	/* No lane alive: the early end. */
	emit_branch(lowering, AMDRDNA_GFX10_S_CBRANCH_SCC0, lowering->early_end_label);

	/* The lanes run are only those alive (and their quads' helpers). */
	restrict_to_alive(lowering);
}

/*
 * Narrows the lanes run to those alive, or in whole quads to the quads
 * with a lane alive.
 */
static void
restrict_to_alive(
	struct gfx10_lowering *lowering)
{
	unsigned mask;

	/* The alive lanes, widened to whole quads when the program runs them. */
	mask = lowering->live_sgpr;
	if (lowering->whole_quads != 0) {
		mask = next_scalar_temp(lowering);
		emit_scalar(lowering,
			    AMDRDNA_GFX10_KIND_SCALAR_ONE,
			    AMDRDNA_GFX10_S_WQM_B32,
			    mask,
			    lowering->live_sgpr,
			    0U);
	}

	/* The lanes run keep only those. */
	emit_scalar(lowering,
		    AMDRDNA_GFX10_KIND_SCALAR_TWO,
		    AMDRDNA_GFX10_S_AND_B32,
		    AMDRDNA_GFX10_EXEC_LO,
		    AMDRDNA_GFX10_EXEC_LO,
		    mask);
}

/*
 * Lowers a sample of a sampled image, or a fetch of one of its texels.
 *
 * The address VGPRs are copied in a row in the order the image access
 * takes them: the packed constant offset, the bias, u and v, the level.
 * A pixel program samples with the derivatives its whole quads give;
 * another stage samples level 0 (or the level given).  The four channels
 * go to the four values from dst, which have consecutive VGPRs.
 *
 * Procedure referenced: Mesa 25.0.7
 * src/amd/compiler/aco_instruction_selection.cpp (visit_tex: the address
 * order offset, bias, compare, derivatives, coordinates, then the level,
 * lines 9216 and 9411-9427).
 */
static int
lower_sample(
	struct gfx10_lowering *lowering,
	const struct drv_gpu_shader_ir_inst *instruction,
	uint32_t dst)
{
	const struct drv_gpu_shader_ir_uniform *uniform;
	struct amdrdna_gfx10_inst inst;
	uint32_t addresses[GFX10_SAMPLE_ADDRESSES];
	uint32_t count;
	uint32_t index;
	uint32_t slot;
	uint32_t message;
	uint32_t offset_bits;
	uint32_t packed;
	uint32_t block;
	uint32_t coordinate_u;
	uint32_t coordinate_v;
	uint32_t inst_count;
	uint32_t level;
	uint32_t bias;
	int32_t offset_u;
	int32_t offset_v;
	int leveled;
	int biased;
	int fetch;
	int derivatives;
	int error;

	/* The sampled image named by (set, binding). */
	slot = GFX10_NO_SLOT;
	for (index = 0U; index < lowering->ir->uniform_count; index++) {
		uniform = &lowering->ir->uniforms[index];
		if (uniform->kind == DRV_GPU_IR_UNIFORM_SAMPLED_IMAGE &&
		    uniform->set == instruction->location &&
		    uniform->binding == instruction->immediate)
			slot = lowering->slot[index];
	}
	if (slot == GFX10_NO_SLOT)
		return refuse(lowering, EINVAL, "a sample names no sampled image");

	/* The message: its kind, its parameters and its constant offset. */
	leveled = 0;
	biased = 0;
	fetch = 0;
	level = 0U;
	bias = 0U;
	coordinate_u = instruction->src[0];
	coordinate_v = instruction->src[1];
	offset_bits = (instruction->component >> 4) & 0xffU;
	if (instruction->op == DRV_GPU_IR_SAMPLE_BIAS) {
		biased = 1;
		bias = instruction->src[2];
	} else if (instruction->op == DRV_GPU_IR_SAMPLE_LOD) {
		leveled = 1;
		level = instruction->src[2];
	} else if (instruction->op == DRV_GPU_IR_TEXTURE) {
		/* The message's parameters in order, the offset in bits 23:8. */
		message = instruction->component & 0x1fU;
		offset_bits = (instruction->component >> 12) & 0xffU;
		coordinate_u = instruction->src[0];
		coordinate_v = instruction->src[0] + 1U;
		if (message == DRV_GPU_IR_TEXTURE_SAMPLE) {
			leveled = 0;
		} else if (message == DRV_GPU_IR_TEXTURE_SAMPLE_BIAS && instruction->src[1] >= 3U) {
			biased = 1;
			bias = instruction->src[0];
			coordinate_u = instruction->src[0] + 1U;
			coordinate_v = instruction->src[0] + 2U;
		} else if (message == DRV_GPU_IR_TEXTURE_SAMPLE_LOD && instruction->src[1] >= 3U) {
			leveled = 1;
			level = instruction->src[0];
			coordinate_u = instruction->src[0] + 1U;
			coordinate_v = instruction->src[0] + 2U;
		} else if (message == DRV_GPU_IR_TEXTURE_LD && instruction->src[1] >= 3U) {
			fetch = 1;
			leveled = 1;
			level = instruction->src[0] + 2U;
		} else {
			return refuse(lowering, ENOTSUP, "the texture message is not compiled yet");
		}
		if (instruction->src[1] < 2U)
			return refuse(lowering, EINVAL, "a texture message without its coordinates");
	}

	/* A bias outside a pixel program has no derivatives to move. */
	derivatives = 0;
	if (lowering->ir->stage == DRV_GPU_STAGE_FRAGMENT && leveled == 0)
		derivatives = 1;
	if (biased != 0 && derivatives == 0)
		return refuse(lowering, ENOTSUP, "a biased sample outside a fragment shader");

	/* The constant offset: u in bits 7:4 and v in 3:0 of the IR's byte, each from -8 to 7. */
	offset_u = (int32_t)((offset_bits >> 4) & 0xfU);
	offset_v = (int32_t)(offset_bits & 0xfU);
	if (offset_u >= 8)
		offset_u -= 16;
	if (offset_v >= 8)
		offset_v -= 16;
	packed = ((uint32_t)offset_u & GFX10_OFFSET_CHANNEL_MASK) |
		 (((uint32_t)offset_v & GFX10_OFFSET_CHANNEL_MASK) << GFX10_OFFSET_CHANNEL_SHIFT);
	if (fetch != 0)
		packed = 0U;

	/* The address VGPRs in order. */
	count = 0U;
	if (packed != 0U)
		addresses[count++] = GFX10_NOT_DEFINED;
	if (biased != 0)
		addresses[count++] = bias;
	addresses[count++] = coordinate_u;
	addresses[count++] = coordinate_v;
	if (leveled != 0)
		addresses[count++] = level;

	/* The coordinates alone already in two VGPRs in a row are the address as they are. */
	if (count == 2U &&
	    lowering->operand_code[coordinate_u] == GFX10_NO_OPERAND &&
	    lowering->operand_code[coordinate_v] == GFX10_NO_OPERAND &&
	    lowering->vgpr[coordinate_v] == lowering->vgpr[coordinate_u] + 1U) {
		block = lowering->vgpr[coordinate_u];
		count = 0U;
	} else {
		/* A block of address VGPRs. */
		error = take_vgpr_block(lowering, count, &block);
		if (error != 0)
			return error;
	}
	inst_count = count;
	if (count == 0U)
		inst_count = 2U;

	/* The address VGPRs filled in order. */
	for (index = 0U; index < count; index++) {
		if (addresses[index] == GFX10_NOT_DEFINED) {
			emit_vector_literal(lowering,
					    AMDRDNA_GFX10_KIND_VECTOR_ONE,
					    AMDRDNA_GFX10_V_MOV_B32,
					    AMDRDNA_GFX10_V(block + index),
					    constant_operand(packed),
					    0U,
					    packed);
		} else {
			emit_vector(lowering,
				    AMDRDNA_GFX10_KIND_VECTOR_ONE,
				    AMDRDNA_GFX10_V_MOV_B32,
				    AMDRDNA_GFX10_V(block + index),
				    value_operand(lowering, addresses[index]),
				    0U,
				    0U);
		}
	}

	/* The image access: the kind by the level, the bias, the offset and the stage. */
	kern_memset(&inst, 0, sizeof(inst));
	inst.kind = AMDRDNA_GFX10_KIND_IMAGE;
	inst.dst = (uint16_t)AMDRDNA_GFX10_V(dst);
	inst.src[0] = (uint16_t)AMDRDNA_GFX10_V(block);
	inst.src[1] = (uint16_t)lowering->image_sgpr[slot];
	inst.src[2] = (uint16_t)lowering->sampler_sgpr[slot];
	inst.mask = 0xfU;
	inst.count = (uint8_t)inst_count;
	if (fetch != 0) {
		inst.opcode = AMDRDNA_GFX10_IMAGE_LOAD_MIP;
		inst.src[2] = AMDRDNA_GFX10_NULL;
		inst.flags = AMDRDNA_GFX10_FLAG_UNNORMALIZED;
	} else if (leveled != 0) {
		inst.opcode = packed != 0U ? AMDRDNA_GFX10_IMAGE_SAMPLE_L_O : AMDRDNA_GFX10_IMAGE_SAMPLE_L;
	} else if (biased != 0) {
		inst.opcode = packed != 0U ? AMDRDNA_GFX10_IMAGE_SAMPLE_B_O : AMDRDNA_GFX10_IMAGE_SAMPLE_B;
	} else if (derivatives != 0) {
		inst.opcode = packed != 0U ? AMDRDNA_GFX10_IMAGE_SAMPLE_O : AMDRDNA_GFX10_IMAGE_SAMPLE;
	} else {
		inst.opcode = packed != 0U ? AMDRDNA_GFX10_IMAGE_SAMPLE_LZ_O : AMDRDNA_GFX10_IMAGE_SAMPLE_LZ;
	}
	emit(lowering, &inst);

	/* The address block goes back. */
	for (index = 0U; index < count; index++)
		give_vgpr(lowering, block + index);

	/* Succeeded: the four channels are in the values from dst. */
	return 0;
}

/*
 * Exports a graphics program's results before it ends.
 *
 * A vertex program exports its position first (with the valid mask bit,
 * which Navi1x needs on position 0), its point size as position 1, the
 * last of them marked last; then each parameter.  A pixel program
 * exports each colour, the last marked last and valid, or nothing marked
 * so when it writes none; the lanes alive are those exported.
 *
 * Procedure referenced: Mesa 25.0.7
 * src/amd/common/nir/ac_nir_prerast_utils.c (ac_nir_export_position: the
 * position exports first, valid_mask on POS0 for GFX10, lines 235-300).
 */
static void
finish_exports(
	struct gfx10_lowering *lowering)
{
	struct amdrdna_gfx10_program *program;
	uint32_t location;
	uint32_t last;
	uint32_t flags;

	program = lowering->program;

	/* A vertex program: the positions, then the parameters. */
	if (lowering->ir->stage == DRV_GPU_STAGE_VERTEX) {
		flags = AMDRDNA_GFX10_FLAG_VALID_MASK;
		if (program->writes_point_size == 0U)
			flags |= AMDRDNA_GFX10_FLAG_LAST;
		emit_export(lowering, AMDRDNA_GFX10_EXPORT_POSITION, lowering->position_vgpr, 0xfU, flags);
		if (program->writes_point_size != 0U)
			emit_export(lowering, AMDRDNA_GFX10_EXPORT_POSITION + 1U, lowering->point_size_vgpr, 0x1U, AMDRDNA_GFX10_FLAG_LAST);
		for (location = 0U; location < AMDRDNA_GFX10_LOCATIONS; location++) {
			if ((program->parameter_outputs & (1U << location)) != 0U)
				emit_export(lowering, AMDRDNA_GFX10_EXPORT_PARAMETER + location, lowering->parameter_vgpr[location], 0xfU, 0U);
		}
		return;
	}

	/* Only a pixel program exports otherwise. */
	if (lowering->ir->stage != DRV_GPU_STAGE_FRAGMENT)
		return;

	/* The lanes exported are those alive. */
	if (program->discards != 0U || lowering->whole_quads != 0) {
		emit_scalar(lowering,
			    AMDRDNA_GFX10_KIND_SCALAR_ONE,
			    AMDRDNA_GFX10_S_MOV_B32,
			    AMDRDNA_GFX10_EXEC_LO,
			    lowering->live_sgpr,
			    0U);
	}

	/* A program that writes no colour exports nothing. */
	if (program->color_outputs == 0U) {
		emit_export(lowering, AMDRDNA_GFX10_EXPORT_NOTHING, 0U, 0U, AMDRDNA_GFX10_FLAG_LAST | AMDRDNA_GFX10_FLAG_VALID_MASK);
		return;
	}

	/* The last colour written. */
	last = 0U;
	for (location = 0U; location < GFX10_COLOR_TARGETS; location++) {
		if ((program->color_outputs & (1U << location)) != 0U)
			last = location;
	}

	/* Each colour, the last marked last and valid. */
	for (location = 0U; location < GFX10_COLOR_TARGETS; location++) {
		if ((program->color_outputs & (1U << location)) == 0U)
			continue;
		flags = 0U;
		if (location == last)
			flags = AMDRDNA_GFX10_FLAG_LAST | AMDRDNA_GFX10_FLAG_VALID_MASK;
		emit_export(lowering, AMDRDNA_GFX10_EXPORT_COLOR + location, lowering->color_vgpr[location], 0xfU, flags);
	}
}

/* Appends an export of the channels mask enables from the VGPRs from first. */
static void
emit_export(
	struct gfx10_lowering *lowering,
	uint32_t target,
	uint32_t first,
	uint32_t mask,
	uint32_t flags)
{
	struct amdrdna_gfx10_inst inst;
	uint32_t channel;

	/* Builds the export; a disabled channel names no register. */
	kern_memset(&inst, 0, sizeof(inst));
	inst.kind = AMDRDNA_GFX10_KIND_EXPORT;
	inst.target = (uint8_t)target;
	inst.mask = (uint8_t)mask;
	inst.flags = (uint8_t)flags;
	for (channel = 0U; channel < 4U; channel++) {
		if ((mask & (1U << channel)) != 0U)
			inst.src[channel] = (uint16_t)AMDRDNA_GFX10_V(first + channel);
	}

	/* Places it. */
	emit(lowering, &inst);
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

	/* A graphics program exports its results. */
	finish_exports(lowering);

	/* The program ends. */
	kern_memset(&inst, 0, sizeof(inst));
	inst.kind = AMDRDNA_GFX10_KIND_CONTROL;
	inst.opcode = AMDRDNA_GFX10_S_ENDPGM;
	emit(lowering, &inst);

	/*
	 * A pixel program that discards has an early end for a wave whose
	 * lanes are all discarded: it exports nothing, marked last and valid,
	 * and ends.
	 *
	 * Procedure referenced: Mesa 25.0.7
	 * src/amd/compiler/aco_lower_to_hw_instr.cpp (the discard early exit:
	 * a null export done vm with exec zero, then s_endpgm).
	 */
	if (program->discards != 0U) {
		emit_label(lowering, lowering->early_end_label);
		emit_scalar(lowering,
			    AMDRDNA_GFX10_KIND_SCALAR_ONE,
			    AMDRDNA_GFX10_S_MOV_B32,
			    AMDRDNA_GFX10_EXEC_LO,
			    AMDRDNA_GFX10_ZERO,
			    0U);
		emit_export(lowering, AMDRDNA_GFX10_EXPORT_NOTHING, 0U, 0U, AMDRDNA_GFX10_FLAG_LAST | AMDRDNA_GFX10_FLAG_VALID_MASK);
		emit(lowering, &inst);
	}
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
	struct amdrdna_gfx10_inst legal;
	uint32_t capacity;

	/* Once the list could not grow, nothing more is added. */
	if (lowering->out_of_memory != 0)
		return;

	/* A vector ALU instruction reads at most two distinct scalar values; the others are copied to VGPRs. */
	if (inst->kind == AMDRDNA_GFX10_KIND_VECTOR_ONE ||
	    inst->kind == AMDRDNA_GFX10_KIND_VECTOR_TWO ||
	    inst->kind == AMDRDNA_GFX10_KIND_VECTOR_COMPARE ||
	    inst->kind == AMDRDNA_GFX10_KIND_VECTOR_WIDE) {
		legal = *inst;
		legalize_scalars(lowering, &legal);
		inst = &legal;
		if (lowering->out_of_memory != 0)
			return;
	}

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
	/* A constant's or a uniform word's own operand. */
	if (lowering->operand_code[value] != GFX10_NO_OPERAND)
		return lowering->operand_code[value];

	/* The value's VGPR as an operand. */
	return AMDRDNA_GFX10_V(lowering->vgpr[value]);
}

/*
 * Reports a VGPR holding a value, for an operand that must be a VGPR (a
 * memory address or data): the value's own, or a scratch copy of its
 * constant or SGPR.
 */
static unsigned
vgpr_operand(
	struct gfx10_lowering *lowering,
	uint32_t value)
{
	unsigned code;

	/* A value in a VGPR is its own operand. */
	code = value_operand(lowering, value);
	if (code >= AMDRDNA_GFX10_VGPR_FIRST)
		return code;

	/* Otherwise a scratch copy. */
	code = scratch_copy(lowering, code);
	return code;
}

/*
 * Copies a scalar operand (an SGPR or an inline constant) into a scratch
 * VGPR the IR instruction keeps until it is lowered; returns the VGPR's
 * operand code.
 */
static unsigned
scratch_copy(
	struct gfx10_lowering *lowering,
	unsigned code)
{
	struct amdrdna_gfx10_inst copy;
	uint32_t reg;
	int error;

	/* A scratch VGPR; with none free the compile fails as out of registers. */
	if (lowering->scratch_count == GFX10_SCRATCH_VGPRS) {
		lowering->out_of_memory = 1;
		return code;
	}
	error = take_vgpr(lowering, &reg);
	if (error != 0) {
		lowering->out_of_memory = 1;
		return code;
	}
	lowering->scratch[lowering->scratch_count] = reg;
	lowering->scratch_count++;

	/* The copy, which reads one scalar value and so needs no legalizing. */
	kern_memset(&copy, 0, sizeof(copy));
	copy.kind = AMDRDNA_GFX10_KIND_VECTOR_ONE;
	copy.opcode = AMDRDNA_GFX10_V_MOV_B32;
	copy.dst = (uint16_t)AMDRDNA_GFX10_V(reg);
	copy.src[0] = (uint16_t)code;
	emit(lowering, &copy);

	/* The VGPR as the operand. */
	return AMDRDNA_GFX10_V(reg);
}

/* Gives back the scratch VGPRs of the IR instruction just lowered. */
static void
give_scratch(
	struct gfx10_lowering *lowering)
{
	/* Every scratch VGPR taken. */
	while (lowering->scratch_count != 0U) {
		lowering->scratch_count--;
		give_vgpr(lowering, lowering->scratch[lowering->scratch_count]);
	}
}

/*
 * Keeps a vector ALU instruction within the constant bus: at most two
 * distinct scalar values (SGPRs, VCC or the literal) among the sources it
 * reads; each further SGPR source is copied into a scratch VGPR.
 */
static void
legalize_scalars(
	struct gfx10_lowering *lowering,
	struct amdrdna_gfx10_inst *inst)
{
	unsigned order[3];
	unsigned seen[4];
	unsigned seen_count;
	unsigned reads;
	unsigned step;
	unsigned index;
	unsigned other;
	unsigned code;
	int known;

	reads = vector_reads(inst);
	seen_count = 0U;

	/* The sources in order, a selection's condition first so that it is never the one copied. */
	order[0] = 0U;
	order[1] = 1U;
	order[2] = 2U;
	if (reads == 3U && inst->kind == AMDRDNA_GFX10_KIND_VECTOR_TWO) {
		order[0] = 2U;
		order[1] = 0U;
		order[2] = 1U;
	}

	/* Each source read: a scalar register or the literal not seen yet counts once. */
	for (step = 0U; step < reads; step++) {
		index = order[step];
		code = inst->src[index];
		if (code >= AMDRDNA_GFX10_INT_FIRST && code != AMDRDNA_GFX10_LITERAL)
			continue;

		/* The same scalar read twice counts once. */
		known = 0;
		for (other = 0U; other < seen_count; other++) {
			if (seen[other] == code)
				known = 1;
		}
		if (known != 0)
			continue;

		/* Within the limit it stays; past it an SGPR is copied. */
		if (seen_count < GFX10_CONSTANT_BUS_LIMIT) {
			seen[seen_count] = code;
			seen_count++;
			continue;
		}
		inst->src[index] = (uint16_t)scratch_copy(lowering, code);
	}
}

/* Reports how many sources a vector ALU instruction reads. */
static unsigned
vector_reads(
	const struct amdrdna_gfx10_inst *inst)
{
	/* Picks the count by the kind and the operation. */
	switch (inst->kind) {
	case AMDRDNA_GFX10_KIND_VECTOR_ONE:
		return 1U;
	case AMDRDNA_GFX10_KIND_VECTOR_TWO:
		/* The selection reads its condition third. */
		if (inst->opcode == AMDRDNA_GFX10_V_CNDMASK_B32)
			return 3U;
		return 2U;
	case AMDRDNA_GFX10_KIND_VECTOR_COMPARE:
		return 2U;
	default:
		/* The multiply-adds read three. */
		if (inst->opcode == AMDRDNA_GFX10_V_FMA_F32 || inst->opcode == AMDRDNA_GFX10_V_MAD_U32_U24)
			return 3U;
		return 2U;
	}
}

/*
 * Gives each uniform word the program reads at a constant offset an SGPR,
 * loaded once at the start with the 16 bytes around it, while the SGPRs
 * left below VCC hold them.  A word a MOVE writes, or one past the SGPRs,
 * is read where it is used instead.
 */
static void
plan_uniforms(
	struct gfx10_lowering *lowering,
	uint32_t *sgpr)
{
	const struct drv_gpu_shader_ir_inst *instruction;
	uint32_t index;
	uint32_t chunk;
	uint32_t slot;
	uint32_t offset;
	uint32_t found;
	uint32_t base;

	/* The chunks start at a multiple of four SGPRs, after the cache for the words that get none. */
	base = (*sgpr + 3U) & ~3U;
	lowering->cache_sgpr = base;
	base += 4U * GFX10_CHUNK_CACHE;
	*sgpr = base;

	/* Each uniform read at a constant offset. */
	for (index = 0U; index < lowering->ir->instruction_count; index++) {
		instruction = &lowering->ir->instructions[index];
		if (instruction->op != DRV_GPU_IR_LOAD_PUSH && instruction->op != DRV_GPU_IR_LOAD_UBO)
			continue;
		if (lowering->moved[instruction->dst] != 0U)
			continue;

		/* The buffer and the chunk. */
		slot = GFX10_NO_SLOT;
		if (instruction->op == DRV_GPU_IR_LOAD_UBO) {
			if (instruction->location >= lowering->ir->uniform_count)
				continue;
			if (lowering->ir->uniforms[instruction->location].kind != DRV_GPU_IR_UNIFORM_BLOCK)
				continue;
			slot = lowering->slot[instruction->location];
		} else if (instruction->immediate + 4U > lowering->ir->push_bytes) {
			continue;
		}
		offset = instruction->immediate & ~15U;

		/* The chunk already loaded, or a new one while the SGPRs last. */
		found = GFX10_NO_SLOT;
		for (chunk = 0U; chunk < lowering->chunk_count; chunk++) {
			if (lowering->chunk_slot[chunk] == slot && lowering->chunk_offset[chunk] == offset)
				found = chunk;
		}
		if (found == GFX10_NO_SLOT) {
			if (lowering->chunk_count == GFX10_UNIFORM_CHUNKS)
				continue;
			if (base + 4U > AMDRDNA_GFX10_SGPR_COUNT)
				continue;
			found = lowering->chunk_count;
			lowering->chunk_slot[found] = slot;
			lowering->chunk_offset[found] = offset;
			lowering->chunk_sgpr[found] = base;
			lowering->chunk_count++;
			base += 4U;
		}

		/* The word's SGPR in its chunk. */
		lowering->operand_code[instruction->dst] =
			(uint16_t)AMDRDNA_GFX10_S(lowering->chunk_sgpr[found] + (instruction->immediate & 15U) / 4U);
	}

	/* The SGPRs the chunks took. */
	if (lowering->chunk_count != 0U)
		*sgpr = base;
}

/*
 * Finds the values that can be defined straight into an output's
 * registers: a value read only by the one store to an output that is
 * stored once in the program, and not written by a MOVE; or the four
 * channels of a sample all stored, in order, to the four channels of one
 * output stored once each.
 */
static int
plan_output_homes(
	struct gfx10_lowering *lowering)
{
	const struct drv_gpu_shader_ir *ir;
	const struct drv_gpu_shader_ir_inst *instruction;
	uint32_t sources[GFX10_MAX_SOURCES];
	uint32_t *reads;
	uint32_t *stores;
	uint32_t index;
	uint32_t source;
	uint32_t target;
	uint32_t value;
	uint32_t channel;
	int count;
	int whole;

	ir = lowering->ir;
	if (ir->stage == DRV_GPU_STAGE_COMPUTE)
		return 0;

	/* How many times each value is read, and how many times each output register is stored. */
	reads = kern_calloc(ir->value_count + 1U, sizeof(*reads));
	if (reads == NULL)
		return ENOMEM;
	stores = kern_calloc(AMDRDNA_GFX10_VGPR_COUNT, sizeof(*stores));
	if (stores == NULL) {
		kern_free(reads);
		return ENOMEM;
	}
	for (index = 0U; index < ir->instruction_count; index++) {
		instruction = &ir->instructions[index];
		count = ir_sources(instruction, sources);
		for (source = 0U; count > 0 && source < (uint32_t)count; source++)
			reads[sources[source]]++;
		if (instruction->op == DRV_GPU_IR_STORE_OUTPUT) {
			target = output_register(lowering, instruction);
			if (target < AMDRDNA_GFX10_VGPR_COUNT)
				stores[target]++;
		}
	}

	/* Each store of a value read only there, to a register stored only there. */
	for (index = 0U; index < ir->instruction_count; index++) {
		instruction = &ir->instructions[index];
		if (instruction->op != DRV_GPU_IR_STORE_OUTPUT)
			continue;
		value = instruction->src[0];
		target = output_register(lowering, instruction);
		if (target >= AMDRDNA_GFX10_VGPR_COUNT || stores[target] != 1U)
			continue;
		if (reads[value] != 1U || lowering->moved[value] != 0U)
			continue;
		lowering->output_home[value] = (uint16_t)target;
	}

	/* A sample keeps its home only when its four channels go, in order, to one output's four registers. */
	for (index = 0U; index < ir->instruction_count; index++) {
		instruction = &ir->instructions[index];
		if (ir_defines(instruction) != 4)
			continue;
		whole = 1;
		for (channel = 0U; channel < 4U; channel++) {
			if (lowering->output_home[instruction->dst + channel] == GFX10_NO_REG ||
			    lowering->output_home[instruction->dst + channel] != lowering->output_home[instruction->dst] + channel)
				whole = 0;
		}
		for (channel = 1U; channel < 4U; channel++)
			lowering->output_home[instruction->dst + channel] = GFX10_NO_REG;
		if (whole == 0)
			lowering->output_home[instruction->dst] = GFX10_NO_REG;
	}

	kern_free(stores);
	kern_free(reads);

	/* Succeeded: the homes are chosen. */
	return 0;
}

/* Reports the register an output store writes (past the VGPRs for one not kept in a register). */
static uint32_t
output_register(
	const struct gfx10_lowering *lowering,
	const struct drv_gpu_shader_ir_inst *instruction)
{
	uint32_t location;

	location = instruction->location;

	/* Picks the register by the stage and the location, as the store does. */
	if (lowering->ir->stage == DRV_GPU_STAGE_FRAGMENT) {
		if (location >= GFX10_COLOR_TARGETS || instruction->component > 3U)
			return AMDRDNA_GFX10_VGPR_COUNT;
		return lowering->color_vgpr[location] + instruction->component;
	}
	if (location == DRV_GPU_IR_LOCATION_POSITION)
		return lowering->position_vgpr + instruction->component;
	if (location == DRV_GPU_IR_LOCATION_POINT_SIZE)
		return lowering->point_size_vgpr;
	if (location < AMDRDNA_GFX10_LOCATIONS && instruction->component <= 3U)
		return lowering->parameter_vgpr[location] + instruction->component;
	return AMDRDNA_GFX10_VGPR_COUNT;
}

/*
 * Reports whether an IR instruction defines a value that has an operand
 * of its own and so needs no instruction: a constant that is an inline
 * constant, or a uniform word loaded at the start.  Sets the constant's
 * operand on the way.
 */
static int
has_operand(
	struct gfx10_lowering *lowering,
	const struct drv_gpu_shader_ir_inst *instruction)
{
	unsigned code;

	/* A uniform word loaded at the start. */
	if (instruction->op == DRV_GPU_IR_LOAD_PUSH || instruction->op == DRV_GPU_IR_LOAD_UBO) {
		if (lowering->operand_code[instruction->dst] != GFX10_NO_OPERAND)
			return 1;
		return 0;
	}

	/* Only a constant otherwise, and one no MOVE writes. */
	if (instruction->op != DRV_GPU_IR_CONST &&
	    instruction->op != DRV_GPU_IR_ICONST &&
	    instruction->op != DRV_GPU_IR_BOOL)
		return 0;
	if (lowering->moved[instruction->dst] != 0U)
		return 0;

	/* An inline constant is its own operand. */
	code = constant_operand(instruction->immediate);
	if (code == AMDRDNA_GFX10_LITERAL)
		return 0;
	lowering->operand_code[instruction->dst] = (uint16_t)code;
	return 1;
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

/* Takes the lowest count consecutive free VGPRs; *reg is the first. */
static int
take_vgpr_block(
	struct gfx10_lowering *lowering,
	uint32_t count,
	uint32_t *reg)
{
	uint32_t index;
	uint32_t inner;
	int free_run;

	/* The lowest start whose count registers are all free. */
	for (index = lowering->vgpr_first; index + count <= AMDRDNA_GFX10_VGPR_COUNT; index++) {
		free_run = 1;
		for (inner = 0U; inner < count; inner++) {
			if (lowering->taken[index + inner] != 0U) {
				free_run = 0;
				break;
			}
		}
		if (free_run != 0)
			break;
	}

	/* No such run is free. */
	if (index + count > AMDRDNA_GFX10_VGPR_COUNT)
		return refuse(lowering, ENOTSUP, "the program needs more VGPRs than there are");

	/* Takes them all. */
	for (inner = 0U; inner < count; inner++)
		lowering->taken[index + inner] = 1U;
	if (index + count > lowering->vgpr_highest)
		lowering->vgpr_highest = index + count;

	/* Succeeded: the block is the caller's. */
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
	for (source = 0U; source < (uint32_t)defines; source++) {
		value = instruction->dst + source;
		if (lowering->last_use[value] <= lowering->index + 1U && lowering->vgpr[value] != GFX10_NO_REG) {
			give_vgpr(lowering, lowering->vgpr[value]);
			lowering->vgpr[value] = GFX10_NO_REG;
		}
	}
}
