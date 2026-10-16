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
 * The VGPRs a vertex program's vertex number, primitive number (when the
 * driver has it delivered) and instance number arrive in (each without its
 * base), with VGPR_COMP_CNT 3 so that v0 .. v3 are delivered; and the SGPR
 * a pixel program's primitive mask arrives in, after its two user SGPRs.
 *
 * Values referenced: Mesa 25.0.7 src/amd/vulkan/radv_shader_args.c
 * (declare_vs_input_vgprs, the legacy VS on GFX10: vertex_id, an unused
 * one, vs_prim_id, then instance_id, lines 152-183;
 * declare_ps_input_vgprs and the prim_mask SGPR, line 855).
 */
#define GFX10_VS_VERTEX_ID		0U
#define GFX10_VS_PRIMITIVE_ID		2U
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

/*
 * The output channels a program stores, by number: a vertex program's
 * position (4) and point size, then for each location four channels (a
 * vertex program's parameter, a pixel program's colour).
 */
#define GFX10_OUTPUT_POSITION		0U
#define GFX10_OUTPUT_POINT_SIZE		4U
#define GFX10_OUTPUT_LOCATIONS		8U
#define GFX10_OUTPUT_CHANNELS		(GFX10_OUTPUT_LOCATIONS + 4U * AMDRDNA_GFX10_LOCATIONS)

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
 * The most address VGPRs a sample takes (an offset, a bias, a depth
 * reference, four gradients, u, v and a level), and the bits of one
 * channel of a packed texel offset: 6 bits, the next channel 8 bits up.
 *
 * Values referenced: Mesa 25.0.7 src/amd/compiler/aco_instruction_selection.cpp
 * (visit_tex: each constant offset & 0x3F shifted by 8 * i, lines
 * 9150-9160).
 */
#define GFX10_SAMPLE_ADDRESSES		10U
#define GFX10_OFFSET_CHANNEL_MASK	0x3fU
#define GFX10_OFFSET_CHANNEL_SHIFT	8U

/*
 * The state of one compilation.
 *
 * Lives on the stack of amdrdna_gfx10_compile_ir(); its arrays are
 * allocated there and freed before it returns.
 */
struct gfx10_lowering {
	/* The program, the options it is compiled with, the result being built, and where a refusal is described. */
	const struct drv_gpu_shader_ir *ir;
	struct amdrdna_gfx10_compile_options options;
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
	 * Each output channel: the value its export reads where it lives
	 * (GFX10_NOT_DEFINED for none), when the channel is stored once with
	 * a value that has a VGPR of its own, which then lives to the end;
	 * otherwise the VGPR the stores copy into (GFX10_NO_REG until the
	 * first store takes one, kept to the end).
	 */
	uint32_t output_value[GFX10_OUTPUT_CHANNELS];
	uint16_t output_reg[GFX10_OUTPUT_CHANNELS];

	/* Each output channel the program writes, which a parameter's export enables. */
	uint8_t output_written[GFX10_OUTPUT_CHANNELS];

	/*
	 * The chunks of 16 uniform bytes loaded at the start: the uniform
	 * buffer's slot (or GFX10_NO_SLOT for the push constants), the byte
	 * offset, and the first of the four SGPRs.
	 */
	uint32_t chunk_slot[GFX10_UNIFORM_CHUNKS];
	uint32_t chunk_offset[GFX10_UNIFORM_CHUNKS];
	uint32_t chunk_sgpr[GFX10_UNIFORM_CHUNKS];
	uint32_t chunk_count;

	/*
	 * The cache of uniform chunks: its first SGPR, each entry's buffer and
	 * offset and whether it holds one, the IR instruction (plus one) up
	 * to which a value read straight from its SGPRs keeps it, and the
	 * next to replace.  One more chunk after the entries is loaded when
	 * every entry is kept; its words are copied at once.
	 */
	uint32_t cache_sgpr;
	uint32_t cache_slot[GFX10_CHUNK_CACHE];
	uint32_t cache_offset[GFX10_CHUNK_CACHE];
	uint8_t cache_valid[GFX10_CHUNK_CACHE];
	uint32_t cache_until[GFX10_CHUNK_CACHE];
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
	 * their bases) and of each fetched location's four channels; the
	 * first SGPR of each fetched location's descriptor.
	 */
	uint32_t vertex_index_vgpr;
	uint32_t instance_index_vgpr;
	uint32_t input_vgpr[AMDRDNA_GFX10_LOCATIONS];
	uint32_t vertex_sgpr[AMDRDNA_GFX10_LOCATIONS];

	/*
	 * A vertex program: the IR instruction (plus one) after which each
	 * fetched location's VGPRs, and the vertex and instance numbers', are
	 * read no more (0: only by the start); their VGPRs go back then.  The
	 * bit of each location whose VGPRs went back, and whether the
	 * numbers' and the hardware's input VGPRs did.
	 */
	uint32_t input_until[AMDRDNA_GFX10_LOCATIONS];
	uint32_t vertex_index_until;
	uint32_t instance_index_until;
	uint32_t inputs_returned;
	int vertex_index_returned;
	int instance_index_returned;

	/*
	 * A pixel program: the first VGPR of the perspective and the linear
	 * barycentrics, of the position and of the face; the SGPR of the lanes
	 * still alive, and
	 * the label of the early end a discard of every lane takes.
	 */
	uint32_t perspective_vgpr;
	uint32_t screen_linear_vgpr;
	uint32_t position_input_vgpr;
	uint32_t face_vgpr;
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

	/*
	 * The Boolean whose lanes VCC_LO holds now (GFX10_NOT_DEFINED for
	 * none): a comparison leaves its own there, a condition taken from a
	 * Boolean leaves that one.  Any other write of VCC_LO or of the lane
	 * mask, and any label, forgets it.
	 */
	uint32_t vcc_value;

	/*
	 * Each value that is a Boolean only ever used as a condition, with VCC_LO
	 * kept from its comparison to its last use: it gets no VGPR.
	 */
	uint8_t *condition_only;

	/* Nonzero when a condition the lowering counted on was not in VCC_LO. */
	int lost_condition;

	/* The Boolean a guard narrowed the lanes by, which VCC_LO still holds when the guard ends. */
	uint32_t guard_condition;

	/* Each IR instruction whose results nothing reads and that has no other effect: it is not lowered. */
	uint8_t *dead;
};

/*
 * What one texture instruction asks of the image: the values of its
 * coordinate, depth reference, bias, level and gradients (du/dx, dv/dx,
 * du/dy, dv/dy), and which of them it has; a fetch of an integer texel, or
 * a size query of a level.
 *
 * Lives on the stack of lower_sample().
 */
struct gfx10_sample {
	uint32_t u;
	uint32_t v;
	uint32_t reference;
	uint32_t bias;
	uint32_t level;
	uint32_t gradient[4];
	int compared;
	int biased;
	int leveled;
	int level_zero;
	int gradients;
	int fetch;
	int size_query;
};

static int refuse(struct gfx10_lowering *lowering, int error, const char *reason);
static int ir_sources(const struct drv_gpu_shader_ir_inst *instruction, uint32_t *sources);
static int ir_defines(const struct drv_gpu_shader_ir_inst *instruction);
static int prepare(struct gfx10_lowering *lowering);
static int find_dead(struct gfx10_lowering *lowering);
static int extend_loops(struct gfx10_lowering *lowering);
static int map_uniforms(struct gfx10_lowering *lowering);
static int prepare_graphics(struct gfx10_lowering *lowering);
static const struct drv_gpu_shader_ir_io *find_input(const struct drv_gpu_shader_ir *ir, uint32_t location);
static int plan_registers(struct gfx10_lowering *lowering);
static uint32_t reserve_vgprs(struct gfx10_lowering *lowering, uint32_t count);
static void prologue_vertex(struct gfx10_lowering *lowering);
static void plan_input_lifetimes(struct gfx10_lowering *lowering);
static void return_inputs(struct gfx10_lowering *lowering, uint32_t upto);
static uint32_t cache_chunk(struct gfx10_lowering *lowering, uint32_t slot, uint32_t offset, int spare);
static int alias_uniform(struct gfx10_lowering *lowering, const struct drv_gpu_shader_ir_inst *instruction);
static int lower_input(struct gfx10_lowering *lowering, const struct drv_gpu_shader_ir_inst *instruction, uint32_t dst);
static int lower_output(struct gfx10_lowering *lowering, const struct drv_gpu_shader_ir_inst *instruction);
static void lower_kill(struct gfx10_lowering *lowering, const struct drv_gpu_shader_ir_inst *instruction);
static int lower_sample(struct gfx10_lowering *lowering, const struct drv_gpu_shader_ir_inst *instruction, uint32_t dst);
static unsigned sample_opcode(const struct gfx10_sample *sample, int derivatives, int offset);
static void restrict_to_alive(struct gfx10_lowering *lowering);
static int finish_exports(struct gfx10_lowering *lowering);
static int export_location(struct gfx10_lowering *lowering, uint32_t target, uint32_t first_channel, uint32_t mask, uint32_t flags);
static int export_colour(struct gfx10_lowering *lowering, uint32_t target, uint32_t first_channel, uint32_t flags);
static int export_source(struct gfx10_lowering *lowering, uint32_t channel, unsigned *code);
static void emit_export(struct gfx10_lowering *lowering, uint32_t target, const unsigned *sources, uint32_t mask, uint32_t flags);
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
static int guard_writes(struct gfx10_lowering *lowering, uint32_t predicate);
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
static int plan_outputs(struct gfx10_lowering *lowering);
static void plan_conditions(struct gfx10_lowering *lowering);
static int defines_condition(const struct gfx10_lowering *lowering, const struct drv_gpu_shader_ir_inst *instruction);
static uint32_t condition_source(const struct drv_gpu_shader_ir_inst *instruction);
static int touches_condition(const struct gfx10_lowering *lowering, const struct drv_gpu_shader_ir_inst *instruction);
static void forget_condition(struct gfx10_lowering *lowering, const struct amdrdna_gfx10_inst *inst);
static uint32_t output_channel(const struct gfx10_lowering *lowering, const struct drv_gpu_shader_ir_inst *instruction);
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
	const struct amdrdna_gfx10_compile_options *options,
	struct amdrdna_gfx10_program **program,
	struct amdrdna_gfx10_diagnostic *diagnostic)
{
	struct drv_gpu_shader_ir *ir;
	struct drv_gpu_shader_ir optimized;
	struct drv_gpu_shader_optimize_options optimize_options;
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
	kern_memset(&optimize_options, 0, sizeof(optimize_options));
	optimize_options.runs_every_block = 1U;
	optimize_options.simplify = 1U;
	kern_memset(optimize_options.needed, 1, sizeof(optimize_options.needed));
	error = drv_gpu_shader_optimize(ir, &optimize_options, &optimized);
	if (error != 0) {
		drv_gpu_shader_ir_free(ir);
		return error;
	}

	/* Lowers the rewritten program, or the parsed one when the optimizer left it alone. */
	if (optimized.instructions != NULL) {
		error = amdrdna_gfx10_compile_ir(&optimized, options, program, diagnostic);
	} else {
		error = amdrdna_gfx10_compile_ir(ir, options, program, diagnostic);
	}

	/* A rewritten program refused for any reason but memory is lowered from the parsed one. */
	if (optimized.instructions != NULL && error != 0 && error != ENOMEM) {
		if (diagnostic != NULL)
			kern_memset(diagnostic, 0, sizeof(*diagnostic));
		error = amdrdna_gfx10_compile_ir(ir, options, program, diagnostic);
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
	const struct amdrdna_gfx10_compile_options *options,
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
	if (options != NULL)
		lowering.options = *options;

	/* The result. */
	lowering.program = kern_calloc(1U, sizeof(*lowering.program));
	if (lowering.program == NULL)
		return ENOMEM;
	lowering.program->stage = ir->stage;
	lowering.program->point_coord_attribute = AMDRDNA_GFX10_NO_ATTRIBUTE;
	lowering.program->primitive_id_attribute = AMDRDNA_GFX10_NO_ATTRIBUTE;

	/* Checks the program, finds the last reads, maps the uniforms and places the registers. */
	error = prepare(&lowering);

	/* Loads what every lane shares. */
	if (error == 0)
		error = prologue(&lowering);

	/* Lowers every instruction in order. */
	for (index = 0U; error == 0 && index < ir->instruction_count; index++) {
		instruction = &ir->instructions[index];
		lowering.index = index;

		/* An instruction whose results nothing reads is not lowered. */
		if (lowering.dead[index] != 0U)
			continue;

		/* A constant or a uniform word with an operand of its own needs no instruction. */
		defines = has_operand(&lowering, instruction);
		if (defines != 0)
			continue;

		/* The destination's VGPR, a value redefined by a MOVE keeping its own; a sample's four in a row. */
		dst = GFX10_NO_REG;
		defines = ir_defines(instruction);
		if (defines == 4) {
			/* A sample's channels in a block of their own. */
			error = take_vgpr_block(&lowering, 4U, &dst);
			if (error != 0)
				break;
			for (block = 0U; block < 4U; block++)
				lowering.vgpr[instruction->dst + block] = (uint16_t)(dst + block);
		} else if (defines != 0 && lowering.condition_only[instruction->dst] != 0U) {
			/* A Boolean used only as a condition stays in VCC_LO. */
			dst = GFX10_NO_REG;
		} else if (defines != 0) {
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

		/* A condition expected in VCC_LO that was not there is the compiler's own fault, refused. */
		if (lowering.lost_condition != 0) {
			error = refuse(&lowering, ENOTSUP, "a condition was not kept where the compiler expected it");
			break;
		}

		/* The scratch VGPRs, and the VGPRs of values no later instruction reads, go back. */
		give_scratch(&lowering);
		retire(&lowering, instruction);
		return_inputs(&lowering, index + 1U);
	}

	/* The end of the program, its waits and its code. */
	if (error == 0)
		error = finish(&lowering);

	/* The per-value and per-uniform arrays and the list are only the compilation's. */
	kern_free(lowering.vgpr);
	kern_free(lowering.last_use);
	kern_free(lowering.operand_code);
	kern_free(lowering.moved);
	kern_free(lowering.condition_only);
	kern_free(lowering.dead);
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

	lowering->condition_only = kern_calloc(ir->value_count + 1U, sizeof(*lowering->condition_only));
	if (lowering->condition_only == NULL)
		return ENOMEM;

	lowering->dead = kern_calloc(ir->instruction_count + 1U, sizeof(*lowering->dead));
	if (lowering->dead == NULL)
		return ENOMEM;

	/* VCC_LO holds no Boolean at the start. */
	lowering->vcc_value = GFX10_NOT_DEFINED;

	/* No value has a VGPR or an operand of its own yet. */
	for (index = 0U; index <= ir->value_count; index++) {
		lowering->vgpr[index] = GFX10_NO_REG;
		lowering->operand_code[index] = GFX10_NO_OPERAND;
	}

	/* No output channel has its value or its VGPR yet. */
	for (index = 0U; index < GFX10_OUTPUT_CHANNELS; index++) {
		lowering->output_value[index] = GFX10_NOT_DEFINED;
		lowering->output_reg[index] = GFX10_NO_REG;
	}

	/* Each instruction: covered, its values in range. */
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

		/* Each source is a value of the program. */
		for (source = 0U; source < (uint32_t)count; source++) {
			if (sources[source] >= ir->value_count)
				return refuse(lowering, EINVAL, "a source is not a value of the program");
		}
	}

	/* The instructions whose results nothing reads. */
	error = find_dead(lowering);
	if (error != 0)
		return error;

	/* Each value's last read by an instruction that is lowered. */
	for (index = 0U; index < ir->instruction_count; index++) {
		if (lowering->dead[index] != 0U)
			continue;
		count = ir_sources(&ir->instructions[index], sources);
		for (source = 0U; count > 0 && source < (uint32_t)count; source++)
			lowering->last_use[sources[source]] = index + 1U;
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

	/* The outputs exported where their values live. */
	error = plan_outputs(lowering);
	if (error != 0)
		return error;

	/* The registers the inputs, the descriptors and the lowering keep. */
	error = plan_registers(lowering);
	if (error != 0)
		return error;

	/* The Booleans that live in VCC_LO alone. */
	plan_conditions(lowering);

	/* How long a vertex program's inputs are read. */
	plan_input_lifetimes(lowering);

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
 * Finds the instructions not to lower: those that only define values,
 * none of which any instruction still lowered reads.  Walking backwards,
 * a dropped instruction's own reads no longer count, so a chain that only
 * fed it goes too.  An atomic always runs.
 */
static int
find_dead(
	struct gfx10_lowering *lowering)
{
	const struct drv_gpu_shader_ir *ir;
	const struct drv_gpu_shader_ir_inst *instruction;
	uint32_t sources[GFX10_MAX_SOURCES];
	uint32_t *reads;
	uint32_t index;
	uint32_t source;
	uint32_t channel;
	int count;
	int defines;
	int unread;

	ir = lowering->ir;

	/* How many instructions read each value. */
	reads = kern_calloc(ir->value_count + 1U, sizeof(*reads));
	if (reads == NULL)
		return ENOMEM;
	for (index = 0U; index < ir->instruction_count; index++) {
		count = ir_sources(&ir->instructions[index], sources);
		for (source = 0U; count > 0 && source < (uint32_t)count; source++)
			reads[sources[source]]++;
	}

	/* From the last instruction back: one whose values nobody reads is dropped, with its reads. */
	for (index = ir->instruction_count; index > 0U; index--) {
		instruction = &ir->instructions[index - 1U];
		defines = ir_defines(instruction);
		if (defines == 0 || instruction->op == DRV_GPU_IR_ATOMIC)
			continue;

		/* Every value it defines is unread. */
		unread = 1;
		for (channel = 0U; channel < (uint32_t)defines; channel++) {
			if (reads[instruction->dst + channel] != 0U)
				unread = 0;
		}
		if (unread == 0)
			continue;

		/* The instruction goes, and its sources lose a reader. */
		lowering->dead[index - 1U] = 1U;
		count = ir_sources(instruction, sources);
		for (source = 0U; count > 0 && source < (uint32_t)count; source++)
			reads[sources[source]]--;
	}
	kern_free(reads);

	/* Succeeded: the dropped instructions are marked. */
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

		/* The body's reads of earlier values, by the instructions that are lowered. */
		for (inner = start + 1U; inner <= index; inner++) {
			if (lowering->dead[inner] != 0U)
				continue;
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
	uint32_t reg;
	int error;

	program = lowering->program;
	error = 0;

	/* The inputs the hardware delivers, by the stage. */
	if (lowering->ir->stage == DRV_GPU_STAGE_COMPUTE) {
		/* The table address and the workgroup id in s0 .. s4, the local ids in v0 .. v2. */
		program->user_sgprs = AMDRDNA_GFX10_ABI_USER_SGPRS;
		lowering->vgpr_first = GFX10_CS_INPUT_VGPRS;
	} else if (lowering->ir->stage == DRV_GPU_STAGE_VERTEX) {
		/*
		 * The table, the base vertex, the first instance and the instance
		 * mask in s0 .. s4; v0 .. v3, taken until the start has read them.
		 */
		program->user_sgprs = AMDRDNA_GFX10_ABI_USER_SGPRS_VS;
		lowering->vgpr_first = 0U;
		for (index = 0U; index < GFX10_VS_INPUT_VGPRS; index++)
			lowering->taken[index] = 1U;
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

	/* A vertex program takes its vertex and instance numbers and fetched locations until their last read. */
	if (lowering->ir->stage == DRV_GPU_STAGE_VERTEX) {
		lowering->vgpr_highest = GFX10_VS_INPUT_VGPRS;

		/* The primitive number, copied at the start into the register its export reads. */
		if (program->exports_primitive_id != 0U) {
			error = take_vgpr(lowering, &reg);
			if (error != 0)
				return error;
			lowering->output_reg[GFX10_OUTPUT_LOCATIONS + 4U * AMDRDNA_GFX10_PRIMITIVE_ID_PARAMETER] = (uint16_t)reg;
		}

		error = take_vgpr(lowering, &lowering->vertex_index_vgpr);
		if (error != 0)
			return error;
		error = take_vgpr(lowering, &lowering->instance_index_vgpr);
		if (error != 0)
			return error;
		for (index = 0U; index < AMDRDNA_GFX10_LOCATIONS; index++) {
			if ((program->fetched_locations & (1U << index)) == 0U)
				continue;
			error = take_vgpr_block(lowering, 4U, &lowering->input_vgpr[index]);
			if (error != 0)
				return error;
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

	/* Succeeded: the registers are placed. */
	lowering->sgpr_highest = sgpr;
	return 0;
}

/*
 * Loads what every lane shares: the descriptors first, then a vertex
 * program's fetches (which wait only for the descriptors), then the
 * uniform words loaded once; a pixel program points M0 at its primitive's
 * attributes and keeps its alive lanes.
 */
static int
prologue(
	struct gfx10_lowering *lowering)
{
	struct amdrdna_gfx10_program *program;
	uint32_t index;
	uint32_t location;

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

	/* Each fetched location's vertex buffer descriptor. */
	for (location = 0U; location < AMDRDNA_GFX10_LOCATIONS; location++) {
		if ((program->fetched_locations & (1U << location)) == 0U)
			continue;
		emit_scalar_load(lowering,
				 AMDRDNA_GFX10_S_LOAD_DWORDX4,
				 lowering->vertex_sgpr[location],
				 AMDRDNA_GFX10_ABI_TABLE,
				 program->vertex_table_offset + AMDRDNA_GFX10_BUFFER_DESCRIPTOR_BYTES * location,
				 4U);
	}

	/* A vertex program fetches its inputs. */
	if (lowering->ir->stage == DRV_GPU_STAGE_VERTEX)
		prologue_vertex(lowering);

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

	/* A pixel program points M0 at its primitive's attributes. */
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

	/* A vertex program's hardware inputs, and the inputs only the start read, go back. */
	if (lowering->ir->stage == DRV_GPU_STAGE_VERTEX) {
		for (index = 0U; index < GFX10_VS_INPUT_VGPRS; index++)
			give_vgpr(lowering, index);
		return_inputs(lowering, 0U);
	}

	/* A list that could not grow fails the compile. */
	if (lowering->out_of_memory != 0)
		return ENOMEM;

	/* Succeeded: the shared values are on their way. */
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

	/* The comparison writes VCC_LO, which holds the Boolean from now on. */
	emit_vector(lowering,
		    AMDRDNA_GFX10_KIND_VECTOR_COMPARE,
		    opcode,
		    AMDRDNA_GFX10_VCC_LO,
		    value_operand(lowering, instruction->src[0]),
		    value_operand(lowering, instruction->src[1]),
		    0U);
	lowering->vcc_value = instruction->dst;

	/* A Boolean read as a value too is made in its VGPR. */
	if (lowering->condition_only[instruction->dst] == 0U)
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
	uint32_t found;

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

	/* The 16 bytes around the word, from the cache of chunks or loaded where there is room. */
	found = cache_chunk(lowering, slot, offset, 1);

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
	uint32_t predicate;
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
	} else if (instruction->op == DRV_GPU_IR_STORE_STORAGE) {
		predicate = GFX10_NOT_DEFINED;
		if (instruction->component != 0U)
			predicate = instruction->src[2];
		guarded = guard_writes(lowering, predicate);
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
	int signed_form;
	int error;

	numerator = value_operand(lowering, instruction->src[0]);
	denominator = value_operand(lowering, instruction->src[1]);
	signed_form = 0;
	if (instruction->op == DRV_GPU_IR_IDIV || instruction->op == DRV_GPU_IR_IREM)
		signed_form = 1;

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
	if (signed_form != 0) {
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
	int guarded;
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

	/* A predicated atomic runs only for the lanes whose predicate holds (and never for a pixel program's helpers). */
	predicate = GFX10_NOT_DEFINED;
	if (instruction->component == DRV_GPU_IR_ATOMIC_PREDICATED) {
		predicate = instruction->src[2];
		if (compare_swap != 0)
			predicate = instruction->src[3];
	}
	guarded = guard_writes(lowering, predicate);

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

	/* The lanes come back after a guarded atomic. */
	if (guarded != 0)
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
	uint32_t next;
	int uses_linear;
	int reads_point_coord;
	int reads_primitive_id;
	int writes_color_1;

	ir = lowering->ir;
	program = lowering->program;
	uses_linear = 0;
	reads_point_coord = 0;
	reads_primitive_id = 0;
	writes_color_1 = 0;

	/* A compute program has none of these. */
	if (ir->stage == DRV_GPU_STAGE_COMPUTE)
		return 0;

	/* Each input read and output written by an instruction that is lowered. */
	for (index = 0U; index < ir->instruction_count; index++) {
		instruction = &ir->instructions[index];
		lowering->index = index;
		location = instruction->location;
		if (lowering->dead[index] != 0U)
			continue;

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
				    (instruction->component & 0x1fU) == DRV_GPU_IR_TEXTURE_SAMPLE_BIAS ||
				    (instruction->component & 0x1fU) == DRV_GPU_IR_TEXTURE_SAMPLE_COMPARE ||
				    (instruction->component & 0x1fU) == DRV_GPU_IR_TEXTURE_SAMPLE_BIAS_COMPARE)) {
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

		/* A pixel program's output: a colour, or the second colour of a dual-source blend, exported as colour 1. */
		if (instruction->op == DRV_GPU_IR_STORE_OUTPUT) {
			if (location == DRV_GPU_IR_LOCATION_SECOND_COLOR) {
				program->dual_source = 1U;
				program->color_outputs |= 1U << 1;
				continue;
			}
			if (location >= GFX10_COLOR_TARGETS)
				return refuse(lowering, ENOTSUP, "a fragment output that is not compiled yet");
			if (location == 1U)
				writes_color_1 = 1;
			program->color_outputs |= 1U << location;
			continue;
		}

		/* A pixel program's input: the position, the face, the point sprite coordinate, the primitive number, or an attribute. */
		if (location == DRV_GPU_SHADER_LOCATION_FRAG_COORD) {
			program->pixel_inputs |= GFX10_PS_POSITION;
		} else if (location == DRV_GPU_SHADER_LOCATION_FRONT_FACING) {
			program->pixel_inputs |= GFX10_PS_FRONT_FACE;
		} else if (location == DRV_GPU_SHADER_LOCATION_POINT_COORD) {
			reads_point_coord = 1;
		} else if (location == DRV_GPU_SHADER_LOCATION_PRIMITIVE_ID) {
			reads_primitive_id = 1;
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

	/* A pixel program always has the perspective barycentrics; the linear ones when an input needs them. */
	if (ir->stage == DRV_GPU_STAGE_FRAGMENT) {
		program->pixel_inputs |= GFX10_PS_PERSP_CENTER;
		if (uses_linear != 0)
			program->pixel_inputs |= GFX10_PS_LINEAR_CENTER;
	}

	/* The second colour takes colour 1's export. */
	if (program->dual_source != 0U && writes_color_1 != 0)
		return refuse(lowering, ENOTSUP, "a second colour with a colour at location 1");

	/*
	 * The point sprite coordinate and the primitive number take the
	 * attributes after the highest the program reads: the hardware makes
	 * the coordinate, the primitive number is read flat from the parameter
	 * the vertex program exports it in.
	 */
	next = 0U;
	for (index = 0U; index < AMDRDNA_GFX10_LOCATIONS; index++) {
		if ((program->attribute_inputs & (1U << index)) != 0U)
			next = index + 1U;
	}
	if (reads_point_coord != 0) {
		if (next == AMDRDNA_GFX10_LOCATIONS)
			return refuse(lowering, ENOTSUP, "no attribute is left for the point sprite coordinate");
		program->point_coord_attribute = next;
		program->attribute_inputs |= 1U << next;
		next++;
	}
	if (reads_primitive_id != 0) {
		if (next == AMDRDNA_GFX10_LOCATIONS)
			return refuse(lowering, ENOTSUP, "no attribute is left for the primitive number");
		program->primitive_id_attribute = next;
		program->attribute_inputs |= 1U << next;
		program->flat_inputs |= 1U << next;
		next++;
	}

	/* A vertex program asked to export the primitive number keeps its parameter free for it. */
	if (ir->stage == DRV_GPU_STAGE_VERTEX && lowering->options.export_primitive_id != 0U) {
		if ((program->parameter_outputs & (1U << AMDRDNA_GFX10_PRIMITIVE_ID_PARAMETER)) != 0U)
			return refuse(lowering, ENOTSUP, "the primitive number's parameter is an output of the program");
		program->parameter_outputs |= 1U << AMDRDNA_GFX10_PRIMITIVE_ID_PARAMETER;
		program->exports_primitive_id = 1U;
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
 * Fetches a vertex program's inputs: the vertex and instance numbers with
 * their bases, and the four channels of each fetched location, by the
 * vertex number or, where the instance mask has the location's bit, by
 * the instance number.  The descriptors were loaded before.
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

	/* The primitive number into the register its export reads. */
	if (program->exports_primitive_id != 0U) {
		emit_vector(lowering,
			    AMDRDNA_GFX10_KIND_VECTOR_ONE,
			    AMDRDNA_GFX10_V_MOV_B32,
			    AMDRDNA_GFX10_V(lowering->output_reg[GFX10_OUTPUT_LOCATIONS + 4U * AMDRDNA_GFX10_PRIMITIVE_ID_PARAMETER]),
			    AMDRDNA_GFX10_V(GFX10_VS_PRIMITIVE_ID),
			    0U,
			    0U);
	}

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
	uint32_t attribute;
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
		lowering->vcc_value = instruction->dst;
		if (lowering->condition_only[instruction->dst] == 0U)
			boolean_from_condition(lowering, dst);
		return 0;
	}

	/* The attribute: the point sprite coordinate's, the primitive number's, or the location's own. */
	attribute = location;
	if (location == DRV_GPU_SHADER_LOCATION_POINT_COORD) {
		attribute = lowering->program->point_coord_attribute;
	} else if (location == DRV_GPU_SHADER_LOCATION_PRIMITIVE_ID) {
		attribute = lowering->program->primitive_id_attribute;
	} else if (location >= AMDRDNA_GFX10_LOCATIONS) {
		return refuse(lowering, ENOTSUP, "a fragment input that is not compiled yet");
	}

	/* An attribute: flat from the provoking vertex, or interpolated in two steps. */
	kern_memset(&inst, 0, sizeof(inst));
	inst.kind = AMDRDNA_GFX10_KIND_INTERPOLATE;
	inst.dst = (uint16_t)AMDRDNA_GFX10_V(dst);
	inst.target = (uint8_t)attribute;
	inst.channel = (uint8_t)instruction->component;
	if ((lowering->program->flat_inputs & (1U << attribute)) != 0U) {
		inst.opcode = AMDRDNA_GFX10_V_INTERP_MOV_F32;
		inst.src[0] = AMDRDNA_GFX10_INTERP_P0;
		emit(lowering, &inst);
		return 0;
	}

	/* The barycentrics: linear for a noperspective input, perspective otherwise (the point sprite coordinate too). */
	barycentric = lowering->perspective_vgpr;
	input = NULL;
	if (location < AMDRDNA_GFX10_LOCATIONS)
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

/*
 * Lowers the write of an output: nothing when the export reads the value
 * where it lives, otherwise a copy into the channel's VGPR, taken at the
 * first store and kept to the end.
 */
static int
lower_output(
	struct gfx10_lowering *lowering,
	const struct drv_gpu_shader_ir_inst *instruction)
{
	uint32_t channel;
	uint32_t reg;
	int error;

	/* The output channel stored. */
	channel = output_channel(lowering, instruction);
	if (channel == GFX10_OUTPUT_CHANNELS)
		return refuse(lowering, ENOTSUP, "outputs of this stage are not compiled");

	/* The export reads the value where it lives. */
	if (lowering->output_value[channel] == instruction->src[0])
		return 0;

	/* The channel's own VGPR, taken at its first store. */
	if (lowering->output_reg[channel] == GFX10_NO_REG) {
		error = take_vgpr(lowering, &reg);
		if (error != 0)
			return error;
		lowering->output_reg[channel] = (uint16_t)reg;
	}

	/* The value into it. */
	emit_vector(lowering,
		    AMDRDNA_GFX10_KIND_VECTOR_ONE,
		    AMDRDNA_GFX10_V_MOV_B32,
		    AMDRDNA_GFX10_V(lowering->output_reg[channel]),
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
 * Lowers a sample of a sampled image, a fetch of one of its texels, or a
 * query of its size.
 *
 * The address VGPRs are copied in a row in the order the image access
 * takes them: the packed constant offset, the bias, the depth reference,
 * the gradients (du/dx, dv/dx, du/dy, dv/dy), u and v, the level; a size
 * query takes the level alone.  A pixel program samples with the
 * derivatives its whole quads give; another stage samples level 0 (or the
 * level given).  The channels go to the values from dst, which have
 * consecutive VGPRs: four, or for a depth compare the one result.
 *
 * Only a 2D image (or a 1D one, which the executor makes a 2D image one
 * texel high) is sampled: the IR does not say an image's dimension, which
 * the image access needs, so a message with the parameters of a 3D image,
 * a cube or an array is refused.
 *
 * Procedure referenced: Mesa 25.0.7
 * src/amd/compiler/aco_instruction_selection.cpp (visit_tex: the address
 * order offset, bias, compare, derivatives (ddx, then ddy), coordinates,
 * then the level, lines 9216 and 9411-9427; the size query with the level
 * alone and no sampler, line 9279).
 */
static int
lower_sample(
	struct gfx10_lowering *lowering,
	const struct drv_gpu_shader_ir_inst *instruction,
	uint32_t dst)
{
	const struct drv_gpu_shader_ir_uniform *uniform;
	struct amdrdna_gfx10_inst inst;
	struct gfx10_sample sample;
	uint32_t addresses[GFX10_SAMPLE_ADDRESSES];
	uint32_t count;
	uint32_t index;
	uint32_t slot;
	uint32_t message;
	uint32_t first;
	uint32_t params;
	uint32_t expected;
	uint32_t offset_bits;
	uint32_t packed;
	uint32_t block;
	uint32_t inst_count;
	int32_t offset_u;
	int32_t offset_v;
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

	/* The sample's parameters, from the two-coordinate operations. */
	kern_memset(&sample, 0, sizeof(sample));
	sample.u = instruction->src[0];
	sample.v = instruction->src[1];
	offset_bits = (instruction->component >> 4) & 0xffU;
	if (instruction->op == DRV_GPU_IR_SAMPLE_BIAS) {
		sample.biased = 1;
		sample.bias = instruction->src[2];
	} else if (instruction->op == DRV_GPU_IR_SAMPLE_LOD) {
		sample.leveled = 1;
		sample.level = instruction->src[2];
	}

	/* A message's parameters in order, the offset in bits 23:8; the count says a 2D image. */
	if (instruction->op == DRV_GPU_IR_TEXTURE) {
		message = instruction->component & 0x1fU;
		offset_bits = (instruction->component >> 12) & 0xffU;
		first = instruction->src[0];
		params = instruction->src[1];

		/* Picks the parameters by the message. */
		switch (message) {
		case DRV_GPU_IR_TEXTURE_SAMPLE:
			expected = 2U;
			sample.u = first;
			sample.v = first + 1U;
			break;
		case DRV_GPU_IR_TEXTURE_SAMPLE_BIAS:
			expected = 3U;
			sample.biased = 1;
			sample.bias = first;
			sample.u = first + 1U;
			sample.v = first + 2U;
			break;
		case DRV_GPU_IR_TEXTURE_SAMPLE_LOD:
			expected = 3U;
			sample.leveled = 1;
			sample.level = first;
			sample.u = first + 1U;
			sample.v = first + 2U;
			break;
		case DRV_GPU_IR_TEXTURE_SAMPLE_COMPARE:
			expected = 3U;
			sample.compared = 1;
			sample.reference = first;
			sample.u = first + 1U;
			sample.v = first + 2U;
			break;
		case DRV_GPU_IR_TEXTURE_SAMPLE_BIAS_COMPARE:
			expected = 4U;
			sample.compared = 1;
			sample.reference = first;
			sample.biased = 1;
			sample.bias = first + 1U;
			sample.u = first + 2U;
			sample.v = first + 3U;
			break;
		case DRV_GPU_IR_TEXTURE_SAMPLE_LOD_COMPARE:
			expected = 4U;
			sample.compared = 1;
			sample.reference = first;
			sample.leveled = 1;
			sample.level = first + 1U;
			sample.u = first + 2U;
			sample.v = first + 3U;
			break;
		case DRV_GPU_IR_TEXTURE_SAMPLE_DERIVS:
			/* u, du/dx, du/dy, v, dv/dx, dv/dy. */
			expected = 6U;
			sample.gradients = 1;
			sample.u = first;
			sample.gradient[0] = first + 1U;
			sample.gradient[2] = first + 2U;
			sample.v = first + 3U;
			sample.gradient[1] = first + 4U;
			sample.gradient[3] = first + 5U;
			break;
		case DRV_GPU_IR_TEXTURE_SAMPLE_DERIV_COMPARE:
			expected = 7U;
			sample.compared = 1;
			sample.reference = first;
			sample.gradients = 1;
			sample.u = first + 1U;
			sample.gradient[0] = first + 2U;
			sample.gradient[2] = first + 3U;
			sample.v = first + 4U;
			sample.gradient[1] = first + 5U;
			sample.gradient[3] = first + 6U;
			break;
		case DRV_GPU_IR_TEXTURE_LD:
			expected = 3U;
			sample.fetch = 1;
			sample.u = first;
			sample.v = first + 1U;
			sample.leveled = 1;
			sample.level = first + 2U;
			break;
		case DRV_GPU_IR_TEXTURE_RESINFO:
			expected = 1U;
			sample.size_query = 1;
			sample.leveled = 1;
			sample.level = first;
			break;
		default:
			return refuse(lowering, ENOTSUP, "the texture message is not compiled yet");
		}

		/* More parameters than a 2D image's: a 3D image, a cube or an array. */
		if (params != expected)
			return refuse(lowering, ENOTSUP, "a texture message of an image other than 2D (the IR does not carry the image's dimension)");
	}

	/* A sample at the constant level 0 takes the access without a level. */
	if (sample.leveled != 0 &&
	    sample.fetch == 0 &&
	    sample.size_query == 0 &&
	    lowering->operand_code[sample.level] == constant_operand(0U)) {
		sample.leveled = 0;
		sample.level_zero = 1;
	}

	/* The implicit derivatives a pixel program's whole quads give; a bias needs them. */
	derivatives = 0;
	if (lowering->ir->stage == DRV_GPU_STAGE_FRAGMENT &&
	    sample.leveled == 0 &&
	    sample.level_zero == 0 &&
	    sample.gradients == 0 &&
	    sample.size_query == 0)
		derivatives = 1;
	if (sample.biased != 0 && derivatives == 0)
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
	if (sample.fetch != 0 || sample.size_query != 0)
		packed = 0U;

	/* The address VGPRs in order. */
	count = 0U;
	if (sample.size_query != 0) {
		addresses[count++] = sample.level;
	} else {
		if (packed != 0U)
			addresses[count++] = GFX10_NOT_DEFINED;
		if (sample.biased != 0)
			addresses[count++] = sample.bias;
		if (sample.compared != 0)
			addresses[count++] = sample.reference;
		if (sample.gradients != 0) {
			for (index = 0U; index < 4U; index++)
				addresses[count++] = sample.gradient[index];
		}
		addresses[count++] = sample.u;
		addresses[count++] = sample.v;
		if (sample.leveled != 0)
			addresses[count++] = sample.level;
	}

	/* The coordinates alone already in two VGPRs in a row are the address as they are. */
	if (count == 2U &&
	    addresses[0] == sample.u &&
	    lowering->operand_code[sample.u] == GFX10_NO_OPERAND &&
	    lowering->operand_code[sample.v] == GFX10_NO_OPERAND &&
	    lowering->vgpr[sample.v] == lowering->vgpr[sample.u] + 1U) {
		block = lowering->vgpr[sample.u];
		count = 0U;
		inst_count = 2U;
	} else {
		/* A block of address VGPRs. */
		error = take_vgpr_block(lowering, count, &block);
		if (error != 0)
			return error;
		inst_count = count;
	}

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

	/* The image access: the opcode by what the sample has; a fetch and a size query take no sampler. */
	kern_memset(&inst, 0, sizeof(inst));
	inst.kind = AMDRDNA_GFX10_KIND_IMAGE;
	inst.opcode = (uint16_t)sample_opcode(&sample, derivatives, packed != 0U);
	inst.dst = (uint16_t)AMDRDNA_GFX10_V(dst);
	inst.src[0] = (uint16_t)AMDRDNA_GFX10_V(block);
	inst.src[1] = (uint16_t)lowering->image_sgpr[slot];
	inst.src[2] = (uint16_t)lowering->sampler_sgpr[slot];
	inst.mask = 0xfU;
	inst.count = (uint8_t)inst_count;
	if (sample.fetch != 0) {
		inst.src[2] = AMDRDNA_GFX10_NULL;
		inst.flags = AMDRDNA_GFX10_FLAG_UNNORMALIZED;
	}
	if (sample.size_query != 0)
		inst.src[2] = AMDRDNA_GFX10_NULL;
	if (sample.compared != 0)
		inst.mask = 0x1U;
	emit(lowering, &inst);

	/* The address block goes back. */
	for (index = 0U; index < count; index++)
		give_vgpr(lowering, block + index);

	/* Succeeded: the channels are in the values from dst. */
	return 0;
}

/* Picks the image opcode of a sample by what it has: a compare, gradients, a level, a bias, the implicit derivatives, an offset. */
static unsigned
sample_opcode(
	const struct gfx10_sample *sample,
	int derivatives,
	int offset)
{
	/* A fetch and a size query. */
	if (sample->fetch != 0)
		return AMDRDNA_GFX10_IMAGE_LOAD_MIP;
	if (sample->size_query != 0)
		return AMDRDNA_GFX10_IMAGE_GET_RESINFO;

	/* A depth compare. */
	if (sample->compared != 0) {
		if (sample->gradients != 0)
			return offset != 0 ? AMDRDNA_GFX10_IMAGE_SAMPLE_C_D_O : AMDRDNA_GFX10_IMAGE_SAMPLE_C_D;
		if (sample->leveled != 0)
			return offset != 0 ? AMDRDNA_GFX10_IMAGE_SAMPLE_C_L_O : AMDRDNA_GFX10_IMAGE_SAMPLE_C_L;
		if (sample->biased != 0)
			return offset != 0 ? AMDRDNA_GFX10_IMAGE_SAMPLE_C_B_O : AMDRDNA_GFX10_IMAGE_SAMPLE_C_B;
		if (derivatives != 0)
			return offset != 0 ? AMDRDNA_GFX10_IMAGE_SAMPLE_C_O : AMDRDNA_GFX10_IMAGE_SAMPLE_C;
		return offset != 0 ? AMDRDNA_GFX10_IMAGE_SAMPLE_C_LZ_O : AMDRDNA_GFX10_IMAGE_SAMPLE_C_LZ;
	}

	/* A plain sample. */
	if (sample->gradients != 0)
		return offset != 0 ? AMDRDNA_GFX10_IMAGE_SAMPLE_D_O : AMDRDNA_GFX10_IMAGE_SAMPLE_D;
	if (sample->leveled != 0)
		return offset != 0 ? AMDRDNA_GFX10_IMAGE_SAMPLE_L_O : AMDRDNA_GFX10_IMAGE_SAMPLE_L;
	if (sample->biased != 0)
		return offset != 0 ? AMDRDNA_GFX10_IMAGE_SAMPLE_B_O : AMDRDNA_GFX10_IMAGE_SAMPLE_B;
	if (derivatives != 0)
		return offset != 0 ? AMDRDNA_GFX10_IMAGE_SAMPLE_O : AMDRDNA_GFX10_IMAGE_SAMPLE;
	return offset != 0 ? AMDRDNA_GFX10_IMAGE_SAMPLE_LZ_O : AMDRDNA_GFX10_IMAGE_SAMPLE_LZ;
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
static int
finish_exports(
	struct gfx10_lowering *lowering)
{
	struct amdrdna_gfx10_program *program;
	unsigned nothing[4];
	uint32_t location;
	uint32_t channel;
	uint32_t mask;
	uint32_t last;
	uint32_t flags;
	int error;

	program = lowering->program;
	nothing[0] = 0U;
	nothing[1] = 0U;
	nothing[2] = 0U;
	nothing[3] = 0U;

	/* A vertex program: the positions, then the parameters. */
	if (lowering->ir->stage == DRV_GPU_STAGE_VERTEX) {
		flags = AMDRDNA_GFX10_FLAG_VALID_MASK;
		if (program->writes_point_size == 0U)
			flags |= AMDRDNA_GFX10_FLAG_LAST;
		error = export_location(lowering, AMDRDNA_GFX10_EXPORT_POSITION, GFX10_OUTPUT_POSITION, 0xfU, flags);
		if (error != 0)
			return error;

		/* The point size, the last position. */
		if (program->writes_point_size != 0U) {
			error = export_location(lowering, AMDRDNA_GFX10_EXPORT_POSITION + 1U, GFX10_OUTPUT_POINT_SIZE, 0x1U, AMDRDNA_GFX10_FLAG_LAST);
			if (error != 0)
				return error;
		}

		/* Each parameter, the channels the program writes. */
		for (location = 0U; location < AMDRDNA_GFX10_LOCATIONS; location++) {
			if ((program->parameter_outputs & (1U << location)) == 0U)
				continue;
			mask = 0U;
			for (channel = 0U; channel < 4U; channel++) {
				if (lowering->output_written[GFX10_OUTPUT_LOCATIONS + 4U * location + channel] != 0U)
					mask |= 1U << channel;
			}
			error = export_location(lowering,
						AMDRDNA_GFX10_EXPORT_PARAMETER + location,
						GFX10_OUTPUT_LOCATIONS + 4U * location,
						mask,
						0U);
			if (error != 0)
				return error;
		}
		return 0;
	}

	/* Only a pixel program exports otherwise. */
	if (lowering->ir->stage != DRV_GPU_STAGE_FRAGMENT)
		return 0;

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
		emit_export(lowering, AMDRDNA_GFX10_EXPORT_NOTHING, nothing, 0U, AMDRDNA_GFX10_FLAG_LAST | AMDRDNA_GFX10_FLAG_VALID_MASK);
		return 0;
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
		error = export_colour(lowering, AMDRDNA_GFX10_EXPORT_COLOR + location, GFX10_OUTPUT_LOCATIONS + 4U * location, flags);
		if (error != 0)
			return error;
	}

	/* Succeeded: every result is exported. */
	return 0;
}

/* Exports the channels mask enables of the output channels from first_channel to a target. */
static int
export_location(
	struct gfx10_lowering *lowering,
	uint32_t target,
	uint32_t first_channel,
	uint32_t mask,
	uint32_t flags)
{
	unsigned sources[4];
	uint32_t channel;
	int error;

	/* The VGPR each enabled channel is read from. */
	for (channel = 0U; channel < 4U; channel++) {
		sources[channel] = 0U;
		if ((mask & (1U << channel)) == 0U)
			continue;
		error = export_source(lowering, first_channel + channel, &sources[channel]);
		if (error != 0)
			return error;
	}

	/* The export. */
	emit_export(lowering, target, sources, mask, flags);

	/* Succeeded: the channels are exported. */
	return 0;
}

/*
 * Exports a colour from the output channels from first_channel on as four
 * 16-bit floats: red and green packed into one VGPR, blue and alpha into
 * another (each rounded toward zero), in a compressed export.
 *
 * Procedure referenced: Mesa 25.0.7
 * src/amd/compiler/instruction_selection/aco_select_ps_epilog.cpp
 * (export_fs_mrt_color, the FP16_ABGR case of GFX10: v_cvt_pkrtz_f16_f32 of
 * channels 0 and 1, and 2 and 3, all four channels enabled, compressed).
 */
static int
export_colour(
	struct gfx10_lowering *lowering,
	uint32_t target,
	uint32_t first_channel,
	uint32_t flags)
{
	unsigned sources[4];
	unsigned packed[4];
	uint32_t channel;
	uint32_t reg;
	int error;

	/* The VGPR each channel is read from. */
	for (channel = 0U; channel < 4U; channel++) {
		error = export_source(lowering, first_channel + channel, &sources[channel]);
		if (error != 0)
			return error;
	}

	/* Two VGPRs, each with two channels packed. */
	packed[2] = 0U;
	packed[3] = 0U;
	for (channel = 0U; channel < 2U; channel++) {
		error = take_vgpr(lowering, &reg);
		if (error != 0)
			return error;
		packed[channel] = AMDRDNA_GFX10_V(reg);
		emit_vector(lowering,
			    AMDRDNA_GFX10_KIND_VECTOR_TWO,
			    AMDRDNA_GFX10_V_CVT_PKRTZ_F16_F32,
			    packed[channel],
			    sources[2U * channel],
			    sources[2U * channel + 1U],
			    0U);
	}

	/* The export of the four channels. */
	emit_export(lowering, target, packed, 0xfU, flags | AMDRDNA_GFX10_FLAG_COMPRESSED);

	/* Succeeded: the colour is exported. */
	return 0;
}

/*
 * Finds the VGPR an output channel is exported from: its value's own,
 * the VGPR its stores copied into, or (a value without a VGPR, a channel
 * never stored) a VGPR taken now, the value copied into it.
 */
static int
export_source(
	struct gfx10_lowering *lowering,
	uint32_t channel,
	unsigned *code)
{
	unsigned source;
	uint32_t reg;
	int error;

	/* The value where it lives, when that is a VGPR. */
	source = GFX10_NO_OPERAND;
	if (lowering->output_value[channel] != GFX10_NOT_DEFINED) {
		source = value_operand(lowering, lowering->output_value[channel]);
		if (source >= AMDRDNA_GFX10_VGPR_FIRST) {
			*code = source;
			return 0;
		}
	}

	/* The channel's own VGPR, taken now when no store took it. */
	if (lowering->output_reg[channel] == GFX10_NO_REG) {
		error = take_vgpr(lowering, &reg);
		if (error != 0)
			return error;
		lowering->output_reg[channel] = (uint16_t)reg;
	}

	/* A value without a VGPR is copied into it. */
	if (source != GFX10_NO_OPERAND) {
		emit_vector(lowering,
			    AMDRDNA_GFX10_KIND_VECTOR_ONE,
			    AMDRDNA_GFX10_V_MOV_B32,
			    AMDRDNA_GFX10_V(lowering->output_reg[channel]),
			    source,
			    0U,
			    0U);
	}

	/* Succeeded: the channel's VGPR. */
	*code = AMDRDNA_GFX10_V(lowering->output_reg[channel]);
	return 0;
}

/* Appends an export of the channels mask enables, each from its VGPR in sources[]. */
static void
emit_export(
	struct gfx10_lowering *lowering,
	uint32_t target,
	const unsigned *sources,
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
			inst.src[channel] = (uint16_t)sources[channel];
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
	unsigned nothing[4];
	uint32_t waited_count;
	uint32_t failed;
	int error;

	program = lowering->program;

	/* A graphics program exports its results. */
	error = finish_exports(lowering);
	if (error != 0)
		return error;

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
		nothing[0] = 0U;
		nothing[1] = 0U;
		nothing[2] = 0U;
		nothing[3] = 0U;
		emit_export(lowering, AMDRDNA_GFX10_EXPORT_NOTHING, nothing, 0U, AMDRDNA_GFX10_FLAG_LAST | AMDRDNA_GFX10_FLAG_VALID_MASK);
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
	lowering->guard_condition = predicate;
}

/*
 * Narrows the lanes of a memory write to those whose predicate holds
 * (GFX10_NOT_DEFINED: every lane run) and, in a pixel program that runs
 * whole quads, to the lanes alive: the helper lanes write nothing.
 * Returns nonzero when the lanes were narrowed, for guard_end().
 */
static int
guard_writes(
	struct gfx10_lowering *lowering,
	uint32_t predicate)
{
	unsigned narrowed;

	/* Without helper lanes only a predicate narrows. */
	if (lowering->whole_quads == 0) {
		if (predicate == GFX10_NOT_DEFINED)
			return 0;
		guard_begin(lowering, predicate);
		return 1;
	}

	/* The alive lanes alone, VCC_LO left as it is. */
	if (predicate == GFX10_NOT_DEFINED) {
		lowering->guard_condition = lowering->vcc_value;
		emit_scalar(lowering,
			    AMDRDNA_GFX10_KIND_SCALAR_ONE,
			    AMDRDNA_GFX10_S_AND_SAVEEXEC_B32,
			    lowering->guard_save,
			    lowering->live_sgpr,
			    0U);
		return 1;
	}

	/* The predicate's lanes that are alive, through a scalar temporary so that VCC_LO keeps the predicate. */
	condition_from_boolean(lowering, predicate);
	narrowed = next_scalar_temp(lowering);
	emit_scalar(lowering,
		    AMDRDNA_GFX10_KIND_SCALAR_TWO,
		    AMDRDNA_GFX10_S_AND_B32,
		    narrowed,
		    AMDRDNA_GFX10_VCC_LO,
		    lowering->live_sgpr);
	emit_scalar(lowering,
		    AMDRDNA_GFX10_KIND_SCALAR_ONE,
		    AMDRDNA_GFX10_S_AND_SAVEEXEC_B32,
		    lowering->guard_save,
		    narrowed,
		    0U);
	lowering->guard_condition = predicate;
	return 1;
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

	/* The guarded access wrote no VCC_LO, and the lanes are those the predicate was taken for. */
	lowering->vcc_value = lowering->guard_condition;
}

/* Sets VCC_LO for the lanes where a Boolean value is true. */
static void
condition_from_boolean(
	struct gfx10_lowering *lowering,
	uint32_t value)
{
	/* VCC_LO already holds it. */
	if (lowering->vcc_value == value)
		return;

	/* A Boolean kept only in VCC_LO has no VGPR to take it from. */
	if (lowering->condition_only[value] != 0U) {
		lowering->lost_condition = 1;
		return;
	}

	/* A lane's Boolean is true when it is not zero. */
	emit_vector(lowering,
		    AMDRDNA_GFX10_KIND_VECTOR_COMPARE,
		    AMDRDNA_GFX10_V_CMP_NE_U32,
		    AMDRDNA_GFX10_VCC_LO,
		    AMDRDNA_GFX10_ZERO,
		    value_operand(lowering, value),
		    0U);
	lowering->vcc_value = value;
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

	/* A write of VCC_LO or of the lane mask, or a label, leaves VCC_LO holding no known Boolean. */
	forget_condition(lowering, inst);
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

	/* The chunks start at a multiple of four SGPRs, after the cache (and its spare chunk) for the words that get none. */
	base = (*sgpr + 3U) & ~3U;
	lowering->cache_sgpr = base;
	base += 4U * (GFX10_CHUNK_CACHE + 1U);
	*sgpr = base;

	/* Each uniform read at a constant offset. */
	for (index = 0U; index < lowering->ir->instruction_count; index++) {
		instruction = &lowering->ir->instructions[index];
		if (instruction->op != DRV_GPU_IR_LOAD_PUSH && instruction->op != DRV_GPU_IR_LOAD_UBO)
			continue;
		if (lowering->moved[instruction->dst] != 0U)
			continue;
		if (lowering->dead[index] != 0U)
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
 * Finds the output channels exported where their values live: a channel
 * stored once in the program, with a value no MOVE writes that an
 * instruction makes in a VGPR of its own (not a constant, a uniform word
 * or a built-in value, which have other operands).  Such a value lives to
 * the end of the program.
 */
static int
plan_outputs(
	struct gfx10_lowering *lowering)
{
	const struct drv_gpu_shader_ir *ir;
	const struct drv_gpu_shader_ir_inst *instruction;
	uint32_t *definer;
	uint32_t stores[GFX10_OUTPUT_CHANNELS];
	uint32_t stored[GFX10_OUTPUT_CHANNELS];
	uint32_t index;
	uint32_t channel;
	uint32_t value;
	int defines;

	ir = lowering->ir;
	if (ir->stage == DRV_GPU_STAGE_COMPUTE)
		return 0;

	/* The operation that first defines each value, among those lowered. */
	definer = kern_calloc(ir->value_count + 1U, sizeof(*definer));
	if (definer == NULL)
		return ENOMEM;
	for (index = 0U; index <= ir->value_count; index++)
		definer[index] = GFX10_NOT_DEFINED;
	for (index = 0U; index < ir->instruction_count; index++) {
		instruction = &ir->instructions[index];
		if (lowering->dead[index] != 0U)
			continue;
		defines = ir_defines(instruction);
		for (channel = 0U; channel < (uint32_t)defines; channel++) {
			if (definer[instruction->dst + channel] == GFX10_NOT_DEFINED)
				definer[instruction->dst + channel] = (uint32_t)instruction->op;
		}
	}

	/* How many times each output channel is stored, and with what. */
	for (channel = 0U; channel < GFX10_OUTPUT_CHANNELS; channel++) {
		stores[channel] = 0U;
		stored[channel] = GFX10_NOT_DEFINED;
	}
	for (index = 0U; index < ir->instruction_count; index++) {
		instruction = &ir->instructions[index];
		if (instruction->op != DRV_GPU_IR_STORE_OUTPUT || lowering->dead[index] != 0U)
			continue;
		channel = output_channel(lowering, instruction);
		if (channel == GFX10_OUTPUT_CHANNELS)
			continue;
		stores[channel]++;
		stored[channel] = instruction->src[0];
		lowering->output_written[channel] = 1U;
	}

	/* The primitive number a vertex program exports is written at the start. */
	if (lowering->program->exports_primitive_id != 0U)
		lowering->output_written[GFX10_OUTPUT_LOCATIONS + 4U * AMDRDNA_GFX10_PRIMITIVE_ID_PARAMETER] = 1U;

	/* Each channel stored once with a value made in a VGPR of its own. */
	for (channel = 0U; channel < GFX10_OUTPUT_CHANNELS; channel++) {
		if (stores[channel] != 1U)
			continue;
		value = stored[channel];
		if (lowering->moved[value] != 0U)
			continue;

		/* The operations whose values have operands of their own, or no definition. */
		switch (definer[value]) {
		case GFX10_NOT_DEFINED:
		case DRV_GPU_IR_CONST:
		case DRV_GPU_IR_ICONST:
		case DRV_GPU_IR_BOOL:
		case DRV_GPU_IR_LOAD_PUSH:
		case DRV_GPU_IR_LOAD_UBO:
		case DRV_GPU_IR_LOAD_SYSTEM:
		case DRV_GPU_IR_STORAGE_SIZE:
			continue;
		default:
			break;
		}

		/* The export reads the value, which lives to the end. */
		lowering->output_value[channel] = value;
		lowering->last_use[value] = ir->instruction_count + 1U;
	}
	kern_free(definer);

	/* Succeeded: the channels exported in place are chosen. */
	return 0;
}

/*
 * Finds the Booleans that never need a VGPR: made by a comparison, read
 * only as the condition of selections, discards, a loop's end or guarded
 * accesses, with nothing between the comparison and the last read that
 * writes VCC_LO or widens the lane mask.  Such a Boolean stays in the
 * VCC_LO its comparison wrote.
 */
static void
plan_conditions(
	struct gfx10_lowering *lowering)
{
	const struct drv_gpu_shader_ir *ir;
	const struct drv_gpu_shader_ir_inst *instruction;
	const struct drv_gpu_shader_ir_inst *reader;
	uint32_t sources[GFX10_MAX_SOURCES];
	uint32_t index;
	uint32_t later;
	uint32_t last;
	uint32_t source;
	uint32_t condition;
	uint32_t value;
	int count;
	int defines;
	int kept;
	int touches;

	ir = lowering->ir;

	/* Each comparison. */
	for (index = 0U; index < ir->instruction_count; index++) {
		instruction = &ir->instructions[index];
		defines = defines_condition(lowering, instruction);
		if (defines == 0 || lowering->dead[index] != 0U)
			continue;
		value = instruction->dst;

		/* A Boolean a MOVE writes, or that nothing reads, keeps the usual VGPR. */
		if (lowering->moved[value] != 0U)
			continue;
		last = lowering->last_use[value];
		if (last <= index + 1U)
			continue;

		/* Each instruction up to the last read must leave VCC_LO and widen no lanes. */
		kept = 1;
		for (later = index + 1U; kept != 0 && later < last && later < ir->instruction_count; later++) {
			reader = &ir->instructions[later];
			if (lowering->dead[later] != 0U)
				continue;
			condition = condition_source(reader);

			/* The Boolean read as a value needs its VGPR. */
			count = ir_sources(reader, sources);
			for (source = 0U; count > 0 && source < (uint32_t)count; source++) {
				if (sources[source] != value)
					continue;
				if (condition != GFX10_NOT_DEFINED && source == condition)
					continue;
				kept = 0;
			}

			/* An instruction that never touches VCC_LO or the lanes keeps the Boolean. */
			touches = touches_condition(lowering, reader);
			if (touches == 0)
				continue;

			/* Another condition, or a discard before the last read, ends it. */
			if (condition == GFX10_NOT_DEFINED || reader->src[condition] != value) {
				kept = 0;
			} else if (reader->op == DRV_GPU_IR_KILL && later + 1U != last) {
				kept = 0;
			}
		}

		/* The Boolean lives in VCC_LO alone. */
		if (kept != 0)
			lowering->condition_only[value] = 1U;
	}
}

/* Reports whether an IR instruction makes a Boolean with a comparison into VCC_LO. */
static int
defines_condition(
	const struct gfx10_lowering *lowering,
	const struct drv_gpu_shader_ir_inst *instruction)
{
	/* Picks by the operation. */
	switch (instruction->op) {
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
		return 1;
	case DRV_GPU_IR_LOAD_INPUT:
		/* The face of a pixel is compared too. */
		if (lowering->ir->stage == DRV_GPU_STAGE_FRAGMENT &&
		    instruction->location == DRV_GPU_SHADER_LOCATION_FRONT_FACING)
			return 1;
		return 0;
	default:
		return 0;
	}
}

/*
 * Reports which source of an IR instruction is read as a condition (the
 * index into src[]), or GFX10_NOT_DEFINED when none is.
 */
static uint32_t
condition_source(
	const struct drv_gpu_shader_ir_inst *instruction)
{
	/* Picks by the operation and, for a guarded access, whether it is guarded. */
	switch (instruction->op) {
	case DRV_GPU_IR_SELECT:
	case DRV_GPU_IR_KILL:
	case DRV_GPU_IR_LOOP_END:
		return 0U;
	case DRV_GPU_IR_LOAD_STORAGE:
	case DRV_GPU_IR_LOAD_SHARED:
		if (instruction->component != 0U)
			return 1U;
		return GFX10_NOT_DEFINED;
	case DRV_GPU_IR_STORE_STORAGE:
	case DRV_GPU_IR_STORE_SHARED:
		if (instruction->component != 0U)
			return 2U;
		return GFX10_NOT_DEFINED;
	case DRV_GPU_IR_ATOMIC:
		if (instruction->component != DRV_GPU_IR_ATOMIC_PREDICATED)
			return GFX10_NOT_DEFINED;
		if (instruction->immediate == DRV_GPU_IR_ATOMIC_CMPXCHG)
			return 3U;
		return 2U;
	default:
		return GFX10_NOT_DEFINED;
	}
}

/* Reports whether lowering an IR instruction may write VCC_LO or change the lanes. */
static int
touches_condition(
	const struct gfx10_lowering *lowering,
	const struct drv_gpu_shader_ir_inst *instruction)
{
	uint32_t condition;
	int defines;

	/* A comparison writes VCC_LO. */
	defines = defines_condition(lowering, instruction);
	if (defines != 0)
		return 1;

	/* An instruction that takes a condition sets VCC_LO from it. */
	condition = condition_source(instruction);
	if (condition != GFX10_NOT_DEFINED)
		return 1;

	/* The loops and the divisions; everything else leaves VCC_LO and the lanes alone. */
	switch (instruction->op) {
	case DRV_GPU_IR_LOOP_BEGIN:
	case DRV_GPU_IR_UDIV:
	case DRV_GPU_IR_UMOD:
	case DRV_GPU_IR_IDIV:
	case DRV_GPU_IR_IREM:
		return 1;
	default:
		return 0;
	}
}

/* Forgets the Boolean VCC_LO held when an instruction writes it or the lane mask, or is a label. */
static void
forget_condition(
	struct gfx10_lowering *lowering,
	const struct amdrdna_gfx10_inst *inst)
{
	struct amdrdna_gfx10_access access;
	uint32_t index;
	uint32_t first;
	uint32_t end;

	/* A label is reached from elsewhere. */
	if (inst->kind == AMDRDNA_GFX10_KIND_LABEL) {
		lowering->vcc_value = GFX10_NOT_DEFINED;
		return;
	}

	/* Each register the instruction writes. */
	amdrdna_gfx10_describe(inst, &access);
	for (index = 0U; index < access.writes_used; index++) {
		first = access.writes[index].first;
		end = first + access.writes[index].count;
		if (first <= AMDRDNA_GFX10_VCC_LO && AMDRDNA_GFX10_VCC_LO < end)
			lowering->vcc_value = GFX10_NOT_DEFINED;
		if (first <= AMDRDNA_GFX10_EXEC_LO && AMDRDNA_GFX10_EXEC_LO < end)
			lowering->vcc_value = GFX10_NOT_DEFINED;
	}
}

/* Reports the output channel a store writes (GFX10_OUTPUT_CHANNELS for one the compiler does not export). */
static uint32_t
output_channel(
	const struct gfx10_lowering *lowering,
	const struct drv_gpu_shader_ir_inst *instruction)
{
	uint32_t location;

	location = instruction->location;

	/* Four channels to a location. */
	if (instruction->component > 3U)
		return GFX10_OUTPUT_CHANNELS;

	/* A pixel program's colours; the second colour is exported as colour 1. */
	if (lowering->ir->stage == DRV_GPU_STAGE_FRAGMENT) {
		if (location == DRV_GPU_IR_LOCATION_SECOND_COLOR)
			return GFX10_OUTPUT_LOCATIONS + 4U + instruction->component;
		if (location >= GFX10_COLOR_TARGETS)
			return GFX10_OUTPUT_CHANNELS;
		return GFX10_OUTPUT_LOCATIONS + 4U * location + instruction->component;
	}

	/* Only a vertex program has the others. */
	if (lowering->ir->stage != DRV_GPU_STAGE_VERTEX)
		return GFX10_OUTPUT_CHANNELS;

	/* The position, the point size, or a parameter. */
	if (location == DRV_GPU_IR_LOCATION_POSITION)
		return GFX10_OUTPUT_POSITION + instruction->component;
	if (location == DRV_GPU_IR_LOCATION_POINT_SIZE)
		return GFX10_OUTPUT_POINT_SIZE;
	if (location < AMDRDNA_GFX10_LOCATIONS)
		return GFX10_OUTPUT_LOCATIONS + 4U * location + instruction->component;
	return GFX10_OUTPUT_CHANNELS;
}

/*
 * Reports whether an IR instruction defines a value that has an operand
 * of its own and so needs no instruction, and gives it that operand: an
 * inline constant; a uniform word loaded at the start or into the cache;
 * a vertex program's fetched channel or vertex or instance number; a
 * compute program's workgroup id or local id; a storage buffer's size.
 * A value a MOVE writes always lives in a VGPR of its own.
 */
static int
has_operand(
	struct gfx10_lowering *lowering,
	const struct drv_gpu_shader_ir_inst *instruction)
{
	const struct drv_gpu_shader_ir_uniform *uniform;
	unsigned code;
	int aliased;

	/* A uniform word loaded at the start. */
	if (instruction->op == DRV_GPU_IR_LOAD_PUSH || instruction->op == DRV_GPU_IR_LOAD_UBO) {
		if (lowering->operand_code[instruction->dst] != GFX10_NO_OPERAND)
			return 1;
	}

	/* A value a MOVE writes, or an instruction that defines nothing, has no operand of its own. */
	if (ir_defines(instruction) != 1)
		return 0;
	if (lowering->moved[instruction->dst] != 0U)
		return 0;

	/* Picks the operand by the operation. */
	code = GFX10_NO_OPERAND;
	switch (instruction->op) {
	case DRV_GPU_IR_CONST:
	case DRV_GPU_IR_ICONST:
	case DRV_GPU_IR_BOOL:
		/* An inline constant is its own operand; another one needs its literal moved. */
		code = constant_operand(instruction->immediate);
		if (code == AMDRDNA_GFX10_LITERAL)
			return 0;
		break;
	case DRV_GPU_IR_LOAD_PUSH:
	case DRV_GPU_IR_LOAD_UBO:
		/* A uniform word read straight from the cache's SGPRs. */
		aliased = alias_uniform(lowering, instruction);
		return aliased;
	case DRV_GPU_IR_LOAD_INPUT:
		/* A vertex program's fetched channels and numbers stay in their VGPRs. */
		if (lowering->ir->stage != DRV_GPU_STAGE_VERTEX)
			return 0;
		if (instruction->location == DRV_GPU_SHADER_LOCATION_VERTEX_INDEX) {
			code = AMDRDNA_GFX10_V(lowering->vertex_index_vgpr);
		} else if (instruction->location == DRV_GPU_SHADER_LOCATION_INSTANCE_INDEX) {
			code = AMDRDNA_GFX10_V(lowering->instance_index_vgpr);
		} else if (instruction->location < AMDRDNA_GFX10_LOCATIONS && instruction->component < 4U) {
			code = AMDRDNA_GFX10_V(lowering->input_vgpr[instruction->location] + instruction->component);
		} else {
			return 0;
		}
		break;
	case DRV_GPU_IR_LOAD_SYSTEM:
		/* The ids a compute program receives stay where they arrive. */
		if (lowering->ir->stage != DRV_GPU_STAGE_COMPUTE)
			return 0;
		if (instruction->component == DRV_GPU_IR_SYSTEM_LOCAL_ID_X) {
			code = AMDRDNA_GFX10_V(GFX10_CS_LOCAL_ID_X);
		} else if (instruction->component == DRV_GPU_IR_SYSTEM_LOCAL_ID_Y) {
			code = AMDRDNA_GFX10_V(GFX10_CS_LOCAL_ID_Y);
		} else if (instruction->component == DRV_GPU_IR_SYSTEM_LOCAL_ID_Z) {
			code = AMDRDNA_GFX10_V(GFX10_CS_LOCAL_ID_Z);
		} else if (instruction->component == DRV_GPU_IR_SYSTEM_GROUP_ID_X) {
			code = AMDRDNA_GFX10_S(GFX10_CS_GROUP_ID_X);
		} else if (instruction->component == DRV_GPU_IR_SYSTEM_GROUP_ID_Y) {
			code = AMDRDNA_GFX10_S(GFX10_CS_GROUP_ID_X + 1U);
		} else if (instruction->component == DRV_GPU_IR_SYSTEM_GROUP_ID_Z) {
			code = AMDRDNA_GFX10_S(GFX10_CS_GROUP_ID_X + 2U);
		} else {
			return 0;
		}
		break;
	case DRV_GPU_IR_STORAGE_SIZE:
		/* The size is the record count word of the buffer's descriptor. */
		if (instruction->location >= lowering->ir->uniform_count)
			return 0;
		uniform = &lowering->ir->uniforms[instruction->location];
		if (uniform->kind != DRV_GPU_IR_UNIFORM_STORAGE)
			return 0;
		code = AMDRDNA_GFX10_S(lowering->storage_sgpr[lowering->slot[instruction->location]] + 2U);
		break;
	default:
		return 0;
	}

	/* The value is its operand. */
	lowering->operand_code[instruction->dst] = (uint16_t)code;
	return 1;
}

/*
 * Reads a uniform word straight from the cache's SGPRs: finds or loads
 * its chunk in an entry, which then stays until the value's last read.
 * Returns 0 when the read is malformed (lower_uniform() refuses it) or no
 * entry is free.
 */
static int
alias_uniform(
	struct gfx10_lowering *lowering,
	const struct drv_gpu_shader_ir_inst *instruction)
{
	const struct drv_gpu_shader_ir_uniform *uniform;
	uint32_t slot;
	uint32_t entry;

	/* The buffer, the push constants or a uniform buffer; a malformed read is left to the lowering's refusal. */
	slot = GFX10_NO_SLOT;
	if (instruction->op == DRV_GPU_IR_LOAD_PUSH) {
		if (instruction->immediate + 4U > lowering->ir->push_bytes)
			return 0;
	} else {
		if (instruction->location >= lowering->ir->uniform_count)
			return 0;
		uniform = &lowering->ir->uniforms[instruction->location];
		if (uniform->kind != DRV_GPU_IR_UNIFORM_BLOCK)
			return 0;
		slot = lowering->slot[instruction->location];
	}

	/* The chunk in an entry of the cache, without the spare chunk. */
	entry = cache_chunk(lowering, slot, instruction->immediate & ~15U, 0);
	if (entry == GFX10_NO_SLOT)
		return 0;

	/* The word's SGPR is the value; the entry stays until its last read. */
	lowering->operand_code[instruction->dst] =
		(uint16_t)AMDRDNA_GFX10_S(lowering->cache_sgpr + 4U * entry + (instruction->immediate & 15U) / 4U);
	if (lowering->last_use[instruction->dst] > lowering->cache_until[entry])
		lowering->cache_until[entry] = lowering->last_use[instruction->dst];

	/* Succeeded: the word needs no copy. */
	return 1;
}

/*
 * Finds a chunk of 16 uniform bytes in the cache, or loads it into an
 * entry no value still reads, the entries taken in turn.  When every
 * entry is still read, it is loaded into the spare chunk after them if
 * spare is nonzero (GFX10_CHUNK_CACHE is returned; the chunk is not kept),
 * or GFX10_NO_SLOT is returned.
 */
static uint32_t
cache_chunk(
	struct gfx10_lowering *lowering,
	uint32_t slot,
	uint32_t offset,
	int spare)
{
	uint32_t entry;
	uint32_t step;
	uint32_t found;
	uint32_t base;

	/* An entry holding the chunk. */
	for (entry = 0U; entry < GFX10_CHUNK_CACHE; entry++) {
		if (lowering->cache_valid[entry] != 0U &&
		    lowering->cache_slot[entry] == slot &&
		    lowering->cache_offset[entry] == offset)
			return entry;
	}

	/* An entry no value still reads, from the next in turn. */
	found = GFX10_NO_SLOT;
	for (step = 0U; step < GFX10_CHUNK_CACHE; step++) {
		entry = (lowering->cache_next + step) % GFX10_CHUNK_CACHE;
		if (lowering->cache_until[entry] <= lowering->index) {
			found = entry;
			break;
		}
	}

	/* Every entry is still read: the spare chunk, or nothing. */
	if (found == GFX10_NO_SLOT && spare == 0)
		return GFX10_NO_SLOT;
	if (found == GFX10_NO_SLOT) {
		found = GFX10_CHUNK_CACHE;
	} else {
		lowering->cache_next = (found + 1U) % GFX10_CHUNK_CACHE;
		lowering->cache_valid[found] = 1U;
		lowering->cache_slot[found] = slot;
		lowering->cache_offset[found] = offset;
		lowering->cache_until[found] = 0U;
	}

	/* Loads the chunk: the push constants from the table, a uniform buffer's through its descriptor. */
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

	/* Reports where the chunk is. */
	return found;
}

/*
 * Finds how long a vertex program reads its fetched locations and its
 * vertex and instance numbers: up to the last read of each value that
 * stays in their VGPRs, or to the input read itself for a value a MOVE
 * writes (which is copied out).
 */
static void
plan_input_lifetimes(
	struct gfx10_lowering *lowering)
{
	const struct drv_gpu_shader_ir_inst *instruction;
	uint32_t index;
	uint32_t until;
	uint32_t location;

	/* Only a vertex program fetches. */
	if (lowering->ir->stage != DRV_GPU_STAGE_VERTEX)
		return;

	/* Each input read that is lowered. */
	for (index = 0U; index < lowering->ir->instruction_count; index++) {
		instruction = &lowering->ir->instructions[index];
		if (instruction->op != DRV_GPU_IR_LOAD_INPUT || lowering->dead[index] != 0U)
			continue;

		/* The read itself, or the value's last read when it stays in the input VGPR. */
		until = index + 1U;
		if (lowering->moved[instruction->dst] == 0U && lowering->last_use[instruction->dst] > until)
			until = lowering->last_use[instruction->dst];

		/* The input it reads. */
		location = instruction->location;
		if (location == DRV_GPU_SHADER_LOCATION_VERTEX_INDEX) {
			if (until > lowering->vertex_index_until)
				lowering->vertex_index_until = until;
		} else if (location == DRV_GPU_SHADER_LOCATION_INSTANCE_INDEX) {
			if (until > lowering->instance_index_until)
				lowering->instance_index_until = until;
		} else if (location < AMDRDNA_GFX10_LOCATIONS) {
			if (until > lowering->input_until[location])
				lowering->input_until[location] = until;
		}
	}
}

/*
 * Gives back a vertex program's input VGPRs read for the last time by now
 * (upto is the IR instruction just lowered plus one, 0 at the start).
 */
static void
return_inputs(
	struct gfx10_lowering *lowering,
	uint32_t upto)
{
	struct amdrdna_gfx10_program *program;
	uint32_t location;
	uint32_t channel;

	program = lowering->program;

	/* Only a vertex program fetches. */
	if (lowering->ir->stage != DRV_GPU_STAGE_VERTEX)
		return;

	/* Each fetched location read no more. */
	for (location = 0U; location < AMDRDNA_GFX10_LOCATIONS; location++) {
		if ((program->fetched_locations & (1U << location)) == 0U)
			continue;
		if ((lowering->inputs_returned & (1U << location)) != 0U)
			continue;
		if (lowering->input_until[location] > upto)
			continue;
		for (channel = 0U; channel < 4U; channel++)
			give_vgpr(lowering, lowering->input_vgpr[location] + channel);
		lowering->inputs_returned |= 1U << location;
	}

	/* The vertex number, read no more. */
	if (lowering->vertex_index_returned == 0 && lowering->vertex_index_until <= upto) {
		give_vgpr(lowering, lowering->vertex_index_vgpr);
		lowering->vertex_index_returned = 1;
	}

	/* The instance number, read no more. */
	if (lowering->instance_index_returned == 0 && lowering->instance_index_until <= upto) {
		give_vgpr(lowering, lowering->instance_index_vgpr);
		lowering->instance_index_returned = 1;
	}
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
