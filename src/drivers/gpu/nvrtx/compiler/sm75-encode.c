/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The encoder of Turing (SM75) instructions (see sm75-encode.h).
 *
 * The placement of every field follows the facts in sm75-isa.h, taken from
 * Mesa's NAK (MIT).  An ALU instruction has three sources: the first is
 * always a register; the second and the third take one of the forms --
 * both registers, one an immediate, one a constant buffer word -- and the
 * form field says which.  When the third source is the immediate or the
 * constant buffer word, the second source moves into the third's register
 * slot.
 */

#include "sm75-encode.h"
#include "sm75-isa.h"

#include <stddef.h>

static void nvrtx_sm75_place_src2(struct nvrtx_sm75_word *word, const struct nvrtx_sm75_src *src);
static void nvrtx_sm75_place_wide(struct nvrtx_sm75_word *word, const struct nvrtx_sm75_src *src);
static void nvrtx_sm75_place_cb(struct nvrtx_sm75_word *word, const struct nvrtx_sm75_src *src);
static void nvrtx_sm75_memory(struct nvrtx_sm75_word *word);

/* Writes a field of up to 64 bits at a bit position. */
void
nvrtx_sm75_set(
	struct nvrtx_sm75_word *word,
	unsigned low,
	unsigned bits,
	uint64_t value)
{
	unsigned bit;
	unsigned index;
	uint32_t mask;

	/* Each bit of the field, into the word that holds it. */
	for (bit = 0U; bit < bits; bit++) {
		index = (low + bit) / 32U;
		mask = 1U << ((low + bit) % 32U);

		/* Sets or clears the bit the value has there. */
		if (((value >> bit) & 1U) != 0U) {
			word->w[index] |= mask;
		} else {
			word->w[index] &= ~mask;
		}
	}
}

/* Reads a field of up to 64 bits at a bit position. */
uint64_t
nvrtx_sm75_get(
	const struct nvrtx_sm75_word *word,
	unsigned low,
	unsigned bits)
{
	uint64_t value;
	unsigned bit;
	unsigned index;

	/* Each bit of the field, lowest first. */
	value = 0U;
	for (bit = 0U; bit < bits; bit++) {
		index = (low + bit) / 32U;

		/* A set bit of the instruction is the same bit of the value. */
		if (((word->w[index] >> ((low + bit) % 32U)) & 1U) != 0U)
			value |= (uint64_t)1U << bit;
	}

	/* Succeeded: the field's value. */
	return value;
}

/* Starts an instruction: all bits zero, guarded by PT, no scoreboard. */
void
nvrtx_sm75_begin(
	struct nvrtx_sm75_word *word)
{
	/* Nothing is set yet. */
	word->w[0] = 0U;
	word->w[1] = 0U;
	word->w[2] = 0U;
	word->w[3] = 0U;

	/* Every thread runs it, and it neither writes nor reads a scoreboard. */
	nvrtx_sm75_guard(word, NVRTX_SM75_PT, 0);
	nvrtx_sm75_control(word, 0U, 0, NVRTX_SM75_BAR_NONE, NVRTX_SM75_BAR_NONE, 0U);
}

/* Guards an instruction by a predicate, or its negation. */
void
nvrtx_sm75_guard(
	struct nvrtx_sm75_word *word,
	unsigned predicate,
	int negate)
{
	/* The predicate and whether it is negated. */
	nvrtx_sm75_set(word, NVRTX_SM75_GUARD_LOW, NVRTX_SM75_PRED_BITS, predicate);
	nvrtx_sm75_set(word, NVRTX_SM75_GUARD_NOT, 1U, (negate != 0) ? 1U : 0U);
}

/*
 * Sets the scheduling control: the cycles before the next instruction, the
 * yield hint, the scoreboard the result releases and the one the source
 * read releases (NVRTX_SM75_BAR_NONE for none), and the scoreboards the
 * instruction waits for before it issues.  The operand reuse stays off.
 */
void
nvrtx_sm75_control(
	struct nvrtx_sm75_word *word,
	unsigned delay,
	int yield,
	unsigned write_bar,
	unsigned read_bar,
	unsigned wait_mask)
{
	/* The delay and the yield. */
	nvrtx_sm75_set(word, NVRTX_SM75_DELAY_LOW, NVRTX_SM75_DELAY_BITS, delay);
	nvrtx_sm75_set(word, NVRTX_SM75_YIELD, 1U, (yield != 0) ? 1U : 0U);

	/* The scoreboards. */
	nvrtx_sm75_set(word, NVRTX_SM75_WRITE_BAR_LOW, NVRTX_SM75_BAR_BITS, write_bar);
	nvrtx_sm75_set(word, NVRTX_SM75_READ_BAR_LOW, NVRTX_SM75_BAR_BITS, read_bar);
	nvrtx_sm75_set(word, NVRTX_SM75_WAIT_LOW, NVRTX_SM75_WAIT_BITS, wait_mask);
}

/* Makes a register source. */
struct nvrtx_sm75_src
nvrtx_sm75_reg(
	uint32_t reg)
{
	struct nvrtx_sm75_src src;

	/* A plain register. */
	src.kind = NVRTX_SM75_SRC_REG;
	src.value = reg;
	src.cb_index = 0U;
	src.neg = 0;
	src.abs = 0;

	/* Succeeded: the operand. */
	return src;
}

/* Makes an immediate source. */
struct nvrtx_sm75_src
nvrtx_sm75_imm(
	uint32_t bits)
{
	struct nvrtx_sm75_src src;

	/* The 32 bits as they are. */
	src.kind = NVRTX_SM75_SRC_IMM;
	src.value = bits;
	src.cb_index = 0U;
	src.neg = 0;
	src.abs = 0;

	/* Succeeded: the operand. */
	return src;
}

/* Makes a constant buffer source: the word at a byte offset of a bound buffer. */
struct nvrtx_sm75_src
nvrtx_sm75_cb(
	uint32_t index,
	uint32_t offset)
{
	struct nvrtx_sm75_src src;

	/* The buffer and the offset. */
	src.kind = NVRTX_SM75_SRC_CB;
	src.value = offset;
	src.cb_index = index;
	src.neg = 0;
	src.abs = 0;

	/* Succeeded: the operand. */
	return src;
}

/*
 * Encodes the ALU form: the destination, the first source (a register or
 * none), and the second and third sources by the form they make.  A NULL
 * source is absent; the opcode is the 9-bit ALU opcode.
 */
void
nvrtx_sm75_alu(
	struct nvrtx_sm75_word *word,
	uint32_t opcode,
	uint32_t dst,
	const struct nvrtx_sm75_src *src0,
	const struct nvrtx_sm75_src *src1,
	const struct nvrtx_sm75_src *src2)
{
	uint32_t form;

	/* The destination. */
	nvrtx_sm75_set(word, NVRTX_SM75_DST_LOW, NVRTX_SM75_REG_BITS, dst);

	/* The first source is a register. */
	if (src0 != NULL) {
		nvrtx_sm75_set(word, NVRTX_SM75_SRC0_LOW, NVRTX_SM75_REG_BITS, src0->value);
		nvrtx_sm75_set(word, NVRTX_SM75_SRC0_ABS, 1U, (src0->abs != 0) ? 1U : 0U);
		nvrtx_sm75_set(word, NVRTX_SM75_SRC0_NEG, 1U, (src0->neg != 0) ? 1U : 0U);
	}

	/* The third source chooses the form when it is not a register. */
	if (src2 != NULL && src2->kind == NVRTX_SM75_SRC_IMM) {
		/* The immediate in the wide slot, the second source in the third's register slot. */
		nvrtx_sm75_set(word, NVRTX_SM75_IMM_LOW, NVRTX_SM75_IMM_BITS, src2->value);
		nvrtx_sm75_place_src2(word, src1);
		form = NVRTX_SM75_FORM_IMM_SRC2;
	} else if (src2 != NULL && src2->kind == NVRTX_SM75_SRC_CB) {
		/* The constant buffer word in the wide slot, the second source in the third's register slot. */
		nvrtx_sm75_place_cb(word, src2);
		nvrtx_sm75_place_src2(word, src1);
		form = NVRTX_SM75_FORM_CB_SRC2;
	} else {
		/* The third source, a register or none, in its slot; the second chooses the form. */
		nvrtx_sm75_place_src2(word, src2);
		form = NVRTX_SM75_FORM_REG;
		if (src1 != NULL && src1->kind == NVRTX_SM75_SRC_IMM) {
			nvrtx_sm75_set(word, NVRTX_SM75_IMM_LOW, NVRTX_SM75_IMM_BITS, src1->value);
			form = NVRTX_SM75_FORM_IMM;
		} else if (src1 != NULL && src1->kind == NVRTX_SM75_SRC_CB) {
			nvrtx_sm75_place_cb(word, src1);
			form = NVRTX_SM75_FORM_CB;
		} else if (src1 != NULL) {
			nvrtx_sm75_place_wide(word, src1);
		}
	}

	/* The opcode and the form. */
	nvrtx_sm75_set(word, NVRTX_SM75_OPCODE_LOW, NVRTX_SM75_ALU_OPCODE_BITS, opcode);
	nvrtx_sm75_set(word, NVRTX_SM75_ALU_FORM_LOW, NVRTX_SM75_ALU_FORM_BITS, form);
}

/* MOV dst, src: all four lanes of a quad written. */
void
nvrtx_sm75_mov(
	struct nvrtx_sm75_word *word,
	uint32_t dst,
	const struct nvrtx_sm75_src *src)
{
	/* The source is the second operand. */
	nvrtx_sm75_alu(word, NVRTX_SM75_OP_MOV, dst, NULL, src, NULL);
	nvrtx_sm75_set(word, NVRTX_SM75_MOV_LANES_LOW, 4U, NVRTX_SM75_MOV_LANES_ALL);
}

/*
 * FADD dst = a + b.  A register b is the second operand; any other b is
 * the third, with RZ in the second.
 */
void
nvrtx_sm75_fadd(
	struct nvrtx_sm75_word *word,
	uint32_t dst,
	const struct nvrtx_sm75_src *a,
	const struct nvrtx_sm75_src *b)
{
	struct nvrtx_sm75_src zero;

	zero = nvrtx_sm75_reg(NVRTX_SM75_RZ);

	/* The operands by the kind of b. */
	if (b->kind == NVRTX_SM75_SRC_REG) {
		nvrtx_sm75_alu(word, NVRTX_SM75_OP_FADD, dst, a, b, NULL);
	} else {
		nvrtx_sm75_alu(word, NVRTX_SM75_OP_FADD, dst, a, &zero, b);
	}

	/* Round to nearest, denormals kept. */
	nvrtx_sm75_set(word, NVRTX_SM75_ROUND_LOW, 2U, NVRTX_SM75_ROUND_NEAREST);
}

/* FMUL dst = a * b, with no post-divide. */
void
nvrtx_sm75_fmul(
	struct nvrtx_sm75_word *word,
	uint32_t dst,
	const struct nvrtx_sm75_src *a,
	const struct nvrtx_sm75_src *b)
{
	struct nvrtx_sm75_src zero;

	zero = nvrtx_sm75_reg(NVRTX_SM75_RZ);

	/* The product, RZ as the third operand. */
	nvrtx_sm75_alu(word, NVRTX_SM75_OP_FMUL, dst, a, b, &zero);
	nvrtx_sm75_set(word, NVRTX_SM75_ROUND_LOW, 2U, NVRTX_SM75_ROUND_NEAREST);
	nvrtx_sm75_set(word, NVRTX_SM75_FMUL_PDIV_LOW, 3U, NVRTX_SM75_FMUL_PDIV_NONE);
}

/* FFMA dst = a * b + c. */
void
nvrtx_sm75_ffma(
	struct nvrtx_sm75_word *word,
	uint32_t dst,
	const struct nvrtx_sm75_src *a,
	const struct nvrtx_sm75_src *b,
	const struct nvrtx_sm75_src *c)
{
	/* The three operands as they are. */
	nvrtx_sm75_alu(word, NVRTX_SM75_OP_FFMA, dst, a, b, c);
	nvrtx_sm75_set(word, NVRTX_SM75_ROUND_LOW, 2U, NVRTX_SM75_ROUND_NEAREST);
}

/* FMNMX: the smaller of a and b when min (the predicate PT), else the larger (!PT). */
void
nvrtx_sm75_fmnmx(
	struct nvrtx_sm75_word *word,
	uint32_t dst,
	const struct nvrtx_sm75_src *a,
	const struct nvrtx_sm75_src *b,
	int min)
{
	struct nvrtx_sm75_src zero;

	zero = nvrtx_sm75_reg(NVRTX_SM75_RZ);

	/* The two operands, RZ as the third, and the choice. */
	nvrtx_sm75_alu(word, NVRTX_SM75_OP_FMNMX, dst, a, b, &zero);
	nvrtx_sm75_set(word, NVRTX_SM75_PSRC_A_LOW, NVRTX_SM75_PRED_BITS, NVRTX_SM75_PT);
	nvrtx_sm75_set(word, NVRTX_SM75_PSRC_A_NOT, 1U, (min != 0) ? 0U : 1U);
}

/* FSETP predicate = compare(a, b) AND PT; the second destination is PT (none). */
void
nvrtx_sm75_fsetp(
	struct nvrtx_sm75_word *word,
	uint32_t predicate,
	uint32_t compare,
	const struct nvrtx_sm75_src *a,
	const struct nvrtx_sm75_src *b)
{
	/* The operands; there is no register destination. */
	nvrtx_sm75_alu(word, NVRTX_SM75_OP_FSETP, NVRTX_SM75_RZ, a, b, NULL);

	/* The comparison, combined by AND with the accumulator PT. */
	nvrtx_sm75_set(word, NVRTX_SM75_SETOP_LOW, 2U, NVRTX_SM75_SETOP_AND);
	nvrtx_sm75_set(word, NVRTX_SM75_CMP_LOW, 4U, compare);
	nvrtx_sm75_set(word, NVRTX_SM75_PSRC_A_LOW, NVRTX_SM75_PRED_BITS, NVRTX_SM75_PT);

	/* The destinations. */
	nvrtx_sm75_set(word, NVRTX_SM75_PDST0_LOW, NVRTX_SM75_PRED_BITS, predicate);
	nvrtx_sm75_set(word, NVRTX_SM75_PDST1_LOW, NVRTX_SM75_PRED_BITS, NVRTX_SM75_PT);
}

/* ISETP predicate = compare(a, b) AND PT, signed or unsigned. */
void
nvrtx_sm75_isetp(
	struct nvrtx_sm75_word *word,
	uint32_t predicate,
	uint32_t compare,
	int is_signed,
	const struct nvrtx_sm75_src *a,
	const struct nvrtx_sm75_src *b)
{
	/* The operands; there is no register destination. */
	nvrtx_sm75_alu(word, NVRTX_SM75_OP_ISETP, NVRTX_SM75_RZ, a, b, NULL);

	/* The low compare and the accumulator are PT. */
	nvrtx_sm75_set(word, NVRTX_SM75_PSRC_C_LOW, NVRTX_SM75_PRED_BITS, NVRTX_SM75_PT);
	nvrtx_sm75_set(word, NVRTX_SM75_PSRC_A_LOW, NVRTX_SM75_PRED_BITS, NVRTX_SM75_PT);

	/* The signedness, the AND, the comparison. */
	nvrtx_sm75_set(word, NVRTX_SM75_SIGNED, 1U, (is_signed != 0) ? 1U : 0U);
	nvrtx_sm75_set(word, NVRTX_SM75_SETOP_LOW, 2U, NVRTX_SM75_SETOP_AND);
	nvrtx_sm75_set(word, NVRTX_SM75_CMP_LOW, 3U, compare);

	/* The destinations. */
	nvrtx_sm75_set(word, NVRTX_SM75_PDST0_LOW, NVRTX_SM75_PRED_BITS, predicate);
	nvrtx_sm75_set(word, NVRTX_SM75_PDST1_LOW, NVRTX_SM75_PRED_BITS, NVRTX_SM75_PT);
}

/* IADD3 dst = a + b + c, the carry inputs !PT (false) and the carry outputs PT (none). */
void
nvrtx_sm75_iadd3(
	struct nvrtx_sm75_word *word,
	uint32_t dst,
	const struct nvrtx_sm75_src *a,
	const struct nvrtx_sm75_src *b,
	const struct nvrtx_sm75_src *c)
{
	/* The three operands. */
	nvrtx_sm75_alu(word, NVRTX_SM75_OP_IADD3, dst, a, b, c);

	/* No carry in. */
	nvrtx_sm75_set(word, NVRTX_SM75_PSRC_A_LOW, NVRTX_SM75_PRED_BITS, NVRTX_SM75_PT);
	nvrtx_sm75_set(word, NVRTX_SM75_PSRC_A_NOT, 1U, 1U);
	nvrtx_sm75_set(word, NVRTX_SM75_PSRC_B_LOW, NVRTX_SM75_PRED_BITS, NVRTX_SM75_PT);
	nvrtx_sm75_set(word, NVRTX_SM75_PSRC_B_NOT, 1U, 1U);

	/* No carry out. */
	nvrtx_sm75_set(word, NVRTX_SM75_PDST0_LOW, NVRTX_SM75_PRED_BITS, NVRTX_SM75_PT);
	nvrtx_sm75_set(word, NVRTX_SM75_PDST1_LOW, NVRTX_SM75_PRED_BITS, NVRTX_SM75_PT);
}

/*
 * IMAD dst = a * b + c, the low 32 bits.  The carry output is PT (none)
 * and the carry input !PT (false), as the CUDA compiler writes it.
 */
void
nvrtx_sm75_imad(
	struct nvrtx_sm75_word *word,
	uint32_t dst,
	const struct nvrtx_sm75_src *a,
	const struct nvrtx_sm75_src *b,
	const struct nvrtx_sm75_src *c,
	int is_signed)
{
	/* The three operands and the signedness. */
	nvrtx_sm75_alu(word, NVRTX_SM75_OP_IMAD, dst, a, b, c);
	nvrtx_sm75_set(word, NVRTX_SM75_SIGNED, 1U, (is_signed != 0) ? 1U : 0U);

	/* No carry out, no carry in. */
	nvrtx_sm75_set(word, NVRTX_SM75_PDST0_LOW, NVRTX_SM75_PRED_BITS, NVRTX_SM75_PT);
	nvrtx_sm75_set(word, NVRTX_SM75_PSRC_A_LOW, NVRTX_SM75_PRED_BITS, NVRTX_SM75_PT);
	nvrtx_sm75_set(word, NVRTX_SM75_PSRC_A_NOT, 1U, 1U);
}

/* IMAD.WIDE.U32: the dst pair = a * b + the c pair, unsigned. */
void
nvrtx_sm75_imad_wide(
	struct nvrtx_sm75_word *word,
	uint32_t dst,
	const struct nvrtx_sm75_src *a,
	const struct nvrtx_sm75_src *b,
	const struct nvrtx_sm75_src *c)
{
	/* The three operands, unsigned. */
	nvrtx_sm75_alu(word, NVRTX_SM75_OP_IMAD_WIDE, dst, a, b, c);
	nvrtx_sm75_set(word, NVRTX_SM75_SIGNED, 1U, 0U);

	/* No carry out, no carry in. */
	nvrtx_sm75_set(word, NVRTX_SM75_PDST0_LOW, NVRTX_SM75_PRED_BITS, NVRTX_SM75_PT);
	nvrtx_sm75_set(word, NVRTX_SM75_PSRC_A_LOW, NVRTX_SM75_PRED_BITS, NVRTX_SM75_PT);
	nvrtx_sm75_set(word, NVRTX_SM75_PSRC_A_NOT, 1U, 1U);
}

/* LOP3 dst = lut(a, b, c), with no predicate output. */
void
nvrtx_sm75_lop3(
	struct nvrtx_sm75_word *word,
	uint32_t dst,
	const struct nvrtx_sm75_src *a,
	const struct nvrtx_sm75_src *b,
	const struct nvrtx_sm75_src *c,
	uint32_t lut)
{
	/* The three operands and the table. */
	nvrtx_sm75_alu(word, NVRTX_SM75_OP_LOP3, dst, a, b, c);
	nvrtx_sm75_set(word, NVRTX_SM75_LUT_LOW, 8U, lut);

	/* The predicate input is !PT and the predicate output PT (none). */
	nvrtx_sm75_set(word, NVRTX_SM75_PSRC_A_LOW, NVRTX_SM75_PRED_BITS, NVRTX_SM75_PT);
	nvrtx_sm75_set(word, NVRTX_SM75_PSRC_A_NOT, 1U, 1U);
	nvrtx_sm75_set(word, NVRTX_SM75_PDST0_LOW, NVRTX_SM75_PRED_BITS, NVRTX_SM75_PT);
}

/*
 * SHF as a 32-bit shift, the count taken modulo 32: left puts the value in
 * the low operand and returns the low half; right puts it in the high
 * operand and returns the high half, unsigned or signed.
 */
void
nvrtx_sm75_shift(
	struct nvrtx_sm75_word *word,
	uint32_t dst,
	uint32_t value,
	const struct nvrtx_sm75_src *count,
	int right,
	int arithmetic)
{
	struct nvrtx_sm75_src source;
	struct nvrtx_sm75_src zero;
	uint32_t type;

	source = nvrtx_sm75_reg(value);
	zero = nvrtx_sm75_reg(NVRTX_SM75_RZ);

	/* The funnel's low and high operands by the direction. */
	if (right != 0) {
		nvrtx_sm75_alu(word, NVRTX_SM75_OP_SHF, dst, &zero, count, &source);
	} else {
		nvrtx_sm75_alu(word, NVRTX_SM75_OP_SHF, dst, &source, count, &zero);
	}

	/* The type: signed for a left shift and an arithmetic right one, unsigned for a logical right one. */
	type = NVRTX_SM75_SHF_TYPE_I32;
	if (right != 0 && arithmetic == 0)
		type = NVRTX_SM75_SHF_TYPE_U32;

	/* The type, the wrap, the direction and the half. */
	nvrtx_sm75_set(word, NVRTX_SM75_SHF_TYPE_LOW, 2U, type);
	nvrtx_sm75_set(word, NVRTX_SM75_SHF_WRAP, 1U, 1U);
	nvrtx_sm75_set(word, NVRTX_SM75_SHF_RIGHT, 1U, (right != 0) ? 1U : 0U);
	nvrtx_sm75_set(word, NVRTX_SM75_SHF_HIGH, 1U, (right != 0) ? 1U : 0U);
}

/* SEL dst = predicate (or its negation) ? a : b. */
void
nvrtx_sm75_sel(
	struct nvrtx_sm75_word *word,
	uint32_t dst,
	const struct nvrtx_sm75_src *a,
	const struct nvrtx_sm75_src *b,
	uint32_t predicate,
	int negate)
{
	/* The two operands and the predicate. */
	nvrtx_sm75_alu(word, NVRTX_SM75_OP_SEL, dst, a, b, NULL);
	nvrtx_sm75_set(word, NVRTX_SM75_PSRC_A_LOW, NVRTX_SM75_PRED_BITS, predicate);
	nvrtx_sm75_set(word, NVRTX_SM75_PSRC_A_NOT, 1U, (negate != 0) ? 1U : 0U);
}

/* MUFU dst = function(src). */
void
nvrtx_sm75_mufu(
	struct nvrtx_sm75_word *word,
	uint32_t dst,
	uint32_t function,
	uint32_t src)
{
	struct nvrtx_sm75_src source;

	source = nvrtx_sm75_reg(src);

	/* The source is the second operand. */
	nvrtx_sm75_alu(word, NVRTX_SM75_OP_MUFU, dst, NULL, &source, NULL);
	nvrtx_sm75_set(word, NVRTX_SM75_MUFU_LOW, 6U, function);
}

/* I2F dst = the float of the 32-bit integer src, rounded to nearest. */
void
nvrtx_sm75_i2f(
	struct nvrtx_sm75_word *word,
	uint32_t dst,
	uint32_t src,
	int is_signed)
{
	struct nvrtx_sm75_src source;

	source = nvrtx_sm75_reg(src);

	/* The source, its signedness, both sizes 32 bits, the rounding. */
	nvrtx_sm75_alu(word, NVRTX_SM75_OP_I2F, dst, NULL, &source, NULL);
	nvrtx_sm75_set(word, NVRTX_SM75_I2F_SIGNED, 1U, (is_signed != 0) ? 1U : 0U);
	nvrtx_sm75_set(word, NVRTX_SM75_CVT_DST_SIZE_LOW, 2U, NVRTX_SM75_CVT_SIZE_32);
	nvrtx_sm75_set(word, NVRTX_SM75_CVT_SRC_SIZE_LOW, 2U, NVRTX_SM75_CVT_SIZE_32);
	nvrtx_sm75_set(word, NVRTX_SM75_ROUND_LOW, 2U, NVRTX_SM75_ROUND_NEAREST);
}

/* F2I dst = the 32-bit integer of the float src, rounded toward zero. */
void
nvrtx_sm75_f2i(
	struct nvrtx_sm75_word *word,
	uint32_t dst,
	uint32_t src,
	int is_signed)
{
	struct nvrtx_sm75_src source;

	source = nvrtx_sm75_reg(src);

	/* The source, the result's signedness, both sizes 32 bits, the rounding. */
	nvrtx_sm75_alu(word, NVRTX_SM75_OP_F2I, dst, NULL, &source, NULL);
	nvrtx_sm75_set(word, NVRTX_SM75_F2I_SIGNED, 1U, (is_signed != 0) ? 1U : 0U);
	nvrtx_sm75_set(word, NVRTX_SM75_CVT_DST_SIZE_LOW, 2U, NVRTX_SM75_CVT_SIZE_32);
	nvrtx_sm75_set(word, NVRTX_SM75_CVT_SRC_SIZE_LOW, 2U, NVRTX_SM75_CVT_SIZE_32);
	nvrtx_sm75_set(word, NVRTX_SM75_ROUND_LOW, 2U, NVRTX_SM75_ROUND_ZERO);
}

/* FRND dst = src rounded to an integral float. */
void
nvrtx_sm75_frnd(
	struct nvrtx_sm75_word *word,
	uint32_t dst,
	uint32_t src,
	uint32_t rounding)
{
	struct nvrtx_sm75_src source;

	source = nvrtx_sm75_reg(src);

	/* The source, both sizes 32 bits, the rounding. */
	nvrtx_sm75_alu(word, NVRTX_SM75_OP_FRND, dst, NULL, &source, NULL);
	nvrtx_sm75_set(word, NVRTX_SM75_CVT_DST_SIZE_LOW, 2U, NVRTX_SM75_CVT_SIZE_32);
	nvrtx_sm75_set(word, NVRTX_SM75_CVT_SRC_SIZE_LOW, 2U, NVRTX_SM75_CVT_SIZE_32);
	nvrtx_sm75_set(word, NVRTX_SM75_ROUND_LOW, 2U, rounding);
}

/* S2R dst = a special register. */
void
nvrtx_sm75_s2r(
	struct nvrtx_sm75_word *word,
	uint32_t dst,
	uint32_t special)
{
	/* The opcode, the destination, the register's number. */
	nvrtx_sm75_set(word, NVRTX_SM75_OPCODE_LOW, NVRTX_SM75_OPCODE_BITS, NVRTX_SM75_OP_S2R);
	nvrtx_sm75_set(word, NVRTX_SM75_DST_LOW, NVRTX_SM75_REG_BITS, dst);
	nvrtx_sm75_set(word, NVRTX_SM75_S2R_INDEX_LOW, 8U, special);
}

/* LDC dst = the 32- or 64-bit word at a byte offset of a bound constant buffer, indexed by RZ. */
void
nvrtx_sm75_ldc(
	struct nvrtx_sm75_word *word,
	uint32_t dst,
	uint32_t index,
	uint32_t offset,
	int wide)
{
	/* The opcode, the destination, the index register RZ, the mode, a bound buffer. */
	nvrtx_sm75_set(word, NVRTX_SM75_OPCODE_LOW, NVRTX_SM75_OPCODE_BITS, NVRTX_SM75_OP_LDC);
	nvrtx_sm75_set(word, NVRTX_SM75_DST_LOW, NVRTX_SM75_REG_BITS, dst);
	nvrtx_sm75_set(word, NVRTX_SM75_SRC0_LOW, NVRTX_SM75_REG_BITS, NVRTX_SM75_RZ);
	nvrtx_sm75_set(word, NVRTX_SM75_LDC_MODE_LOW, 2U, 0U);
	nvrtx_sm75_set(word, NVRTX_SM75_CB_BINDLESS, 1U, 0U);

	/* The buffer, the offset and the size. */
	nvrtx_sm75_set(word, NVRTX_SM75_CB_INDEX_LOW, NVRTX_SM75_CB_INDEX_BITS, index);
	nvrtx_sm75_set(word, NVRTX_SM75_CB_OFFSET_LOW, NVRTX_SM75_CB_OFFSET_BITS, offset);
	nvrtx_sm75_set(word, NVRTX_SM75_MEM_TYPE_LOW, 3U, (wide != 0) ? NVRTX_SM75_MEM_B64 : NVRTX_SM75_MEM_B32);
}

/* LDG.E dst = the 32-bit word at the 64-bit address in a register pair. */
void
nvrtx_sm75_ldg(
	struct nvrtx_sm75_word *word,
	uint32_t dst,
	uint32_t address)
{
	/* The opcode, no predicate output, the access. */
	nvrtx_sm75_set(word, NVRTX_SM75_OPCODE_LOW, NVRTX_SM75_OPCODE_BITS, NVRTX_SM75_OP_LDG);
	nvrtx_sm75_set(word, NVRTX_SM75_PDST0_LOW, NVRTX_SM75_PRED_BITS, NVRTX_SM75_PT);
	nvrtx_sm75_memory(word);

	/* The destination and the address, no offset. */
	nvrtx_sm75_set(word, NVRTX_SM75_DST_LOW, NVRTX_SM75_REG_BITS, dst);
	nvrtx_sm75_set(word, NVRTX_SM75_SRC0_LOW, NVRTX_SM75_REG_BITS, address);
	nvrtx_sm75_set(word, NVRTX_SM75_MEM_OFFSET_LOW, NVRTX_SM75_MEM_OFFSET_BITS, 0U);
}

/* STG.E: the 32-bit data register to the 64-bit address in a register pair. */
void
nvrtx_sm75_stg(
	struct nvrtx_sm75_word *word,
	uint32_t address,
	uint32_t data)
{
	/* The opcode and the access. */
	nvrtx_sm75_set(word, NVRTX_SM75_OPCODE_LOW, NVRTX_SM75_OPCODE_BITS, NVRTX_SM75_OP_STG);
	nvrtx_sm75_memory(word);

	/* The address, the data, no offset. */
	nvrtx_sm75_set(word, NVRTX_SM75_SRC0_LOW, NVRTX_SM75_REG_BITS, address);
	nvrtx_sm75_set(word, NVRTX_SM75_STORE_DATA_LOW, NVRTX_SM75_REG_BITS, data);
	nvrtx_sm75_set(word, NVRTX_SM75_MEM_OFFSET_LOW, NVRTX_SM75_MEM_OFFSET_BITS, 0U);
}

/* EXIT, unconditional. */
void
nvrtx_sm75_exit(
	struct nvrtx_sm75_word *word)
{
	/* The opcode; the predicate source is PT. */
	nvrtx_sm75_set(word, NVRTX_SM75_OPCODE_LOW, NVRTX_SM75_OPCODE_BITS, NVRTX_SM75_OP_EXIT);
	nvrtx_sm75_set(word, NVRTX_SM75_PSRC_A_LOW, NVRTX_SM75_PRED_BITS, NVRTX_SM75_PT);
}

/* NOP. */
void
nvrtx_sm75_nop(
	struct nvrtx_sm75_word *word)
{
	/* The opcode alone. */
	nvrtx_sm75_set(word, NVRTX_SM75_OPCODE_LOW, NVRTX_SM75_OPCODE_BITS, NVRTX_SM75_OP_NOP);
}

/* BRA by a signed number of instructions from the next one (-1 is the BRA itself). */
void
nvrtx_sm75_bra(
	struct nvrtx_sm75_word *word,
	int32_t instructions)
{
	int64_t words;

	/* The offset counts 32-bit words, four to an instruction. */
	words = (int64_t)instructions * (int64_t)NVRTX_SM75_INSTRUCTION_WORDS;

	/* The opcode, not uniform, the predicate source PT, the offset. */
	nvrtx_sm75_set(word, NVRTX_SM75_OPCODE_LOW, NVRTX_SM75_OPCODE_BITS, NVRTX_SM75_OP_BRA);
	nvrtx_sm75_set(word, NVRTX_SM75_SRC1_LOW, 1U, 0U);
	nvrtx_sm75_set(word, NVRTX_SM75_PSRC_A_LOW, NVRTX_SM75_PRED_BITS, NVRTX_SM75_PT);
	nvrtx_sm75_set(word, NVRTX_SM75_BRA_OFFSET_LOW, NVRTX_SM75_BRA_OFFSET_BITS, (uint64_t)words);
}

/* Places a source in the third source's register slot, with that slot's abs and neg; none places nothing. */
static void
nvrtx_sm75_place_src2(
	struct nvrtx_sm75_word *word,
	const struct nvrtx_sm75_src *src)
{
	/* An absent source leaves the slot zero. */
	if (src == NULL)
		return;

	/* The register and its modifiers. */
	nvrtx_sm75_set(word, NVRTX_SM75_SRC2_LOW, NVRTX_SM75_REG_BITS, src->value);
	nvrtx_sm75_set(word, NVRTX_SM75_SRC2_ABS, 1U, (src->abs != 0) ? 1U : 0U);
	nvrtx_sm75_set(word, NVRTX_SM75_SRC2_NEG, 1U, (src->neg != 0) ? 1U : 0U);
}

/* Places a register in the second source's slot, with its abs and neg. */
static void
nvrtx_sm75_place_wide(
	struct nvrtx_sm75_word *word,
	const struct nvrtx_sm75_src *src)
{
	/* The register and its modifiers. */
	nvrtx_sm75_set(word, NVRTX_SM75_SRC1_LOW, NVRTX_SM75_REG_BITS, src->value);
	nvrtx_sm75_set(word, NVRTX_SM75_SRC1_ABS, 1U, (src->abs != 0) ? 1U : 0U);
	nvrtx_sm75_set(word, NVRTX_SM75_SRC1_NEG, 1U, (src->neg != 0) ? 1U : 0U);
}

/* Places a constant buffer word in the wide slot: its offset and its buffer, bound, with its abs and neg. */
static void
nvrtx_sm75_place_cb(
	struct nvrtx_sm75_word *word,
	const struct nvrtx_sm75_src *src)
{
	/* The offset, the buffer, a bound buffer. */
	nvrtx_sm75_set(word, NVRTX_SM75_CB_OFFSET_LOW, NVRTX_SM75_CB_OFFSET_BITS, src->value);
	nvrtx_sm75_set(word, NVRTX_SM75_CB_INDEX_LOW, NVRTX_SM75_CB_INDEX_BITS, src->cb_index);
	nvrtx_sm75_set(word, NVRTX_SM75_CB_BINDLESS, 1U, 0U);

	/* The modifiers. */
	nvrtx_sm75_set(word, NVRTX_SM75_SRC1_ABS, 1U, (src->abs != 0) ? 1U : 0U);
	nvrtx_sm75_set(word, NVRTX_SM75_SRC1_NEG, 1U, (src->neg != 0) ? 1U : 0U);
}

/* Sets a global access: a 64-bit address, 32 bits, strong at the GPU's scope, normal eviction. */
static void
nvrtx_sm75_memory(
	struct nvrtx_sm75_word *word)
{
	/* The address size and the data size. */
	nvrtx_sm75_set(word, NVRTX_SM75_MEM_A64, 1U, 1U);
	nvrtx_sm75_set(word, NVRTX_SM75_MEM_TYPE_LOW, 3U, NVRTX_SM75_MEM_B32);

	/* The scope, the order and the eviction priority. */
	nvrtx_sm75_set(word, NVRTX_SM75_MEM_SCOPE_LOW, 2U, NVRTX_SM75_MEM_SCOPE_GPU);
	nvrtx_sm75_set(word, NVRTX_SM75_MEM_ORDER_LOW, 2U, NVRTX_SM75_MEM_ORDER_STRONG);
	nvrtx_sm75_set(word, NVRTX_SM75_MEM_EVICT_LOW, 3U, NVRTX_SM75_MEM_EVICT_NORMAL);
}
