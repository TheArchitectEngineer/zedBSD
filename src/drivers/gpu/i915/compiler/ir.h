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
 * The STORE_OUTPUT location of the PointSize builtin, component 0: the
 * point width of the VUE header.
 */
#define I915_IR_LOCATION_POINT_SIZE	0xFFFFFFFEU

/*
 * The kinds of bound resource (struct i915_shader_ir_uniform.kind): a
 * combined image sampler, a uniform buffer block whose words the shader
 * reads at constant offsets, or a storage buffer the shader reads and
 * writes in memory at offsets it computes.
 */
#define I915_IR_UNIFORM_SAMPLED_IMAGE	1U
#define I915_IR_UNIFORM_BLOCK		2U
#define I915_IR_UNIFORM_STORAGE		3U

/*
 * The set of the storage buffer a compute shader reads gl_NumWorkGroups
 * from (ws101-p002): a uniform of kind I915_IR_UNIFORM_STORAGE with this
 * set and binding 0 names three words -- the group counts x, y, z -- that
 * the dispatch places and whose address it delivers like any other storage
 * buffer's.  No descriptor set of a pipeline layout has this number.
 */
#define I915_IR_SYSTEM_SET		0xFFFFFFFFU

/*
 * The built-in values of a compute invocation a LOAD_SYSTEM reads, as its
 * `component` (ws101-p002): the invocation's place in its workgroup (x, y,
 * z and the linear index) and the workgroup's place in the dispatch.
 */
#define I915_IR_SYSTEM_LOCAL_ID_X	0U
#define I915_IR_SYSTEM_LOCAL_ID_Y	1U
#define I915_IR_SYSTEM_LOCAL_ID_Z	2U
#define I915_IR_SYSTEM_LOCAL_INDEX	3U
#define I915_IR_SYSTEM_GROUP_ID_X	4U
#define I915_IR_SYSTEM_GROUP_ID_Y	5U
#define I915_IR_SYSTEM_GROUP_ID_Z	6U
#define I915_IR_SYSTEM_COUNT		7U

/*
 * The operations of an ATOMIC instruction, as its `immediate`
 * (ws101-p002): the value numbers of the data port's atomic operations
 * (Mesa brw_eu_defines.h, BRW_AOP_*), so the code generator passes them
 * through.  An increment and a decrement are ADD and SUB of one.
 */
#define I915_IR_ATOMIC_AND		1U
#define I915_IR_ATOMIC_OR		2U
#define I915_IR_ATOMIC_XOR		3U
#define I915_IR_ATOMIC_XCHG		4U
#define I915_IR_ATOMIC_ADD		7U
#define I915_IR_ATOMIC_SUB		8U
#define I915_IR_ATOMIC_SMAX		10U
#define I915_IR_ATOMIC_SMIN		11U
#define I915_IR_ATOMIC_UMAX		12U
#define I915_IR_ATOMIC_UMIN		13U
#define I915_IR_ATOMIC_CMPXCHG		14U

/*
 * The `component` of an ATOMIC instruction that is predicated: its last
 * source is the Boolean of the channels that run it.
 */
#define I915_IR_ATOMIC_PREDICATED	1U

/*
 * The `location` of an ATOMIC instruction on a word of the workgroup's
 * shared memory rather than of a storage buffer (ws101-p006).
 */
#define I915_IR_LOCATION_SHARED		0xFFFFFFFDU

/*
 * The memory a FENCE orders, and whether it acquires, as bits of its
 * `immediate`; a BARRIER's `immediate` names the fences before it
 * (ws101-p006).
 */
#define I915_IR_FENCE_GLOBAL		1U
#define I915_IR_FENCE_SHARED		2U
#define I915_IR_FENCE_ACQUIRE		4U

/* The most bytes of shared memory a workgroup has (the device reports it as maxComputeSharedMemorySize). */
#define I915_IR_MAX_SHARED_BYTES	16384U

/*
 * The sampler messages of a TEXTURE instruction, numbered as the message
 * type field of the descriptor takes them (Mesa brw_eu_defines.h,
 * GFX5_SAMPLER_MESSAGE_*, HSW_SAMPLER_MESSAGE_SAMPLE_DERIV_COMPARE), each
 * with the parameters it takes in order ([ref] is the depth reference of
 * a compare, the coordinate is u [v [r [ai]]]):
 *
 *   SAMPLE           u v r ai                 LD       u v lod r (integers)
 *   SAMPLE_BIAS      bias u v r ai            RESINFO  lod (an integer)
 *   SAMPLE_LOD       lod u v r ai
 *   SAMPLE_COMPARE   ref u v r ai
 *   SAMPLE_DERIVS    u dudx dudy v dvdx dvdy r drdx drdy ai
 *   SAMPLE_BIAS_COMPARE, SAMPLE_LOD_COMPARE   ref bias/lod u v r ai
 *   SAMPLE_DERIV_COMPARE                      ref u dudx dudy ...
 *
 * A message may stop after its last parameter that matters; the rest read
 * as zero.
 */
#define I915_IR_TEXTURE_SAMPLE			0U
#define I915_IR_TEXTURE_SAMPLE_BIAS		1U
#define I915_IR_TEXTURE_SAMPLE_LOD		2U
#define I915_IR_TEXTURE_SAMPLE_COMPARE		3U
#define I915_IR_TEXTURE_SAMPLE_DERIVS		4U
#define I915_IR_TEXTURE_SAMPLE_BIAS_COMPARE	5U
#define I915_IR_TEXTURE_SAMPLE_LOD_COMPARE	6U
#define I915_IR_TEXTURE_LD			7U
#define I915_IR_TEXTURE_RESINFO			10U
#define I915_IR_TEXTURE_SAMPLE_DERIV_COMPARE	20U

/* The most parameters a TEXTURE instruction passes. */
#define I915_IR_TEXTURE_MAX_PARAMS		11U

/*
 * The pipeline stage a shader runs in.
 *
 * The parser takes it from the module's entry point; the code generator
 * chooses the payload and the terminating message by it.
 */
enum i915_shader_stage {
	I915_STAGE_VERTEX = 0,
	I915_STAGE_FRAGMENT = 1,
	I915_STAGE_COMPUTE = 2,
	I915_STAGE_COUNT = 3
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

	/*
	 * dst .. dst + 3 = texture(set `location`, binding `immediate`) at
	 * (src[0], src[1]), the level of detail the pixel's derivatives choose;
	 * `component` is the constant texel offset (u in bits 11:8, v in 7:4,
	 * each -8 .. 7), 0 for none.
	 */
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

	/* As SAMPLE, the level of detail the derivatives choose moved by the bias src[2]. */
	I915_IR_SAMPLE_BIAS,

	/* As SAMPLE, at the level of detail src[2]. */
	I915_IR_SAMPLE_LOD,

	/*
	 * dst = the difference of src[0] across the pixel's 2x2 quad (fragment
	 * only): right minus left, bottom minus top.  DDX and DDY take the quad's
	 * top left pixel's difference for all four (coarse); DDX_FINE each
	 * row's own.
	 */
	I915_IR_DDX,
	I915_IR_DDX_FINE,
	I915_IR_DDY,

	/* dst = src[0] and src[1] as 16-bit floats, the low half and the high half. */
	I915_IR_PACK_HALF,

	/* dst = the float of the 16-bit float at bits 16 * `component` + 15 .. 16 * `component` of src[0]. */
	I915_IR_UNPACK_HALF,

	/* As DDY, each column's own difference (fine). */
	I915_IR_DDY_FINE,

	/*
	 * dst .. dst + 3 = the reply of one sampler message to the image and
	 * sampler at set `location`, binding `immediate`: the message's
	 * parameters are the src[1] consecutive values from src[0] on, in the
	 * order the message takes them (I915_IR_TEXTURE_*); `component` is the
	 * message type (bits 4:0) and the constant texel offset of the header
	 * (bits 23:8: u in 11:8, v in 7:4, r in 3:0 of the offset, shifted by
	 * 8), no header when the offset is 0.
	 */
	I915_IR_TEXTURE,

	/*
	 * dst = the word at byte offset src[0] (a value) of storage buffer
	 * `location` (an index of the uniform list), read from memory; with
	 * `component` 1 only where the Boolean src[1] (the predicate of the
	 * block the load is in) holds, the other channels' dst left as it was.
	 */
	I915_IR_LOAD_STORAGE,

	/*
	 * The word at byte offset src[0] of storage buffer `location` = src[1],
	 * written to memory; with `component` 1 only where the Boolean src[2]
	 * (the predicate of the block the store is in) holds.
	 */
	I915_IR_STORE_STORAGE,

	/*
	 * The start of a skippable block (ws075-p023): the instructions up to
	 * the matching SKIP_END are the body of one block whose predicate is the
	 * Boolean src[0]; a thread none of whose channels is in it may jump over
	 * them.  The parser checks that nothing made between the two is seen
	 * afterwards except through a selection or an AND by that predicate, and
	 * turns a pair it cannot prove so into NOPs.
	 */
	I915_IR_SKIP_BEGIN,

	/* The end of a skippable block. */
	I915_IR_SKIP_END,

	/*
	 * dst = the compute built-in value `component` (I915_IR_SYSTEM_*) of
	 * the channel's invocation (ws101-p002).
	 */
	I915_IR_LOAD_SYSTEM,

	/*
	 * dst = the value the word at byte offset src[0] of storage buffer
	 * `location` held, which the operation `immediate` (I915_IR_ATOMIC_*)
	 * of it and src[1] replaces in one indivisible step; a compare and
	 * exchange writes src[1] only where the word equals src[2].  The
	 * Boolean predicate is the source after the values (src[2], or src[3]
	 * for a compare and exchange) when `component` is
	 * I915_IR_ATOMIC_PREDICATED.  The code generator asks the memory for
	 * the old value only when an instruction reads dst (ws101-p002).
	 */
	I915_IR_ATOMIC,

	/*
	 * dst = the bytes of storage buffer `location` its descriptor gives the
	 * shader, the range an OpArrayLength divides (ws101-p002).
	 */
	I915_IR_STORAGE_SIZE,

	/*
	 * dst = the word at byte offset src[0] of the workgroup's shared memory;
	 * with `component` 1 read only where the Boolean src[1] holds, the
	 * other channels keeping whatever their register held (ws101-p006).
	 */
	I915_IR_LOAD_SHARED,

	/*
	 * The word at byte offset src[0] of the workgroup's shared memory =
	 * src[1]; with `component` 1 only where the Boolean src[2] holds
	 * (ws101-p006).
	 */
	I915_IR_STORE_SHARED,

	/*
	 * Every invocation of the workgroup waits here until all have come
	 * (a workgroup execution barrier), after the fences `immediate` names
	 * (I915_IR_FENCE_*) (ws101-p006).  Every thread of the group must reach
	 * it the same number of times: the parser refuses one after a return
	 * and one in a loop not every invocation enters.
	 */
	I915_IR_BARRIER,

	/*
	 * The memory accesses before it are done, and visible to the others
	 * that use the memory, before any after it (a memory fence of the
	 * memory `immediate` names, I915_IR_FENCE_*) (ws101-p006).
	 */
	I915_IR_FENCE,

	/* The number of operations above; it is not an operation of its own. */
	I915_IR_OP_COUNT
};

/*
 * One input or output location of a shader's interface.
 *
 * The parser records one per location a located interface variable takes
 * (an array or a block several); the list lives as long as the IR that owns
 * it.
 */
struct i915_shader_ir_io {
	uint32_t location;
	uint32_t components;
	uint32_t type;

	/* Nonzero for a Flat input: the draw sets it up as the provoking vertex's value. */
	uint32_t flat;

	/* Nonzero for a NoPerspective input: interpolated linearly in screen space. */
	uint32_t noperspective;
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

	/*
	 * A texture instruction (SAMPLE, SAMPLE_BIAS, SAMPLE_LOD, TEXTURE):
	 * the Boolean of the channels whose result is used, plus one; zero
	 * when every channel's is.  A channel outside it only ever meets the
	 * result in a selection that takes something else, so the code
	 * generator may run the message for the guard's channels alone and
	 * skip it when no channel is in the guard (ws075-p021).  Other
	 * instructions leave it zero.
	 */
	uint32_t guard;
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

	/*
	 * Compute: the workgroup's size in invocations along x, y and z, from
	 * the LocalSize or LocalSizeId execution mode; zero for another stage.
	 */
	uint32_t local_size[3];

	/*
	 * Compute (ws101-p006): the bytes of shared memory the workgroup's
	 * Workgroup variables take, and nonzero when the shader has a
	 * workgroup barrier.
	 */
	uint32_t shared_bytes;
	uint32_t uses_barrier;
};

#endif /* DRIVERS_GPU_I915_COMPILER_IR_H */
