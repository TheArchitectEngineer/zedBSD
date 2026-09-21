/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Gen12 EU instruction encoder.
 *
 * Each instruction is four little-endian 32-bit words.  Field bit positions, hardware opcode
 * values, register types and the descriptor layout of SEND are transcribed into
 * linux/eu-encoding-gen12.inc from Mesa (MIT); the placement logic here is new.  E-128: every
 * emitter is checked bit for bit against Mesa's assembler and disassembler (gentool) by
 * plan/ws031/tests/run-vk-gentool-test.sh.
 *
 * SOFTWARE SCOREBOARD.  Gen12 hardware does not track register dependencies between instructions;
 * the program says them in the SWSB byte (brw_lower_scoreboard.cpp).  This encoder says the one
 * thing that is always true of its output: every instruction depends on the one before it.
 *   - an in-order instruction (MOV, ADD, MUL) waits for the previous in-order instruction (@1);
 *   - an out-of-order instruction (MATH, SEND) waits the same way, names itself with token 0 and
 *     is followed by a sync.nop that waits until it has written its destination (or, having
 *     none, has read its sources), so nothing overlaps it and token 0 is free again;
 *   - the SEND that ends the thread waits for the previous instruction and needs nothing after it.
 * XXX: this serialises the kernel.  It is correct for any program this compiler emits and leaves
 * the pipelining Mesa's dataflow analysis would allow on the table.
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
static void i915_vk_eu_common(struct i915_vk_eu_buf *buffer, uint32_t *inst, uint32_t opcode, int in_order);
static void i915_vk_eu_sync(struct i915_vk_eu_buf *buffer, uint32_t swsb);
static void i915_vk_eu_dst(uint32_t *inst, struct i915_vk_eu_reg reg);
static void i915_vk_eu_src0(uint32_t *inst, struct i915_vk_eu_reg reg);
static void i915_vk_eu_src1(uint32_t *inst, struct i915_vk_eu_reg reg);

/* The one token this encoder uses: nothing else is in flight when it is set (see above). */
#define EU_TOKEN 0U

/* Prepares an empty instruction buffer. */
void
i915_vk_eu_init(
	struct i915_vk_eu_buf *buffer)
{
	buffer->words = NULL;
	buffer->count = 0U;
	buffer->capacity = 0U;
	buffer->error = 0;
	buffer->in_order = 0U;
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

/* Names a general register operand: eight channels of `type`. */
static struct i915_vk_eu_reg
i915_vk_eu_grf_typed(
	uint32_t nr,
	uint32_t type)
{
	struct i915_vk_eu_reg reg;

	memset(&reg, 0, sizeof(reg));
	reg.file = EU_FILE_GRF;
	reg.nr = nr;
	reg.type = type;
	reg.vstride = EU_VSTRIDE_8;
	reg.width = EU_WIDTH_8;
	reg.hstride = EU_HSTRIDE_1;
	return reg;
}

/* Names a general register operand of 32-bit float type. */
struct i915_vk_eu_reg
i915_vk_eu_grf(
	uint32_t nr)
{
	return i915_vk_eu_grf_typed(nr, EU_TYPE_F);
}

/* The same register as eight unsigned 32-bit words (URB handles, anything copied bit for bit). */
struct i915_vk_eu_reg
i915_vk_eu_grf_ud(
	uint32_t nr)
{
	return i915_vk_eu_grf_typed(nr, EU_TYPE_UD);
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

/* Names a 32-bit signed integer immediate operand. */
struct i915_vk_eu_reg
i915_vk_eu_imm_d(
	uint32_t value)
{
	struct i915_vk_eu_reg reg;

	memset(&reg, 0, sizeof(reg));
	reg.file = EU_FILE_IMM;
	reg.type = EU_TYPE_D;
	reg.immediate = value;
	return reg;
}

/* Names the null register (as a float operand it carries the ordinary SIMD8 region). */
struct i915_vk_eu_reg
i915_vk_eu_null(
	void)
{
	struct i915_vk_eu_reg reg;

	reg = i915_vk_eu_grf_typed(0U, EU_TYPE_F);
	reg.file = EU_FILE_ARF;
	return reg;
}

/* Encodes a move.  A destination of another type than float makes it a move of that type. */
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

	i915_vk_eu_common(buffer, inst, EU_OP_MOV, 1);
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

	/* The hardware has one immediate slot, and it belongs to the second source. */
	if (src0.file == EU_FILE_IMM) {
		buffer->error = 1;
		return;
	}

	inst = i915_vk_eu_reserve(buffer);
	if (inst == NULL)
		return;

	i915_vk_eu_common(buffer, inst, opcode, 1);
	i915_vk_eu_dst(inst, dst);
	i915_vk_eu_src0(inst, src0);
	i915_vk_eu_src1(inst, src1);
}

/* Encodes a multiply-add; the three-source operand layout is not encoded. */
void
i915_vk_eu_mad(
	struct i915_vk_eu_buf *buffer,
	struct i915_vk_eu_reg dst,
	struct i915_vk_eu_reg src0,
	struct i915_vk_eu_reg src1,
	struct i915_vk_eu_reg src2)
{
	/*
	 * XXX: unimplemented.  An instruction without its operands is a different instruction, so the
	 * buffer is poisoned: no caller can ship it by accident.  The compiler lowers a*b+c to MUL, ADD.
	 */
	(void)dst;
	(void)src0;
	(void)src1;
	(void)src2;
	buffer->error = 1;
}

/* Encodes a one-operand math function (out of order: see the scoreboard note above). */
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
	} else if (func == I915_VK_EU_MATH_COS) {
		selector = EU_MATH_COS;
	} else {
		buffer->error = 1;
		return;
	}

	inst = i915_vk_eu_reserve(buffer);
	if (inst == NULL)
		return;

	i915_vk_eu_common(buffer, inst, EU_OP_MATH, 0);
	i915_vk_eu_set(inst, EU_MATH_FUNCTION_HI, EU_MATH_FUNCTION_LO, selector);
	i915_vk_eu_dst(inst, dst);
	i915_vk_eu_src0(inst, src0);
	i915_vk_eu_src1(inst, src1);

	i915_vk_eu_sync(buffer, EU_SWSB_SYNC_DST(EU_TOKEN));
}

/*
 * Encodes a message: `src0` and, for a split payload, `src1` are the first registers of the two
 * payload runs whose lengths the descriptors carry (desc: mlen, rlen, the function's control;
 * ex_desc: the length of the second run).  `conditional` selects SENDC, the form a render-target
 * write takes.  `end_of_thread`: the message retires the thread (its payload must then sit in
 * r112..r127, which is the caller's business).
 */
void
i915_vk_eu_send(
	struct i915_vk_eu_buf *buffer,
	struct i915_vk_eu_reg dst,
	struct i915_vk_eu_reg src0,
	struct i915_vk_eu_reg src1,
	uint32_t sfid,
	uint32_t descriptor,
	uint32_t ex_descriptor,
	int conditional,
	int end_of_thread)
{
	uint32_t *inst;

	/* The low six bits of the extended descriptor are not encodable (they held the SFID once). */
	if ((ex_descriptor & 0x3fU) != 0U || sfid > 15U) {
		buffer->error = 1;
		return;
	}

	inst = i915_vk_eu_reserve(buffer);
	if (inst == NULL)
		return;

	i915_vk_eu_common(buffer, inst, conditional != 0 ? EU_OP_SENDC : EU_OP_SEND, end_of_thread != 0 ? 2 : 0);
	i915_vk_eu_bit(inst, EU_SEND_EOT_BIT, end_of_thread != 0 ? 1U : 0U);
	i915_vk_eu_set(inst, EU_SEND_SFID_HI, EU_SEND_SFID_LO, sfid);

	/* operands: a file bit and a register number, nothing else */
	i915_vk_eu_bit(inst, EU_DST_REG_FILE_BIT, dst.file == EU_FILE_GRF ? 1U : 0U);
	i915_vk_eu_set(inst, EU_DST_REG_NR_HI, EU_DST_REG_NR_LO, dst.nr);
	i915_vk_eu_bit(inst, EU_SRC0_REG_FILE_BIT, src0.file == EU_FILE_GRF ? 1U : 0U);
	i915_vk_eu_set(inst, EU_SRC0_REG_NR_HI, EU_SRC0_REG_NR_LO, src0.nr);
	i915_vk_eu_bit(inst, EU_SRC1_REG_FILE_BIT, src1.file == EU_FILE_GRF ? 1U : 0U);
	i915_vk_eu_set(inst, EU_SRC1_REG_NR_HI, EU_SRC1_REG_NR_LO, src1.nr);

	/* the descriptors, scattered as eu-encoding-gen12.inc says */
	i915_vk_eu_set(inst, 123U, 122U, descriptor >> 30);
	i915_vk_eu_set(inst, 71U, 67U, descriptor >> 25);
	i915_vk_eu_set(inst, 55U, 51U, descriptor >> 20);
	i915_vk_eu_set(inst, 121U, 113U, descriptor >> 11);
	i915_vk_eu_set(inst, 91U, 81U, descriptor);
	i915_vk_eu_set(inst, 127U, 124U, ex_descriptor >> 28);
	i915_vk_eu_set(inst, 97U, 96U, ex_descriptor >> 26);
	i915_vk_eu_set(inst, 65U, 64U, ex_descriptor >> 24);
	i915_vk_eu_set(inst, 47U, 35U, ex_descriptor >> 11);
	i915_vk_eu_set(inst, 103U, 99U, ex_descriptor >> 6);

	if (end_of_thread == 0)
		i915_vk_eu_sync(buffer, dst.file == EU_FILE_GRF ? EU_SWSB_SYNC_DST(EU_TOKEN) : EU_SWSB_SYNC_SRC(EU_TOKEN));
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

	i915_vk_eu_set(inst, EU_OPCODE_HI, EU_OPCODE_LO, EU_OP_NOP);
}

/* Reserves and zeroes one instruction, returning its four words. */
static uint32_t *
i915_vk_eu_reserve(
	struct i915_vk_eu_buf *buffer)
{
	uint32_t *grown;
	uint32_t *inst;
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
	inst = &buffer->words[buffer->count];
	memset(inst, 0, GEN12_EU_DWORDS * sizeof(uint32_t));
	buffer->count += GEN12_EU_DWORDS;
	return inst;
}

/*
 * Sets the control fields every instruction carries, the scoreboard byte among them.
 * `in_order`: 1 = an in-order instruction, 0 = an out-of-order one that a sync.nop follows,
 * 2 = the message that ends the thread.
 */
static void
i915_vk_eu_common(
	struct i915_vk_eu_buf *buffer,
	uint32_t *inst,
	uint32_t opcode,
	int in_order)
{
	uint32_t regdist;

	/* The first in-order instruction has nothing before it to wait for. */
	regdist = buffer->in_order != 0U ? 1U : 0U;

	i915_vk_eu_set(inst, EU_OPCODE_HI, EU_OPCODE_LO, opcode);
	i915_vk_eu_set(inst, EU_EXEC_SIZE_HI, EU_EXEC_SIZE_LO, EU_EXEC_SIZE_8);
	if (in_order == 0)
		i915_vk_eu_set(inst, EU_SWSB_HI, EU_SWSB_LO,
			regdist != 0U ? EU_SWSB_REGDIST_SET(regdist, EU_TOKEN) : (0x40U | EU_TOKEN));
	else
		i915_vk_eu_set(inst, EU_SWSB_HI, EU_SWSB_LO, EU_SWSB_REGDIST(regdist));
	if (in_order == 1)
		buffer->in_order++;
}

/* sync.nop: the front end waits here until the named dependency is resolved. */
static void
i915_vk_eu_sync(
	struct i915_vk_eu_buf *buffer,
	uint32_t swsb)
{
	uint32_t *inst;

	inst = i915_vk_eu_reserve(buffer);
	if (inst == NULL)
		return;

	i915_vk_eu_set(inst, EU_OPCODE_HI, EU_OPCODE_LO, EU_OP_SYNC);
	i915_vk_eu_set(inst, EU_SWSB_HI, EU_SWSB_LO, swsb);
	i915_vk_eu_set(inst, EU_EXEC_SIZE_HI, EU_EXEC_SIZE_LO, EU_EXEC_SIZE_1);
	i915_vk_eu_bit(inst, EU_NO_MASK_BIT, 1U);
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
	i915_vk_eu_set(inst, EU_SRC0_REG_TYPE_HI, EU_SRC0_REG_TYPE_LO, reg.type);

	/* An immediate says so with its own bit and carries its value in the last word. */
	if (reg.file == EU_FILE_IMM) {
		i915_vk_eu_bit(inst, EU_SRC0_IS_IMM_BIT, 1U);
		i915_vk_eu_set(inst, EU_IMM32_HI, EU_IMM32_LO, reg.immediate);
		return;
	}

	i915_vk_eu_bit(inst, EU_SRC0_REG_FILE_BIT, reg.file == EU_FILE_GRF ? 1U : 0U);
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
	i915_vk_eu_set(inst, EU_SRC1_REG_TYPE_HI, EU_SRC1_REG_TYPE_LO, reg.type);

	if (reg.file == EU_FILE_IMM) {
		i915_vk_eu_bit(inst, EU_SRC1_IS_IMM_BIT, 1U);
		i915_vk_eu_set(inst, EU_IMM32_HI, EU_IMM32_LO, reg.immediate);
		return;
	}

	i915_vk_eu_bit(inst, EU_SRC1_REG_FILE_BIT, reg.file == EU_FILE_GRF ? 1U : 0U);
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
