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
 * The IR is SCALAR: every value is one 32-bit float.  A SPIR-V vector is a
 * group of scalars, and the operations that only rearrange components
 * (construct, extract, shuffle, component access, local store and load)
 * leave no instruction behind -- the parser resolves them to the scalars
 * involved.  The IR is straight-line SSA: every value is defined once, by an
 * instruction that comes before every reader.
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
 * It is the minimal set the vkdemo shaders and the rectangle kernels need;
 * the code generator refuses any other value rather than dropping it.
 */
enum i915_shader_ir_op {
	I915_IR_NOP = 0,

	/* dst = the float whose bits are `immediate`. */
	I915_IR_CONST,

	/* dst = input `location`, component `component`. */
	I915_IR_LOAD_INPUT,

	/* Output `location` (or POSITION), component `component` = src[0]. */
	I915_IR_STORE_OUTPUT,

	/* dst = the push-constant float at byte offset `immediate`. */
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
 * One bound resource of a shader: a sampled image, or a push-constant range.
 *
 * The n-th sampled image of the shader is sampler n and binding-table entry
 * 1 + n of the kernel the code generator produces.
 */
struct i915_shader_ir_uniform {
	uint32_t set;
	uint32_t binding;
	uint32_t kind;
	uint32_t offset;
	uint32_t size;
};

/*
 * One SSA instruction on scalar values.
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
