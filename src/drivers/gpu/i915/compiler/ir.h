/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The scalar shader IR: the record the SPIR-V parser writes and the EU code
 * generator reads.
 *
 * The IR is SCALAR: every value is 32 bits to a channel -- a float, a 32-bit
 * integer, or a Boolean as all ones (true) or zero (false), which only the
 * comparisons, the logic operations and BOOL define and only the logic
 * operations, SELECT's condition, KILL and LOOP_END read.  A value carries
 * no type; the operation that reads it says how its bits are read.  A
 * SPIR-V vector or matrix is a group of scalars, and the operations that
 * only rearrange components (construct, extract, shuffle, transpose,
 * component access, local store and load) leave no instruction behind --
 * the parser resolves them to the scalars involved.
 *
 * The IR is SSA with one exception: every value is defined once, by an
 * instruction that comes before every reader, except the target of a MOVE,
 * a loop variable, which the parser defines before a loop and again at the
 * loop's end.  Structured control flow is if-converted by the parser: both
 * sides of a selection run for every channel and SELECT keeps, channel by
 * channel, what the side the channel took computed; a discard is KILL of the
 * channels that reach it.  A loop is the instructions between LOOP_BEGIN and
 * LOOP_END, run again as long as LOOP_END's Boolean holds for a channel;
 * the loop variables carry what one pass leaves to the next and to the code
 * after the loop.
 *
 * This header declares types only.
 */

#ifndef DRIVERS_GPU_I915_COMPILER_IR_H
#define DRIVERS_GPU_I915_COMPILER_IR_H

#include <stdint.h>

/*
 * The STORE_OUTPUT location of the Position builtin.
 *
 * It is not a user location, so no located output can collide with it.
 */
#define I915_IR_LOCATION_POSITION	0xFFFFFFFFU

/*
 * The kinds of bound resource (struct i915_shader_ir_uniform.kind): a
 * combined image sampler, or a uniform buffer block whose words the shader
 * reads at constant offsets.
 */
#define I915_IR_UNIFORM_SAMPLED_IMAGE	1U
#define I915_IR_UNIFORM_BLOCK		2U

/*
 * The pipeline stage a shader runs in.
 *
 * The parser takes it from the module's entry point; the code generator
 * chooses the payload and the terminating message by it.
 */
enum i915_shader_stage {
	I915_STAGE_VERTEX = 0,
	I915_STAGE_FRAGMENT = 1,
	I915_STAGE_COUNT = 2
};

/*
 * The operation of one IR instruction.
 *
 * It is the set the vkdemo and mview shaders, the rectangle kernels and the
 * shader tests need; the code generator refuses any other value rather than
 * dropping it.
 */
enum i915_shader_ir_op {
	I915_IR_NOP = 0,

	/* dst = the float whose bits are `immediate`. */
	I915_IR_CONST,

	/* dst = input `location`, component `component`. */
	I915_IR_LOAD_INPUT,

	/* Output `location` (or POSITION), component `component` = src[0]. */
	I915_IR_STORE_OUTPUT,

	/* dst = the push-constant word at byte offset `immediate`, bit for bit. */
	I915_IR_LOAD_PUSH,

	/* dst = src[0] + src[1]. */
	I915_IR_FADD,

	/* dst = src[0] - src[1]. */
	I915_IR_FSUB,

	/* dst = src[0] * src[1]. */
	I915_IR_FMUL,

	/* dst = -src[0]. */
	I915_IR_FNEG,

	/* dst = 1 / sqrt(src[0]). */
	I915_IR_RSQ,

	/* dst = sin(src[0]). */
	I915_IR_SIN,

	/* dst = cos(src[0]). */
	I915_IR_COS,

	/* dst .. dst + 3 = texture(set `location`, binding `immediate`) at (src[0], src[1]). */
	I915_IR_SAMPLE,

	/* dst = 1 / src[0]. */
	I915_IR_RCP,

	/* dst = sqrt(src[0]). */
	I915_IR_SQRT,

	/* dst = 2 ^ src[0]. */
	I915_IR_EXP2,

	/* dst = log2(src[0]). */
	I915_IR_LOG2,

	/* dst = |src[0]|. */
	I915_IR_FABS,

	/* dst = floor(src[0]). */
	I915_IR_FLOOR,

	/* dst = src[0] - floor(src[0]). */
	I915_IR_FRACT,

	/* dst = the smaller of src[0] and src[1]. */
	I915_IR_FMIN,

	/* dst = the larger of src[0] and src[1]. */
	I915_IR_FMAX,

	/* dst = the Boolean src[0] < src[1]; false when either is NaN. */
	I915_IR_FLT,

	/* dst = the Boolean src[0] >= src[1]; false when either is NaN. */
	I915_IR_FGE,

	/* dst = the Boolean src[0] == src[1]; false when either is NaN. */
	I915_IR_FEQ,

	/* dst = the Boolean src[0] != src[1]; true when either is NaN. */
	I915_IR_FNEU,

	/* dst = the Boolean src[0] and src[1]. */
	I915_IR_AND,

	/* dst = the Boolean src[0] or src[1]. */
	I915_IR_OR,

	/* dst = the Boolean not src[0]. */
	I915_IR_NOT,

	/* dst = the Boolean whose bits are `immediate` (all ones or zero). */
	I915_IR_BOOL,

	/* dst = src[1] where the Boolean src[0] is true, else src[2], bit for bit. */
	I915_IR_SELECT,

	/* Discards the pixels where the Boolean src[0] is true (fragment only). */
	I915_IR_KILL,

	/* dst = src[0] rounded toward zero (a float). */
	I915_IR_FTRUNC,

	/* dst = the 32-bit integer whose bits are `immediate`. */
	I915_IR_ICONST,

	/*
	 * dst = the word at byte offset `immediate` of uniform block
	 * `location` (an index of the uniform list), bit for bit.
	 */
	I915_IR_LOAD_UBO,

	/* dst = src[0] + src[1], integers modulo 2^32. */
	I915_IR_IADD,

	/* dst = src[0] - src[1], integers modulo 2^32. */
	I915_IR_ISUB,

	/* dst = the low 32 bits of src[0] * src[1]. */
	I915_IR_IMUL,

	/* dst = -src[0], an integer modulo 2^32. */
	I915_IR_INEG,

	/* dst = src[0] / src[1], unsigned, rounded toward zero; undefined for a zero divisor. */
	I915_IR_UDIV,

	/* dst = src[0] % src[1], unsigned; undefined for a zero divisor. */
	I915_IR_UMOD,

	/* dst = the bitwise and, or, exclusive or of src[0] and src[1]. */
	I915_IR_IAND,
	I915_IR_IOR,
	I915_IR_IXOR,

	/* dst = the bitwise complement of src[0]. */
	I915_IR_INOT,

	/* dst = src[0] shifted left, right logically, right arithmetically by src[1] (mod 32). */
	I915_IR_SHL,
	I915_IR_SHR,
	I915_IR_ASR,

	/* dst = the float of the signed / unsigned integer src[0]. */
	I915_IR_I2F,
	I915_IR_U2F,

	/* dst = the signed / unsigned integer of the float src[0], rounded toward zero. */
	I915_IR_F2I,
	I915_IR_F2U,

	/* dst = the Boolean of the signed comparison src[0] <, >= src[1]. */
	I915_IR_ILT,
	I915_IR_IGE,

	/* dst = the Boolean of the unsigned comparison src[0] <, >= src[1]. */
	I915_IR_ULT,
	I915_IR_UGE,

	/* dst = the Boolean src[0] == src[1], != src[1], as integers. */
	I915_IR_IEQ,
	I915_IR_INE,

	/*
	 * dst = src[0], bit for bit.  The only instruction whose destination
	 * may be defined before: a loop variable is moved into before its loop
	 * and again at the loop's end.
	 */
	I915_IR_MOVE,

	/* The first instruction of a loop's body. */
	I915_IR_LOOP_BEGIN,

	/*
	 * The end of a loop's body: the channels where the Boolean src[0] is
	 * true run the body again; the loop ends when it is false for all.
	 */
	I915_IR_LOOP_END,

	/*
	 * dst = src[0] / src[1], signed, rounded toward zero; undefined for a
	 * zero divisor and for the most negative integer divided by -1.
	 */
	I915_IR_IDIV,

	/* dst = src[0] - src[1] * (src[0] / src[1]), signed: the sign of src[0] (SPIR-V OpSRem). */
	I915_IR_IREM,

	/* dst = src[0] rounded to the nearest integer, a tie to the even one (a float). */
	I915_IR_FROUND_EVEN,

	I915_IR_OP_COUNT
};

/*
 * One input or output slot of a shader's interface.
 *
 * The parser records one per located interface variable; the list lives as
 * long as the IR that owns it.
 */
struct i915_shader_ir_io {
	uint32_t location;
	uint32_t components;
	uint32_t type;
};

/*
 * One bound resource of a shader: a sampled image or a uniform block.
 *
 * The n-th sampled image of the shader is sampler n and binding-table entry
 * 1 + n of the kernel the code generator produces.  A uniform block records
 * the bytes [offset, offset + size) the shader reads of it, which the draw
 * delivers with the push constants.
 */
struct i915_shader_ir_uniform {
	uint32_t set;
	uint32_t binding;
	uint32_t kind;
	uint32_t offset;
	uint32_t size;
};

/*
 * One instruction on scalar values.
 *
 * Sources name values defined by earlier instructions; which of the fields
 * mean something is stated by the operation.
 */
struct i915_shader_ir_inst {
	enum i915_shader_ir_op op;
	uint32_t dst;
	uint32_t src[4];
	uint32_t immediate;
	uint32_t location;
	uint32_t component;
};

/*
 * A parsed shader ready for lowering to EU code.
 *
 * The parser allocates it with its lists and drv_i915_shader_ir_free()
 * releases it.  A caller that builds one by hand (the rectangle kernels)
 * owns its storage and never passes it to the free function.
 */
struct i915_shader_ir {
	enum i915_shader_stage stage;
	struct i915_shader_ir_io *inputs;
	uint32_t input_count;
	struct i915_shader_ir_io *outputs;
	uint32_t output_count;
	struct i915_shader_ir_uniform *uniforms;
	uint32_t uniform_count;
	struct i915_shader_ir_inst *instructions;
	uint32_t instruction_count;
	uint32_t value_count;

	/* Bytes of push constants the shader reads. */
	uint32_t push_bytes;
};

#endif /* DRIVERS_GPU_I915_COMPILER_IR_H */
