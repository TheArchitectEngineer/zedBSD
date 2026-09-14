/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * SPIR-V parser producing a baseline, straight-line IR the compiler lowers
 * one instruction at a time.  Contract for p004; see external-design.md
 * section 4.4.  The IR types here are the record the compiler reads.
 */

#ifndef I915_VK_SPIRV_H
#define I915_VK_SPIRV_H

#include "vk-internal.h"

/* Baseline IR opcodes; the minimal set the vkdemo shaders need. */
enum i915_vk_ir_op {
	I915_VK_IR_NOP = 0,
	I915_VK_IR_LOAD_INPUT,
	I915_VK_IR_STORE_OUTPUT,
	I915_VK_IR_LOAD_PUSH,
	I915_VK_IR_FADD,
	I915_VK_IR_FSUB,
	I915_VK_IR_FMUL,
	I915_VK_IR_FMAD,
	I915_VK_IR_DOT,
	I915_VK_IR_RSQ,
	I915_VK_IR_SIN,
	I915_VK_IR_COS,
	I915_VK_IR_COMPOSE,
	I915_VK_IR_EXTRACT,
	I915_VK_IR_SAMPLE,
	I915_VK_IR_OP_COUNT
};

/* A shader input or output slot. */
struct i915_vk_io {
	uint32_t location;
	uint32_t components;
	uint32_t type;
};

/* A push constant range or a bound resource (sampled image, uniform). */
struct i915_vk_uniform {
	uint32_t set;
	uint32_t binding;
	uint32_t kind;
	uint32_t offset;
	uint32_t size;
};

/* One SSA instruction; sources name earlier values. */
struct i915_vk_inst {
	enum i915_vk_ir_op op;
	uint32_t dst;
	uint32_t src[4];
	uint32_t immediate;
	uint32_t swizzle;
};

/* A parsed shader ready for baseline lowering. */
struct i915_vk_shader_ir {
	enum i915_vk_stage stage;
	struct i915_vk_io *inputs;
	uint32_t input_count;
	struct i915_vk_io *outputs;
	uint32_t output_count;
	struct i915_vk_uniform *uniforms;
	uint32_t uniform_count;
	struct i915_vk_inst *instructions;
	uint32_t instruction_count;
	uint32_t value_count;
};

/* Parses SPIR-V words into IR; unsupported opcodes return EINVAL. */
int
i915_vk_spirv_parse(
	const uint32_t *code,
	size_t words,
	enum i915_vk_stage stage,
	struct i915_vk_shader_ir **out);

/* Releases a parsed shader IR. */
void
i915_vk_spirv_free(
	struct i915_vk_shader_ir *ir);

#endif /* I915_VK_SPIRV_H */
