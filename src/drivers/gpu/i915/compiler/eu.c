/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Gen12 EU instruction encoder (see eu.h).
 *
 * Each instruction is four little-endian 32-bit words.  Field bit positions,
 * hardware opcode values, register types and the descriptor layout of SEND
 * are transcribed into data/eu-encoding-gen12.inc from Mesa (MIT); the
 * placement logic here is new.  Every emitter is checked bit for bit against
 * Mesa's assembler and disassembler (gentool) by
 * plan/ws031/tests/run-vk-gentool-test.sh.
 *
 * SOFTWARE SCOREBOARD.  Gen12 hardware does not track register dependencies
 * between instructions; the program says them in the SWSB byte
 * (brw_lower_scoreboard.cpp).  This encoder says the one thing that is always
 * true of its output: every instruction depends on the one before it.
 *
 *   - an in-order instruction (MOV, ADD, MUL) waits for the previous in-order
 *     instruction (@1);
 *   - an out-of-order instruction (MATH, SEND) waits the same way, names
 *     itself with token 0 and is followed by a sync.nop that waits until it
 *     has written its destination (or, having none, has read its sources), so
 *     nothing overlaps it and token 0 is free again;
 *   - the SEND that ends the thread waits for the previous instruction and
 *     needs nothing after it.
 *
 * XXX: this serialises the kernel.  It is correct for any program this
 * compiler emits and leaves the pipelining Mesa's dataflow analysis would
 * allow on the table.
 */

#include "eu.h"

#include <kern/kmem.h>

#include <errno.h>
#include <string.h>

#include "../data/eu-encoding-gen12.inc"

/*
 * Marks a parameter a function deliberately leaves unread.
 *
 * The compiler includes no driver header, so it spells the marker itself
 * unless the including translation unit already has it.
 */
#ifndef UNUSED_PARAMETER
#define UNUSED_PARAMETER(parameter) ((void)(parameter))
#endif

/*
 * The one scoreboard token this encoder uses.
 *
 * Nothing else is in flight when it is set (see the scoreboard note above).
 */
#define I915_EU_TOKEN			0U

/* The scoreboard class of an instruction, as i915_eu_common() takes it. */
#define I915_EU_OUT_OF_ORDER		0
#define I915_EU_IN_ORDER		1
#define I915_EU_END_OF_THREAD		2

/* The token-set SWSB byte of an out-of-order instruction with nothing to wait for. */
#define I915_EU_SWSB_TOKEN_SET		0x40U

/* The first buffer capacity, in words; the buffer doubles from there. */
#define I915_EU_FIRST_CAPACITY		64U

/* The largest shared-function identifier the SEND encoding has room for. */
#define I915_EU_SFID_MAX		15U

/* The extended-descriptor bits that are not encodable (they held the SFID once). */
#define I915_EU_EX_DESC_LOW_MASK	0x3fU

static struct i915_eu_reg i915_eu_grf_typed(uint32_t nr, uint32_t type);
static uint32_t *i915_eu_reserve(struct i915_eu_buf *buffer);
static void i915_eu_common(struct i915_eu_buf *buffer, uint32_t *inst, uint32_t opcode, int order);
static void i915_eu_sync(struct i915_eu_buf *buffer, uint32_t swsb);
static void i915_eu_dst(uint32_t *inst, struct i915_eu_reg reg);
static void i915_eu_src0(uint32_t *inst, struct i915_eu_reg reg);
static void i915_eu_src1(uint32_t *inst, struct i915_eu_reg reg);
static uint32_t i915_eu_file_bit(struct i915_eu_reg reg);
static void i915_eu_set(uint32_t *inst, unsigned high, unsigned low, uint32_t value);
static void i915_eu_bit(uint32_t *inst, unsigned position, uint32_t value);

/*
 * Prepares an empty instruction buffer.
 */
void
drv_i915_eu_init(
	struct i915_eu_buf *buffer)
{
	/* Starts with no storage, no error and no instruction to wait for. */
	buffer->words = NULL;
	buffer->count = 0U;
	buffer->capacity = 0U;
	buffer->error = 0;
	buffer->in_order = 0U;
}

/*
 * Releases the storage of an instruction buffer.
 *
 * The error and the scoreboard count are left as they are.
 */
void
drv_i915_eu_free(
	struct i915_eu_buf *buffer)
{
	/* Releases the instruction words, if any were ever reserved. */
	if (buffer->words != NULL)
		kern_free(buffer->words);

	/* Leaves the buffer empty. */
	buffer->words = NULL;
	buffer->count = 0U;
	buffer->capacity = 0U;
}

/*
 * Returns the encoded instruction words and their length in bytes.
 */
const uint32_t *
drv_i915_eu_data(
	const struct i915_eu_buf *buffer,
	size_t *bytes)
{
	/* Reports the length alongside the words. */
	*bytes = buffer->count * sizeof(uint32_t);

	/* Succeeded: the words stay owned by the buffer. */
	return buffer->words;
}

/*
 * Names a general register operand of 32-bit float type.
 */
struct i915_eu_reg
drv_i915_eu_grf(
	uint32_t nr)
{
	struct i915_eu_reg reg;

	/* Builds eight float channels of the register. */
	reg = i915_eu_grf_typed(nr, EU_TYPE_F);

	/* Succeeded: the operand is a plain SIMD8 float register. */
	return reg;
}

/*
 * Names a general register as eight unsigned 32-bit words.
 *
 * This is how URB handles, and anything else copied bit for bit, are read.
 */
struct i915_eu_reg
drv_i915_eu_grf_ud(
	uint32_t nr)
{
	struct i915_eu_reg reg;

	/* Builds eight unsigned-word channels of the register. */
	reg = i915_eu_grf_typed(nr, EU_TYPE_UD);

	/* Succeeded: the operand is a plain SIMD8 word register. */
	return reg;
}

/*
 * Names one float of a general register, replicated to every channel.
 *
 * The float sits at byte `subnr` and is read with region <0;1,0>: how a value
 * that is the same for the whole dispatch, such as a push constant, is read.
 */
struct i915_eu_reg
drv_i915_eu_grf_scalar(
	uint32_t nr,
	uint32_t subnr)
{
	struct i915_eu_reg reg;

	/* Names the register and the float inside it. */
	memset(&reg, 0, sizeof(reg));
	reg.file = EU_FILE_GRF;
	reg.nr = nr;
	reg.subnr = subnr;
	reg.type = EU_TYPE_F;

	/* A zero vertical and horizontal stride replicate the one float. */
	reg.vstride = EU_VSTRIDE_0;
	reg.width = EU_WIDTH_1;
	reg.hstride = EU_HSTRIDE_0;

	/* Succeeded: the operand reads one float to every channel. */
	return reg;
}

/*
 * Returns the same operand read negated.
 *
 * It is a source modifier, not valid for a destination or an immediate.
 */
struct i915_eu_reg
drv_i915_eu_negate(
	struct i915_eu_reg reg)
{
	/* Negating twice reads the value unchanged again. */
	reg.negate = reg.negate ^ 1U;

	/* Succeeded: the operand now carries the negate modifier. */
	return reg;
}

/*
 * Names a 32-bit float immediate operand from its bits.
 */
struct i915_eu_reg
drv_i915_eu_imm_f(
	uint32_t bits)
{
	struct i915_eu_reg reg;

	/* Carries the float bits unchanged; no floating point is involved. */
	memset(&reg, 0, sizeof(reg));
	reg.file = EU_FILE_IMM;
	reg.type = EU_TYPE_F;
	reg.immediate = bits;

	/* Succeeded: the operand is a float immediate. */
	return reg;
}

/*
 * Names a 32-bit signed integer immediate operand.
 */
struct i915_eu_reg
drv_i915_eu_imm_d(
	uint32_t value)
{
	struct i915_eu_reg reg;

	/* Carries the integer bits unchanged. */
	memset(&reg, 0, sizeof(reg));
	reg.file = EU_FILE_IMM;
	reg.type = EU_TYPE_D;
	reg.immediate = value;

	/* Succeeded: the operand is an integer immediate. */
	return reg;
}

/*
 * Names the null register.
 *
 * As a float operand it carries the ordinary SIMD8 region.
 */
struct i915_eu_reg
drv_i915_eu_null(
	void)
{
	struct i915_eu_reg reg;

	/* The null register is the architecture file's register zero. */
	reg = i915_eu_grf_typed(0U, EU_TYPE_F);
	reg.file = EU_FILE_ARF;

	/* Succeeded: the operand discards a result or supplies nothing. */
	return reg;
}

/*
 * Encodes a move.
 *
 * A destination of another type than float makes it a move of that type.
 */
void
drv_i915_eu_mov(
	struct i915_eu_buf *buffer,
	struct i915_eu_reg dst,
	struct i915_eu_reg src)
{
	uint32_t *inst;

	/* Reserves the instruction; a poisoned or full buffer takes nothing. */
	inst = i915_eu_reserve(buffer);
	if (inst == NULL)
		return;

	/* Encodes an in-order move and its two operands. */
	i915_eu_common(buffer, inst, EU_OP_MOV, I915_EU_IN_ORDER);
	i915_eu_dst(inst, dst);
	i915_eu_src0(inst, src);
}

/*
 * Encodes a two-source arithmetic instruction.
 *
 * Only add and multiply are encoded; any other operation, and an immediate
 * first source, poison the buffer.
 */
void
drv_i915_eu_alu2(
	struct i915_eu_buf *buffer,
	enum i915_eu_alu op,
	struct i915_eu_reg dst,
	struct i915_eu_reg src0,
	struct i915_eu_reg src1)
{
	uint32_t *inst;
	uint32_t opcode;

	/* Only add and multiply are encoded at the baseline; others are refused. */
	if (op == I915_EU_ADD) {
		opcode = EU_OP_ADD;
	} else if (op == I915_EU_MUL) {
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

	/* Reserves the instruction; a poisoned or full buffer takes nothing. */
	inst = i915_eu_reserve(buffer);
	if (inst == NULL)
		return;

	/* Encodes an in-order arithmetic instruction and its three operands. */
	i915_eu_common(buffer, inst, opcode, I915_EU_IN_ORDER);
	i915_eu_dst(inst, dst);
	i915_eu_src0(inst, src0);
	i915_eu_src1(inst, src1);
}

/*
 * Refuses a multiply-add: the three-source operand layout is not encoded.
 */
void
drv_i915_eu_mad(
	struct i915_eu_buf *buffer,
	struct i915_eu_reg dst,
	struct i915_eu_reg src0,
	struct i915_eu_reg src1,
	struct i915_eu_reg src2)
{
	UNUSED_PARAMETER(dst);
	UNUSED_PARAMETER(src0);
	UNUSED_PARAMETER(src1);
	UNUSED_PARAMETER(src2);

	/*
	 * XXX: unimplemented.  An instruction without its operands is a different
	 * instruction, so the buffer is poisoned: no caller can ship it by
	 * accident.  The compiler lowers a*b+c to MUL, ADD.
	 */
	buffer->error = 1;
}

/*
 * Encodes a one-operand math function.
 *
 * Math is out of order, so a sync.nop on its destination follows it (see the
 * scoreboard note above).
 */
void
drv_i915_eu_math(
	struct i915_eu_buf *buffer,
	enum i915_eu_math func,
	struct i915_eu_reg dst,
	struct i915_eu_reg src0,
	struct i915_eu_reg src1)
{
	uint32_t *inst;
	uint32_t selector;

	/* The function selector maps to the Gen math sub-opcode. */
	if (func == I915_EU_MATH_INV) {
		selector = EU_MATH_INV;
	} else if (func == I915_EU_MATH_RSQ) {
		selector = EU_MATH_RSQ;
	} else if (func == I915_EU_MATH_SQRT) {
		selector = EU_MATH_SQRT;
	} else if (func == I915_EU_MATH_SIN) {
		selector = EU_MATH_SIN;
	} else if (func == I915_EU_MATH_COS) {
		selector = EU_MATH_COS;
	} else {
		buffer->error = 1;
		return;
	}

	/* Reserves the instruction; a poisoned or full buffer takes nothing. */
	inst = i915_eu_reserve(buffer);
	if (inst == NULL)
		return;

	/* Encodes an out-of-order math instruction, its function and its operands. */
	i915_eu_common(buffer, inst, EU_OP_MATH, I915_EU_OUT_OF_ORDER);
	i915_eu_set(inst, EU_MATH_FUNCTION_HI, EU_MATH_FUNCTION_LO, selector);
	i915_eu_dst(inst, dst);
	i915_eu_src0(inst, src0);
	i915_eu_src1(inst, src1);

	/* Waits until the math has written its destination. */
	i915_eu_sync(buffer, EU_SWSB_SYNC_DST(I915_EU_TOKEN));
}

/*
 * Encodes a message to a shared function.
 *
 * `src0` and, for a split payload, `src1` are the first registers of the two
 * payload runs whose lengths the descriptors carry (desc: mlen, rlen, the
 * function's control; ex_desc: the length of the second run).
 * `conditional` selects SENDC, the form a render-target write takes.
 * `end_of_thread`: the message retires the thread (its payload must then sit
 * in r112..r127, which is the caller's business).
 */
void
drv_i915_eu_send(
	struct i915_eu_buf *buffer,
	struct i915_eu_reg dst,
	struct i915_eu_reg src0,
	struct i915_eu_reg src1,
	uint32_t sfid,
	uint32_t descriptor,
	uint32_t ex_descriptor,
	int conditional,
	int end_of_thread)
{
	uint32_t *inst;
	uint32_t opcode;
	uint32_t eot;
	uint32_t swsb;
	int order;

	/* The low six bits of the extended descriptor are not encodable (they held the SFID once). */
	if ((ex_descriptor & I915_EU_EX_DESC_LOW_MASK) != 0U) {
		buffer->error = 1;
		return;
	}

	/* The shared-function field has four bits. */
	if (sfid > I915_EU_SFID_MAX) {
		buffer->error = 1;
		return;
	}

	/* Reserves the instruction; a poisoned or full buffer takes nothing. */
	inst = i915_eu_reserve(buffer);
	if (inst == NULL)
		return;

	/* A render-target write is the conditional form of the message. */
	if (conditional != 0) {
		opcode = EU_OP_SENDC;
	} else {
		opcode = EU_OP_SEND;
	}

	/* The message that ends the thread is its own scoreboard class. */
	if (end_of_thread != 0) {
		order = I915_EU_END_OF_THREAD;
		eot = 1U;
	} else {
		order = I915_EU_OUT_OF_ORDER;
		eot = 0U;
	}

	/* Encodes the control fields, the end-of-thread bit and the shared function. */
	i915_eu_common(buffer, inst, opcode, order);
	i915_eu_bit(inst, EU_SEND_EOT_BIT, eot);
	i915_eu_set(inst, EU_SEND_SFID_HI, EU_SEND_SFID_LO, sfid);

	/* Encodes the operands: a file bit and a register number, nothing else. */
	i915_eu_bit(inst, EU_DST_REG_FILE_BIT, i915_eu_file_bit(dst));
	i915_eu_set(inst, EU_DST_REG_NR_HI, EU_DST_REG_NR_LO, dst.nr);
	i915_eu_bit(inst, EU_SRC0_REG_FILE_BIT, i915_eu_file_bit(src0));
	i915_eu_set(inst, EU_SRC0_REG_NR_HI, EU_SRC0_REG_NR_LO, src0.nr);
	i915_eu_bit(inst, EU_SRC1_REG_FILE_BIT, i915_eu_file_bit(src1));
	i915_eu_set(inst, EU_SRC1_REG_NR_HI, EU_SRC1_REG_NR_LO, src1.nr);

	/* Scatters the message descriptor as eu-encoding-gen12.inc says. */
	i915_eu_set(inst, 123U, 122U, descriptor >> 30);
	i915_eu_set(inst, 71U, 67U, descriptor >> 25);
	i915_eu_set(inst, 55U, 51U, descriptor >> 20);
	i915_eu_set(inst, 121U, 113U, descriptor >> 11);
	i915_eu_set(inst, 91U, 81U, descriptor);

	/* Scatters the extended descriptor the same way. */
	i915_eu_set(inst, 127U, 124U, ex_descriptor >> 28);
	i915_eu_set(inst, 97U, 96U, ex_descriptor >> 26);
	i915_eu_set(inst, 65U, 64U, ex_descriptor >> 24);
	i915_eu_set(inst, 47U, 35U, ex_descriptor >> 11);
	i915_eu_set(inst, 103U, 99U, ex_descriptor >> 6);

	/* The message that ends the thread needs nothing after it. */
	if (end_of_thread != 0)
		return;

	/*
	 * A message with a destination is waited for until it has written it;
	 * one without, until it has read its sources.
	 */
	if (dst.file == EU_FILE_GRF) {
		swsb = EU_SWSB_SYNC_DST(I915_EU_TOKEN);
	} else {
		swsb = EU_SWSB_SYNC_SRC(I915_EU_TOKEN);
	}

	/* Waits until the message is done with its registers. */
	i915_eu_sync(buffer, swsb);
}

/*
 * Encodes a no-op.
 */
void
drv_i915_eu_nop(
	struct i915_eu_buf *buffer)
{
	uint32_t *inst;

	/* Reserves the instruction; a poisoned or full buffer takes nothing. */
	inst = i915_eu_reserve(buffer);
	if (inst == NULL)
		return;

	/* Only the opcode is set; the no-op carries no scoreboard byte. */
	i915_eu_set(inst, EU_OPCODE_HI, EU_OPCODE_LO, EU_OP_NOP);
}

/* Names a general register operand: eight channels of `type`. */
static struct i915_eu_reg
i915_eu_grf_typed(
	uint32_t nr,
	uint32_t type)
{
	struct i915_eu_reg reg;

	/* Names the register and the type its channels are read as. */
	memset(&reg, 0, sizeof(reg));
	reg.file = EU_FILE_GRF;
	reg.nr = nr;
	reg.type = type;

	/* The ordinary SIMD8 region <8;8,1>: eight consecutive channels. */
	reg.vstride = EU_VSTRIDE_8;
	reg.width = EU_WIDTH_8;
	reg.hstride = EU_HSTRIDE_1;

	/* Succeeded: the operand reads the whole register. */
	return reg;
}

/* Reserves and zeroes one instruction, returning its four words. */
static uint32_t *
i915_eu_reserve(
	struct i915_eu_buf *buffer)
{
	uint32_t *grown;
	uint32_t *inst;
	size_t capacity;

	/* A prior error stops further encoding. */
	if (buffer->error != 0)
		return NULL;

	/* The buffer doubles geometrically to amortize the growth. */
	if (buffer->count + GEN12_EU_DWORDS > buffer->capacity) {
		/* The first growth takes the first capacity; each later one doubles. */
		if (buffer->capacity == 0U) {
			capacity = I915_EU_FIRST_CAPACITY;
		} else {
			capacity = buffer->capacity * 2U;
		}

		/* Allocates the larger storage; a failure poisons the buffer. */
		grown = kern_calloc(capacity, sizeof(uint32_t));
		if (grown == NULL) {
			buffer->error = 1;
			return NULL;
		}

		/* Moves the instructions encoded so far into the larger storage. */
		if (buffer->words != NULL) {
			memcpy(grown, buffer->words, buffer->count * sizeof(uint32_t));
			kern_free(buffer->words);
		}

		/* Publishes the larger storage. */
		buffer->words = grown;
		buffer->capacity = capacity;
	}

	/* The new instruction starts zeroed so only set fields carry bits. */
	inst = &buffer->words[buffer->count];
	memset(inst, 0, GEN12_EU_DWORDS * sizeof(uint32_t));
	buffer->count += GEN12_EU_DWORDS;

	/* Succeeded: the caller fills the four words. */
	return inst;
}

/*
 * Sets the control fields every instruction carries, the scoreboard byte among
 * them; `order` is I915_EU_IN_ORDER, I915_EU_OUT_OF_ORDER (a sync.nop follows)
 * or I915_EU_END_OF_THREAD.
 */
static void
i915_eu_common(
	struct i915_eu_buf *buffer,
	uint32_t *inst,
	uint32_t opcode,
	int order)
{
	uint32_t regdist;
	uint32_t swsb;

	/* The first in-order instruction has nothing before it to wait for. */
	if (buffer->in_order != 0U) {
		regdist = 1U;
	} else {
		regdist = 0U;
	}

	/* Encodes the opcode and the SIMD8 execution size. */
	i915_eu_set(inst, EU_OPCODE_HI, EU_OPCODE_LO, opcode);
	i915_eu_set(inst, EU_EXEC_SIZE_HI, EU_EXEC_SIZE_LO, EU_EXEC_SIZE_8);

	/*
	 * An out-of-order instruction also names itself with the token; with
	 * nothing to wait for, the byte says only that.
	 */
	if (order == I915_EU_OUT_OF_ORDER) {
		if (regdist != 0U) {
			swsb = EU_SWSB_REGDIST_SET(regdist, I915_EU_TOKEN);
		} else {
			swsb = I915_EU_SWSB_TOKEN_SET | I915_EU_TOKEN;
		}
	} else {
		swsb = EU_SWSB_REGDIST(regdist);
	}

	/* Encodes the scoreboard byte. */
	i915_eu_set(inst, EU_SWSB_HI, EU_SWSB_LO, swsb);

	/*
	 * The in-order count is what the next instruction's register distance
	 * counts back over; only an in-order instruction moves it.
	 */
	if (order == I915_EU_IN_ORDER)
		buffer->in_order++;
}

/* Encodes a sync.nop: the front end waits here until the named dependency is resolved. */
static void
i915_eu_sync(
	struct i915_eu_buf *buffer,
	uint32_t swsb)
{
	uint32_t *inst;

	/* Reserves the instruction; a poisoned or full buffer takes nothing. */
	inst = i915_eu_reserve(buffer);
	if (inst == NULL)
		return;

	/* Encodes a SIMD1 sync with the dependency it waits on, outside the channel mask. */
	i915_eu_set(inst, EU_OPCODE_HI, EU_OPCODE_LO, EU_OP_SYNC);
	i915_eu_set(inst, EU_SWSB_HI, EU_SWSB_LO, swsb);
	i915_eu_set(inst, EU_EXEC_SIZE_HI, EU_EXEC_SIZE_LO, EU_EXEC_SIZE_1);
	i915_eu_bit(inst, EU_NO_MASK_BIT, 1U);
}

/* Encodes a destination register. */
static void
i915_eu_dst(
	uint32_t *inst,
	struct i915_eu_reg reg)
{
	/* A destination is always written with horizontal stride one. */
	i915_eu_bit(inst, EU_DST_REG_FILE_BIT, i915_eu_file_bit(reg));
	i915_eu_set(inst, EU_DST_REG_TYPE_HI, EU_DST_REG_TYPE_LO, reg.type);
	i915_eu_set(inst, EU_DST_HSTRIDE_HI, EU_DST_HSTRIDE_LO, EU_HSTRIDE_1);
	i915_eu_set(inst, EU_DST_SUBREG_HI, EU_DST_SUBREG_LO, reg.subnr);
	i915_eu_set(inst, EU_DST_REG_NR_HI, EU_DST_REG_NR_LO, reg.nr);
}

/* Encodes source zero, whether a register or an immediate. */
static void
i915_eu_src0(
	uint32_t *inst,
	struct i915_eu_reg reg)
{
	/* Encodes the type the source is read as. */
	i915_eu_set(inst, EU_SRC0_REG_TYPE_HI, EU_SRC0_REG_TYPE_LO, reg.type);

	/* An immediate says so with its own bit and carries its value in the last word. */
	if (reg.file == EU_FILE_IMM) {
		i915_eu_bit(inst, EU_SRC0_IS_IMM_BIT, 1U);
		i915_eu_set(inst, EU_IMM32_HI, EU_IMM32_LO, reg.immediate);
		return;
	}

	/* Encodes the register, its region and its negate modifier. */
	i915_eu_bit(inst, EU_SRC0_REG_FILE_BIT, i915_eu_file_bit(reg));
	i915_eu_set(inst, EU_SRC0_REG_NR_HI, EU_SRC0_REG_NR_LO, reg.nr);
	i915_eu_set(inst, EU_SRC0_SUBREG_HI, EU_SRC0_SUBREG_LO, reg.subnr);
	i915_eu_set(inst, EU_SRC0_HSTRIDE_HI, EU_SRC0_HSTRIDE_LO, reg.hstride);
	i915_eu_set(inst, EU_SRC0_WIDTH_HI, EU_SRC0_WIDTH_LO, reg.width);
	i915_eu_set(inst, EU_SRC0_VSTRIDE_HI, EU_SRC0_VSTRIDE_LO, reg.vstride);
	i915_eu_bit(inst, EU_SRC0_NEGATE_BIT, reg.negate & 1U);
}

/* Encodes source one, whether a register or an immediate. */
static void
i915_eu_src1(
	uint32_t *inst,
	struct i915_eu_reg reg)
{
	/* Encodes the type the source is read as. */
	i915_eu_set(inst, EU_SRC1_REG_TYPE_HI, EU_SRC1_REG_TYPE_LO, reg.type);

	/* An immediate says so with its own bit and carries its value in the last word. */
	if (reg.file == EU_FILE_IMM) {
		i915_eu_bit(inst, EU_SRC1_IS_IMM_BIT, 1U);
		i915_eu_set(inst, EU_IMM32_HI, EU_IMM32_LO, reg.immediate);
		return;
	}

	/* Encodes the register, its region and its negate modifier. */
	i915_eu_bit(inst, EU_SRC1_REG_FILE_BIT, i915_eu_file_bit(reg));
	i915_eu_set(inst, EU_SRC1_REG_NR_HI, EU_SRC1_REG_NR_LO, reg.nr);
	i915_eu_set(inst, EU_SRC1_SUBREG_HI, EU_SRC1_SUBREG_LO, reg.subnr);
	i915_eu_set(inst, EU_SRC1_HSTRIDE_HI, EU_SRC1_HSTRIDE_LO, reg.hstride);
	i915_eu_set(inst, EU_SRC1_WIDTH_HI, EU_SRC1_WIDTH_LO, reg.width);
	i915_eu_set(inst, EU_SRC1_VSTRIDE_HI, EU_SRC1_VSTRIDE_LO, reg.vstride);
	i915_eu_bit(inst, EU_SRC1_NEGATE_BIT, reg.negate & 1U);
}

/* Returns the hardware file bit of an operand: one for a general register, zero otherwise. */
static uint32_t
i915_eu_file_bit(
	struct i915_eu_reg reg)
{
	/* The hardware has one bit: general register or not. */
	if (reg.file == EU_FILE_GRF)
		return 1U;

	/* Anything else (the null register) is the architecture file. */
	return 0U;
}

/* Writes value into the inclusive bit range [high:low] of the instruction. */
static void
i915_eu_set(
	uint32_t *inst,
	unsigned high,
	unsigned low,
	uint32_t value)
{
	unsigned position;

	/* Each bit of the value takes its place from the low end upward. */
	for (position = low; position <= high; position++)
		i915_eu_bit(inst, position, (value >> (position - low)) & 1U);
}

/* Sets or clears one bit of the 128-bit instruction. */
static void
i915_eu_bit(
	uint32_t *inst,
	unsigned position,
	uint32_t value)
{
	uint32_t mask;

	/* The instruction is four little-endian words; the bit selects one. */
	mask = 1U << (position % 32U);

	/* Sets or clears the bit as the value's low bit says. */
	if ((value & 1U) != 0U) {
		inst[position / 32U] |= mask;
	} else {
		inst[position / 32U] &= ~mask;
	}
}
