/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Gen12 EU instruction encoder: turns one instruction into its 128-bit word.
 *
 * The values (opcodes, field positions, message descriptors) are transcribed
 * with attribution into data/eu-encoding-gen12.inc; instruction selection is
 * the code generator's job.  Only the compiler and its host tests use this
 * header.
 */

#ifndef DRIVERS_GPU_I915_COMPILER_EU_H
#define DRIVERS_GPU_I915_COMPILER_EU_H

#include <stddef.h>
#include <stdint.h>

/*
 * A growable buffer of encoded EU instructions.
 *
 * The code generator owns one for the length of one compile.  Once `error`
 * is set, every further emitter leaves the buffer as it is, so a failure
 * anywhere in a sequence is seen once at the end.
 */
struct i915_eu_buf {
	uint32_t *words;
	size_t count;
	size_t capacity;
	int error;

	/* In-order instructions so far: what the scoreboard byte counts back over. */
	unsigned in_order;
};

/*
 * One instruction operand: register file, number, subregister, type and
 * region, or an immediate.
 *
 * Operands are values; the helpers below build them and the emitters encode
 * them.
 */
struct i915_eu_reg {
	uint32_t file;
	uint32_t nr;
	uint32_t subnr;
	uint32_t type;
	uint32_t vstride;
	uint32_t width;
	uint32_t hstride;
	uint32_t immediate;

	/* Source modifier: the operand is read as -value. */
	uint32_t negate;
};

/*
 * The two-source arithmetic operations a caller can ask for.
 *
 * Only ADD and MUL are encoded; the others are refused by poisoning the
 * buffer.
 */
enum i915_eu_alu {
	I915_EU_ADD = 0,
	I915_EU_MUL,
	I915_EU_SUB,
	I915_EU_AND,
	I915_EU_OR,
	I915_EU_SHL,
	I915_EU_SHR,
	I915_EU_ALU_COUNT
};

/*
 * The math functions a caller can ask for.
 *
 * Each maps to one hardware math selector.
 */
enum i915_eu_math {
	I915_EU_MATH_INV = 0,
	I915_EU_MATH_RSQ,
	I915_EU_MATH_SQRT,
	I915_EU_MATH_SIN,
	I915_EU_MATH_COS,
	I915_EU_MATH_COUNT
};

void drv_i915_eu_init(struct i915_eu_buf *buffer);
void drv_i915_eu_free(struct i915_eu_buf *buffer);
const uint32_t *drv_i915_eu_data(const struct i915_eu_buf *buffer, size_t *bytes);

struct i915_eu_reg drv_i915_eu_grf(uint32_t nr);
struct i915_eu_reg drv_i915_eu_grf_ud(uint32_t nr);
struct i915_eu_reg drv_i915_eu_grf_scalar(uint32_t nr, uint32_t subnr);
struct i915_eu_reg drv_i915_eu_negate(struct i915_eu_reg reg);
struct i915_eu_reg drv_i915_eu_imm_f(uint32_t bits);
struct i915_eu_reg drv_i915_eu_imm_d(uint32_t value);
struct i915_eu_reg drv_i915_eu_null(void);

void drv_i915_eu_mov(struct i915_eu_buf *buffer, struct i915_eu_reg dst, struct i915_eu_reg src);
void drv_i915_eu_alu2(struct i915_eu_buf *buffer, enum i915_eu_alu op, struct i915_eu_reg dst, struct i915_eu_reg src0, struct i915_eu_reg src1);
void drv_i915_eu_mad(struct i915_eu_buf *buffer, struct i915_eu_reg dst, struct i915_eu_reg src0, struct i915_eu_reg src1, struct i915_eu_reg src2);
void drv_i915_eu_math(struct i915_eu_buf *buffer, enum i915_eu_math func, struct i915_eu_reg dst, struct i915_eu_reg src0, struct i915_eu_reg src1);
void drv_i915_eu_send(struct i915_eu_buf *buffer, struct i915_eu_reg dst, struct i915_eu_reg src0, struct i915_eu_reg src1, uint32_t sfid, uint32_t descriptor, uint32_t ex_descriptor, int conditional, int end_of_thread);
void drv_i915_eu_nop(struct i915_eu_buf *buffer);

#endif /* DRIVERS_GPU_I915_COMPILER_EU_H */
