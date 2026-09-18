/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Gen12 EU instruction encoder.
 *
 * Each instruction is four little-endian 32-bit words.  Field bit positions,
 * hardware opcode values and register types are transcribed into
 * linux/eu-encoding-gen12.inc from Mesa (mesa-23.1.0, MIT); the placement logic
 * here is new.  The core arithmetic forms (mov, add, mul, math) are encoded in
 * full; three-source (mad) and message (send) forms encode their control fields
 * and are completed during the on-hardware bring-up.  Software scoreboard (SWSB)
 * dependency encoding is left to that bring-up as well.
 */

#include "vk-internal.h"
#include "eu.h"

#include <kern/kmem.h>

#include <errno.h>
#include <string.h>

#include "linux/eu-encoding-gen12.inc"

static void i915_vk_eu_set(uint32_t *inst, unsigned high, unsigned low, uint32_t value);
static void i915_vk_eu_bit(uint32_t *inst, unsigned position, uint32_t value);
static uint32_t *i915_vk_eu_reserve(struct i915_vk_eu_buf *buffer);
static void i915_vk_eu_common(uint32_t *inst, uint32_t opcode);
static void i915_vk_eu_dst(uint32_t *inst, struct i915_vk_eu_reg reg);
static void i915_vk_eu_src0(uint32_t *inst, struct i915_vk_eu_reg reg);
static void i915_vk_eu_src1(uint32_t *inst, struct i915_vk_eu_reg reg);

/* Prepares an empty instruction buffer. */
void
i915_vk_eu_init(
	struct i915_vk_eu_buf *buffer)
{
	buffer->words = NULL;
	buffer->count = 0U;
	buffer->capacity = 0U;
	buffer->error = 0;
}

/* Releases an instruction buffer. */
void
i915_vk_eu_free(
	struct i915_vk_eu_buf *buffer)
{
	if (buffer->words != NULL)
		kern_free(buffer->words);
	buffer->words = NULL;
	buffer->count = 0U;
	buffer->capacity = 0U;
}

/* Returns the encoded bytes and their length. */
const uint32_t *
i915_vk_eu_data(
	const struct i915_vk_eu_buf *buffer,
	size_t *bytes)
{
	*bytes = buffer->count * sizeof(uint32_t);
	return buffer->words;
}

/* Names a general register operand of 32-bit float type. */
struct i915_vk_eu_reg
i915_vk_eu_grf(
	uint32_t nr)
{
	struct i915_vk_eu_reg reg;

	memset(&reg, 0, sizeof(reg));
	reg.file = EU_FILE_GRF;
	reg.nr = nr;
	reg.type = EU_TYPE_F;
	reg.vstride = EU_VSTRIDE_8;
	reg.width = EU_WIDTH_8;
	reg.hstride = EU_HSTRIDE_1;
	return reg;
}

/* Names one float of a general register, replicated to every channel. */
struct i915_vk_eu_reg
i915_vk_eu_grf_scalar(
	uint32_t nr,
	uint32_t subnr)
{
	struct i915_vk_eu_reg reg;

	memset(&reg, 0, sizeof(reg));
	reg.file = EU_FILE_GRF;
	reg.nr = nr;
	reg.subnr = subnr;
	reg.type = EU_TYPE_F;
	reg.vstride = EU_VSTRIDE_0;
	reg.width = EU_WIDTH_1;
	reg.hstride = EU_HSTRIDE_0;
	return reg;
}

/* The same register read negated. */
struct i915_vk_eu_reg
i915_vk_eu_negate(
	struct i915_vk_eu_reg reg)
{
	reg.negate = reg.negate ^ 1U;
	return reg;
}

/* Names a 32-bit float immediate operand. */
struct i915_vk_eu_reg
i915_vk_eu_imm_f(
	uint32_t bits)
{
	struct i915_vk_eu_reg reg;

	memset(&reg, 0, sizeof(reg));
	reg.file = EU_FILE_IMM;
	reg.type = EU_TYPE_F;
	reg.immediate = bits;
	return reg;
}

/* Names the null register. */
struct i915_vk_eu_reg
i915_vk_eu_null(
	void)
{
	struct i915_vk_eu_reg reg;

	memset(&reg, 0, sizeof(reg));
	reg.file = EU_FILE_ARF;
	reg.type = EU_TYPE_F;
	return reg;
}

/* Encodes a move. */
void
i915_vk_eu_mov(
	struct i915_vk_eu_buf *buffer,
	struct i915_vk_eu_reg dst,
	struct i915_vk_eu_reg src)
{
	uint32_t *inst;

	inst = i915_vk_eu_reserve(buffer);
	if (inst == NULL)
		return;

	i915_vk_eu_common(inst, EU_OP_MOV);
	i915_vk_eu_dst(inst, dst);
	i915_vk_eu_src0(inst, src);
}

/* Encodes a two-source arithmetic instruction. */
void
i915_vk_eu_alu2(
	struct i915_vk_eu_buf *buffer,
	enum i915_vk_eu_alu op,
	struct i915_vk_eu_reg dst,
	struct i915_vk_eu_reg src0,
	struct i915_vk_eu_reg src1)
{
	uint32_t *inst;
	uint32_t opcode;

	/* Only add and multiply are encoded at the baseline; others are refused. */
	if (op == I915_VK_EU_ADD) {
		opcode = EU_OP_ADD;
	} else if (op == I915_VK_EU_MUL) {
		opcode = EU_OP_MUL;
	} else {
		buffer->error = 1;
		return;
	}

	inst = i915_vk_eu_reserve(buffer);
	if (inst == NULL)
		return;

	i915_vk_eu_common(inst, opcode);
	i915_vk_eu_dst(inst, dst);
	i915_vk_eu_src0(inst, src0);
	i915_vk_eu_src1(inst, src1);
}

/* Encodes a multiply-add; the three-source operands are filled at bring-up. */
void
i915_vk_eu_mad(
	struct i915_vk_eu_buf *buffer,
	struct i915_vk_eu_reg dst,
	struct i915_vk_eu_reg src0,
	struct i915_vk_eu_reg src1,
	struct i915_vk_eu_reg src2)
{
	uint32_t *inst;

	/*
	 * The three-source operand layout is not encoded.  An instruction without its operands
	 * is a different instruction, so the buffer is poisoned: no caller can ship it by accident.
	 */
	(void)src0;
	(void)src1;
	(void)src2;
	buffer->error = 1;

	inst = i915_vk_eu_reserve(buffer);
	if (inst == NULL)
		return;

	i915_vk_eu_common(inst, EU_OP_MAD);
	i915_vk_eu_dst(inst, dst);
}

/* Encodes a math function. */
void
i915_vk_eu_math(
	struct i915_vk_eu_buf *buffer,
	enum i915_vk_eu_math func,
	struct i915_vk_eu_reg dst,
	struct i915_vk_eu_reg src0,
	struct i915_vk_eu_reg src1)
{
	uint32_t *inst;
	uint32_t selector;

	/* The function selector maps to the Gen math sub-opcode. */
	if (func == I915_VK_EU_MATH_INV) {
		selector = EU_MATH_INV;
	} else if (func == I915_VK_EU_MATH_RSQ) {
		selector = EU_MATH_RSQ;
	} else if (func == I915_VK_EU_MATH_SQRT) {
		selector = EU_MATH_SQRT;
	} else if (func == I915_VK_EU_MATH_SIN) {
		selector = EU_MATH_SIN;
	} else {
		selector = EU_MATH_COS;
	}

	inst = i915_vk_eu_reserve(buffer);
	if (inst == NULL)
		return;

	i915_vk_eu_common(inst, EU_OP_MATH);
	i915_vk_eu_set(inst, EU_MATH_FUNCTION_HI, EU_MATH_FUNCTION_LO, selector);
	i915_vk_eu_dst(inst, dst);
	i915_vk_eu_src0(inst, src0);
	i915_vk_eu_src1(inst, src1);
}

/* Encodes a message send; the descriptors are placed for bring-up completion. */
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
	int end_of_thread)
{
	uint32_t *inst;

	/* The message length, SFID and descriptors are refined on hardware. */
	(void)ex_descriptor;
	(void)mlen;
	(void)rlen;
	(void)end_of_thread;

	inst = i915_vk_eu_reserve(buffer);
	if (inst == NULL)
		return;

	i915_vk_eu_common(inst, EU_OP_SEND);
	i915_vk_eu_dst(inst, dst);
	i915_vk_eu_src0(inst, src);

	/* The immediate descriptor and SFID land in the last word for now. */
	inst[3] = descriptor;
	i915_vk_eu_set(inst, 27, 24, sfid & 0xFU);
}

/* Encodes a no-op. */
void
i915_vk_eu_nop(
	struct i915_vk_eu_buf *buffer)
{
	uint32_t *inst;

	inst = i915_vk_eu_reserve(buffer);
	if (inst == NULL)
		return;

	i915_vk_eu_common(inst, EU_OP_NOP);
}

/* Reserves and zeroes one instruction, returning its four words. */
static uint32_t *
i915_vk_eu_reserve(
	struct i915_vk_eu_buf *buffer)
{
	uint32_t *grown;
	size_t capacity;

	/* A prior error stops further encoding. */
	if (buffer->error != 0)
		return NULL;

	/* The buffer doubles geometrically to amortize the growth. */
	if (buffer->count + GEN12_EU_DWORDS > buffer->capacity) {
		capacity = buffer->capacity == 0U ? 64U : buffer->capacity * 2U;
		grown = kern_calloc(capacity, sizeof(uint32_t));
		if (grown == NULL) {
			buffer->error = 1;
			return NULL;
		}

		if (buffer->words != NULL) {
			memcpy(grown, buffer->words, buffer->count * sizeof(uint32_t));
			kern_free(buffer->words);
		}
		buffer->words = grown;
		buffer->capacity = capacity;
	}

	/* The new instruction starts zeroed so only set fields carry bits. */
	grown = buffer->words + buffer->count;
	memset(grown, 0, GEN12_EU_DWORDS * sizeof(uint32_t));
	buffer->count += GEN12_EU_DWORDS;
	return grown;
}

/* Sets the control fields every instruction carries. */
static void
i915_vk_eu_common(
	uint32_t *inst,
	uint32_t opcode)
{
	i915_vk_eu_set(inst, EU_OPCODE_HI, EU_OPCODE_LO, opcode);
	i915_vk_eu_set(inst, EU_EXEC_SIZE_HI, EU_EXEC_SIZE_LO, EU_EXEC_SIZE_8);
}

/* Encodes a destination register. */
static void
i915_vk_eu_dst(
	uint32_t *inst,
	struct i915_vk_eu_reg reg)
{
	i915_vk_eu_bit(inst, EU_DST_REG_FILE_BIT, reg.file == EU_FILE_GRF ? 1U : 0U);
	i915_vk_eu_set(inst, EU_DST_REG_TYPE_HI, EU_DST_REG_TYPE_LO, reg.type);
	i915_vk_eu_set(inst, EU_DST_HSTRIDE_HI, EU_DST_HSTRIDE_LO, EU_HSTRIDE_1);
	i915_vk_eu_set(inst, EU_DST_SUBREG_HI, EU_DST_SUBREG_LO, reg.subnr);
	i915_vk_eu_set(inst, EU_DST_REG_NR_HI, EU_DST_REG_NR_LO, reg.nr);
}

/* Encodes source zero, whether a register or an immediate. */
static void
i915_vk_eu_src0(
	uint32_t *inst,
	struct i915_vk_eu_reg reg)
{
	/* The register file is a two-bit field split across two positions. */
	i915_vk_eu_bit(inst, EU_SRC0_REG_FILE_LO_BIT, reg.file & 1U);
	i915_vk_eu_bit(inst, EU_SRC0_REG_FILE_HI_BIT, (reg.file >> 1) & 1U);
	i915_vk_eu_set(inst, EU_SRC0_REG_TYPE_HI, EU_SRC0_REG_TYPE_LO, reg.type);

	/* An immediate carries its value in the last word instead of a region. */
	if (reg.file == EU_FILE_IMM) {
		inst[3] = reg.immediate;
		return;
	}

	i915_vk_eu_set(inst, EU_SRC0_REG_NR_HI, EU_SRC0_REG_NR_LO, reg.nr);
	i915_vk_eu_set(inst, EU_SRC0_SUBREG_HI, EU_SRC0_SUBREG_LO, reg.subnr);
	i915_vk_eu_set(inst, EU_SRC0_HSTRIDE_HI, EU_SRC0_HSTRIDE_LO, reg.hstride);
	i915_vk_eu_set(inst, EU_SRC0_WIDTH_HI, EU_SRC0_WIDTH_LO, reg.width);
	i915_vk_eu_set(inst, EU_SRC0_VSTRIDE_HI, EU_SRC0_VSTRIDE_LO, reg.vstride);
	i915_vk_eu_bit(inst, EU_SRC0_NEGATE_BIT, reg.negate & 1U);
}

/* Encodes source one, whether a register or an immediate. */
static void
i915_vk_eu_src1(
	uint32_t *inst,
	struct i915_vk_eu_reg reg)
{
	i915_vk_eu_bit(inst, EU_SRC1_REG_FILE_LO_BIT, reg.file & 1U);
	i915_vk_eu_bit(inst, EU_SRC1_REG_FILE_HI_BIT, (reg.file >> 1) & 1U);
	i915_vk_eu_set(inst, EU_SRC1_REG_TYPE_HI, EU_SRC1_REG_TYPE_LO, reg.type);

	if (reg.file == EU_FILE_IMM) {
		inst[3] = reg.immediate;
		return;
	}

	i915_vk_eu_set(inst, EU_SRC1_REG_NR_HI, EU_SRC1_REG_NR_LO, reg.nr);
	i915_vk_eu_set(inst, EU_SRC1_SUBREG_HI, EU_SRC1_SUBREG_LO, reg.subnr);
	i915_vk_eu_set(inst, EU_SRC1_HSTRIDE_HI, EU_SRC1_HSTRIDE_LO, reg.hstride);
	i915_vk_eu_set(inst, EU_SRC1_WIDTH_HI, EU_SRC1_WIDTH_LO, reg.width);
	i915_vk_eu_set(inst, EU_SRC1_VSTRIDE_HI, EU_SRC1_VSTRIDE_LO, reg.vstride);
	i915_vk_eu_bit(inst, EU_SRC1_NEGATE_BIT, reg.negate & 1U);
}

/* Writes value into the inclusive bit range [high:low] of the instruction. */
static void
i915_vk_eu_set(
	uint32_t *inst,
	unsigned high,
	unsigned low,
	uint32_t value)
{
	unsigned position;

	/* Each bit of the value takes its place from the low end upward. */
	for (position = low; position <= high; position++)
		i915_vk_eu_bit(inst, position, (value >> (position - low)) & 1U);
}

/* Sets or clears one bit of the 128-bit instruction. */
static void
i915_vk_eu_bit(
	uint32_t *inst,
	unsigned position,
	uint32_t value)
{
	uint32_t mask;

	/* The instruction is four little-endian words; the bit selects one. */
	mask = 1U << (position % 32U);
	if ((value & 1U) != 0U)
		inst[position / 32U] |= mask;
	else
		inst[position / 32U] &= ~mask;
}
