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
 *
 * The IR is SCALAR: every value is one 32-bit float.  A SPIR-V vector is a
 * group of scalars, and the operations that only rearrange components
 * (construct, extract, shuffle, component access, local store / load) leave
 * no instruction behind -- the parser resolves them to the scalars involved.
 */

#ifndef I915_VK_SPIRV_H
#define I915_VK_SPIRV_H

#include "vk-internal.h"

/* Baseline IR opcodes; the minimal set the vkdemo shaders need. */
enum i915_vk_ir_op {
	I915_VK_IR_NOP = 0,
	I915_VK_IR_CONST,		/* dst = the float whose bits are `immediate` */
	I915_VK_IR_LOAD_INPUT,		/* dst = input `location`, component `component` */
	I915_VK_IR_STORE_OUTPUT,	/* output `location` (or POSITION), component `component` = src[0] */
	I915_VK_IR_LOAD_PUSH,		/* dst = the push-constant float at byte offset `immediate` */
	I915_VK_IR_FADD,		/* dst = src[0] + src[1] */
	I915_VK_IR_FSUB,		/* dst = src[0] - src[1] */
	I915_VK_IR_FMUL,		/* dst = src[0] * src[1] */
	I915_VK_IR_FNEG,		/* dst = -src[0] */
	I915_VK_IR_RSQ,
	I915_VK_IR_SIN,
	I915_VK_IR_COS,
	I915_VK_IR_SAMPLE,		/* dst .. dst + 3 = texture(set `location`, binding `immediate`) at (src[0], src[1]) */
	I915_VK_IR_OP_COUNT
};

/* STORE_OUTPUT `location` of the Position builtin (not a user location). */
#define I915_VK_IR_LOCATION_POSITION 0xFFFFFFFFU

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

/* One SSA instruction on scalar values; sources name earlier values. */
struct i915_vk_inst {
	enum i915_vk_ir_op op;
	uint32_t dst;
	uint32_t src[4];
	uint32_t immediate;
	uint32_t location;
	uint32_t component;
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
	uint32_t push_bytes;	/* bytes of push constants the shader reads */
};

/*
 * Why a module was refused.  `reason` is a static string; `opcode` / `word_offset` name the
 * instruction.  EINVAL = malformed module; ENOTSUP = valid SPIR-V whose meaning this parser
 * does not lower (it is never skipped: a skipped instruction is a wrong shader).
 */
struct i915_vk_spirv_diag {
	uint32_t opcode;
	uint32_t word_offset;
	const char *reason;
};

/* Parses SPIR-V words into IR.  EINVAL: malformed; ENOTSUP: not lowered by this parser. */
int
i915_vk_spirv_parse(
	const uint32_t *code,
	size_t words,
	enum i915_vk_stage stage,
	struct i915_vk_shader_ir **out);

/* The same, reporting the refused instruction in `diag` (may be NULL). */
int
i915_vk_spirv_parse_diag(
	const uint32_t *code,
	size_t words,
	enum i915_vk_stage stage,
	struct i915_vk_shader_ir **out,
	struct i915_vk_spirv_diag *diag);

/* Releases a parsed shader IR. */
void
i915_vk_spirv_free(
	struct i915_vk_shader_ir *ir);

#endif /* I915_VK_SPIRV_H */
