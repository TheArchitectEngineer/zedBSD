/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Gen12 EU instruction encoder: turns one instruction into its 128-bit word.
 * Values (opcodes, field positions, message descriptors) are transcribed with
 * attribution into vk/linux/eu-encoding.inc; instruction selection is the
 * compiler's job.  Contract for p005; see external-design.md section 4.5.
 */

#ifndef I915_VK_EU_H
#define I915_VK_EU_H

#include "vk-internal.h"

/* A growable buffer of encoded EU instructions. */
struct i915_vk_eu_buf {
	uint32_t *words;
	size_t count;
	size_t capacity;
	int error;
};

/* An operand: register file, number, subregister, type and region. */
struct i915_vk_eu_reg {
	uint32_t file;
	uint32_t nr;
	uint32_t subnr;
	uint32_t type;
	uint32_t vstride;
	uint32_t width;
	uint32_t hstride;
	uint32_t immediate;
};

/* ALU and math function selectors. */
enum i915_vk_eu_alu {
	I915_VK_EU_ADD = 0,
	I915_VK_EU_MUL,
	I915_VK_EU_SUB,
	I915_VK_EU_AND,
	I915_VK_EU_OR,
	I915_VK_EU_SHL,
	I915_VK_EU_SHR,
	I915_VK_EU_ALU_COUNT
};

enum i915_vk_eu_math {
	I915_VK_EU_MATH_INV = 0,
	I915_VK_EU_MATH_RSQ,
	I915_VK_EU_MATH_SQRT,
	I915_VK_EU_MATH_SIN,
	I915_VK_EU_MATH_COS,
	I915_VK_EU_MATH_COUNT
};

/* Buffer lifetime and access. */
void
i915_vk_eu_init(
	struct i915_vk_eu_buf *buffer);

void
i915_vk_eu_free(
	struct i915_vk_eu_buf *buffer);

const uint32_t *
i915_vk_eu_data(
	const struct i915_vk_eu_buf *buffer,
	size_t *bytes);

/* Operand helpers. */
struct i915_vk_eu_reg
i915_vk_eu_grf(
	uint32_t nr);

struct i915_vk_eu_reg
i915_vk_eu_imm_f(
	uint32_t bits);

struct i915_vk_eu_reg
i915_vk_eu_null(
	void);

/* Instruction emitters (SIMD8 by default). */
void
i915_vk_eu_mov(
	struct i915_vk_eu_buf *buffer,
	struct i915_vk_eu_reg dst,
	struct i915_vk_eu_reg src);

void
i915_vk_eu_alu2(
	struct i915_vk_eu_buf *buffer,
	enum i915_vk_eu_alu op,
	struct i915_vk_eu_reg dst,
	struct i915_vk_eu_reg src0,
	struct i915_vk_eu_reg src1);

void
i915_vk_eu_mad(
	struct i915_vk_eu_buf *buffer,
	struct i915_vk_eu_reg dst,
	struct i915_vk_eu_reg src0,
	struct i915_vk_eu_reg src1,
	struct i915_vk_eu_reg src2);

void
i915_vk_eu_math(
	struct i915_vk_eu_buf *buffer,
	enum i915_vk_eu_math func,
	struct i915_vk_eu_reg dst,
	struct i915_vk_eu_reg src0,
	struct i915_vk_eu_reg src1);

void
i915_vk_eu_send(
	struct i915_vk_eu_buf *buffer,
	struct i915_vk_eu_reg dst,
	struct i915_vk_eu_reg src,
	uint32_t sfid,
	uint32_t descriptor,
	uint32_t ex_descriptor,
	uint32_t mlen,
	uint32_t rlen,
	int end_of_thread);

void
i915_vk_eu_nop(
	struct i915_vk_eu_buf *buffer);

#endif /* I915_VK_EU_H */
