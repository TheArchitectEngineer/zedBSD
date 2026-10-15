/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

#include "sm75.h"

#include "drivers/gpu/compiler/optimize.h"
#include "drivers/gpu/compiler/spirv.h"

#include <kern/kcrt.h>
#include <kern/kmem.h>
#include <uapi/errno.h>

#include <stddef.h>

/*
 * The encoder of Turing (SM75) instructions (see sm75.h).
 *
 * The placement of every field follows the facts in sm75.h.  An ALU instruction has three sources: the first is
 * always a register; the second and the third take one of the forms --
 * both registers, one an immediate, one a constant buffer word -- and the
 * form field says which.  When the third source is the immediate or the
 * constant buffer word, the second source moves into the third's register
 * slot.
 */

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

/*
 * Guards an instruction by a predicate, or its negation.
 *
 * Procedure referenced: Mesa 25.3.6 src/nouveau/compiler/nak/sm70_encode.rs
 * (set_pred).
 */
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
 *
 * Procedure referenced: Mesa 25.3.6 src/nouveau/compiler/nak/sm70_encode.rs
 * (set_instr_deps).
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
 *
 * Procedure referenced: Mesa 25.3.6 src/nouveau/compiler/nak/sm70_encode.rs
 * (encode_alu_base).
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

/*
 * MOV dst, src: all four lanes of a quad written.
 *
 * Procedure referenced: Mesa 25.3.6 src/nouveau/compiler/nak/sm70_encode.rs
 * (OpMov::encode).
 */
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
 *
 * Procedure referenced: Mesa 25.3.6 src/nouveau/compiler/nak/sm70_encode.rs
 * (OpFAdd::encode).
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

/*
 * FMUL dst = a * b, with no post-divide.
 *
 * Procedure referenced: Mesa 25.3.6 src/nouveau/compiler/nak/sm70_encode.rs
 * (OpFMul::encode).
 */
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

/*
 * FFMA dst = a * b + c.
 *
 * Procedure referenced: Mesa 25.3.6 src/nouveau/compiler/nak/sm70_encode.rs
 * (OpFFma::encode).
 */
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

/*
 * FMNMX: the smaller of a and b when min (the predicate PT), else the larger
 * (!PT).
 *
 * Procedure referenced: Mesa 25.3.6 src/nouveau/compiler/nak/sm70_encode.rs
 * (OpFMnMx::encode).
 */
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

/*
 * FSETP predicate = compare(a, b) AND PT; the second destination is PT
 * (none).
 *
 * Procedure referenced: Mesa 25.3.6 src/nouveau/compiler/nak/sm70_encode.rs
 * (OpFSetP::encode).
 */
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

/*
 * ISETP predicate = compare(a, b) AND PT, signed or unsigned.
 *
 * Procedure referenced: Mesa 25.3.6 src/nouveau/compiler/nak/sm70_encode.rs
 * (OpISetP::encode).
 */
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

/*
 * IADD3 dst = a + b + c, the carry inputs !PT (false) and the carry outputs
 * PT (none).
 *
 * Procedure referenced: Mesa 25.3.6 src/nouveau/compiler/nak/sm70_encode.rs
 * (OpIAdd3::encode).
 */
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
 * and the carry input !PT (false).
 *
 * Value referenced: the SM75 code the CUDA compiler emits (IMAD's carry
 * operands).
 *
 * Procedure referenced: Mesa 25.3.6 src/nouveau/compiler/nak/sm70_encode.rs
 * (OpIMad::encode).
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

/*
 * IMAD.WIDE.U32: the dst pair = a * b + the c pair, unsigned.
 *
 * Procedure referenced: Mesa 25.3.6 src/nouveau/compiler/nak/sm70_encode.rs
 * (OpIMad64::encode).
 */
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

/*
 * LOP3 dst = lut(a, b, c), with no predicate output.
 *
 * Procedure referenced: Mesa 25.3.6 src/nouveau/compiler/nak/sm70_encode.rs
 * (OpLop3::encode).
 */
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
 *
 * Procedure referenced: Mesa 25.3.6 src/nouveau/compiler/nak/sm70_encode.rs
 * (OpShf::encode) and src/nouveau/compiler/nak/builder.rs (SSABuilder::shl
 * and SSABuilder::shr).
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

/*
 * SEL dst = predicate (or its negation) ? a : b.
 *
 * Procedure referenced: Mesa 25.3.6 src/nouveau/compiler/nak/sm70_encode.rs
 * (OpSel::encode).
 */
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

/*
 * MUFU dst = function(src).
 *
 * Procedure referenced: Mesa 25.3.6 src/nouveau/compiler/nak/sm70_encode.rs
 * (OpMuFu::encode).
 */
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

/*
 * I2F dst = the float of the 32-bit integer src, rounded to nearest.
 *
 * Procedure referenced: Mesa 25.3.6 src/nouveau/compiler/nak/sm70_encode.rs
 * (OpI2F::encode).
 */
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

/*
 * F2I dst = the 32-bit integer of the float src, rounded toward zero.
 *
 * Procedure referenced: Mesa 25.3.6 src/nouveau/compiler/nak/sm70_encode.rs
 * (OpF2I::encode).
 */
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

/*
 * FRND dst = src rounded to an integral float.
 *
 * Procedure referenced: Mesa 25.3.6 src/nouveau/compiler/nak/sm70_encode.rs
 * (OpFRnd::encode).
 */
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

/*
 * S2R dst = a special register.
 *
 * Procedure referenced: Mesa 25.3.6 src/nouveau/compiler/nak/sm70_encode.rs
 * (OpS2R::encode).
 */
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

/*
 * LDC dst = the 32- or 64-bit word at a byte offset of a bound constant
 * buffer, indexed by RZ.
 *
 * Procedure referenced: Mesa 25.3.6 src/nouveau/compiler/nak/sm70_encode.rs
 * (OpLdc::encode).
 */
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

/*
 * LDG.E dst = the 32-bit word at the 64-bit address in a register pair.
 *
 * Procedure referenced: Mesa 25.3.6 src/nouveau/compiler/nak/sm70_encode.rs
 * (OpLd::encode).
 */
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

/*
 * STG.E: the 32-bit data register to the 64-bit address in a register pair.
 *
 * Procedure referenced: Mesa 25.3.6 src/nouveau/compiler/nak/sm70_encode.rs
 * (OpSt::encode).
 */
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

/*
 * Sets a global access's memory order for a shader model.
 *
 * Before SM80 the scope (77..78, the GPU's) and the order (79..80, strong)
 * are two fields; from SM80 on they are one (77..80), in which strong at
 * the GPU's scope is 7.
 *
 * Value referenced: Mesa 25.3.6 src/nouveau/compiler/nak/sm70_encode.rs
 * (set_mem_order).
 */
void
nvrtx_sm75_memory_order(
	struct nvrtx_sm75_word *word,
	uint32_t sm)
{
	/* Ampere and Ada: the one field. */
	if (sm >= NVRTX_SM_AMPERE_FIRST) {
		nvrtx_sm75_set(word, NVRTX_SM80_MEM_ORDER_LOW, NVRTX_SM80_MEM_ORDER_BITS, NVRTX_SM80_MEM_STRONG_GPU);
		return;
	}

	/* Turing: the scope and the order. */
	nvrtx_sm75_set(word, NVRTX_SM75_MEM_SCOPE_LOW, 2U, NVRTX_SM75_MEM_SCOPE_GPU);
	nvrtx_sm75_set(word, NVRTX_SM75_MEM_ORDER_LOW, 2U, NVRTX_SM75_MEM_ORDER_STRONG);
}

/*
 * EXIT, unconditional.
 *
 * Procedure referenced: Mesa 25.3.6 src/nouveau/compiler/nak/sm70_encode.rs
 * (OpExit::encode).
 */
void
nvrtx_sm75_exit(
	struct nvrtx_sm75_word *word)
{
	/* The opcode; the predicate source is PT. */
	nvrtx_sm75_set(word, NVRTX_SM75_OPCODE_LOW, NVRTX_SM75_OPCODE_BITS, NVRTX_SM75_OP_EXIT);
	nvrtx_sm75_set(word, NVRTX_SM75_PSRC_A_LOW, NVRTX_SM75_PRED_BITS, NVRTX_SM75_PT);
}

/*
 * NOP.
 *
 * Procedure referenced: Mesa 25.3.6 src/nouveau/compiler/nak/sm70_encode.rs
 * (OpNop::encode).
 */
void
nvrtx_sm75_nop(
	struct nvrtx_sm75_word *word)
{
	/* The opcode alone. */
	nvrtx_sm75_set(word, NVRTX_SM75_OPCODE_LOW, NVRTX_SM75_OPCODE_BITS, NVRTX_SM75_OP_NOP);
}

/*
 * BRA by a signed number of instructions from the next one (-1 is the BRA
 * itself).
 *
 * Procedure referenced: Mesa 25.3.6 src/nouveau/compiler/nak/sm70_encode.rs
 * (OpBra::encode, get_rel_offset and set_rel_offset).
 */
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

/*
 * LDS dst = the 32-bit word at the shared memory offset in a register.
 *
 * Procedure referenced: Mesa 25.3.6 src/nouveau/compiler/nak/sm70_encode.rs
 * (OpLd::encode).
 */
void
nvrtx_sm75_lds(
	struct nvrtx_sm75_word *word,
	uint32_t dst,
	uint32_t offset)
{
	/* The opcode, the size, the destination and the offset register. */
	nvrtx_sm75_set(word, NVRTX_SM75_OPCODE_LOW, NVRTX_SM75_OPCODE_BITS, NVRTX_SM75_OP_LDS);
	nvrtx_sm75_set(word, NVRTX_SM75_MEM_TYPE_LOW, 3U, NVRTX_SM75_MEM_B32);
	nvrtx_sm75_set(word, NVRTX_SM75_DST_LOW, NVRTX_SM75_REG_BITS, dst);
	nvrtx_sm75_set(word, NVRTX_SM75_SRC0_LOW, NVRTX_SM75_REG_BITS, offset);
}

/*
 * STS: the 32-bit data register to the shared memory offset in a register.
 *
 * Procedure referenced: Mesa 25.3.6 src/nouveau/compiler/nak/sm70_encode.rs
 * (OpSt::encode).
 */
void
nvrtx_sm75_sts(
	struct nvrtx_sm75_word *word,
	uint32_t offset,
	uint32_t data)
{
	/* The opcode, the size, the offset and the data registers. */
	nvrtx_sm75_set(word, NVRTX_SM75_OPCODE_LOW, NVRTX_SM75_OPCODE_BITS, NVRTX_SM75_OP_STS);
	nvrtx_sm75_set(word, NVRTX_SM75_MEM_TYPE_LOW, 3U, NVRTX_SM75_MEM_B32);
	nvrtx_sm75_set(word, NVRTX_SM75_SRC0_LOW, NVRTX_SM75_REG_BITS, offset);
	nvrtx_sm75_set(word, NVRTX_SM75_STORE_DATA_LOW, NVRTX_SM75_REG_BITS, data);
}

/*
 * An atomic returning the old value: ATOMG on the 64-bit address in a
 * register pair (strong at the GPU's scope), or ATOMS on the shared memory
 * offset in a register.
 *
 * Procedure referenced: Mesa 25.3.6 src/nouveau/compiler/nak/sm70_encode.rs
 * (OpAtom::encode).
 */
void
nvrtx_sm75_atomic(
	struct nvrtx_sm75_word *word,
	int shared,
	uint32_t dst,
	uint32_t address,
	uint32_t data,
	uint32_t operation,
	int is_signed)
{
	/* The opcode and, for global memory, the access. */
	if (shared != 0) {
		nvrtx_sm75_set(word, NVRTX_SM75_OPCODE_LOW, NVRTX_SM75_OPCODE_BITS, NVRTX_SM75_OP_ATOMS);
	} else {
		nvrtx_sm75_set(word, NVRTX_SM75_OPCODE_LOW, NVRTX_SM75_OPCODE_BITS, NVRTX_SM75_OP_ATOMG);
		nvrtx_sm75_set(word, NVRTX_SM75_PDST0_LOW, NVRTX_SM75_PRED_BITS, NVRTX_SM75_PT);
		nvrtx_sm75_memory(word);
	}

	/* The registers, the operation and the type. */
	nvrtx_sm75_set(word, NVRTX_SM75_DST_LOW, NVRTX_SM75_REG_BITS, dst);
	nvrtx_sm75_set(word, NVRTX_SM75_SRC0_LOW, NVRTX_SM75_REG_BITS, address);
	nvrtx_sm75_set(word, NVRTX_SM75_STORE_DATA_LOW, NVRTX_SM75_REG_BITS, data);
	nvrtx_sm75_set(word, NVRTX_SM75_MEM_OFFSET_LOW, NVRTX_SM75_MEM_OFFSET_BITS, 0U);
	nvrtx_sm75_set(word, NVRTX_SM75_ATOM_OP_LOW, 4U, operation);
	nvrtx_sm75_set(word, NVRTX_SM75_ATOM_TYPE_LOW, 3U, (is_signed != 0) ? NVRTX_SM75_ATOM_I32 : NVRTX_SM75_ATOM_U32);
}

/*
 * A compare and exchange returning the old value, global or shared.
 *
 * Procedure referenced: Mesa 25.3.6 src/nouveau/compiler/nak/sm70_encode.rs
 * (OpAtom::encode).
 */
void
nvrtx_sm75_atomic_cas(
	struct nvrtx_sm75_word *word,
	int shared,
	uint32_t dst,
	uint32_t address,
	uint32_t compare,
	uint32_t data)
{
	/* The opcode and, for global memory, the access. */
	if (shared != 0) {
		nvrtx_sm75_set(word, NVRTX_SM75_OPCODE_LOW, NVRTX_SM75_OPCODE_BITS, NVRTX_SM75_OP_ATOMS_CAS);
	} else {
		nvrtx_sm75_set(word, NVRTX_SM75_OPCODE_LOW, NVRTX_SM75_OPCODE_BITS, NVRTX_SM75_OP_ATOMG_CAS);
		nvrtx_sm75_set(word, NVRTX_SM75_PDST0_LOW, NVRTX_SM75_PRED_BITS, NVRTX_SM75_PT);
		nvrtx_sm75_memory(word);
	}

	/* The registers and the type (unsigned 32 bits). */
	nvrtx_sm75_set(word, NVRTX_SM75_DST_LOW, NVRTX_SM75_REG_BITS, dst);
	nvrtx_sm75_set(word, NVRTX_SM75_SRC0_LOW, NVRTX_SM75_REG_BITS, address);
	nvrtx_sm75_set(word, NVRTX_SM75_STORE_DATA_LOW, NVRTX_SM75_REG_BITS, compare);
	nvrtx_sm75_set(word, NVRTX_SM75_CAS_DATA_LOW, NVRTX_SM75_REG_BITS, data);
	nvrtx_sm75_set(word, NVRTX_SM75_MEM_OFFSET_LOW, NVRTX_SM75_MEM_OFFSET_BITS, 0U);
	nvrtx_sm75_set(word, NVRTX_SM75_ATOM_TYPE_LOW, 3U, NVRTX_SM75_ATOM_U32);
}

/*
 * MEMBAR of a scope, not for MMIO and not sequentially consistent.
 *
 * Procedure referenced: Mesa 25.3.6 src/nouveau/compiler/nak/sm70_encode.rs
 * (OpMemBar::encode).
 */
void
nvrtx_sm75_membar(
	struct nvrtx_sm75_word *word,
	uint32_t scope)
{
	/* The opcode and the scope. */
	nvrtx_sm75_set(word, NVRTX_SM75_OPCODE_LOW, NVRTX_SM75_OPCODE_BITS, NVRTX_SM75_OP_MEMBAR);
	nvrtx_sm75_set(word, NVRTX_SM75_MEMBAR_SCOPE_LOW, 3U, scope);
}

/*
 * BAR.SYNC 0: every thread of the workgroup waits here.
 *
 * Procedure referenced: Mesa 25.3.6 src/nouveau/compiler/nak/sm70_encode.rs
 * (OpBar::encode).
 */
void
nvrtx_sm75_bar(
	struct nvrtx_sm75_word *word)
{
	/* The opcode alone names barrier 0 and the whole workgroup. */
	nvrtx_sm75_set(word, NVRTX_SM75_OPCODE_LOW, NVRTX_SM75_OPCODE_BITS, NVRTX_SM75_OP_BAR);
}

/*
 * BSSY: a convergence barrier for the threads that reach the BSYNC so many
 * instructions after the next one.
 *
 * Procedure referenced: Mesa 25.3.6 src/nouveau/compiler/nak/sm70_encode.rs
 * (OpBSSy::encode, get_rel_offset and set_rel_offset).
 */
void
nvrtx_sm75_bssy(
	struct nvrtx_sm75_word *word,
	uint32_t barrier,
	int32_t instructions)
{
	int64_t words;

	/* The offset counts 32-bit words, four to an instruction. */
	words = (int64_t)instructions * (int64_t)NVRTX_SM75_INSTRUCTION_WORDS;

	/* The opcode, the barrier, the offset, the predicate source PT. */
	nvrtx_sm75_set(word, NVRTX_SM75_OPCODE_LOW, NVRTX_SM75_OPCODE_BITS, NVRTX_SM75_OP_BSSY);
	nvrtx_sm75_set(word, NVRTX_SM75_CONVERGE_BAR_LOW, 4U, barrier);
	nvrtx_sm75_set(word, NVRTX_SM75_BSSY_OFFSET_LOW, NVRTX_SM75_BSSY_OFFSET_BITS, (uint64_t)words);
	nvrtx_sm75_set(word, NVRTX_SM75_PSRC_A_LOW, NVRTX_SM75_PRED_BITS, NVRTX_SM75_PT);
}

/*
 * BSYNC: waits until every thread the barrier collects has come.
 *
 * Procedure referenced: Mesa 25.3.6 src/nouveau/compiler/nak/sm70_encode.rs
 * (OpBSync::encode).
 */
void
nvrtx_sm75_bsync(
	struct nvrtx_sm75_word *word,
	uint32_t barrier)
{
	/* The opcode, the barrier, the predicate source PT. */
	nvrtx_sm75_set(word, NVRTX_SM75_OPCODE_LOW, NVRTX_SM75_OPCODE_BITS, NVRTX_SM75_OP_BSYNC);
	nvrtx_sm75_set(word, NVRTX_SM75_CONVERGE_BAR_LOW, 4U, barrier);
	nvrtx_sm75_set(word, NVRTX_SM75_PSRC_A_LOW, NVRTX_SM75_PRED_BITS, NVRTX_SM75_PT);
}

/*
 * Makes a BRA conditional on a predicate (the branch's own condition source).
 *
 * Procedure referenced: Mesa 25.3.6 src/nouveau/compiler/nak/sm70_encode.rs
 * (OpBra::encode).
 */
void
nvrtx_sm75_bra_if(
	struct nvrtx_sm75_word *word,
	uint32_t predicate,
	int negate)
{
	/* The condition and its negation. */
	nvrtx_sm75_set(word, NVRTX_SM75_PSRC_A_LOW, NVRTX_SM75_PRED_BITS, predicate);
	nvrtx_sm75_set(word, NVRTX_SM75_PSRC_A_NOT, 1U, (negate != 0) ? 1U : 0U);
}

/*
 * ALD dst = one word of the vertex's input attribute at an address (the
 * vertex and the offset RZ).
 *
 * Procedure referenced: Mesa 25.3.6 src/nouveau/compiler/nak/sm70_encode.rs
 * (OpALd::encode).
 */
void
nvrtx_sm75_ald(
	struct nvrtx_sm75_word *word,
	uint32_t dst,
	uint32_t address)
{
	/* The opcode, the destination, the registers RZ, the address, one component. */
	nvrtx_sm75_set(word, NVRTX_SM75_OPCODE_LOW, NVRTX_SM75_OPCODE_BITS, NVRTX_SM75_OP_ALD);
	nvrtx_sm75_set(word, NVRTX_SM75_DST_LOW, NVRTX_SM75_REG_BITS, dst);
	nvrtx_sm75_set(word, NVRTX_SM75_SRC0_LOW, NVRTX_SM75_REG_BITS, NVRTX_SM75_RZ);
	nvrtx_sm75_set(word, NVRTX_SM75_SRC1_LOW, NVRTX_SM75_REG_BITS, NVRTX_SM75_RZ);
	nvrtx_sm75_set(word, NVRTX_SM75_ATTR_ADDRESS_LOW, NVRTX_SM75_ATTR_ADDRESS_BITS, address);
	nvrtx_sm75_set(word, NVRTX_SM75_ATTR_COMPONENTS_LOW, 2U, 0U);
}

/*
 * AST: one word of the vertex's output attribute at an address (the vertex
 * and the offset RZ).
 *
 * Procedure referenced: Mesa 25.3.6 src/nouveau/compiler/nak/sm70_encode.rs
 * (OpASt::encode).
 */
void
nvrtx_sm75_ast(
	struct nvrtx_sm75_word *word,
	uint32_t address,
	uint32_t data)
{
	/* The opcode, the data, the registers RZ, the address, one component. */
	nvrtx_sm75_set(word, NVRTX_SM75_OPCODE_LOW, NVRTX_SM75_OPCODE_BITS, NVRTX_SM75_OP_AST);
	nvrtx_sm75_set(word, NVRTX_SM75_STORE_DATA_LOW, NVRTX_SM75_REG_BITS, data);
	nvrtx_sm75_set(word, NVRTX_SM75_AST_VERTEX_LOW, NVRTX_SM75_REG_BITS, NVRTX_SM75_RZ);
	nvrtx_sm75_set(word, NVRTX_SM75_SRC0_LOW, NVRTX_SM75_REG_BITS, NVRTX_SM75_RZ);
	nvrtx_sm75_set(word, NVRTX_SM75_ATTR_ADDRESS_LOW, NVRTX_SM75_ATTR_ADDRESS_BITS, address);
	nvrtx_sm75_set(word, NVRTX_SM75_ATTR_COMPONENTS_LOW, 2U, 0U);
}

/*
 * IPA dst = the fragment's value of the attribute at an address, at the
 * pixel's centre, with no offset.
 *
 * Procedure referenced: Mesa 25.3.6 src/nouveau/compiler/nak/sm70_encode.rs
 * (OpIpa::encode).
 */
void
nvrtx_sm75_ipa(
	struct nvrtx_sm75_word *word,
	uint32_t dst,
	uint32_t address,
	uint32_t frequency)
{
	/* The opcode, the destination, the address in words, the location and the frequency, the offset RZ, no predicate. */
	nvrtx_sm75_set(word, NVRTX_SM75_OPCODE_LOW, NVRTX_SM75_OPCODE_BITS, NVRTX_SM75_OP_IPA);
	nvrtx_sm75_set(word, NVRTX_SM75_DST_LOW, NVRTX_SM75_REG_BITS, dst);
	nvrtx_sm75_set(word, NVRTX_SM75_IPA_ADDRESS_LOW, 8U, address >> 2);
	nvrtx_sm75_set(word, NVRTX_SM75_IPA_FREQUENCY_LOW, 2U, frequency);
	nvrtx_sm75_set(word, NVRTX_SM75_SRC1_LOW, NVRTX_SM75_REG_BITS, NVRTX_SM75_RZ);
	nvrtx_sm75_set(word, NVRTX_SM75_PDST0_LOW, NVRTX_SM75_PRED_BITS, NVRTX_SM75_PT);
}

/*
 * TEX.2D with its handle in a constant buffer: four channels into two
 * register pairs, the coordinates from a pair, a level of detail mode and
 * register, no fault predicate, normal eviction.
 *
 * Procedure referenced: Mesa 25.3.6 src/nouveau/compiler/nak/sm70_encode.rs
 * (OpTex::encode and set_tex_cb_ref).
 */
void
nvrtx_sm75_tex(
	struct nvrtx_sm75_word *word,
	uint32_t first,
	uint32_t second,
	uint32_t coordinates,
	uint32_t lod,
	uint32_t lod_mode,
	uint32_t index,
	uint32_t offset)
{
	/* The opcode and the handle's place. */
	nvrtx_sm75_set(word, NVRTX_SM75_OPCODE_LOW, NVRTX_SM75_OPCODE_BITS, NVRTX_SM75_OP_TEX);
	nvrtx_sm75_set(word, NVRTX_SM75_TEX_HANDLE_LOW, NVRTX_SM75_TEX_HANDLE_BITS, offset / 4U);
	nvrtx_sm75_set(word, NVRTX_SM75_TEX_BUFFER_LOW, 5U, index);

	/* The destinations and the sources. */
	nvrtx_sm75_set(word, NVRTX_SM75_DST_LOW, NVRTX_SM75_REG_BITS, first);
	nvrtx_sm75_set(word, NVRTX_SM75_TEX_DST2_LOW, NVRTX_SM75_REG_BITS, second);
	nvrtx_sm75_set(word, NVRTX_SM75_SRC0_LOW, NVRTX_SM75_REG_BITS, coordinates);
	nvrtx_sm75_set(word, NVRTX_SM75_SRC1_LOW, NVRTX_SM75_REG_BITS, lod);

	/* 2D, all four channels, the level of detail mode, no fault predicate, normal eviction. */
	nvrtx_sm75_set(word, NVRTX_SM75_TEX_DIM_LOW, 3U, NVRTX_SM75_TEX_DIM_2D);
	nvrtx_sm75_set(word, NVRTX_SM75_TEX_CHANNELS_LOW, 4U, 0xfU);
	nvrtx_sm75_set(word, NVRTX_SM75_TEX_LOD_LOW, 3U, lod_mode);
	nvrtx_sm75_set(word, NVRTX_SM75_PDST0_LOW, NVRTX_SM75_PRED_BITS, NVRTX_SM75_PT);
	nvrtx_sm75_set(word, NVRTX_SM75_MEM_EVICT_LOW, 3U, NVRTX_SM75_MEM_EVICT_NORMAL);
}

/*
 * Makes a TEX a TLD, which fetches the texel at integer coordinates and an
 * explicit level (or level zero).
 *
 * Procedure referenced: Mesa 25.3.6 src/nouveau/compiler/nak/sm70_encode.rs
 * (OpTld::encode).
 */
void
nvrtx_sm75_tex_fetch(
	struct nvrtx_sm75_word *word)
{
	/* The opcode; every other field is the TEX's. */
	nvrtx_sm75_set(word, NVRTX_SM75_OPCODE_LOW, NVRTX_SM75_OPCODE_BITS, NVRTX_SM75_OP_TLD);
}

/*
 * Makes a TEX or a TLD read a constant texel offset from the register after
 * its level of detail.
 *
 * Procedure referenced: Mesa 25.3.6 src/nouveau/compiler/nak/sm70_encode.rs
 * (OpTex::encode and OpTld::encode).
 */
void
nvrtx_sm75_tex_offset(
	struct nvrtx_sm75_word *word)
{
	/* AOFFI. */
	nvrtx_sm75_set(word, NVRTX_SM75_TEX_AOFFI, 1U, 1U);
}

/*
 * KILL: the fragment is discarded (the predicate source PT; the guard chooses
 * the threads).
 *
 * Procedure referenced: Mesa 25.3.6 src/nouveau/compiler/nak/sm70_encode.rs
 * (OpKill::encode).
 */
void
nvrtx_sm75_kill(
	struct nvrtx_sm75_word *word)
{
	/* The opcode and the predicate source. */
	nvrtx_sm75_set(word, NVRTX_SM75_OPCODE_LOW, NVRTX_SM75_OPCODE_BITS, NVRTX_SM75_OP_KILL);
	nvrtx_sm75_set(word, NVRTX_SM75_PSRC_A_LOW, NVRTX_SM75_PRED_BITS, NVRTX_SM75_PT);
}

/*
 * Places a source in the third source's register slot, with that slot's abs
 * and neg; none places nothing.
 *
 * Procedure referenced: Mesa 25.3.6 src/nouveau/compiler/nak/sm70_encode.rs
 * (encode_alu_src2).
 */
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

/*
 * Places a register in the second source's slot, with its abs and neg.
 *
 * Procedure referenced: Mesa 25.3.6 src/nouveau/compiler/nak/sm70_encode.rs
 * (encode_alu_reg).
 */
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

/*
 * Places a constant buffer word in the wide slot: its offset and its buffer,
 * bound, with its abs and neg.
 *
 * Procedure referenced: Mesa 25.3.6 src/nouveau/compiler/nak/sm70_encode.rs
 * (encode_alu_cb and set_src_cb).
 */
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

/*
 * Sets a global access: a 64-bit address, 32 bits, strong at the GPU's scope,
 * normal eviction.
 *
 * Procedure referenced: Mesa 25.3.6 src/nouveau/compiler/nak/sm70_encode.rs
 * (set_mem_access).
 */
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

/*
 * The dependence marking of the nvrtx shader compiler (see sm75.h).
 *
 * The reordering (nvrtx_sm75_reorder()) works inside each block of
 * instructions between the fixed ones (branches, convergence, barriers,
 * discards, EXIT, NOPs) and branch targets: an instruction may move past
 * another only when neither reads or writes what the other writes, and
 * memory accesses keep their order when either one stores.  Among the
 * instructions whose predecessors have issued, the one with the longest
 * latency-weighted path to the block's end goes first (a list scheduler),
 * the latencies of variable-latency instructions estimated.
 *
 * The marking walks the program once in order.  For every register and
 * predicate it keeps who wrote it last and when (a cycle count from the
 * start of the block), or which scoreboard will say the write is done, and
 * which scoreboards will say the reads of it are done.  An instruction
 * issues at the first cycle every one of its reads and writes allows, and
 * the instruction before it gets that many cycles as its delay.  The
 * tables' few values that depend on predication or on finer classes are
 * taken at their largest, so the delays are never short.
 *
 * Ampere and Ada (SM86, SM89) differ from Turing only in the read after
 * write of a fixed-latency result: a variable-latency reader waits 4
 * cycles (5 for the address unit's) instead of 6, an IMAD.WIDE's addend
 * waits 4 for another IMAD.WIDE instead of 2, and a guard, a branch or a
 * variable-latency reader of a comparison waits 13 instead of 12.  Their
 * write after write, write after read and worst latencies are within the
 * values below, which both use; BAR and MEMBAR execute in 6 cycles, which
 * NVRTX_SM75_DELAY_SYNC_MIN already waits.
 */

/*
 * Values referenced: Mesa 25.3.6 src/nouveau/compiler/nak/calc_instr_deps.rs,
 * BarAlloc (lines 186-196: six scoreboards).
 */
/* The scoreboards an instruction may set, and the mask of all of them. */
#define NVRTX_SM75_SCOREBOARDS		6U
#define NVRTX_SM75_SCOREBOARD_ALL	0x3fU

/* zedBSD's own. */
/* The general registers and predicates the pass tracks. */
#define NVRTX_SM75_TRACKED_GPRS		256U
#define NVRTX_SM75_TRACKED_PREDICATES	8U

/*
 * Values referenced: Mesa 25.3.6 src/nouveau/compiler/nak/calc_instr_deps.rs,
 * calc_delays (lines 692-698: a scoreboard is waited for 2 cycles after it is
 * set at the earliest).
 */
/* The cycles after a scoreboard is set before an instruction may wait for it. */
#define NVRTX_SM75_SCOREBOARD_SETTLE	2U

/*
 * Values referenced: Mesa 25.3.6
 * src/nouveau/compiler/nak/sm75_instr_latencies.rs, read_after_write and
 * pred_read_after_write with no reader (lines 205-412, 695-776),
 * src/nouveau/compiler/nak/sm80_instr_latencies.rs, the same (lines 259-576,
 * 889-958), and src/nouveau/compiler/nak/sm70.rs, worst_latency and
 * instr_latency (lines 24-67, 263-273).
 */
/* The worst latency of a fixed-latency general register and predicate result, which a block's end waits out. */
#define NVRTX_SM75_WORST_GPR		6U
#define NVRTX_SM75_WORST_PREDICATE	13U

/*
 * Values referenced: Mesa 25.3.6
 * src/nouveau/compiler/nak/sm75_instr_latencies.rs, write_after_write and
 * write_after_read (lines 414-693), and
 * src/nouveau/compiler/nak/sm80_instr_latencies.rs, the same (lines 578-842),
 * at their largest.
 */
/* A write after a write, and a variable-latency write after a fixed-latency write or read. */
#define NVRTX_SM75_WAW_FIXED		2U
#define NVRTX_SM75_WAW_DECOUPLED	6U
#define NVRTX_SM75_WAR_DECOUPLED	2U

/*
 * Values referenced: Mesa 25.3.6 src/nouveau/compiler/nak/sm70.rs,
 * exec_latency (lines 169-185), and
 * src/nouveau/compiler/nak/calc_instr_deps.rs, calc_delays (lines 811-818: a
 * NOP of 2 cycles after an instruction with an execution latency).
 *
 * Value referenced: the SM75 code the CUDA compiler emits (EXIT's delay of
 * 5).
 */
/* The fewest cycles after a barrier or a memory barrier (its execution takes 5 on Turing, 6 from SM80 on), and after EXIT. */
#define NVRTX_SM75_DELAY_SYNC_MIN	6U
#define NVRTX_SM75_DELAY_EXIT_MIN	5U
#define NVRTX_SM75_DELAY_AFTER_SYNC	2U

/* zedBSD's own estimates. */
/* The latencies the reordering assumes for the variable-latency instructions (only priorities, never correctness). */
#define NVRTX_SM75_GUESS_TEXTURE	100U
#define NVRTX_SM75_GUESS_GLOBAL		300U
#define NVRTX_SM75_GUESS_SHARED		30U
#define NVRTX_SM75_GUESS_CONSTANT	25U
#define NVRTX_SM75_GUESS_UNIT		15U
#define NVRTX_SM75_GUESS_SPECIAL	20U

/* zedBSD's own. */
/* A pair the reordering must keep in order, and a block not yet ended. */
#define NVRTX_SM75_NO_DEPENDENCE	0xffffffffU

/* No scoreboard, no predicate, and a register never written. */
#define NVRTX_SM75_NO_BAR		0xffU
#define NVRTX_SM75_NO_PREDICATE		NVRTX_SM75_PT

/* zedBSD's own. */
/*
 * What the pass knows of one general register or predicate: whether a
 * fixed-latency write is in flight, its class and its cycle; the
 * scoreboard of a variable-latency write in flight; the scoreboards of the
 * variable-latency reads in flight; and the last fixed-latency read.
 */
struct nvrtx_sm75_track {
	int written;
	uint32_t write_class;
	uint32_t write_cycle;
	uint32_t write_bar;
	uint32_t read_bars;
	uint32_t read_cycle;
};

/*
 * The state of one pass: the shader model, the registers and predicates,
 * each scoreboard's use (busy) and the cycle it was set, and the cycle of
 * the instruction before.  Allocated by nvrtx_sm75_schedule() for one
 * call.
 */
struct nvrtx_sm75_scheduler {
	uint32_t sm;
	struct nvrtx_sm75_track gpr[NVRTX_SM75_TRACKED_GPRS];
	struct nvrtx_sm75_track predicate[NVRTX_SM75_TRACKED_PREDICATES];
	uint32_t bar_busy;
	uint32_t bar_set_cycle[NVRTX_SM75_SCOREBOARDS];
	uint32_t cycle;
};

/*
 * One instruction of a block being reordered: its operands, its opcode, the
 * latency-weighted path to the block's end, the predecessors not yet
 * issued, the earliest cycle they allow, and whether it has issued.  An
 * array of them lives for one call of nvrtx_sm75_reorder().
 */
struct nvrtx_sm75_node {
	struct nvrtx_sm75_operands operands;
	uint32_t opcode;
	uint32_t words[NVRTX_SM75_INSTRUCTION_WORDS];
	uint32_t priority;
	uint32_t waiting;
	uint32_t earliest;
	int issued;
};

static int nvrtx_sm75_reorder_block(struct nvrtx_sm75_node *nodes, uint32_t count, uint32_t sm, uint32_t *code);
static uint32_t nvrtx_sm75_dependence(const struct nvrtx_sm75_node *before, const struct nvrtx_sm75_node *after, uint32_t sm);
static int nvrtx_sm75_memory_kind(uint32_t opcode);
static uint32_t nvrtx_sm75_guess(const struct nvrtx_sm75_node *node);
static int nvrtx_sm75_fixed(const struct nvrtx_sm75_operands *operands);
static int nvrtx_sm75_operands_alu(const struct nvrtx_sm75_word *word, uint32_t opcode, struct nvrtx_sm75_operands *operands);
static int nvrtx_sm75_operands_other(const struct nvrtx_sm75_word *word, uint32_t opcode, struct nvrtx_sm75_operands *operands);
static void nvrtx_sm75_add_read(struct nvrtx_sm75_operands *operands, uint32_t reg, uint32_t read_class);
static void nvrtx_sm75_add_write(struct nvrtx_sm75_operands *operands, uint32_t reg);
static void nvrtx_sm75_mark_targets(const uint32_t *code, uint32_t count, uint8_t *target);
static void nvrtx_sm75_flush(struct nvrtx_sm75_scheduler *scheduler, uint32_t *earliest, uint32_t *wait);
static void nvrtx_sm75_clear(struct nvrtx_sm75_scheduler *scheduler);
static void nvrtx_sm75_constrain(struct nvrtx_sm75_scheduler *scheduler, const struct nvrtx_sm75_operands *operands, uint32_t *earliest, uint32_t *wait);
static void nvrtx_sm75_take_bars(struct nvrtx_sm75_scheduler *scheduler, const struct nvrtx_sm75_operands *operands, uint32_t *wait, uint32_t *write_bar, uint32_t *read_bar);
static void nvrtx_sm75_record(struct nvrtx_sm75_scheduler *scheduler, const struct nvrtx_sm75_operands *operands, uint32_t issue, uint32_t write_bar, uint32_t read_bar);
static void nvrtx_sm75_release(struct nvrtx_sm75_scheduler *scheduler, uint32_t wait);
static uint32_t nvrtx_sm75_raw(uint32_t sm, uint32_t writer, uint32_t reader);
static uint32_t nvrtx_sm75_raw_turing(uint32_t writer, uint32_t reader);
static uint32_t nvrtx_sm75_raw_ampere(uint32_t writer, uint32_t reader);
static uint32_t nvrtx_sm75_predicate_raw(uint32_t sm, uint32_t reader);
static uint32_t nvrtx_sm75_later(uint32_t a, uint32_t b);

/*
 * Reads an instruction's operands from its four words.
 *
 * Returns 0 with the operands, or 1 for an opcode the lowering does not
 * emit.  The guard predicate is read by every guarded instruction.
 */
int
nvrtx_sm75_operands(
	const uint32_t *words,
	struct nvrtx_sm75_operands *operands)
{
	struct nvrtx_sm75_word word;
	uint32_t opcode;
	int unknown;

	word.w[0] = words[0];
	word.w[1] = words[1];
	word.w[2] = words[2];
	word.w[3] = words[3];

	/* Nothing read or written yet, one cycle to the next instruction. */
	kern_memset(operands, 0, sizeof(*operands));
	operands->predicate_read = NVRTX_SM75_NO_PREDICATE;
	operands->predicate_write = NVRTX_SM75_NO_PREDICATE;
	operands->min_delay = 1U;

	/* The guard, unless it is PT. */
	operands->guard = (uint32_t)nvrtx_sm75_get(&word, NVRTX_SM75_GUARD_LOW, NVRTX_SM75_PRED_BITS);

	/* The opcodes of their own encodings, then the ALU ones by their 9 bits. */
	opcode = (uint32_t)nvrtx_sm75_get(&word, NVRTX_SM75_OPCODE_LOW, NVRTX_SM75_OPCODE_BITS);
	unknown = nvrtx_sm75_operands_other(&word, opcode, operands);
	if (unknown == 0)
		return 0;

	opcode = (uint32_t)nvrtx_sm75_get(&word, NVRTX_SM75_OPCODE_LOW, NVRTX_SM75_ALU_OPCODE_BITS);
	unknown = nvrtx_sm75_operands_alu(&word, opcode, operands);
	if (unknown != 0)
		return 1;

	/* Succeeded: an ALU instruction. */
	return 0;
}

/*
 * Reorders the instructions inside each block of a program.
 *
 * A block is a run of instructions that are not fixed (see
 * nvrtx_sm75_fixed()), cut at every branch target.  Returns 0, or 1 with
 * the instruction and the reason.
 */
int
nvrtx_sm75_reorder(
	uint32_t *code,
	uint32_t count,
	uint32_t sm,
	uint32_t *instruction,
	const char **reason)
{
	struct nvrtx_sm75_node *nodes;
	uint8_t *cut;
	uint32_t index;
	uint32_t start;
	uint32_t end;
	int unknown;
	int fixed;
	int error;

	/* The nodes, one per instruction. */
	nodes = kern_calloc(count + 1U, sizeof(*nodes));
	if (nodes == NULL) {
		*instruction = 0U;
		*reason = "no memory for the reordering";
		return 1;
	}

	/* Where blocks are cut: before a branch target; a fixed instruction is a cut of its own. */
	cut = kern_calloc(count + 1U, sizeof(*cut));
	if (cut == NULL) {
		kern_free(nodes);
		*instruction = 0U;
		*reason = "no memory for the branch targets";
		return 1;
	}
	nvrtx_sm75_mark_targets(code, count, cut);

	/* Every instruction's operands and words. */
	for (index = 0U; index < count; index++) {
		unknown = nvrtx_sm75_operands(code + (size_t)index * NVRTX_SM75_INSTRUCTION_WORDS, &nodes[index].operands);
		if (unknown != 0) {
			kern_free(cut);
			kern_free(nodes);
			*instruction = index;
			*reason = "the reordering does not know an instruction";
			return 1;
		}
		kern_memcpy(nodes[index].words, code + (size_t)index * NVRTX_SM75_INSTRUCTION_WORDS, sizeof(nodes[index].words));
		nodes[index].opcode = nodes[index].words[0] & 0xfffU;
	}

	/* Each block: from a cut or after a fixed instruction, up to the next of either. */
	error = 0;
	start = 0U;
	while (start < count && error == 0) {
		/* A fixed instruction stays. */
		fixed = nvrtx_sm75_fixed(&nodes[start].operands);
		if (fixed != 0) {
			start++;
			continue;
		}

		/* The block's end: the end of the code, a branch target or a fixed instruction. */
		end = start + 1U;
		while (end < count) {
			if (cut[end] != 0U)
				break;
			fixed = nvrtx_sm75_fixed(&nodes[end].operands);
			if (fixed != 0)
				break;
			end++;
		}

		/* The block's instructions in their new order. */
		if (end - start > 1U) {
			error = nvrtx_sm75_reorder_block(nodes + start, end - start, sm, code + (size_t)start * NVRTX_SM75_INSTRUCTION_WORDS);
			if (error != 0) {
				*instruction = start;
				*reason = "no memory for the reordering";
			}
		}
		start = end;
	}

	kern_free(cut);
	kern_free(nodes);

	/* Reports a failure. */
	if (error != 0)
		return 1;

	/* Succeeded: every block is reordered. */
	return 0;
}

/*
 * Sets the scheduling control of every instruction of a program.
 *
 * Returns 0, or 1 with the instruction and the reason when an instruction
 * is not known or the wait before one would not fit the delay field.
 *
 * Procedure referenced: Mesa 25.3.6
 * src/nouveau/compiler/nak/calc_instr_deps.rs (assign_barriers and
 * calc_delays).
 */
int
nvrtx_sm75_schedule(
	uint32_t *code,
	uint32_t count,
	uint32_t sm,
	uint32_t *instruction,
	const char **reason)
{
	struct nvrtx_sm75_scheduler *scheduler;
	struct nvrtx_sm75_operands operands;
	struct nvrtx_sm75_word word;
	uint8_t *target;
	uint32_t *slot;
	uint32_t *previous;
	uint32_t index;
	uint32_t earliest;
	uint32_t wait;
	uint32_t write_bar;
	uint32_t read_bar;
	uint32_t bar;
	uint32_t previous_min;
	uint32_t delay;
	int previous_sync;
	int unknown;

	/* The pass's state, which is too large for a kernel stack. */
	scheduler = kern_calloc(1U, sizeof(*scheduler));
	if (scheduler == NULL) {
		*instruction = 0U;
		*reason = "no memory for the dependence marking";
		return 1;
	}

	/* The branch targets, which start blocks. */
	target = kern_calloc(count + 1U, sizeof(*target));
	if (target == NULL) {
		kern_free(scheduler);
		*instruction = 0U;
		*reason = "no memory for the branch targets";
		return 1;
	}
	nvrtx_sm75_mark_targets(code, count, target);

	/* The shader model's latencies, and nothing in flight at the program's start. */
	scheduler->sm = sm;
	nvrtx_sm75_clear(scheduler);
	scheduler->cycle = 0U;
	previous = NULL;
	previous_min = 0U;
	previous_sync = 0;

	/* Each instruction in order. */
	for (index = 0U; index < count; index++) {
		slot = code + (size_t)index * NVRTX_SM75_INSTRUCTION_WORDS;

		/* What it reads and writes. */
		unknown = nvrtx_sm75_operands(slot, &operands);
		if (unknown != 0) {
			kern_free(target);
			kern_free(scheduler);
			*instruction = index;
			*reason = "the dependence marking does not know an instruction";
			return 1;
		}

		/* The NOP after a barrier waits 2 cycles. */
		if (previous_sync != 0 && operands.class_of == NVRTX_SM75_CLASS_DISPATCH)
			operands.min_delay = NVRTX_SM75_DELAY_AFTER_SYNC;

		/* The earliest it may issue after the instruction before. */
		earliest = scheduler->cycle + previous_min;
		wait = 0U;

		/* A branch target or a block's last instruction waits for everything. */
		if (target[index] != 0U || operands.boundary != 0) {
			nvrtx_sm75_flush(scheduler, &earliest, &wait);
			nvrtx_sm75_clear(scheduler);
		}

		/* Its own reads and writes, then the scoreboards it sets. */
		nvrtx_sm75_constrain(scheduler, &operands, &earliest, &wait);
		nvrtx_sm75_take_bars(scheduler, &operands, &wait, &write_bar, &read_bar);

		/* A scoreboard is waited for only once it has settled. */
		for (bar = 0U; bar < NVRTX_SM75_SCOREBOARDS; bar++) {
			if ((wait & (1U << bar)) == 0U)
				continue;
			earliest = nvrtx_sm75_later(earliest, scheduler->bar_set_cycle[bar] + NVRTX_SM75_SCOREBOARD_SETTLE);
		}

		/* The instruction before waits that many cycles; the field holds 15 at most. */
		if (previous != NULL) {
			delay = earliest - scheduler->cycle;
			if (delay > NVRTX_SM75_DELAY_MAX) {
				kern_free(target);
				kern_free(scheduler);
				*instruction = index;
				*reason = "a wait longer than the delay field holds";
				return 1;
			}
			kern_memcpy(word.w, previous, sizeof(word.w));
			nvrtx_sm75_set(&word, NVRTX_SM75_DELAY_LOW, NVRTX_SM75_DELAY_BITS, delay);
			kern_memcpy(previous, word.w, sizeof(word.w));
		}

		/* The waits are done when it issues; then its own results and scoreboards are in flight. */
		nvrtx_sm75_release(scheduler, wait);
		nvrtx_sm75_record(scheduler, &operands, earliest, write_bar, read_bar);

		/* Its scoreboards, its waits and its yield; the delay comes with the next one. */
		kern_memcpy(word.w, slot, sizeof(word.w));
		nvrtx_sm75_control(&word, operands.min_delay, operands.boundary, write_bar, read_bar, wait);
		kern_memcpy(slot, word.w, sizeof(word.w));

		/* A block's last instruction leaves nothing in flight behind it. */
		if (operands.boundary != 0)
			nvrtx_sm75_clear(scheduler);

		scheduler->cycle = earliest;
		previous = slot;
		previous_min = operands.min_delay;
		previous_sync = 0;
		if (operands.min_delay == NVRTX_SM75_DELAY_SYNC_MIN)
			previous_sync = 1;
	}

	kern_free(target);
	kern_free(scheduler);

	/* Succeeded: every instruction has its control. */
	return 0;
}

/*
 * Reorders one block by list scheduling and writes it back over its code.
 * Returns 0, or ENOMEM-like 1 when the order cannot be kept.
 */
static int
nvrtx_sm75_reorder_block(
	struct nvrtx_sm75_node *nodes,
	uint32_t count,
	uint32_t sm,
	uint32_t *code)
{
	uint32_t *order;
	uint32_t before;
	uint32_t after;
	uint32_t latency;
	uint32_t path;
	uint32_t issued;
	uint32_t best;
	uint32_t soonest;
	uint32_t cycle;

	/* The new order. */
	order = kern_calloc(count, sizeof(*order));
	if (order == NULL)
		return 1;

	/* Each instruction's longest latency-weighted path to the block's end, from the last back. */
	for (before = count; before > 0U; before--) {
		path = nvrtx_sm75_guess(&nodes[before - 1U]);
		for (after = before; after < count; after++) {
			latency = nvrtx_sm75_dependence(&nodes[before - 1U], &nodes[after], sm);
			if (latency == NVRTX_SM75_NO_DEPENDENCE)
				continue;
			if (latency + nodes[after].priority > path)
				path = latency + nodes[after].priority;
		}
		nodes[before - 1U].priority = path;
	}

	/* Each instruction's predecessors. */
	for (after = 0U; after < count; after++) {
		nodes[after].waiting = 0U;
		nodes[after].earliest = 0U;
		nodes[after].issued = 0;
		for (before = 0U; before < after; before++) {
			latency = nvrtx_sm75_dependence(&nodes[before], &nodes[after], sm);
			if (latency != NVRTX_SM75_NO_DEPENDENCE)
				nodes[after].waiting++;
		}
	}

	/* Issues one instruction at a time: the ready one of the highest priority that can issue now, else the soonest. */
	cycle = 0U;
	for (issued = 0U; issued < count; issued++) {
		best = count;
		soonest = count;
		for (after = 0U; after < count; after++) {
			if (nodes[after].issued != 0 || nodes[after].waiting != 0U)
				continue;

			/* The soonest ready one, the first in the old order among equals. */
			if (soonest == count || nodes[after].earliest < nodes[soonest].earliest)
				soonest = after;

			/* The ready one of the highest priority that can issue at this cycle. */
			if (nodes[after].earliest > cycle)
				continue;
			if (best == count || nodes[after].priority > nodes[best].priority)
				best = after;
		}

		/* Nothing can issue now: the soonest one, later. */
		if (best == count) {
			best = soonest;
			cycle = nodes[best].earliest;
		}

		/* A ready instruction always exists in a block without cycles; none means the dependences are wrong. */
		if (best == count) {
			kern_free(order);
			return 1;
		}

		/* It issues, and its successors may issue after its latency. */
		order[issued] = best;
		nodes[best].issued = 1;
		for (after = best + 1U; after < count; after++) {
			latency = nvrtx_sm75_dependence(&nodes[best], &nodes[after], sm);
			if (latency == NVRTX_SM75_NO_DEPENDENCE)
				continue;
			nodes[after].waiting--;
			if (cycle + latency > nodes[after].earliest)
				nodes[after].earliest = cycle + latency;
		}
		cycle++;
	}

	/* The block's words in the new order. */
	for (issued = 0U; issued < count; issued++)
		kern_memcpy(code + (size_t)issued * NVRTX_SM75_INSTRUCTION_WORDS, nodes[order[issued]].words, sizeof(nodes[0].words));

	kern_free(order);

	/* Succeeded: the block is reordered. */
	return 0;
}

/*
 * Reports the cycles an instruction must issue after an earlier one of its
 * block (an estimate for a variable-latency result), or
 * NVRTX_SM75_NO_DEPENDENCE when the two may swap: a read of a register or
 * predicate the earlier one writes, a write of one it reads or writes, and
 * two memory accesses of which one stores.
 */
static uint32_t
nvrtx_sm75_dependence(
	const struct nvrtx_sm75_node *before,
	const struct nvrtx_sm75_node *after,
	uint32_t sm)
{
	const struct nvrtx_sm75_operands *first;
	const struct nvrtx_sm75_operands *second;
	uint32_t latency;
	uint32_t i;
	uint32_t j;
	int first_memory;
	int second_memory;

	first = &before->operands;
	second = &after->operands;
	latency = NVRTX_SM75_NO_DEPENDENCE;

	/* A register the second reads that the first writes: the first's result latency. */
	for (i = 0U; i < first->write_count; i++) {
		for (j = 0U; j < second->read_count; j++) {
			if (first->writes[i] != second->reads[j])
				continue;
			if (first->class_of == NVRTX_SM75_CLASS_DECOUPLED)
				return nvrtx_sm75_guess(before);
			return nvrtx_sm75_raw(sm, first->class_of, second->read_class[j]);
		}
	}

	/* A predicate the second reads (as its guard or its source) that the first writes. */
	if (first->predicate_write != NVRTX_SM75_NO_PREDICATE) {
		if (first->predicate_write == second->guard)
			return nvrtx_sm75_predicate_raw(sm, NVRTX_SM75_CLASS_DECOUPLED);
		if (first->predicate_write == second->predicate_read)
			return nvrtx_sm75_predicate_raw(sm, second->class_of);
	}

	/* A register the second writes that the first reads or writes. */
	for (j = 0U; j < second->write_count; j++) {
		for (i = 0U; i < first->read_count; i++) {
			if (first->reads[i] == second->writes[j])
				latency = 1U;
		}
		for (i = 0U; i < first->write_count; i++) {
			if (first->writes[i] == second->writes[j])
				latency = 1U;
		}
	}

	/* A predicate the second writes that the first reads or writes. */
	if (second->predicate_write != NVRTX_SM75_NO_PREDICATE) {
		if (second->predicate_write == first->guard ||
		    second->predicate_write == first->predicate_read ||
		    second->predicate_write == first->predicate_write)
			latency = 1U;
	}

	/* Two memory accesses, one of them a store or an atomic. */
	first_memory = nvrtx_sm75_memory_kind(before->opcode);
	second_memory = nvrtx_sm75_memory_kind(after->opcode);
	if (first_memory != 0 &&
	    second_memory != 0 &&
	    (first_memory == 2 || second_memory == 2))
		latency = 1U;

	/* Succeeded: the order the pair needs, or none. */
	return latency;
}

/* Reports 2 for an instruction that stores to memory (or an attribute), 1 for one that loads it, 0 for neither. */
static int
nvrtx_sm75_memory_kind(
	uint32_t opcode)
{
	/* The stores and the atomics. */
	if (opcode == NVRTX_SM75_OP_STG ||
	    opcode == NVRTX_SM75_OP_STS ||
	    opcode == NVRTX_SM75_OP_AST)
		return 2;
	if (opcode == NVRTX_SM75_OP_ATOMG ||
	    opcode == NVRTX_SM75_OP_ATOMG_CAS ||
	    opcode == NVRTX_SM75_OP_ATOMS ||
	    opcode == NVRTX_SM75_OP_ATOMS_CAS)
		return 2;

	/* The loads. */
	if (opcode == NVRTX_SM75_OP_LDG ||
	    opcode == NVRTX_SM75_OP_LDS ||
	    opcode == NVRTX_SM75_OP_ALD)
		return 1;

	/* Succeeded: no memory access. */
	return 0;
}

/* Reports the latency the reordering assumes for an instruction's result. */
static uint32_t
nvrtx_sm75_guess(
	const struct nvrtx_sm75_node *node)
{
	/* A fixed-latency result. */
	if (node->operands.class_of != NVRTX_SM75_CLASS_DECOUPLED)
		return NVRTX_SM75_WORST_GPR;

	/* The variable-latency ones by their opcode. */
	if (node->opcode == NVRTX_SM75_OP_TEX || node->opcode == NVRTX_SM75_OP_TLD)
		return NVRTX_SM75_GUESS_TEXTURE;
	if (node->opcode == NVRTX_SM75_OP_LDG ||
	    node->opcode == NVRTX_SM75_OP_ATOMG ||
	    node->opcode == NVRTX_SM75_OP_ATOMG_CAS)
		return NVRTX_SM75_GUESS_GLOBAL;
	if (node->opcode == NVRTX_SM75_OP_LDS ||
	    node->opcode == NVRTX_SM75_OP_ATOMS ||
	    node->opcode == NVRTX_SM75_OP_ATOMS_CAS)
		return NVRTX_SM75_GUESS_SHARED;
	if (node->opcode == NVRTX_SM75_OP_LDC)
		return NVRTX_SM75_GUESS_CONSTANT;
	if (node->opcode == NVRTX_SM75_OP_S2R)
		return NVRTX_SM75_GUESS_SPECIAL;
	/* Succeeded: the special function unit, the conversions, the attribute loads and the interpolation. */
	return NVRTX_SM75_GUESS_UNIT;
}

/* Reports nonzero for an instruction that keeps its place: one that ends a block, or a NOP. */
static int
nvrtx_sm75_fixed(
	const struct nvrtx_sm75_operands *operands)
{
	/* The ends of blocks. */
	if (operands->boundary != 0)
		return 1;

	/* NOPs (the padding, and the one after a barrier). */
	if (operands->class_of == NVRTX_SM75_CLASS_DISPATCH)
		return 1;

	/* Succeeded: it may move. */
	return 0;
}

/*
 * Values referenced: Mesa 25.3.6
 * src/nouveau/compiler/nak/sm75_instr_latencies.rs, op_category and
 * needs_scoreboards (lines 49-203, 1187-1208), and
 * src/nouveau/compiler/nak/sm80_instr_latencies.rs, the same (lines 85-257,
 * 1433-1453).
 */
/* Reads the operands of an ALU instruction by its 9-bit opcode; nonzero for one not known. */
static int
nvrtx_sm75_operands_alu(
	const struct nvrtx_sm75_word *word,
	uint32_t opcode,
	struct nvrtx_sm75_operands *operands)
{
	uint32_t form;
	uint32_t dst;
	uint32_t src0;
	uint32_t src1;
	uint32_t src2;
	int has_src0;
	int has_src2;
	int writes_dst;

	form = (uint32_t)nvrtx_sm75_get(word, NVRTX_SM75_ALU_FORM_LOW, NVRTX_SM75_ALU_FORM_BITS);
	dst = (uint32_t)nvrtx_sm75_get(word, NVRTX_SM75_DST_LOW, NVRTX_SM75_REG_BITS);
	src0 = (uint32_t)nvrtx_sm75_get(word, NVRTX_SM75_SRC0_LOW, NVRTX_SM75_REG_BITS);
	src1 = (uint32_t)nvrtx_sm75_get(word, NVRTX_SM75_SRC1_LOW, NVRTX_SM75_REG_BITS);
	src2 = (uint32_t)nvrtx_sm75_get(word, NVRTX_SM75_SRC2_LOW, NVRTX_SM75_REG_BITS);

	/* The forms the lowering emits. */
	if (form < NVRTX_SM75_FORM_REG || form > NVRTX_SM75_FORM_CB)
		return 1;

	/* Which sources the operation has, whether it writes a register, its class. */
	has_src0 = 1;
	has_src2 = 1;
	writes_dst = 1;
	operands->class_of = NVRTX_SM75_CLASS_ALU;
	switch (opcode) {
	case NVRTX_SM75_OP_MOV:
	case NVRTX_SM75_OP_MUFU:
	case NVRTX_SM75_OP_I2F:
	case NVRTX_SM75_OP_F2I:
	case NVRTX_SM75_OP_FRND:
		/* One source, the second. */
		has_src0 = 0;
		has_src2 = 0;
		if (opcode != NVRTX_SM75_OP_MOV)
			operands->class_of = NVRTX_SM75_CLASS_DECOUPLED;
		break;
	case NVRTX_SM75_OP_SEL:
		/* Two sources and the condition. */
		has_src2 = 0;
		operands->predicate_read = (uint32_t)nvrtx_sm75_get(word, NVRTX_SM75_PSRC_A_LOW, NVRTX_SM75_PRED_BITS);
		break;
	case NVRTX_SM75_OP_FSETP:
	case NVRTX_SM75_OP_ISETP:
		/* Two sources and a predicate result. */
		has_src2 = 0;
		writes_dst = 0;
		operands->predicate_write = (uint32_t)nvrtx_sm75_get(word, NVRTX_SM75_PDST0_LOW, NVRTX_SM75_PRED_BITS);
		break;
	case NVRTX_SM75_OP_FADD:
		/* A third source only as the moved second one of an immediate or constant form. */
		operands->class_of = NVRTX_SM75_CLASS_FMA;
		if (form == NVRTX_SM75_FORM_REG ||
		    form == NVRTX_SM75_FORM_IMM ||
		    form == NVRTX_SM75_FORM_CB)
			has_src2 = 0;
		break;
	case NVRTX_SM75_OP_FMUL:
	case NVRTX_SM75_OP_FFMA:
		operands->class_of = NVRTX_SM75_CLASS_FMA;
		break;
	case NVRTX_SM75_OP_IMAD:
		operands->class_of = NVRTX_SM75_CLASS_IMAD;
		break;
	case NVRTX_SM75_OP_IMAD_WIDE:
		operands->class_of = NVRTX_SM75_CLASS_IMAD_WIDE;
		break;
	case NVRTX_SM75_OP_FMNMX:
	case NVRTX_SM75_OP_IADD3:
	case NVRTX_SM75_OP_LOP3:
	case NVRTX_SM75_OP_SHF:
		break;
	default:
		return 1;
	}

	/* The first source. */
	if (has_src0 != 0)
		nvrtx_sm75_add_read(operands, src0, operands->class_of);

	/* The second source when it is a register. */
	if (form == NVRTX_SM75_FORM_REG)
		nvrtx_sm75_add_read(operands, src1, operands->class_of);

	/* The register in the third slot; an IMAD.WIDE's addend is a pair it reads late. */
	if (has_src2 != 0 &&
	    opcode == NVRTX_SM75_OP_IMAD_WIDE &&
	    src2 != NVRTX_SM75_RZ) {
		nvrtx_sm75_add_read(operands, src2, NVRTX_SM75_CLASS_DISPATCH);
		nvrtx_sm75_add_read(operands, src2 + 1U, NVRTX_SM75_CLASS_DISPATCH);
	} else if (has_src2 != 0) {
		nvrtx_sm75_add_read(operands, src2, operands->class_of);
	}

	/* The result, a pair for IMAD.WIDE. */
	if (writes_dst != 0)
		nvrtx_sm75_add_write(operands, dst);
	if (writes_dst != 0 &&
	    opcode == NVRTX_SM75_OP_IMAD_WIDE &&
	    dst != NVRTX_SM75_RZ)
		nvrtx_sm75_add_write(operands, dst + 1U);

	/* Succeeded: the operands are read. */
	return 0;
}

/*
 * Values referenced: Mesa 25.3.6
 * src/nouveau/compiler/nak/sm75_instr_latencies.rs, op_category and
 * needs_scoreboards (lines 49-203, 1187-1208), and
 * src/nouveau/compiler/nak/sm80_instr_latencies.rs, the same (lines 85-257,
 * 1433-1453: DecoupledAgu for the address unit's reads).
 */
/* Reads the operands of an instruction of its own 12-bit opcode; nonzero for one not known. */
static int
nvrtx_sm75_operands_other(
	const struct nvrtx_sm75_word *word,
	uint32_t opcode,
	struct nvrtx_sm75_operands *operands)
{
	uint32_t dst;
	uint32_t src0;
	uint32_t src1;
	uint32_t src2;
	uint32_t mode;
	uint32_t extra;
	uint32_t index;

	dst = (uint32_t)nvrtx_sm75_get(word, NVRTX_SM75_DST_LOW, NVRTX_SM75_REG_BITS);
	src0 = (uint32_t)nvrtx_sm75_get(word, NVRTX_SM75_SRC0_LOW, NVRTX_SM75_REG_BITS);
	src1 = (uint32_t)nvrtx_sm75_get(word, NVRTX_SM75_SRC1_LOW, NVRTX_SM75_REG_BITS);
	src2 = (uint32_t)nvrtx_sm75_get(word, NVRTX_SM75_SRC2_LOW, NVRTX_SM75_REG_BITS);
	operands->class_of = NVRTX_SM75_CLASS_DECOUPLED;

	/* The operands by the opcode. */
	switch (opcode) {
	case NVRTX_SM75_OP_NOP:
		operands->class_of = NVRTX_SM75_CLASS_DISPATCH;
		break;
	case NVRTX_SM75_OP_S2R:
	case NVRTX_SM75_OP_ALD:
	case NVRTX_SM75_OP_IPA:
		/* A result; the lowering gives none of them a register source (the vertex, offset and interpolation registers are RZ). */
		nvrtx_sm75_add_write(operands, dst);
		break;
	case NVRTX_SM75_OP_LDC:
		/* A result, a pair for a 64-bit load. */
		nvrtx_sm75_add_write(operands, dst);
		mode = (uint32_t)nvrtx_sm75_get(word, NVRTX_SM75_MEM_TYPE_LOW, 3U);
		if (mode == NVRTX_SM75_MEM_B64)
			nvrtx_sm75_add_write(operands, dst + 1U);
		nvrtx_sm75_add_read(operands, src0, NVRTX_SM75_CLASS_DECOUPLED);
		break;
	case NVRTX_SM75_OP_LDG:
	case NVRTX_SM75_OP_STG:
	case NVRTX_SM75_OP_ATOMG:
	case NVRTX_SM75_OP_ATOMG_CAS:
		/* The 64-bit address pair, the data, the compare-and-exchange's second data, the result. */
		nvrtx_sm75_add_read(operands, src0, NVRTX_SM75_CLASS_AGU);
		nvrtx_sm75_add_read(operands, src0 + 1U, NVRTX_SM75_CLASS_AGU);
		if (opcode != NVRTX_SM75_OP_LDG)
			nvrtx_sm75_add_read(operands, src1, NVRTX_SM75_CLASS_AGU);
		if (opcode == NVRTX_SM75_OP_ATOMG_CAS)
			nvrtx_sm75_add_read(operands, src2, NVRTX_SM75_CLASS_AGU);
		if (opcode != NVRTX_SM75_OP_STG)
			nvrtx_sm75_add_write(operands, dst);
		break;
	case NVRTX_SM75_OP_LDS:
	case NVRTX_SM75_OP_STS:
	case NVRTX_SM75_OP_ATOMS:
	case NVRTX_SM75_OP_ATOMS_CAS:
		/* The offset, the data, the second data, the result. */
		nvrtx_sm75_add_read(operands, src0, NVRTX_SM75_CLASS_AGU);
		if (opcode != NVRTX_SM75_OP_LDS)
			nvrtx_sm75_add_read(operands, src1, NVRTX_SM75_CLASS_AGU);
		if (opcode == NVRTX_SM75_OP_ATOMS_CAS)
			nvrtx_sm75_add_read(operands, src2, NVRTX_SM75_CLASS_AGU);
		if (opcode != NVRTX_SM75_OP_STS)
			nvrtx_sm75_add_write(operands, dst);
		break;
	case NVRTX_SM75_OP_AST:
		/* The data; the vertex and offset registers are RZ. */
		nvrtx_sm75_add_read(operands, src1, NVRTX_SM75_CLASS_AGU);
		nvrtx_sm75_add_read(operands, src0, NVRTX_SM75_CLASS_AGU);
		nvrtx_sm75_add_read(operands, src2, NVRTX_SM75_CLASS_AGU);
		break;
	case NVRTX_SM75_OP_TEX:
	case NVRTX_SM75_OP_TLD:
		/* The coordinates' pair, the level of detail and the offset after it, the two result pairs. */
		nvrtx_sm75_add_read(operands, src0, NVRTX_SM75_CLASS_DECOUPLED);
		nvrtx_sm75_add_read(operands, src0 + 1U, NVRTX_SM75_CLASS_DECOUPLED);
		mode = (uint32_t)nvrtx_sm75_get(word, NVRTX_SM75_TEX_LOD_LOW, 3U);
		extra = 0U;
		if (mode == NVRTX_SM75_TEX_LOD_BIAS || mode == NVRTX_SM75_TEX_LOD_EXPLICIT)
			extra++;
		index = (uint32_t)nvrtx_sm75_get(word, NVRTX_SM75_TEX_AOFFI, 1U);
		if (index != 0U)
			extra++;
		for (index = 0U; index < extra && src1 != NVRTX_SM75_RZ; index++)
			nvrtx_sm75_add_read(operands, src1 + index, NVRTX_SM75_CLASS_DECOUPLED);
		nvrtx_sm75_add_write(operands, dst);
		if (dst != NVRTX_SM75_RZ)
			nvrtx_sm75_add_write(operands, dst + 1U);
		nvrtx_sm75_add_write(operands, src2);
		if (src2 != NVRTX_SM75_RZ)
			nvrtx_sm75_add_write(operands, src2 + 1U);
		break;
	case NVRTX_SM75_OP_BRA:
		/* The condition; a branch ends its block. */
		operands->predicate_read = (uint32_t)nvrtx_sm75_get(word, NVRTX_SM75_PSRC_A_LOW, NVRTX_SM75_PRED_BITS);
		operands->boundary = 1;
		break;
	case NVRTX_SM75_OP_BSSY:
	case NVRTX_SM75_OP_BSYNC:
	case NVRTX_SM75_OP_KILL:
		/* Convergence and discards end their blocks. */
		operands->boundary = 1;
		break;
	case NVRTX_SM75_OP_BAR:
	case NVRTX_SM75_OP_MEMBAR:
		/* A barrier ends its block and takes 5 cycles to execute. */
		operands->boundary = 1;
		operands->min_delay = NVRTX_SM75_DELAY_SYNC_MIN;
		break;
	case NVRTX_SM75_OP_EXIT:
		/* EXIT reads every output and ends the program. */
		operands->boundary = 1;
		operands->min_delay = NVRTX_SM75_DELAY_EXIT_MIN;
		break;
	default:
		return 1;
	}

	/* Succeeded: the operands are read. */
	return 0;
}

/* Adds a register read of a class, unless it is RZ or the list is full. */
static void
nvrtx_sm75_add_read(
	struct nvrtx_sm75_operands *operands,
	uint32_t reg,
	uint32_t read_class)
{
	/* RZ is never waited for. */
	if (reg >= NVRTX_SM75_RZ)
		return;
	if (operands->read_count == NVRTX_SM75_OPERANDS_READS)
		return;

	/* The register and how it is read. */
	operands->reads[operands->read_count] = reg;
	operands->read_class[operands->read_count] = read_class;
	operands->read_count++;
}

/* Adds a register write, unless it is RZ or the list is full. */
static void
nvrtx_sm75_add_write(
	struct nvrtx_sm75_operands *operands,
	uint32_t reg)
{
	/* A write to RZ is dropped. */
	if (reg >= NVRTX_SM75_RZ)
		return;
	if (operands->write_count == NVRTX_SM75_OPERANDS_WRITES)
		return;

	/* The register. */
	operands->writes[operands->write_count] = reg;
	operands->write_count++;
}

/* Marks the instructions a branch or a convergence barrier leads to. */
static void
nvrtx_sm75_mark_targets(
	const uint32_t *code,
	uint32_t count,
	uint8_t *target)
{
	struct nvrtx_sm75_word word;
	uint64_t offset;
	int64_t to;
	uint32_t opcode;
	uint32_t index;

	/* Each branch and convergence barrier: its target counts in words from the next instruction. */
	for (index = 0U; index < count; index++) {
		kern_memcpy(word.w, code + (size_t)index * NVRTX_SM75_INSTRUCTION_WORDS, sizeof(word.w));
		opcode = (uint32_t)nvrtx_sm75_get(&word, NVRTX_SM75_OPCODE_LOW, NVRTX_SM75_OPCODE_BITS);

		/* The offset field and its sign. */
		if (opcode == NVRTX_SM75_OP_BRA) {
			offset = nvrtx_sm75_get(&word, NVRTX_SM75_BRA_OFFSET_LOW, NVRTX_SM75_BRA_OFFSET_BITS);
			if ((offset >> (NVRTX_SM75_BRA_OFFSET_BITS - 1U)) != 0U)
				offset |= ~((1ULL << NVRTX_SM75_BRA_OFFSET_BITS) - 1U);
		} else if (opcode == NVRTX_SM75_OP_BSSY) {
			offset = nvrtx_sm75_get(&word, NVRTX_SM75_BSSY_OFFSET_LOW, NVRTX_SM75_BSSY_OFFSET_BITS);
			if ((offset >> (NVRTX_SM75_BSSY_OFFSET_BITS - 1U)) != 0U)
				offset |= ~((1ULL << NVRTX_SM75_BSSY_OFFSET_BITS) - 1U);
		} else {
			continue;
		}

		/* A target inside the program starts a block. */
		to = (int64_t)index + 1 + (int64_t)offset / (int64_t)NVRTX_SM75_INSTRUCTION_WORDS;
		if (to >= 0 && to < (int64_t)count)
			target[to] = 1U;
	}
}

/*
 * Makes an instruction wait for everything in flight: every busy
 * scoreboard, every fixed-latency result's worst latency.
 *
 * Procedure referenced: Mesa 25.3.6
 * src/nouveau/compiler/nak/calc_instr_deps.rs (assign_barriers and
 * calc_delays).
 */
static void
nvrtx_sm75_flush(
	struct nvrtx_sm75_scheduler *scheduler,
	uint32_t *earliest,
	uint32_t *wait)
{
	uint32_t index;

	/* Every scoreboard in use. */
	*wait |= scheduler->bar_busy;

	/* Every fixed-latency register result. */
	for (index = 0U; index < NVRTX_SM75_TRACKED_GPRS; index++) {
		if (scheduler->gpr[index].written == 0)
			continue;
		if (scheduler->gpr[index].write_bar != NVRTX_SM75_NO_BAR)
			continue;
		*earliest = nvrtx_sm75_later(*earliest, scheduler->gpr[index].write_cycle + NVRTX_SM75_WORST_GPR);
	}

	/* Every predicate result. */
	for (index = 0U; index < NVRTX_SM75_TRACKED_PREDICATES; index++) {
		if (scheduler->predicate[index].written == 0)
			continue;
		*earliest = nvrtx_sm75_later(*earliest, scheduler->predicate[index].write_cycle + NVRTX_SM75_WORST_PREDICATE);
	}
}

/* Forgets everything in flight (after a block's end has waited it out); the scoreboards' set cycles stay. */
static void
nvrtx_sm75_clear(
	struct nvrtx_sm75_scheduler *scheduler)
{
	uint32_t index;

	/* The registers and predicates. */
	for (index = 0U; index < NVRTX_SM75_TRACKED_GPRS; index++) {
		kern_memset(&scheduler->gpr[index], 0, sizeof(scheduler->gpr[index]));
		scheduler->gpr[index].write_bar = NVRTX_SM75_NO_BAR;
	}
	for (index = 0U; index < NVRTX_SM75_TRACKED_PREDICATES; index++) {
		kern_memset(&scheduler->predicate[index], 0, sizeof(scheduler->predicate[index]));
		scheduler->predicate[index].write_bar = NVRTX_SM75_NO_BAR;
	}

	/* No scoreboard is in use. */
	scheduler->bar_busy = 0U;
}

/*
 * Applies an instruction's reads and writes: a result in flight on a
 * scoreboard is waited for, a fixed-latency one is waited out by cycles.
 *
 * Procedure referenced: Mesa 25.3.6
 * src/nouveau/compiler/nak/calc_instr_deps.rs (assign_barriers and
 * calc_delays).
 */
static void
nvrtx_sm75_constrain(
	struct nvrtx_sm75_scheduler *scheduler,
	const struct nvrtx_sm75_operands *operands,
	uint32_t *earliest,
	uint32_t *wait)
{
	const struct nvrtx_sm75_track *track;
	uint32_t index;
	uint32_t reader;

	/* Each register read: after its writer. */
	for (index = 0U; index < operands->read_count; index++) {
		track = &scheduler->gpr[operands->reads[index]];
		if (track->written == 0)
			continue;
		if (track->write_bar != NVRTX_SM75_NO_BAR) {
			*wait |= 1U << track->write_bar;
			continue;
		}
		*earliest = nvrtx_sm75_later(*earliest, track->write_cycle + nvrtx_sm75_raw(scheduler->sm, track->write_class, operands->read_class[index]));
	}

	/* Each register write: after the write and the reads before it. */
	for (index = 0U; index < operands->write_count; index++) {
		track = &scheduler->gpr[operands->writes[index]];
		*wait |= track->read_bars;
		if (track->written != 0 && track->write_bar != NVRTX_SM75_NO_BAR)
			*wait |= 1U << track->write_bar;

		/* A fixed-latency write before it. */
		if (track->written != 0 && track->write_bar == NVRTX_SM75_NO_BAR) {
			if (operands->class_of == NVRTX_SM75_CLASS_DECOUPLED) {
				*earliest = nvrtx_sm75_later(*earliest, track->write_cycle + NVRTX_SM75_WAW_DECOUPLED);
			} else {
				*earliest = nvrtx_sm75_later(*earliest, track->write_cycle + NVRTX_SM75_WAW_FIXED);
			}
		}

		/* A fixed-latency read before a variable-latency write. */
		if (operands->class_of == NVRTX_SM75_CLASS_DECOUPLED)
			*earliest = nvrtx_sm75_later(*earliest, track->read_cycle + NVRTX_SM75_WAR_DECOUPLED);
	}

	/* The guard and the predicate source: after the comparison that wrote them. */
	if (operands->guard != NVRTX_SM75_NO_PREDICATE) {
		track = &scheduler->predicate[operands->guard];
		if (track->written != 0)
			*earliest = nvrtx_sm75_later(*earliest, track->write_cycle + nvrtx_sm75_predicate_raw(scheduler->sm, NVRTX_SM75_CLASS_DECOUPLED));
	}
	if (operands->predicate_read != NVRTX_SM75_NO_PREDICATE) {
		track = &scheduler->predicate[operands->predicate_read];
		reader = operands->class_of;
		if (operands->boundary != 0)
			reader = NVRTX_SM75_CLASS_DECOUPLED;
		if (track->written != 0)
			*earliest = nvrtx_sm75_later(*earliest, track->write_cycle + nvrtx_sm75_predicate_raw(scheduler->sm, reader));
	}

	/* The predicate written: after the write and the reads before it. */
	if (operands->predicate_write != NVRTX_SM75_NO_PREDICATE) {
		track = &scheduler->predicate[operands->predicate_write];
		if (track->written != 0)
			*earliest = nvrtx_sm75_later(*earliest, track->write_cycle + NVRTX_SM75_WAW_FIXED);
		*earliest = nvrtx_sm75_later(*earliest, track->read_cycle + NVRTX_SM75_WAR_DECOUPLED);
	}
}

/*
 * Procedure referenced: Mesa 25.3.6
 * src/nouveau/compiler/nak/calc_instr_deps.rs (BarAlloc, try_find_free_bar
 * and free_some_bar, lines 214-228: the lowest free scoreboard, else the one
 * set longest ago).
 */
/*
 * Chooses the scoreboards a variable-latency instruction sets: one its
 * result releases, when it writes a register, and one the read of its
 * sources releases, when it reads one.  A scoreboard is free when nothing
 * in flight names it and the instruction does not wait for it already
 * ending its use; when none is free, the one set longest ago is waited for
 * by this instruction and reused.
 */
static void
nvrtx_sm75_take_bars(
	struct nvrtx_sm75_scheduler *scheduler,
	const struct nvrtx_sm75_operands *operands,
	uint32_t *wait,
	uint32_t *write_bar,
	uint32_t *read_bar)
{
	uint32_t free_bars;
	uint32_t bar;
	uint32_t oldest;
	uint32_t candidate;
	uint32_t need;
	uint32_t taken;

	/* A fixed-latency instruction sets none. */
	*write_bar = NVRTX_SM75_BAR_NONE;
	*read_bar = NVRTX_SM75_BAR_NONE;
	if (operands->class_of != NVRTX_SM75_CLASS_DECOUPLED)
		return;

	/* How many it needs. */
	need = 0U;
	if (operands->write_count != 0U)
		need++;
	if (operands->read_count != 0U)
		need++;

	/* Each one needed, the lowest free first, else the one set longest ago. */
	taken = 0U;
	while (need > 0U) {
		free_bars = NVRTX_SM75_SCOREBOARD_ALL & ~(scheduler->bar_busy & ~*wait) & ~taken;
		bar = NVRTX_SM75_SCOREBOARDS;
		oldest = 0xffffffffU;
		for (candidate = 0U; candidate < NVRTX_SM75_SCOREBOARDS; candidate++) {
			if ((taken & (1U << candidate)) != 0U)
				continue;

			/* The lowest free one. */
			if ((free_bars & (1U << candidate)) != 0U) {
				bar = candidate;
				break;
			}

			/* Else the one set longest ago so far. */
			if (scheduler->bar_set_cycle[candidate] < oldest) {
				oldest = scheduler->bar_set_cycle[candidate];
				bar = candidate;
			}
		}

		/* A busy one is waited for by this instruction, which ends its use. */
		if ((free_bars & (1U << bar)) == 0U)
			*wait |= 1U << bar;
		taken |= 1U << bar;

		/* The result's first, then the sources'. */
		if (*write_bar == NVRTX_SM75_BAR_NONE && operands->write_count != 0U) {
			*write_bar = bar;
		} else {
			*read_bar = bar;
		}
		need--;
	}
}

/*
 * Records an instruction issued at a cycle: what it reads and writes is in
 * flight, on its scoreboards when it has them.
 */
static void
nvrtx_sm75_record(
	struct nvrtx_sm75_scheduler *scheduler,
	const struct nvrtx_sm75_operands *operands,
	uint32_t issue,
	uint32_t write_bar,
	uint32_t read_bar)
{
	struct nvrtx_sm75_track *track;
	uint32_t index;

	/* The reads: on the read scoreboard, or at this cycle. */
	for (index = 0U; index < operands->read_count; index++) {
		track = &scheduler->gpr[operands->reads[index]];
		if (read_bar != NVRTX_SM75_BAR_NONE) {
			track->read_bars |= 1U << read_bar;
		} else {
			track->read_cycle = issue;
		}
	}

	/* The writes: on the result scoreboard, or fixed-latency of the instruction's class. */
	for (index = 0U; index < operands->write_count; index++) {
		track = &scheduler->gpr[operands->writes[index]];
		track->written = 1;
		track->write_class = operands->class_of;
		track->write_cycle = issue;
		track->write_bar = NVRTX_SM75_NO_BAR;
		if (write_bar != NVRTX_SM75_BAR_NONE)
			track->write_bar = write_bar;
	}

	/* The predicates. */
	if (operands->guard != NVRTX_SM75_NO_PREDICATE)
		scheduler->predicate[operands->guard].read_cycle = issue;
	if (operands->predicate_read != NVRTX_SM75_NO_PREDICATE)
		scheduler->predicate[operands->predicate_read].read_cycle = issue;
	if (operands->predicate_write != NVRTX_SM75_NO_PREDICATE) {
		scheduler->predicate[operands->predicate_write].written = 1;
		scheduler->predicate[operands->predicate_write].write_cycle = issue;
	}

	/* The scoreboards it set are in use from this cycle. */
	if (write_bar != NVRTX_SM75_BAR_NONE) {
		scheduler->bar_busy |= 1U << write_bar;
		scheduler->bar_set_cycle[write_bar] = issue;
	}
	if (read_bar != NVRTX_SM75_BAR_NONE) {
		scheduler->bar_busy |= 1U << read_bar;
		scheduler->bar_set_cycle[read_bar] = issue;
	}
}

/* Ends the use of the scoreboards an instruction waited for: what they guarded is complete. */
static void
nvrtx_sm75_release(
	struct nvrtx_sm75_scheduler *scheduler,
	uint32_t wait)
{
	struct nvrtx_sm75_track *track;
	uint32_t index;

	/* Nothing waited. */
	if (wait == 0U)
		return;

	/* Each register: a result on a waited scoreboard is ready, reads on them are done. */
	for (index = 0U; index < NVRTX_SM75_TRACKED_GPRS; index++) {
		track = &scheduler->gpr[index];
		track->read_bars &= ~wait;
		if (track->write_bar != NVRTX_SM75_NO_BAR && (wait & (1U << track->write_bar)) != 0U) {
			track->written = 0;
			track->write_bar = NVRTX_SM75_NO_BAR;
		}
	}

	/* The scoreboards are free. */
	scheduler->bar_busy &= ~wait;
}

/*
 * Reports the cycles from a fixed-latency register write to a read in a
 * shader model's latency table.  An IMAD.WIDE writes as its upper half,
 * and its addend is read as NVRTX_SM75_CLASS_DISPATCH here.
 */
static uint32_t
nvrtx_sm75_raw(
	uint32_t sm,
	uint32_t writer,
	uint32_t reader)
{
	uint32_t latency;

	/* Ampere and Ada share one table. */
	if (sm >= NVRTX_SM_AMPERE_FIRST) {
		latency = nvrtx_sm75_raw_ampere(writer, reader);

		/* Succeeded: Ampere's and Ada's latency. */
		return latency;
	}

	/* Turing's. */
	latency = nvrtx_sm75_raw_turing(writer, reader);

	/* Succeeded: Turing's latency. */
	return latency;
}

/*
 * Values referenced: Mesa 25.3.6
 * src/nouveau/compiler/nak/sm75_instr_latencies.rs, read_after_write (lines
 * 205-412).
 */
/*
 * Reports Turing's cycles from a fixed-latency register write to a read
 * (the addend of an IMAD.WIDE reads late).
 */
static uint32_t
nvrtx_sm75_raw_turing(
	uint32_t writer,
	uint32_t reader)
{
	/* A variable-latency reader, of the address unit or not, waits 6 for any fixed-latency writer. */
	if (reader == NVRTX_SM75_CLASS_DECOUPLED || reader == NVRTX_SM75_CLASS_AGU)
		return 6U;

	/* An IMAD.WIDE's addend (the lower half reader). */
	if (reader == NVRTX_SM75_CLASS_DISPATCH) {
		if (writer == NVRTX_SM75_CLASS_ALU)
			return 5U;
		if (writer == NVRTX_SM75_CLASS_IMAD_WIDE)
			return 2U;
		return 4U;
	}

	/* An IMAD.WIDE's factors. */
	if (reader == NVRTX_SM75_CLASS_IMAD_WIDE) {
		if (writer == NVRTX_SM75_CLASS_ALU)
			return 5U;
		if (writer == NVRTX_SM75_CLASS_IMAD_WIDE)
			return 6U;
		return 4U;
	}

	/* A float or IMAD reader. */
	if (reader == NVRTX_SM75_CLASS_FMA || reader == NVRTX_SM75_CLASS_IMAD) {
		if (writer == NVRTX_SM75_CLASS_ALU || writer == NVRTX_SM75_CLASS_DISPATCH)
			return 5U;
		return 4U;
	}

	/* An ALU reader. */
	if (writer == NVRTX_SM75_CLASS_ALU || writer == NVRTX_SM75_CLASS_DISPATCH)
		return 4U;

	/* Succeeded: an FMA, IMAD or IMAD.WIDE writer of an ALU reader. */
	return 5U;
}

/*
 * Values referenced: Mesa 25.3.6
 * src/nouveau/compiler/nak/sm80_instr_latencies.rs, read_after_write (lines
 * 259-576).
 */
/*
 * Reports Ampere's and Ada's cycles from a fixed-latency register write to
 * a read (sm80_instr_latencies.rs, read_after_write: the writers are
 * CoupledAlu, CoupledFMA and IMADWideWriteDH, the addend is the
 * IMADWideReadCL reader, which reads as CoupledFMA does).
 */
static uint32_t
nvrtx_sm75_raw_ampere(
	uint32_t writer,
	uint32_t reader)
{
	/* A variable-latency reader of the address unit (DecoupledAgu) waits 5, any other (Decoupled) 4. */
	if (reader == NVRTX_SM75_CLASS_AGU)
		return 5U;
	if (reader == NVRTX_SM75_CLASS_DECOUPLED)
		return 4U;

	/* An IMAD.WIDE's factors (IMADWideReadAB). */
	if (reader == NVRTX_SM75_CLASS_IMAD_WIDE) {
		if (writer == NVRTX_SM75_CLASS_ALU || writer == NVRTX_SM75_CLASS_DISPATCH)
			return 5U;
		if (writer == NVRTX_SM75_CLASS_IMAD_WIDE)
			return 6U;
		return 4U;
	}

	/* A float or IMAD reader, or an IMAD.WIDE's addend. */
	if (reader == NVRTX_SM75_CLASS_FMA ||
	    reader == NVRTX_SM75_CLASS_IMAD ||
	    reader == NVRTX_SM75_CLASS_DISPATCH) {
		if (writer == NVRTX_SM75_CLASS_ALU || writer == NVRTX_SM75_CLASS_DISPATCH)
			return 5U;
		return 4U;
	}

	/* An ALU reader. */
	if (writer == NVRTX_SM75_CLASS_ALU || writer == NVRTX_SM75_CLASS_DISPATCH)
		return 4U;

	/* Succeeded: an FMA, IMAD or IMAD.WIDE writer of an ALU reader. */
	return 5U;
}

/*
 * Values referenced: Mesa 25.3.6
 * src/nouveau/compiler/nak/sm75_instr_latencies.rs, pred_read_after_write
 * (lines 695-776), and src/nouveau/compiler/nak/sm80_instr_latencies.rs,
 * PredLatencySM80::pred_read_after_write (lines 889-958).
 */
/*
 * Reports the cycles from a comparison's predicate (an ALU write) to a
 * read: 4 for an ALU reader, 5 for a float or IMAD one, and for a guard, a
 * branch or a variable-latency reader 12 on Turing (pred_read_after_write)
 * and 13 on Ampere and Ada (PredLatencySM80's Guard and Decoupled).
 */
static uint32_t
nvrtx_sm75_predicate_raw(
	uint32_t sm,
	uint32_t reader)
{
	/* An ALU reader. */
	if (reader == NVRTX_SM75_CLASS_ALU)
		return 4U;

	/* A float or IMAD reader. */
	if (reader == NVRTX_SM75_CLASS_FMA ||
	    reader == NVRTX_SM75_CLASS_IMAD ||
	    reader == NVRTX_SM75_CLASS_IMAD_WIDE)
		return 5U;

	/* A guard, a branch or a variable-latency reader on Ampere and Ada. */
	if (sm >= NVRTX_SM_AMPERE_FIRST)
		return 13U;

	/* Succeeded: a guard, a branch or a variable-latency reader on Turing. */
	return 12U;
}

/* Reports the later of two cycles. */
static uint32_t
nvrtx_sm75_later(
	uint32_t a,
	uint32_t b)
{
	/* The first when it is later. */
	if (a > b)
		return a;

	/* Succeeded: the second. */
	return b;
}

/*
 * The lowering of the nvrtx shader compiler (see sm75.h).
 *
 * One pass over the scalar IR chooses the SM75 instructions of each
 * IR instruction and assigns registers as it goes: a value gets a
 * register when it is defined and gives it back after the last
 * instruction that reads it; a value read inside a loop it was defined
 * before lives to the loop's end.  Addresses take an even pair.  A Boolean of the IR is a register of
 * all ones or zero; a comparison sets P0 and turns it into a Boolean
 * at once, and a selection or a guarded access turns the Boolean back
 * into P0.
 *
 * The dependences are marked when the program is complete (the
 * dependence marking above): each read waits only as long as the shader model's
 * latency table says for its writer, or for the scoreboard of a
 * variable-latency writer.  The lowering says of each instruction it
 * emits whether it is of fixed or variable latency or ends a block,
 * and the marking's reading of the instruction must agree.
 *
 * A global access gets the shader model's memory order field when it
 * is emitted; nothing else the lowering chooses depends on the shader
 * model.
 */

/* A value without a register. */
#define NVRTX_SM75_NO_REG		0xffffU

/* The most values one IR instruction reads (a texture message's parameters). */
#define NVRTX_SM75_MAX_SOURCES		DRV_GPU_IR_TEXTURE_MAX_PARAMS

/* The most SM75 instructions one IR instruction becomes, and the end of the program (EXIT, BRA and the padding). */
#define NVRTX_SM75_EXPANSION		40U
#define NVRTX_SM75_TAIL			10U

/* The instructions in one 128-byte line of code, to which the program is padded. */
#define NVRTX_SM75_LINE_INSTRUCTIONS	8U

/* What kind of instruction the lowering emits (checked against the dependence marking's reading of it). */
#define NVRTX_SM75_FIXED		0
#define NVRTX_SM75_FIXED_PREDICATE	1
#define NVRTX_SM75_VARIABLE_WRITE	2
#define NVRTX_SM75_VARIABLE_STORE	3
#define NVRTX_SM75_VARIABLE_SPECIAL	4
#define NVRTX_SM75_CONTROL		5
#define NVRTX_SM75_SYNC			6

/* The deepest loop nest (one convergence barrier each), and a value not yet defined. */
#define NVRTX_SM75_LOOP_DEPTH		16U
#define NVRTX_SM75_NOT_DEFINED		0xffffffffU

/*
 * 2^32 less 512 as a float (0x4f7ffffe), which scales a reciprocal to a 32-bit fixed-point one.
 *
 * Value referenced: Mesa 25.0.7 src/compiler/nir/nir_lower_idiv.c
 * (emit_udiv).
 */
#define NVRTX_SM75_RECIPROCAL_SCALE	0x4f7ffffeU

/*
 * Value referenced: Mesa 25.3.6 src/nouveau/compiler/nak/builder.rs
 * (SSABuilder::fsin and SSABuilder::fcos: the argument scaled by 1 / (2 pi)
 * before MUFU.SIN and MUFU.COS, NVRTX_SM75_INV_TWO_PI).
 */
/* All ones, the Boolean true; the sign bit; and 1 / (2 pi) as a float, which the sine and cosine units take. */
#define NVRTX_SM75_TRUE			0xffffffffU
#define NVRTX_SM75_SIGN			0x80000000U
#define NVRTX_SM75_INV_TWO_PI		0x3e22f983U

/*
 * Values referenced: Mesa 25.3.6 src/nouveau/compiler/nak/ir.rs
 * (LogicOp3::SRC_MASKS and LogicOp3::new_lut: the sources as 0xf0, 0xcc
 * and 0xaa).
 */
/* The truth tables of LOP3 used: a and b, a or b, a xor b, not a. */
#define NVRTX_SM75_LUT_AND		0xc0U
#define NVRTX_SM75_LUT_OR		0xfcU
#define NVRTX_SM75_LUT_XOR		0x3cU
#define NVRTX_SM75_LUT_NOT		0x0fU

/*
 * The registers a program may use without counting against it: the
 * register allocation spreads values over this many before it needs more,
 * so a register is reused as late as possible and the reordering is not
 * held back by a write after a read or a write.  62 and the hardware's two
 * are 64, which still lets a Turing SM keep its most threads resident
 * (65536 registers for 1024 threads).
 */
#define NVRTX_SM75_GPR_POOL		62U

/*
 * Value referenced: Mesa 25.3.6
 * src/nouveau/headers/nvidia/classes/cla097sph.h, NVA097_SPHV3T2_OMAP_RED(i)
 * to NVA097_SPHV3T2_OMAP_ALPHA(i) (bits 576..607, eight targets), and
 * src/nouveau/compiler/nak/sph.rs (omap_target).
 */
/* The colour targets a fragment program writes. */
#define NVRTX_SM75_COLOR_TARGETS	8U

/* What a uniform of the IR maps to: no slot. */
#define NVRTX_SM75_NO_SLOT		0xffffffffU

/*
 * The state of one compilation.
 *
 * Lives on the stack of nvrtx_sm75_compile_ir(); its arrays are allocated
 * there and freed before it returns.
 */
struct nvrtx_sm75_lowering {
	/* The shader model, the program, the result being built, and where a refusal is described. */
	uint32_t sm;
	const struct drv_gpu_shader_ir *ir;
	struct nvrtx_sm75_binary *binary;
	struct nvrtx_sm75_diagnostic *diagnostic;

	/* Each value's register (NVRTX_SM75_NO_REG while it has none), and the IR instruction that last reads it, plus one (0: none). */
	uint16_t *reg;
	uint32_t *last_use;

	/*
	 * Which registers are taken, one past the highest ever taken, and when
	 * each was given back (the instructions emitted by then, plus one; 0 for
	 * never taken).
	 */
	uint8_t taken[NVRTX_SM75_GPR_USABLE];
	uint32_t highest;
	uint32_t freed_at[NVRTX_SM75_GPR_USABLE];

	/* Each uniform's storage slot or constant buffer, NVRTX_SM75_NO_SLOT for neither. */
	uint32_t *slot;

	/* The code: room in instructions and instructions emitted. */
	uint32_t capacity;
	uint32_t count;

	/* The IR instruction being lowered. */
	uint32_t index;

	/* A fragment program: each colour location's first output register (NVRTX_SM75_NO_REG when not written), and the register of 1 / w. */
	uint32_t color_reg[NVRTX_SM75_COLOR_TARGETS];
	uint32_t inverse_w;

	/* A vertex program: the attribute words its stores reach, in words (the header's store request). */
	uint32_t store_first;
	uint32_t store_last;

	/* The loops being lowered: each one's BSSY and first instruction, innermost last. */
	uint32_t loop_bssy[NVRTX_SM75_LOOP_DEPTH];
	uint32_t loop_start[NVRTX_SM75_LOOP_DEPTH];
	uint32_t loop_depth;
};

/*
 * What one texture operation passes the sampler (nvrtx_sm75_texture_operands()):
 * the values of its coordinates and of its level of detail (or bias;
 * NVRTX_SM75_NOT_DEFINED for none), the level of detail mode, whether it
 * is a fetch (TLD) rather than a sample, and its constant texel offset as
 * the register TEX reads takes it.  Lives on the lowering's stack.
 */
struct nvrtx_sm75_texture_operands {
	uint32_t u;
	uint32_t v;
	uint32_t lod;
	uint32_t mode;
	int fetch;
	int has_offset;
	uint32_t offset;
};

static int nvrtx_sm75_known_model(uint32_t sm);
static int nvrtx_sm75_is_global(const struct nvrtx_sm75_word *word);
static int nvrtx_sm75_refuse(struct nvrtx_sm75_lowering *lowering, int error, const char *reason);
static int nvrtx_sm75_prepare(struct nvrtx_sm75_lowering *lowering);
static int nvrtx_sm75_sources(const struct drv_gpu_shader_ir_inst *instruction, uint32_t *sources);
static int nvrtx_sm75_defines(const struct drv_gpu_shader_ir_inst *instruction);
static int nvrtx_sm75_map_uniforms(struct nvrtx_sm75_lowering *lowering);
static int nvrtx_sm75_lower(struct nvrtx_sm75_lowering *lowering, const struct drv_gpu_shader_ir_inst *instruction, uint32_t dst);
static int nvrtx_sm75_lower_alu(struct nvrtx_sm75_lowering *lowering, const struct drv_gpu_shader_ir_inst *instruction, uint32_t dst);
static int nvrtx_sm75_lower_compare(struct nvrtx_sm75_lowering *lowering, const struct drv_gpu_shader_ir_inst *instruction, uint32_t dst);
static int nvrtx_sm75_lower_unit(struct nvrtx_sm75_lowering *lowering, const struct drv_gpu_shader_ir_inst *instruction, uint32_t dst);
static int nvrtx_sm75_lower_system(struct nvrtx_sm75_lowering *lowering, const struct drv_gpu_shader_ir_inst *instruction, uint32_t dst);
static int nvrtx_sm75_lower_storage(struct nvrtx_sm75_lowering *lowering, const struct drv_gpu_shader_ir_inst *instruction, uint32_t dst);
static int nvrtx_sm75_lower_loop(struct nvrtx_sm75_lowering *lowering, const struct drv_gpu_shader_ir_inst *instruction);
static int nvrtx_sm75_lower_divide(struct nvrtx_sm75_lowering *lowering, const struct drv_gpu_shader_ir_inst *instruction, uint32_t dst);
static int nvrtx_sm75_divide_unsigned(struct nvrtx_sm75_lowering *lowering, uint32_t numerator, uint32_t denominator, uint32_t quotient, uint32_t remainder);
static int nvrtx_sm75_absolute(struct nvrtx_sm75_lowering *lowering, uint32_t value, uint32_t result);
static int nvrtx_sm75_negate_if_p0(struct nvrtx_sm75_lowering *lowering, uint32_t value);
static int nvrtx_sm75_lower_atomic(struct nvrtx_sm75_lowering *lowering, const struct drv_gpu_shader_ir_inst *instruction, uint32_t dst);
static int nvrtx_sm75_lower_shared(struct nvrtx_sm75_lowering *lowering, const struct drv_gpu_shader_ir_inst *instruction, uint32_t dst);
static int nvrtx_sm75_lower_sync(struct nvrtx_sm75_lowering *lowering, const struct drv_gpu_shader_ir_inst *instruction);
static int nvrtx_sm75_predicate(struct nvrtx_sm75_lowering *lowering, uint32_t value);
static int nvrtx_sm75_extend_loops(struct nvrtx_sm75_lowering *lowering);
static int nvrtx_sm75_prepare_graphics(struct nvrtx_sm75_lowering *lowering);
static int nvrtx_sm75_prologue(struct nvrtx_sm75_lowering *lowering);
static int nvrtx_sm75_lower_input(struct nvrtx_sm75_lowering *lowering, const struct drv_gpu_shader_ir_inst *instruction, uint32_t dst);
static int nvrtx_sm75_lower_output(struct nvrtx_sm75_lowering *lowering, const struct drv_gpu_shader_ir_inst *instruction);
static int nvrtx_sm75_lower_sample(struct nvrtx_sm75_lowering *lowering, const struct drv_gpu_shader_ir_inst *instruction);
static int nvrtx_sm75_texture_operands(struct nvrtx_sm75_lowering *lowering, const struct drv_gpu_shader_ir_inst *instruction, struct nvrtx_sm75_texture_operands *operands);
static int nvrtx_sm75_lower_kill(struct nvrtx_sm75_lowering *lowering, const struct drv_gpu_shader_ir_inst *instruction);
static const struct drv_gpu_shader_ir_io *nvrtx_sm75_input(const struct drv_gpu_shader_ir *ir, uint32_t location);
static void nvrtx_sm75_header(struct nvrtx_sm75_lowering *lowering);
static void nvrtx_sm75_sph_set(uint32_t *sph, unsigned low, unsigned bits, uint32_t value);
static void nvrtx_sm75_mark_store(struct nvrtx_sm75_lowering *lowering, uint32_t address);
static int nvrtx_sm75_is_sample(enum drv_gpu_shader_ir_op op);
static int nvrtx_sm75_storage_address(struct nvrtx_sm75_lowering *lowering, uint32_t slot, uint32_t offset, uint32_t *address);
static int nvrtx_sm75_emit(struct nvrtx_sm75_lowering *lowering, struct nvrtx_sm75_word *word, int kind);
static int nvrtx_sm75_finish(struct nvrtx_sm75_lowering *lowering);
static int nvrtx_sm75_take(struct nvrtx_sm75_lowering *lowering, uint32_t *reg);
static int nvrtx_sm75_take_pair(struct nvrtx_sm75_lowering *lowering, uint32_t *reg);
static void nvrtx_sm75_give(struct nvrtx_sm75_lowering *lowering, uint32_t reg);
static void nvrtx_sm75_retire(struct nvrtx_sm75_lowering *lowering, const struct drv_gpu_shader_ir_inst *instruction);
static struct nvrtx_sm75_src nvrtx_sm75_value(const struct nvrtx_sm75_lowering *lowering, uint32_t value);

/*
 * Compiles a SPIR-V compute module.
 *
 * The module is parsed by the shared frontend, optimized, and lowered; a
 * program the lowering refuses after the optimizer rewrote it is lowered
 * again from the parsed one, which keeps the parsed program's outcome.
 * Returns 0 with the program, or the frontend's or the lowering's error
 * with the diagnostic filled.
 */
int
nvrtx_sm75_compile(
	const uint32_t *words,
	size_t word_count,
	struct nvrtx_sm75_binary **binary,
	struct nvrtx_sm75_diagnostic *diagnostic)
{
	int error;

	/* The compute stage. */
	error = nvrtx_sm75_compile_stage(words, word_count, DRV_GPU_STAGE_COMPUTE, binary, diagnostic);
	if (error != 0)
		return error;

	/* Succeeded: the caller owns the program. */
	return 0;
}

/*
 * Compiles one stage of a SPIR-V module (compute, vertex or fragment) for
 * Turing, as nvrtx_sm75_compile() does the compute one.
 */
int
nvrtx_sm75_compile_stage(
	const uint32_t *words,
	size_t word_count,
	enum drv_gpu_shader_stage stage,
	struct nvrtx_sm75_binary **binary,
	struct nvrtx_sm75_diagnostic *diagnostic)
{
	int error;

	/* Turing's code. */
	error = nvrtx_sm75_compile_stage_sm(words, word_count, stage, NVRTX_SM_TURING, binary, diagnostic);
	if (error != 0)
		return error;

	/* Succeeded: the caller owns the program. */
	return 0;
}

/*
 * Compiles a parsed program for Turing.
 *
 * Returns 0 with the program in *binary, or the error of
 * nvrtx_sm75_compile_ir_sm().
 */
int
nvrtx_sm75_compile_ir(
	const struct drv_gpu_shader_ir *ir,
	struct nvrtx_sm75_binary **binary,
	struct nvrtx_sm75_diagnostic *diagnostic)
{
	int error;

	/* Turing's code. */
	error = nvrtx_sm75_compile_ir_sm(ir, NVRTX_SM_TURING, binary, diagnostic);
	if (error != 0)
		return error;

	/* Succeeded: the caller owns the program. */
	return 0;
}

/*
 * Compiles one stage of a SPIR-V module for a shader model.
 *
 * The module is parsed by the shared frontend, optimized, and lowered; a
 * program the lowering refuses after the optimizer rewrote it is lowered
 * again from the parsed one, which keeps the parsed program's outcome.
 * Returns 0 with the program, or the frontend's or the lowering's error
 * with the diagnostic filled.
 */
int
nvrtx_sm75_compile_stage_sm(
	const uint32_t *words,
	size_t word_count,
	enum drv_gpu_shader_stage stage,
	uint32_t sm,
	struct nvrtx_sm75_binary **binary,
	struct nvrtx_sm75_diagnostic *diagnostic)
{
	struct drv_gpu_shader_ir *ir;
	struct drv_gpu_shader_ir optimized;
	struct drv_gpu_shader_optimize_options options;
	struct drv_gpu_compile_diagnostic parse_diagnostic;
	int error;

	/* A refused compile never leaves an old output pointer live. */
	if (binary == NULL)
		return EINVAL;
	*binary = NULL;

	/* Starts with no refusal. */
	if (diagnostic != NULL)
		kern_memset(diagnostic, 0, sizeof(*diagnostic));

	/* A module without words is refused. */
	if (words == NULL)
		return EINVAL;

	/* Parses the compute stage. */
	ir = NULL;
	parse_diagnostic.reason = NULL;
	error = drv_gpu_shader_parse(words, word_count, stage, &ir, &parse_diagnostic);
	if (error != 0) {
		if (diagnostic != NULL) {
			diagnostic->instruction = parse_diagnostic.word_offset;
			diagnostic->reason = parse_diagnostic.reason;
		}
		return error;
	}

	/* Optimizes a private copy: every block runs (there are no skipped blocks), and the known bits fold. */
	kern_memset(&options, 0, sizeof(options));
	options.runs_every_block = 1U;
	options.simplify = 1U;
	kern_memset(options.needed, 1, sizeof(options.needed));
	error = drv_gpu_shader_optimize(ir, &options, &optimized);
	if (error != 0) {
		drv_gpu_shader_ir_free(ir);
		return error;
	}

	/* Lowers the rewritten program, or the parsed one when the optimizer left it alone. */
	if (optimized.instructions != NULL) {
		error = nvrtx_sm75_compile_ir_sm(&optimized, sm, binary, diagnostic);
	} else {
		error = nvrtx_sm75_compile_ir_sm(ir, sm, binary, diagnostic);
	}

	/* A rewritten program refused for any reason but memory is lowered from the parsed one. */
	if (optimized.instructions != NULL && error != 0 && error != ENOMEM) {
		if (diagnostic != NULL)
			kern_memset(diagnostic, 0, sizeof(*diagnostic));
		error = nvrtx_sm75_compile_ir_sm(ir, sm, binary, diagnostic);
	}

	/* The IR retires before the program is handed over. */
	kern_free(optimized.instructions);
	drv_gpu_shader_ir_free(ir);

	/* Reports the lowering's refusal. */
	if (error != 0)
		return error;

	/* Succeeded: the caller owns the program. */
	return 0;
}

/*
 * Compiles a parsed program for a shader model.
 *
 * Returns 0 with the program in *binary, EINVAL for a malformed program,
 * ENOTSUP for one the compiler does not cover or a shader model it does
 * not target (both with the diagnostic), or ENOMEM.
 */
int
nvrtx_sm75_compile_ir_sm(
	const struct drv_gpu_shader_ir *ir,
	uint32_t sm,
	struct nvrtx_sm75_binary **binary,
	struct nvrtx_sm75_diagnostic *diagnostic)
{
	struct nvrtx_sm75_lowering lowering;
	const struct drv_gpu_shader_ir_inst *instruction;
	uint32_t dst;
	uint32_t index;
	int defines;
	int known;
	int error;

	/* A refused compile never leaves an old output pointer live. */
	if (binary == NULL || ir == NULL)
		return EINVAL;
	*binary = NULL;

	/* A shader model the compiler does not target is refused. */
	known = nvrtx_sm75_known_model(sm);
	if (known == 0) {
		if (diagnostic != NULL) {
			diagnostic->instruction = 0U;
			diagnostic->reason = "the shader model is not one the compiler targets (75, 86 or 89)";
		}
		return ENOTSUP;
	}

	/* The state, empty. */
	kern_memset(&lowering, 0, sizeof(lowering));
	lowering.sm = sm;
	lowering.ir = ir;
	lowering.diagnostic = diagnostic;

	/* The result, for the shader model. */
	lowering.binary = kern_calloc(1U, sizeof(*lowering.binary));
	if (lowering.binary == NULL)
		return ENOMEM;
	lowering.binary->shader_model = sm;

	/* Checks the program, finds the last reads, maps the uniforms and makes room for the code. */
	error = nvrtx_sm75_prepare(&lowering);

	/* A fragment program's 1 / w first. */
	if (error == 0)
		error = nvrtx_sm75_prologue(&lowering);

	/* Lowers every instruction in order. */
	for (index = 0U; error == 0 && index < ir->instruction_count; index++) {
		instruction = &ir->instructions[index];
		lowering.index = index;

		/* The destination's register, a value redefined by a MOVE keeping its own. */
		dst = NVRTX_SM75_RZ;
		defines = nvrtx_sm75_defines(instruction);
		if (defines != 0) {
			if (lowering.reg[instruction->dst] == NVRTX_SM75_NO_REG) {
				error = nvrtx_sm75_take(&lowering, &dst);
				if (error != 0)
					break;
				lowering.reg[instruction->dst] = (uint16_t)dst;
			}
			dst = lowering.reg[instruction->dst];
		}

		/* The instruction's SM75 instructions. */
		error = nvrtx_sm75_lower(&lowering, instruction, dst);
		if (error != 0)
			break;

		/* Registers of values no later instruction reads go back. */
		nvrtx_sm75_retire(&lowering, instruction);
	}

	/* The end of the program. */
	if (error == 0)
		error = nvrtx_sm75_finish(&lowering);

	/* The per-value and per-uniform arrays are only the compilation's. */
	kern_free(lowering.reg);
	kern_free(lowering.last_use);
	kern_free(lowering.slot);

	/* A refused program is never handed over. */
	if (error != 0) {
		nvrtx_sm75_binary_free(lowering.binary);
		return error;
	}

	/* Succeeded: the caller owns the program. */
	*binary = lowering.binary;
	return 0;
}

/* Frees a compiled program. */
void
nvrtx_sm75_binary_free(
	struct nvrtx_sm75_binary *binary)
{
	/* Nothing to free. */
	if (binary == NULL)
		return;

	/* The code, then the record. */
	kern_free(binary->code);
	kern_free(binary);
}

/* Reports nonzero for a shader model the compiler targets: Turing (75), Ampere (86) and Ada (89). */
static int
nvrtx_sm75_known_model(
	uint32_t sm)
{
	/* The three. */
	if (sm == NVRTX_SM_TURING)
		return 1;
	if (sm == NVRTX_SM_AMPERE)
		return 1;
	if (sm == NVRTX_SM_ADA)
		return 1;

	/* Succeeded: any other is not targeted. */
	return 0;
}

/* Reports nonzero for an instruction that accesses global memory (LDG, STG and the global atomics). */
static int
nvrtx_sm75_is_global(
	const struct nvrtx_sm75_word *word)
{
	uint32_t opcode;

	/* The 12-bit opcode (the memory accesses have no ALU form). */
	opcode = (uint32_t)nvrtx_sm75_get(word, NVRTX_SM75_OPCODE_LOW, NVRTX_SM75_OPCODE_BITS);

	/* The loads and the stores. */
	if (opcode == NVRTX_SM75_OP_LDG || opcode == NVRTX_SM75_OP_STG)
		return 1;

	/* The atomics. */
	if (opcode == NVRTX_SM75_OP_ATOMG || opcode == NVRTX_SM75_OP_ATOMG_CAS)
		return 1;

	/* Succeeded: no global access. */
	return 0;
}

/* Records a refusal of the current instruction and reports its error. */
static int
nvrtx_sm75_refuse(
	struct nvrtx_sm75_lowering *lowering,
	int error,
	const char *reason)
{
	/* The instruction and the reason, when the caller asked for them. */
	if (lowering->diagnostic != NULL) {
		lowering->diagnostic->instruction = lowering->index;
		lowering->diagnostic->reason = reason;
	}

	/* Succeeded in describing the refusal; the error goes back. */
	return error;
}

/*
 * Checks that the program uses only operations the compiler covers, finds each
 * value's last read, maps the uniforms, and allocates the per-value arrays
 * and the code.
 */
static int
nvrtx_sm75_prepare(
	struct nvrtx_sm75_lowering *lowering)
{
	const struct drv_gpu_shader_ir *ir;
	const struct drv_gpu_shader_ir_inst *instruction;
	struct nvrtx_sm75_binary *binary;
	uint32_t sources[NVRTX_SM75_MAX_SOURCES];
	uint32_t source_count;
	uint32_t index;
	uint32_t source;
	int defines;
	int count;
	int error;

	ir = lowering->ir;
	binary = lowering->binary;

	/* Compute, vertex and fragment programs are compiled. */
	if (ir->stage != DRV_GPU_STAGE_COMPUTE && ir->stage != DRV_GPU_STAGE_VERTEX && ir->stage != DRV_GPU_STAGE_FRAGMENT)
		return nvrtx_sm75_refuse(lowering, ENOTSUP, "only compute, vertex and fragment shaders are compiled");

	/* The per-value arrays. */
	lowering->reg = kern_calloc(ir->value_count + 1U, sizeof(*lowering->reg));
	if (lowering->reg == NULL)
		return ENOMEM;

	lowering->last_use = kern_calloc(ir->value_count + 1U, sizeof(*lowering->last_use));
	if (lowering->last_use == NULL)
		return ENOMEM;

	/* No value has a register yet. */
	for (index = 0U; index <= ir->value_count; index++)
		lowering->reg[index] = NVRTX_SM75_NO_REG;

	/* Each instruction: covered, its values in range, its sources' last reads. */
	for (index = 0U; index < ir->instruction_count; index++) {
		instruction = &ir->instructions[index];
		lowering->index = index;

		/* The sources of an operation the compiler covers. */
		count = nvrtx_sm75_sources(instruction, sources);
		if (count < 0)
			return nvrtx_sm75_refuse(lowering, ENOTSUP, "the operation is not compiled yet");

		/* The destination is a value of the program. */
		defines = nvrtx_sm75_defines(instruction);
		if (defines != 0 && instruction->dst >= ir->value_count)
			return nvrtx_sm75_refuse(lowering, EINVAL, "a destination is not a value of the program");

		/* A sample's four results are values of the program. */
		defines = nvrtx_sm75_is_sample(instruction->op);
		if (defines != 0 && (instruction->dst >= ir->value_count || ir->value_count - instruction->dst < 4U))
			return nvrtx_sm75_refuse(lowering, EINVAL, "a sample's results are not values of the program");

		/* Each source is a value of the program, read last here so far. */
		source_count = (uint32_t)count;
		for (source = 0U; source < source_count; source++) {
			if (sources[source] >= ir->value_count)
				return nvrtx_sm75_refuse(lowering, EINVAL, "a source is not a value of the program");
			lowering->last_use[sources[source]] = index + 1U;
		}
	}

	/* Values read inside a loop that were made before it live to the loop's end. */
	error = nvrtx_sm75_extend_loops(lowering);
	if (error != 0)
		return error;

	/* The uniforms' slots. */
	error = nvrtx_sm75_map_uniforms(lowering);
	if (error != 0)
		return error;

	/* A graphics program's outputs, inputs and header. */
	error = nvrtx_sm75_prepare_graphics(lowering);
	if (error != 0)
		return error;

	/* The code: every instruction's largest expansion and the end. */
	lowering->capacity = ir->instruction_count * NVRTX_SM75_EXPANSION + NVRTX_SM75_TAIL;
	binary->code = kern_calloc(lowering->capacity, NVRTX_SM75_INSTRUCTION_BYTES);
	if (binary->code == NULL)
		return ENOMEM;

	/* The program's interface. */
	binary->local_size[0] = ir->local_size[0];
	binary->local_size[1] = ir->local_size[1];
	binary->local_size[2] = ir->local_size[2];
	binary->shared_bytes = ir->shared_bytes;
	binary->barrier_count = 0U;
	if (ir->uses_barrier != 0U)
		binary->barrier_count = 1U;

	/* Succeeded: the program can be lowered. */
	lowering->index = 0U;
	return 0;
}

/*
 * Lists the values an instruction reads; reports their number, or -1 for
 * an operation the compiler does not cover.
 */
static int
nvrtx_sm75_sources(
	const struct drv_gpu_shader_ir_inst *instruction,
	uint32_t *sources)
{
	int count;

	/* The sources by the operation. */
	switch (instruction->op) {
	case DRV_GPU_IR_NOP:
	case DRV_GPU_IR_SKIP_BEGIN:
	case DRV_GPU_IR_SKIP_END:
	case DRV_GPU_IR_CONST:
	case DRV_GPU_IR_ICONST:
	case DRV_GPU_IR_BOOL:
	case DRV_GPU_IR_LOAD_PUSH:
	case DRV_GPU_IR_LOAD_UBO:
	case DRV_GPU_IR_LOAD_SYSTEM:
	case DRV_GPU_IR_STORAGE_SIZE:
		return 0;
	case DRV_GPU_IR_FNEG:
	case DRV_GPU_IR_FABS:
	case DRV_GPU_IR_RSQ:
	case DRV_GPU_IR_RCP:
	case DRV_GPU_IR_SQRT:
	case DRV_GPU_IR_EXP2:
	case DRV_GPU_IR_LOG2:
	case DRV_GPU_IR_SIN:
	case DRV_GPU_IR_COS:
	case DRV_GPU_IR_FLOOR:
	case DRV_GPU_IR_FRACT:
	case DRV_GPU_IR_FTRUNC:
	case DRV_GPU_IR_FROUND_EVEN:
	case DRV_GPU_IR_NOT:
	case DRV_GPU_IR_INEG:
	case DRV_GPU_IR_INOT:
	case DRV_GPU_IR_I2F:
	case DRV_GPU_IR_U2F:
	case DRV_GPU_IR_F2I:
	case DRV_GPU_IR_F2U:
	case DRV_GPU_IR_MOVE:
		sources[0] = instruction->src[0];
		return 1;
	case DRV_GPU_IR_FADD:
	case DRV_GPU_IR_FSUB:
	case DRV_GPU_IR_FMUL:
	case DRV_GPU_IR_FMIN:
	case DRV_GPU_IR_FMAX:
	case DRV_GPU_IR_FLT:
	case DRV_GPU_IR_FGE:
	case DRV_GPU_IR_FEQ:
	case DRV_GPU_IR_FNEU:
	case DRV_GPU_IR_AND:
	case DRV_GPU_IR_OR:
	case DRV_GPU_IR_IADD:
	case DRV_GPU_IR_ISUB:
	case DRV_GPU_IR_IMUL:
	case DRV_GPU_IR_IAND:
	case DRV_GPU_IR_IOR:
	case DRV_GPU_IR_IXOR:
	case DRV_GPU_IR_SHL:
	case DRV_GPU_IR_SHR:
	case DRV_GPU_IR_ASR:
	case DRV_GPU_IR_ILT:
	case DRV_GPU_IR_IGE:
	case DRV_GPU_IR_ULT:
	case DRV_GPU_IR_UGE:
	case DRV_GPU_IR_IEQ:
	case DRV_GPU_IR_INE:
		sources[0] = instruction->src[0];
		sources[1] = instruction->src[1];
		return 2;
	case DRV_GPU_IR_SELECT:
		sources[0] = instruction->src[0];
		sources[1] = instruction->src[1];
		sources[2] = instruction->src[2];
		return 3;
	case DRV_GPU_IR_LOAD_STORAGE:
		sources[0] = instruction->src[0];
		if (instruction->component == 0U)
			return 1;
		sources[1] = instruction->src[1];
		return 2;
	case DRV_GPU_IR_STORE_STORAGE:
	case DRV_GPU_IR_STORE_SHARED:
		sources[0] = instruction->src[0];
		sources[1] = instruction->src[1];
		if (instruction->component == 0U)
			return 2;
		sources[2] = instruction->src[2];
		return 3;
	case DRV_GPU_IR_LOAD_SHARED:
		sources[0] = instruction->src[0];
		if (instruction->component == 0U)
			return 1;
		sources[1] = instruction->src[1];
		return 2;
	case DRV_GPU_IR_LOOP_BEGIN:
	case DRV_GPU_IR_BARRIER:
	case DRV_GPU_IR_FENCE:
		return 0;
	case DRV_GPU_IR_LOOP_END:
	case DRV_GPU_IR_STORE_OUTPUT:
	case DRV_GPU_IR_KILL:
		sources[0] = instruction->src[0];
		return 1;
	case DRV_GPU_IR_LOAD_INPUT:
		return 0;
	case DRV_GPU_IR_SAMPLE:
		sources[0] = instruction->src[0];
		sources[1] = instruction->src[1];
		return 2;
	case DRV_GPU_IR_SAMPLE_LOD:
	case DRV_GPU_IR_SAMPLE_BIAS:
		sources[0] = instruction->src[0];
		sources[1] = instruction->src[1];
		sources[2] = instruction->src[2];
		return 3;
	case DRV_GPU_IR_TEXTURE:
		/* The message's parameters: src[1] consecutive values from src[0]. */
		if (instruction->src[1] == 0U || instruction->src[1] > NVRTX_SM75_MAX_SOURCES)
			break;
		for (count = 0; count < (int)instruction->src[1]; count++)
			sources[count] = instruction->src[0] + (uint32_t)count;
		return count;
	case DRV_GPU_IR_UDIV:
	case DRV_GPU_IR_UMOD:
	case DRV_GPU_IR_IDIV:
	case DRV_GPU_IR_IREM:
		sources[0] = instruction->src[0];
		sources[1] = instruction->src[1];
		return 2;
	case DRV_GPU_IR_ATOMIC:
		/* The offset and the value, the compare value of a compare and exchange, then the predicate. */
		sources[0] = instruction->src[0];
		sources[1] = instruction->src[1];
		count = 2;
		if (instruction->immediate == DRV_GPU_IR_ATOMIC_CMPXCHG) {
			sources[count] = instruction->src[count];
			count++;
		}
		if (instruction->component == DRV_GPU_IR_ATOMIC_PREDICATED) {
			sources[count] = instruction->src[count];
			count++;
		}
		return count;
	default:
		break;
	}

	/* Succeeded in finding the operation the compiler does not cover. */
	return -1;
}

/* Reports nonzero when an instruction defines its destination value. */
static int
nvrtx_sm75_defines(
	const struct drv_gpu_shader_ir_inst *instruction)
{
	/* The operations without a result: every skippable block is run, so its markers are nothing. */
	if (instruction->op == DRV_GPU_IR_NOP)
		return 0;
	if (instruction->op == DRV_GPU_IR_SKIP_BEGIN)
		return 0;
	if (instruction->op == DRV_GPU_IR_SKIP_END)
		return 0;
	if (instruction->op == DRV_GPU_IR_STORE_SHARED)
		return 0;
	if (instruction->op == DRV_GPU_IR_LOOP_BEGIN)
		return 0;
	if (instruction->op == DRV_GPU_IR_LOOP_END)
		return 0;
	if (instruction->op == DRV_GPU_IR_BARRIER)
		return 0;
	if (instruction->op == DRV_GPU_IR_FENCE)
		return 0;
	if (instruction->op == DRV_GPU_IR_STORE_OUTPUT)
		return 0;
	if (instruction->op == DRV_GPU_IR_KILL)
		return 0;

	/* A sample defines four values, which its lowering gives registers. */
	if (instruction->op == DRV_GPU_IR_SAMPLE)
		return 0;
	if (instruction->op == DRV_GPU_IR_SAMPLE_LOD)
		return 0;
	if (instruction->op == DRV_GPU_IR_SAMPLE_BIAS)
		return 0;
	if (instruction->op == DRV_GPU_IR_TEXTURE)
		return 0;
	if (instruction->op == DRV_GPU_IR_STORE_STORAGE)
		return 0;

	/* Succeeded: every other covered operation has one. */
	return 1;
}

/*
 * Gives each storage buffer of the uniform list a slot of the storage table
 * and each uniform buffer a constant buffer, in the list's order, and
 * records them in the result.
 */
static int
nvrtx_sm75_map_uniforms(
	struct nvrtx_sm75_lowering *lowering)
{
	const struct drv_gpu_shader_ir *ir;
	const struct drv_gpu_shader_ir_uniform *uniform;
	struct nvrtx_sm75_binary *binary;
	uint32_t index;

	ir = lowering->ir;
	binary = lowering->binary;

	/* One slot per uniform. */
	lowering->slot = kern_calloc(ir->uniform_count + 1U, sizeof(*lowering->slot));
	if (lowering->slot == NULL)
		return ENOMEM;

	/* Each uniform in order. */
	for (index = 0U; index < ir->uniform_count; index++) {
		uniform = &ir->uniforms[index];
		lowering->slot[index] = NVRTX_SM75_NO_SLOT;

		/* A storage buffer takes the next entry of the table. */
		if (uniform->kind == DRV_GPU_IR_UNIFORM_STORAGE) {
			if (binary->storage_count == NVRTX_SM75_CB_STORAGE_MAX)
				return nvrtx_sm75_refuse(lowering, ENOTSUP, "more storage buffers than the table holds");
			lowering->slot[index] = binary->storage_count;
			binary->storage[binary->storage_count].set = uniform->set;
			binary->storage[binary->storage_count].binding = uniform->binding;
			binary->storage_count++;
		} else if (uniform->kind == DRV_GPU_IR_UNIFORM_SAMPLED_IMAGE) {
			/* A sampled image takes the next texture handle. */
			if (binary->texture_count == NVRTX_SM75_CB_TEXTURES_MAX)
				return nvrtx_sm75_refuse(lowering, ENOTSUP, "more sampled images than texture handles");
			lowering->slot[index] = binary->texture_count;
			binary->textures[binary->texture_count].set = uniform->set;
			binary->textures[binary->texture_count].binding = uniform->binding;
			binary->texture_count++;
		} else if (uniform->kind == DRV_GPU_IR_UNIFORM_BLOCK) {
			/* A uniform buffer takes the next constant buffer. */
			if (binary->uniform_count == NVRTX_SM75_CB_UNIFORM_MAX)
				return nvrtx_sm75_refuse(lowering, ENOTSUP, "more uniform buffers than constant buffers");
			lowering->slot[index] = NVRTX_SM75_CB_UNIFORM_FIRST + binary->uniform_count;
			binary->uniforms[binary->uniform_count].set = uniform->set;
			binary->uniforms[binary->uniform_count].binding = uniform->binding;
			binary->uniforms[binary->uniform_count].size = uniform->offset + uniform->size;
			binary->uniform_count++;
		}
	}

	/* Succeeded: every buffer has its place. */
	return 0;
}

/* Lowers one IR instruction into dst (RZ when it has no result). */
static int
nvrtx_sm75_lower(
	struct nvrtx_sm75_lowering *lowering,
	const struct drv_gpu_shader_ir_inst *instruction,
	uint32_t dst)
{
	struct nvrtx_sm75_word word;
	struct nvrtx_sm75_src source;
	uint32_t end;
	int error;

	/* The operations of their own families, then the moves of a value. */
	switch (instruction->op) {
	case DRV_GPU_IR_NOP:
	case DRV_GPU_IR_SKIP_BEGIN:
	case DRV_GPU_IR_SKIP_END:
		return 0;
	case DRV_GPU_IR_FLT:
	case DRV_GPU_IR_FGE:
	case DRV_GPU_IR_FEQ:
	case DRV_GPU_IR_FNEU:
	case DRV_GPU_IR_ILT:
	case DRV_GPU_IR_IGE:
	case DRV_GPU_IR_ULT:
	case DRV_GPU_IR_UGE:
	case DRV_GPU_IR_IEQ:
	case DRV_GPU_IR_INE:
	case DRV_GPU_IR_SELECT:
		error = nvrtx_sm75_lower_compare(lowering, instruction, dst);
		return error;
	case DRV_GPU_IR_RSQ:
	case DRV_GPU_IR_RCP:
	case DRV_GPU_IR_SQRT:
	case DRV_GPU_IR_EXP2:
	case DRV_GPU_IR_LOG2:
	case DRV_GPU_IR_SIN:
	case DRV_GPU_IR_COS:
	case DRV_GPU_IR_FLOOR:
	case DRV_GPU_IR_FRACT:
	case DRV_GPU_IR_FTRUNC:
	case DRV_GPU_IR_FROUND_EVEN:
	case DRV_GPU_IR_I2F:
	case DRV_GPU_IR_U2F:
	case DRV_GPU_IR_F2I:
	case DRV_GPU_IR_F2U:
		error = nvrtx_sm75_lower_unit(lowering, instruction, dst);
		return error;
	case DRV_GPU_IR_LOAD_SYSTEM:
		error = nvrtx_sm75_lower_system(lowering, instruction, dst);
		return error;
	case DRV_GPU_IR_LOAD_STORAGE:
	case DRV_GPU_IR_STORE_STORAGE:
	case DRV_GPU_IR_STORAGE_SIZE:
		error = nvrtx_sm75_lower_storage(lowering, instruction, dst);
		return error;
	case DRV_GPU_IR_LOOP_BEGIN:
	case DRV_GPU_IR_LOOP_END:
		error = nvrtx_sm75_lower_loop(lowering, instruction);
		return error;
	case DRV_GPU_IR_UDIV:
	case DRV_GPU_IR_UMOD:
	case DRV_GPU_IR_IDIV:
	case DRV_GPU_IR_IREM:
		error = nvrtx_sm75_lower_divide(lowering, instruction, dst);
		return error;
	case DRV_GPU_IR_ATOMIC:
		error = nvrtx_sm75_lower_atomic(lowering, instruction, dst);
		return error;
	case DRV_GPU_IR_LOAD_SHARED:
	case DRV_GPU_IR_STORE_SHARED:
		error = nvrtx_sm75_lower_shared(lowering, instruction, dst);
		return error;
	case DRV_GPU_IR_BARRIER:
	case DRV_GPU_IR_FENCE:
		error = nvrtx_sm75_lower_sync(lowering, instruction);
		return error;
	case DRV_GPU_IR_LOAD_INPUT:
		error = nvrtx_sm75_lower_input(lowering, instruction, dst);
		return error;
	case DRV_GPU_IR_STORE_OUTPUT:
		error = nvrtx_sm75_lower_output(lowering, instruction);
		return error;
	case DRV_GPU_IR_SAMPLE:
	case DRV_GPU_IR_SAMPLE_LOD:
	case DRV_GPU_IR_SAMPLE_BIAS:
	case DRV_GPU_IR_TEXTURE:
		error = nvrtx_sm75_lower_sample(lowering, instruction);
		return error;
	case DRV_GPU_IR_KILL:
		error = nvrtx_sm75_lower_kill(lowering, instruction);
		return error;
	case DRV_GPU_IR_CONST:
	case DRV_GPU_IR_ICONST:
	case DRV_GPU_IR_BOOL:
		source = nvrtx_sm75_imm(instruction->immediate);
		break;
	case DRV_GPU_IR_LOAD_PUSH:
		/* A push constant word lies in the first 256 bytes of constant buffer 0. */
		end = instruction->immediate + 4U;
		if (end > NVRTX_SM75_CB_PUSH_BYTES || end < instruction->immediate || (instruction->immediate & 3U) != 0U)
			return nvrtx_sm75_refuse(lowering, ENOTSUP, "a push constant beyond the 256 bytes of the root buffer");
		if (end > lowering->binary->push_bytes)
			lowering->binary->push_bytes = end;
		source = nvrtx_sm75_cb(NVRTX_SM75_CB_ROOT, NVRTX_SM75_CB_PUSH + instruction->immediate);
		break;
	case DRV_GPU_IR_LOAD_UBO:
		/*
		 * A uniform buffer word at an offset the constant buffer field
		 * holds.
		 *
		 * Value referenced: Mesa 25.3.6
		 * src/nouveau/compiler/nak/sm70_encode.rs
		 * (SM70Encoder::set_src_cb: the offset field of 16 bits).
		 */
		if (instruction->location >= lowering->ir->uniform_count || lowering->ir->uniforms[instruction->location].kind != DRV_GPU_IR_UNIFORM_BLOCK)
			return nvrtx_sm75_refuse(lowering, EINVAL, "a uniform load names no uniform buffer");
		if (instruction->immediate > 0xfffcU || (instruction->immediate & 3U) != 0U)
			return nvrtx_sm75_refuse(lowering, ENOTSUP, "a uniform buffer offset beyond 64 KiB");
		source = nvrtx_sm75_cb(lowering->slot[instruction->location], instruction->immediate);
		break;
	case DRV_GPU_IR_MOVE:
		source = nvrtx_sm75_value(lowering, instruction->src[0]);
		break;
	default:
		error = nvrtx_sm75_lower_alu(lowering, instruction, dst);
		return error;
	}

	/* A value no instruction reads needs no move. */
	if (dst == NVRTX_SM75_RZ)
		return 0;

	/* MOV dst, the source. */
	nvrtx_sm75_begin(&word);
	nvrtx_sm75_mov(&word, dst, &source);
	error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_FIXED);
	if (error != 0)
		return error;

	/* Succeeded: the value is in its register. */
	return 0;
}

/* Lowers the float and integer arithmetic and the bitwise operations. */
static int
nvrtx_sm75_lower_alu(
	struct nvrtx_sm75_lowering *lowering,
	const struct drv_gpu_shader_ir_inst *instruction,
	uint32_t dst)
{
	struct nvrtx_sm75_word word;
	struct nvrtx_sm75_src a;
	struct nvrtx_sm75_src b;
	struct nvrtx_sm75_src zero;
	struct nvrtx_sm75_src mask;
	int error;

	nvrtx_sm75_begin(&word);
	zero = nvrtx_sm75_reg(NVRTX_SM75_RZ);
	a = nvrtx_sm75_value(lowering, instruction->src[0]);
	b = zero;

	/* The second source of the binary operations. */
	if (instruction->op != DRV_GPU_IR_FNEG &&
	    instruction->op != DRV_GPU_IR_FABS &&
	    instruction->op != DRV_GPU_IR_NOT &&
	    instruction->op != DRV_GPU_IR_INOT &&
	    instruction->op != DRV_GPU_IR_INEG)
		b = nvrtx_sm75_value(lowering, instruction->src[1]);

	/* The SM75 instruction of the operation. */
	switch (instruction->op) {
	case DRV_GPU_IR_FADD:
		nvrtx_sm75_fadd(&word, dst, &a, &b);
		break;
	case DRV_GPU_IR_FSUB:
		b.neg = 1;
		nvrtx_sm75_fadd(&word, dst, &a, &b);
		break;
	case DRV_GPU_IR_FMUL:
		nvrtx_sm75_fmul(&word, dst, &a, &b);
		break;
	case DRV_GPU_IR_FMIN:
		nvrtx_sm75_fmnmx(&word, dst, &a, &b, 1);
		break;
	case DRV_GPU_IR_FMAX:
		nvrtx_sm75_fmnmx(&word, dst, &a, &b, 0);
		break;
	case DRV_GPU_IR_FNEG:
		/* The sign bit flipped, exactly (also for zero and NaN). */
		mask = nvrtx_sm75_imm(NVRTX_SM75_SIGN);
		nvrtx_sm75_lop3(&word, dst, &a, &mask, &zero, NVRTX_SM75_LUT_XOR);
		break;
	case DRV_GPU_IR_FABS:
		/* The sign bit cleared. */
		mask = nvrtx_sm75_imm(~NVRTX_SM75_SIGN);
		nvrtx_sm75_lop3(&word, dst, &a, &mask, &zero, NVRTX_SM75_LUT_AND);
		break;
	case DRV_GPU_IR_IADD:
		nvrtx_sm75_iadd3(&word, dst, &a, &b, &zero);
		break;
	case DRV_GPU_IR_ISUB:
		b.neg = 1;
		nvrtx_sm75_iadd3(&word, dst, &a, &b, &zero);
		break;
	case DRV_GPU_IR_INEG:
		a.neg = 1;
		nvrtx_sm75_iadd3(&word, dst, &a, &zero, &zero);
		break;
	case DRV_GPU_IR_IMUL:
		nvrtx_sm75_imad(&word, dst, &a, &b, &zero, 0);
		break;
	case DRV_GPU_IR_AND:
	case DRV_GPU_IR_IAND:
		nvrtx_sm75_lop3(&word, dst, &a, &b, &zero, NVRTX_SM75_LUT_AND);
		break;
	case DRV_GPU_IR_OR:
	case DRV_GPU_IR_IOR:
		nvrtx_sm75_lop3(&word, dst, &a, &b, &zero, NVRTX_SM75_LUT_OR);
		break;
	case DRV_GPU_IR_IXOR:
		nvrtx_sm75_lop3(&word, dst, &a, &b, &zero, NVRTX_SM75_LUT_XOR);
		break;
	case DRV_GPU_IR_NOT:
	case DRV_GPU_IR_INOT:
		nvrtx_sm75_lop3(&word, dst, &a, &zero, &zero, NVRTX_SM75_LUT_NOT);
		break;
	case DRV_GPU_IR_SHL:
		nvrtx_sm75_shift(&word, dst, a.value, &b, 0, 0);
		break;
	case DRV_GPU_IR_SHR:
		nvrtx_sm75_shift(&word, dst, a.value, &b, 1, 0);
		break;
	case DRV_GPU_IR_ASR:
		nvrtx_sm75_shift(&word, dst, a.value, &b, 1, 1);
		break;
	default:
		return nvrtx_sm75_refuse(lowering, ENOTSUP, "the operation is not compiled yet");
	}

	/* One instruction of fixed latency. */
	error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_FIXED);
	if (error != 0)
		return error;

	/* Succeeded: the result is in dst. */
	return 0;
}

/*
 * Lowers a comparison into P0 and then a Boolean, and a selection by a
 * Boolean.
 */
static int
nvrtx_sm75_lower_compare(
	struct nvrtx_sm75_lowering *lowering,
	const struct drv_gpu_shader_ir_inst *instruction,
	uint32_t dst)
{
	struct nvrtx_sm75_word word;
	struct nvrtx_sm75_src a;
	struct nvrtx_sm75_src b;
	struct nvrtx_sm75_src zero;
	struct nvrtx_sm75_src truth;
	int error;

	zero = nvrtx_sm75_reg(NVRTX_SM75_RZ);
	truth = nvrtx_sm75_imm(NVRTX_SM75_TRUE);
	a = nvrtx_sm75_value(lowering, instruction->src[0]);
	b = nvrtx_sm75_value(lowering, instruction->src[1]);

	/* P0 from the comparison, or from the selection's condition. */
	nvrtx_sm75_begin(&word);
	switch (instruction->op) {
	case DRV_GPU_IR_FLT:
		nvrtx_sm75_fsetp(&word, NVRTX_SM75_P0, NVRTX_SM75_FCMP_LT, &a, &b);
		break;
	case DRV_GPU_IR_FGE:
		nvrtx_sm75_fsetp(&word, NVRTX_SM75_P0, NVRTX_SM75_FCMP_GE, &a, &b);
		break;
	case DRV_GPU_IR_FEQ:
		nvrtx_sm75_fsetp(&word, NVRTX_SM75_P0, NVRTX_SM75_FCMP_EQ, &a, &b);
		break;
	case DRV_GPU_IR_FNEU:
		nvrtx_sm75_fsetp(&word, NVRTX_SM75_P0, NVRTX_SM75_FCMP_UNORD_NE, &a, &b);
		break;
	case DRV_GPU_IR_ILT:
		nvrtx_sm75_isetp(&word, NVRTX_SM75_P0, NVRTX_SM75_ICMP_LT, 1, &a, &b);
		break;
	case DRV_GPU_IR_IGE:
		nvrtx_sm75_isetp(&word, NVRTX_SM75_P0, NVRTX_SM75_ICMP_GE, 1, &a, &b);
		break;
	case DRV_GPU_IR_ULT:
		nvrtx_sm75_isetp(&word, NVRTX_SM75_P0, NVRTX_SM75_ICMP_LT, 0, &a, &b);
		break;
	case DRV_GPU_IR_UGE:
		nvrtx_sm75_isetp(&word, NVRTX_SM75_P0, NVRTX_SM75_ICMP_GE, 0, &a, &b);
		break;
	case DRV_GPU_IR_IEQ:
		nvrtx_sm75_isetp(&word, NVRTX_SM75_P0, NVRTX_SM75_ICMP_EQ, 0, &a, &b);
		break;
	case DRV_GPU_IR_INE:
		nvrtx_sm75_isetp(&word, NVRTX_SM75_P0, NVRTX_SM75_ICMP_NE, 0, &a, &b);
		break;
	default:
		/* SELECT: P0 is the condition (src[0]) being nonzero. */
		nvrtx_sm75_isetp(&word, NVRTX_SM75_P0, NVRTX_SM75_ICMP_NE, 0, &a, &zero);
		break;
	}

	error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_FIXED_PREDICATE);
	if (error != 0)
		return error;

	/* A result no instruction reads is not made. */
	if (dst == NVRTX_SM75_RZ)
		return 0;

	/* The result: the selected source, or the Boolean of P0. */
	nvrtx_sm75_begin(&word);
	if (instruction->op == DRV_GPU_IR_SELECT) {
		a = nvrtx_sm75_value(lowering, instruction->src[1]);
		b = nvrtx_sm75_value(lowering, instruction->src[2]);
		nvrtx_sm75_sel(&word, dst, &a, &b, NVRTX_SM75_P0, 0);
	} else {
		nvrtx_sm75_sel(&word, dst, &zero, &truth, NVRTX_SM75_P0, 1);
	}

	error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_FIXED);
	if (error != 0)
		return error;

	/* Succeeded: the result is in dst. */
	return 0;
}

/*
 * Lowers the operations of the units of variable latency: the special
 * functions (sine and cosine take the angle in turns), the conversions and
 * the roundings (the fraction is x - floor(x)).
 */
static int
nvrtx_sm75_lower_unit(
	struct nvrtx_sm75_lowering *lowering,
	const struct drv_gpu_shader_ir_inst *instruction,
	uint32_t dst)
{
	struct nvrtx_sm75_word word;
	struct nvrtx_sm75_src a;
	struct nvrtx_sm75_src b;
	struct nvrtx_sm75_src turns;
	uint32_t source;
	uint32_t temporary;
	uint32_t function;
	int error;

	source = lowering->reg[instruction->src[0]];
	temporary = NVRTX_SM75_RZ;

	/*
	 * Sine and cosine first scale the angle into turns.
	 *
	 * Procedure referenced: Mesa 25.3.6
	 * src/nouveau/compiler/nak/builder.rs (SSABuilder::fsin and
	 * SSABuilder::fcos: the angle times 1 / (2 pi) from SM70 on).
	 */
	if (instruction->op == DRV_GPU_IR_SIN || instruction->op == DRV_GPU_IR_COS) {
		error = nvrtx_sm75_take(lowering, &temporary);
		if (error != 0)
			return error;

		a = nvrtx_sm75_reg(source);
		turns = nvrtx_sm75_imm(NVRTX_SM75_INV_TWO_PI);
		nvrtx_sm75_begin(&word);
		nvrtx_sm75_fmul(&word, temporary, &a, &turns);
		error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_FIXED);
		if (error != 0) {
			nvrtx_sm75_give(lowering, temporary);
			return error;
		}
		source = temporary;
	}

	/* The fraction rounds into a temporary first. */
	if (instruction->op == DRV_GPU_IR_FRACT) {
		error = nvrtx_sm75_take(lowering, &temporary);
		if (error != 0)
			return error;
	}

	/* The unit's instruction. */
	nvrtx_sm75_begin(&word);
	function = NVRTX_SM75_MUFU_RCP;
	switch (instruction->op) {
	case DRV_GPU_IR_RSQ:
		function = NVRTX_SM75_MUFU_RSQ;
		break;
	case DRV_GPU_IR_SQRT:
		function = NVRTX_SM75_MUFU_SQRT;
		break;
	case DRV_GPU_IR_EXP2:
		function = NVRTX_SM75_MUFU_EX2;
		break;
	case DRV_GPU_IR_LOG2:
		function = NVRTX_SM75_MUFU_LG2;
		break;
	case DRV_GPU_IR_SIN:
		function = NVRTX_SM75_MUFU_SIN;
		break;
	case DRV_GPU_IR_COS:
		function = NVRTX_SM75_MUFU_COS;
		break;
	default:
		break;
	}

	/* The conversions and the roundings; the special functions otherwise. */
	switch (instruction->op) {
	case DRV_GPU_IR_I2F:
		nvrtx_sm75_i2f(&word, dst, source, 1);
		break;
	case DRV_GPU_IR_U2F:
		nvrtx_sm75_i2f(&word, dst, source, 0);
		break;
	case DRV_GPU_IR_F2I:
		nvrtx_sm75_f2i(&word, dst, source, 1);
		break;
	case DRV_GPU_IR_F2U:
		nvrtx_sm75_f2i(&word, dst, source, 0);
		break;
	case DRV_GPU_IR_FLOOR:
		nvrtx_sm75_frnd(&word, dst, source, NVRTX_SM75_ROUND_DOWN);
		break;
	case DRV_GPU_IR_FTRUNC:
		nvrtx_sm75_frnd(&word, dst, source, NVRTX_SM75_ROUND_ZERO);
		break;
	case DRV_GPU_IR_FROUND_EVEN:
		nvrtx_sm75_frnd(&word, dst, source, NVRTX_SM75_ROUND_NEAREST);
		break;
	case DRV_GPU_IR_FRACT:
		nvrtx_sm75_frnd(&word, temporary, source, NVRTX_SM75_ROUND_DOWN);
		break;
	default:
		nvrtx_sm75_mufu(&word, dst, function, source);
		break;
	}

	error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_VARIABLE_WRITE);

	/* The fraction subtracts the rounded value. */
	if (error == 0 && instruction->op == DRV_GPU_IR_FRACT) {
		a = nvrtx_sm75_reg(source);
		b = nvrtx_sm75_reg(temporary);
		b.neg = 1;
		nvrtx_sm75_begin(&word);
		nvrtx_sm75_fadd(&word, dst, &a, &b);
		error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_FIXED);
	}

	/* The temporary goes back. */
	if (temporary != NVRTX_SM75_RZ)
		nvrtx_sm75_give(lowering, temporary);

	/* Reports an emission past the code's room. */
	if (error != 0)
		return error;

	/* Succeeded: the result is in dst. */
	return 0;
}

/*
 * Lowers a built-in value of the compute invocation: its place in its
 * group, the group's place in the grid, and its linear index in the group
 * (x + width * (y + height * z)).
 */
static int
nvrtx_sm75_lower_system(
	struct nvrtx_sm75_lowering *lowering,
	const struct drv_gpu_shader_ir_inst *instruction,
	uint32_t dst)
{
	struct nvrtx_sm75_word word;
	struct nvrtx_sm75_src a;
	struct nvrtx_sm75_src b;
	struct nvrtx_sm75_src c;
	uint32_t ids[3];
	uint32_t axis;
	uint32_t special;
	int error;

	/* The register of a single built-in. */
	special = 0U;
	switch (instruction->component) {
	case DRV_GPU_IR_SYSTEM_LOCAL_ID_X:
		special = NVRTX_SM75_SR_TID_X;
		break;
	case DRV_GPU_IR_SYSTEM_LOCAL_ID_Y:
		special = NVRTX_SM75_SR_TID_Y;
		break;
	case DRV_GPU_IR_SYSTEM_LOCAL_ID_Z:
		special = NVRTX_SM75_SR_TID_Z;
		break;
	case DRV_GPU_IR_SYSTEM_GROUP_ID_X:
		special = NVRTX_SM75_SR_CTAID_X;
		break;
	case DRV_GPU_IR_SYSTEM_GROUP_ID_Y:
		special = NVRTX_SM75_SR_CTAID_Y;
		break;
	case DRV_GPU_IR_SYSTEM_GROUP_ID_Z:
		special = NVRTX_SM75_SR_CTAID_Z;
		break;
	case DRV_GPU_IR_SYSTEM_LOCAL_INDEX:
		break;
	default:
		return nvrtx_sm75_refuse(lowering, ENOTSUP, "a built-in value a compute shader does not have");
	}

	/* A single built-in is one read of its special register. */
	if (special != 0U) {
		nvrtx_sm75_begin(&word);
		nvrtx_sm75_s2r(&word, dst, special);
		error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_VARIABLE_SPECIAL);
		if (error != 0)
			return error;
		return 0;
	}

	/* The linear index: the three local ids into temporaries. */
	for (axis = 0U; axis < 3U; axis++) {
		error = nvrtx_sm75_take(lowering, &ids[axis]);
		if (error != 0) {
			while (axis > 0U) {
				axis--;
				nvrtx_sm75_give(lowering, ids[axis]);
			}
			return error;
		}

		nvrtx_sm75_begin(&word);
		nvrtx_sm75_s2r(&word, ids[axis], NVRTX_SM75_SR_TID_X + axis);
		error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_VARIABLE_SPECIAL);
		if (error != 0)
			break;
	}

	/* z * height + y into the z temporary, then that * width + x into dst. */
	if (error == 0) {
		a = nvrtx_sm75_reg(ids[2]);
		b = nvrtx_sm75_imm(lowering->ir->local_size[1]);
		c = nvrtx_sm75_reg(ids[1]);
		nvrtx_sm75_begin(&word);
		nvrtx_sm75_imad(&word, ids[2], &a, &b, &c, 0);
		error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_FIXED);
	}
	if (error == 0) {
		a = nvrtx_sm75_reg(ids[2]);
		b = nvrtx_sm75_imm(lowering->ir->local_size[0]);
		c = nvrtx_sm75_reg(ids[0]);
		nvrtx_sm75_begin(&word);
		nvrtx_sm75_imad(&word, dst, &a, &b, &c, 0);
		error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_FIXED);
	}

	/* The temporaries go back. */
	for (axis = 0U; axis < 3U; axis++)
		nvrtx_sm75_give(lowering, ids[axis]);

	/* Reports an emission past the code's room. */
	if (error != 0)
		return error;

	/* Succeeded: the index is in dst. */
	return 0;
}

/*
 * Lowers a storage buffer access: the buffer's address from the root
 * constant buffer plus the byte offset, then a global load or store, under
 * P0 when the access is predicated; or the buffer's size.
 */
static int
nvrtx_sm75_lower_storage(
	struct nvrtx_sm75_lowering *lowering,
	const struct drv_gpu_shader_ir_inst *instruction,
	uint32_t dst)
{
	struct nvrtx_sm75_word word;
	struct nvrtx_sm75_src a;
	struct nvrtx_sm75_src zero;
	struct nvrtx_sm75_src size;
	uint32_t slot;
	uint32_t address;
	uint32_t guard;
	int error;

	/* The access names a storage buffer of the list. */
	if (instruction->location >= lowering->ir->uniform_count || lowering->ir->uniforms[instruction->location].kind != DRV_GPU_IR_UNIFORM_STORAGE)
		return nvrtx_sm75_refuse(lowering, EINVAL, "a storage access names no storage buffer");
	slot = lowering->slot[instruction->location];

	/* The size is a word of the buffer's entry. */
	if (instruction->op == DRV_GPU_IR_STORAGE_SIZE) {
		size = nvrtx_sm75_cb(NVRTX_SM75_CB_ROOT, NVRTX_SM75_CB_STORAGE + slot * NVRTX_SM75_CB_STORAGE_ENTRY + NVRTX_SM75_CB_STORAGE_SIZE);
		nvrtx_sm75_begin(&word);
		nvrtx_sm75_mov(&word, dst, &size);
		error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_FIXED);
		if (error != 0)
			return error;
		return 0;
	}

	/* The address of the word. */
	error = nvrtx_sm75_storage_address(lowering, slot, lowering->reg[instruction->src[0]], &address);
	if (error != 0)
		return error;

	/* P0 from the predicate of a predicated access. */
	guard = NVRTX_SM75_PT;
	if (instruction->component != 0U) {
		guard = instruction->src[1];
		if (instruction->op == DRV_GPU_IR_STORE_STORAGE)
			guard = instruction->src[2];
		a = nvrtx_sm75_value(lowering, guard);
		zero = nvrtx_sm75_reg(NVRTX_SM75_RZ);
		nvrtx_sm75_begin(&word);
		nvrtx_sm75_isetp(&word, NVRTX_SM75_P0, NVRTX_SM75_ICMP_NE, 0, &a, &zero);
		error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_FIXED_PREDICATE);
		guard = NVRTX_SM75_P0;
	}

	/* The load or the store, guarded. */
	if (error == 0) {
		nvrtx_sm75_begin(&word);
		if (instruction->op == DRV_GPU_IR_LOAD_STORAGE) {
			nvrtx_sm75_ldg(&word, dst, address);
		} else {
			nvrtx_sm75_stg(&word, address, lowering->reg[instruction->src[1]]);
		}
		nvrtx_sm75_guard(&word, guard, 0);

		/* A load writes a result; a store only reads its sources. */
		if (instruction->op == DRV_GPU_IR_LOAD_STORAGE) {
			error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_VARIABLE_WRITE);
		} else {
			error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_VARIABLE_STORE);
		}
	}

	/* The address pair goes back. */
	nvrtx_sm75_give(lowering, address);
	nvrtx_sm75_give(lowering, address + 1U);

	/* Reports an emission past the code's room. */
	if (error != 0)
		return error;

	/* Succeeded: the access is made. */
	return 0;
}

/*
 * Lowers a loop's ends.  The threads of a warp may leave a loop at
 * different passes, so the loop is a convergence region: LOOP_BEGIN is a
 * BSSY whose barrier collects the threads at the BSYNC after the loop, and
 * LOOP_END branches back to the loop's first instruction for the threads
 * whose Boolean holds; the others go on to the BSYNC and wait there.
 *
 * Procedure referenced: Mesa 25.3.6 src/nouveau/compiler/nak_nir_lower_cf.c
 * (lower_cf_list: the loop's break scope, push_scope and pop_scope) and
 * src/nouveau/compiler/nak/from_nir.rs (ShaderFromNir::parse_intrinsic: the
 * OpBSSy of bar_set_nv and the OpBSync of bar_sync_nv).
 */
static int
nvrtx_sm75_lower_loop(
	struct nvrtx_sm75_lowering *lowering,
	const struct drv_gpu_shader_ir_inst *instruction)
{
	struct nvrtx_sm75_word word;
	uint32_t *slot;
	uint32_t depth;
	uint32_t bssy;
	int32_t back;
	int error;

	/* LOOP_BEGIN: the BSSY (its target is set at the loop's end), and the loop starts after it. */
	if (instruction->op == DRV_GPU_IR_LOOP_BEGIN) {
		depth = lowering->loop_depth;
		if (depth == NVRTX_SM75_LOOP_DEPTH)
			return nvrtx_sm75_refuse(lowering, ENOTSUP, "loops nested deeper than the convergence barriers");

		nvrtx_sm75_begin(&word);
		nvrtx_sm75_bssy(&word, depth, 0);
		lowering->loop_bssy[depth] = lowering->count;
		error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_CONTROL);
		if (error != 0)
			return error;

		lowering->loop_start[depth] = lowering->count;
		lowering->loop_depth++;
		return 0;
	}

	/* LOOP_END closes the innermost loop. */
	if (lowering->loop_depth == 0U)
		return nvrtx_sm75_refuse(lowering, EINVAL, "a loop's end without its beginning");
	lowering->loop_depth--;
	depth = lowering->loop_depth;

	/* P0: the thread runs the body again. */
	error = nvrtx_sm75_predicate(lowering, instruction->src[0]);
	if (error != 0)
		return error;

	/* Back to the loop's first instruction where P0 holds. */
	back = (int32_t)lowering->loop_start[depth] - (int32_t)(lowering->count + 1U);
	nvrtx_sm75_begin(&word);
	nvrtx_sm75_bra(&word, back);
	nvrtx_sm75_bra_if(&word, NVRTX_SM75_P0, 0);
	error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_CONTROL);
	if (error != 0)
		return error;

	/* The BSYNC the loop's threads meet at, and the BSSY's target made its address. */
	bssy = lowering->loop_bssy[depth];
	nvrtx_sm75_begin(&word);
	nvrtx_sm75_bsync(&word, depth);
	slot = lowering->binary->code + (size_t)bssy * NVRTX_SM75_INSTRUCTION_WORDS;
	error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_CONTROL);
	if (error != 0)
		return error;

	/*
	 * The BSSY's offset: from the instruction after the BSSY to the
	 * BSYNC, in 32-bit words.
	 *
	 * Value referenced: Mesa 25.3.6 src/nouveau/compiler/nak/sm70_encode.rs
	 * (SM70Encoder::get_rel_offset: the target's place minus the next
	 * instruction's, in 32-bit words).
	 */
	kern_memcpy(word.w, slot, sizeof(word.w));
	nvrtx_sm75_set(&word, NVRTX_SM75_BSSY_OFFSET_LOW, NVRTX_SM75_BSSY_OFFSET_BITS, (uint64_t)((int64_t)(lowering->count - 1U - (bssy + 1U)) * NVRTX_SM75_INSTRUCTION_WORDS));
	kern_memcpy(slot, word.w, sizeof(word.w));

	/* Succeeded: the loop is closed. */
	return 0;
}

/*
 * Lowers the integer division and remainder.  The unsigned ones use the
 * float reciprocal of the divisor scaled to 32-bit fixed point, refined
 * once, then a quotient corrected at most twice; the signed ones divide the
 * magnitudes and give the quotient the sign of the operands' product and
 * the remainder the dividend's sign (SPIR-V OpSDiv and OpSRem).
 *
 * Procedure referenced: Mesa 25.0.7 src/compiler/nir/nir_lower_idiv.c
 * (emit_idiv: the magnitudes divided, and the sign of the quotient from the
 * operands' exclusive or).
 */
static int
nvrtx_sm75_lower_divide(
	struct nvrtx_sm75_lowering *lowering,
	const struct drv_gpu_shader_ir_inst *instruction,
	uint32_t dst)
{
	struct nvrtx_sm75_word word;
	struct nvrtx_sm75_src a;
	struct nvrtx_sm75_src b;
	struct nvrtx_sm75_src zero;
	uint32_t numerator;
	uint32_t denominator;
	uint32_t magnitude_n;
	uint32_t magnitude_d;
	uint32_t other;
	uint32_t sign;
	int is_signed;
	int error;

	numerator = lowering->reg[instruction->src[0]];
	denominator = lowering->reg[instruction->src[1]];
	is_signed = 0;
	if (instruction->op == DRV_GPU_IR_IDIV || instruction->op == DRV_GPU_IR_IREM)
		is_signed = 1;

	/* The unsigned ones: the quotient or the remainder into dst, the other into a temporary. */
	if (is_signed == 0) {
		error = nvrtx_sm75_take(lowering, &other);
		if (error != 0)
			return error;
		if (instruction->op == DRV_GPU_IR_UDIV) {
			error = nvrtx_sm75_divide_unsigned(lowering, numerator, denominator, dst, other);
		} else {
			error = nvrtx_sm75_divide_unsigned(lowering, numerator, denominator, other, dst);
		}
		nvrtx_sm75_give(lowering, other);
		if (error != 0)
			return error;
		return 0;
	}

	/* The signed ones: the magnitudes first. */
	error = nvrtx_sm75_take(lowering, &magnitude_n);
	if (error != 0)
		return error;
	error = nvrtx_sm75_take(lowering, &magnitude_d);
	if (error != 0) {
		nvrtx_sm75_give(lowering, magnitude_n);
		return error;
	}
	error = nvrtx_sm75_take(lowering, &other);
	if (error != 0) {
		nvrtx_sm75_give(lowering, magnitude_n);
		nvrtx_sm75_give(lowering, magnitude_d);
		return error;
	}

	error = nvrtx_sm75_absolute(lowering, numerator, magnitude_n);
	if (error == 0)
		error = nvrtx_sm75_absolute(lowering, denominator, magnitude_d);

	/* The unsigned division of the magnitudes. */
	if (error == 0 && instruction->op == DRV_GPU_IR_IDIV)
		error = nvrtx_sm75_divide_unsigned(lowering, magnitude_n, magnitude_d, dst, other);
	if (error == 0 && instruction->op == DRV_GPU_IR_IREM)
		error = nvrtx_sm75_divide_unsigned(lowering, magnitude_n, magnitude_d, other, dst);

	/* P0: the result is negative (the operands' signs differ for the quotient, the dividend is negative for the remainder). */
	zero = nvrtx_sm75_reg(NVRTX_SM75_RZ);
	sign = numerator;
	if (error == 0 && instruction->op == DRV_GPU_IR_IDIV) {
		a = nvrtx_sm75_reg(numerator);
		b = nvrtx_sm75_reg(denominator);
		nvrtx_sm75_begin(&word);
		nvrtx_sm75_lop3(&word, other, &a, &b, &zero, NVRTX_SM75_LUT_XOR);
		error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_FIXED);
		sign = other;
	}
	if (error == 0) {
		a = nvrtx_sm75_reg(sign);
		nvrtx_sm75_begin(&word);
		nvrtx_sm75_isetp(&word, NVRTX_SM75_P0, NVRTX_SM75_ICMP_LT, 1, &a, &zero);
		error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_FIXED_PREDICATE);
	}

	/* The result negated where P0 holds. */
	if (error == 0)
		error = nvrtx_sm75_negate_if_p0(lowering, dst);

	/* The temporaries go back. */
	nvrtx_sm75_give(lowering, magnitude_n);
	nvrtx_sm75_give(lowering, magnitude_d);
	nvrtx_sm75_give(lowering, other);

	/* Reports an emission past the code's room. */
	if (error != 0)
		return error;

	/* Succeeded: the result is in dst. */
	return 0;
}

/*
 * Divides two unsigned 32-bit registers: the quotient and the remainder
 * into their registers (both written, neither a source):
 *
 *   r = f2u(rcp(u2f(d)) * (2^32 - 512))          about 2^32 / d, below it
 *   r = r + hi(r * (-d * r))                      one refinement
 *   q = hi(n * r);  m = n - q * d
 *   twice: if (m >= d) { q = q + 1; m = m - d; }
 *
 * Procedure referenced: Mesa 25.0.7 src/compiler/nir/nir_lower_idiv.c
 * (emit_udiv).
 */
static int
nvrtx_sm75_divide_unsigned(
	struct nvrtx_sm75_lowering *lowering,
	uint32_t numerator,
	uint32_t denominator,
	uint32_t quotient,
	uint32_t remainder)
{
	struct nvrtx_sm75_word word;
	struct nvrtx_sm75_src a;
	struct nvrtx_sm75_src b;
	struct nvrtx_sm75_src c;
	struct nvrtx_sm75_src zero;
	uint32_t reciprocal;
	uint32_t scratch;
	uint32_t pair;
	unsigned step;
	int error;

	zero = nvrtx_sm75_reg(NVRTX_SM75_RZ);

	/* A register for the reciprocal, one for the products, and a pair for the high halves. */
	error = nvrtx_sm75_take(lowering, &reciprocal);
	if (error != 0)
		return error;
	error = nvrtx_sm75_take(lowering, &scratch);
	if (error != 0) {
		nvrtx_sm75_give(lowering, reciprocal);
		return error;
	}
	error = nvrtx_sm75_take_pair(lowering, &pair);
	if (error != 0) {
		nvrtx_sm75_give(lowering, reciprocal);
		nvrtx_sm75_give(lowering, scratch);
		return error;
	}

	/* The float reciprocal of the divisor, scaled and made an integer. */
	nvrtx_sm75_begin(&word);
	nvrtx_sm75_i2f(&word, scratch, denominator, 0);
	error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_VARIABLE_WRITE);
	if (error == 0) {
		nvrtx_sm75_begin(&word);
		nvrtx_sm75_mufu(&word, scratch, NVRTX_SM75_MUFU_RCP, scratch);
		error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_VARIABLE_WRITE);
	}
	if (error == 0) {
		a = nvrtx_sm75_reg(scratch);
		b = nvrtx_sm75_imm(NVRTX_SM75_RECIPROCAL_SCALE);
		nvrtx_sm75_begin(&word);
		nvrtx_sm75_fmul(&word, scratch, &a, &b);
		error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_FIXED);
	}
	if (error == 0) {
		nvrtx_sm75_begin(&word);
		nvrtx_sm75_f2i(&word, reciprocal, scratch, 0);
		error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_VARIABLE_WRITE);
	}

	/* The refinement: -d * r, its product's high half with r, added to r. */
	if (error == 0) {
		a = nvrtx_sm75_reg(denominator);
		a.neg = 1;
		nvrtx_sm75_begin(&word);
		nvrtx_sm75_iadd3(&word, scratch, &a, &zero, &zero);
		error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_FIXED);
	}
	if (error == 0) {
		a = nvrtx_sm75_reg(scratch);
		b = nvrtx_sm75_reg(reciprocal);
		nvrtx_sm75_begin(&word);
		nvrtx_sm75_imad(&word, scratch, &a, &b, &zero, 0);
		error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_FIXED);
	}
	if (error == 0) {
		a = nvrtx_sm75_reg(reciprocal);
		b = nvrtx_sm75_reg(scratch);
		nvrtx_sm75_begin(&word);
		nvrtx_sm75_imad_wide(&word, pair, &a, &b, &zero);
		error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_FIXED);
	}
	if (error == 0) {
		a = nvrtx_sm75_reg(reciprocal);
		b = nvrtx_sm75_reg(pair + 1U);
		nvrtx_sm75_begin(&word);
		nvrtx_sm75_iadd3(&word, reciprocal, &a, &b, &zero);
		error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_FIXED);
	}

	/* The quotient: the high half of n * r. */
	if (error == 0) {
		a = nvrtx_sm75_reg(numerator);
		b = nvrtx_sm75_reg(reciprocal);
		nvrtx_sm75_begin(&word);
		nvrtx_sm75_imad_wide(&word, pair, &a, &b, &zero);
		error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_FIXED);
	}
	if (error == 0) {
		b = nvrtx_sm75_reg(pair + 1U);
		nvrtx_sm75_begin(&word);
		nvrtx_sm75_mov(&word, quotient, &b);
		error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_FIXED);
	}

	/* The remainder: n - q * d. */
	if (error == 0) {
		a = nvrtx_sm75_reg(quotient);
		b = nvrtx_sm75_reg(denominator);
		nvrtx_sm75_begin(&word);
		nvrtx_sm75_imad(&word, scratch, &a, &b, &zero, 0);
		error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_FIXED);
	}
	if (error == 0) {
		a = nvrtx_sm75_reg(numerator);
		b = nvrtx_sm75_reg(scratch);
		b.neg = 1;
		nvrtx_sm75_begin(&word);
		nvrtx_sm75_iadd3(&word, remainder, &a, &b, &zero);
		error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_FIXED);
	}

	/* The two corrections, each where the remainder is still the divisor or more. */
	for (step = 0U; error == 0 && step < 2U; step++) {
		a = nvrtx_sm75_reg(remainder);
		b = nvrtx_sm75_reg(denominator);
		nvrtx_sm75_begin(&word);
		nvrtx_sm75_isetp(&word, NVRTX_SM75_P0, NVRTX_SM75_ICMP_GE, 0, &a, &b);
		error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_FIXED_PREDICATE);
		if (error != 0)
			break;

		a = nvrtx_sm75_reg(quotient);
		c = nvrtx_sm75_imm(1U);
		nvrtx_sm75_begin(&word);
		nvrtx_sm75_iadd3(&word, quotient, &a, &c, &zero);
		nvrtx_sm75_guard(&word, NVRTX_SM75_P0, 0);
		error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_FIXED);
		if (error != 0)
			break;

		a = nvrtx_sm75_reg(remainder);
		b = nvrtx_sm75_reg(denominator);
		b.neg = 1;
		nvrtx_sm75_begin(&word);
		nvrtx_sm75_iadd3(&word, remainder, &a, &b, &zero);
		nvrtx_sm75_guard(&word, NVRTX_SM75_P0, 0);
		error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_FIXED);
	}

	/* The temporaries go back. */
	nvrtx_sm75_give(lowering, reciprocal);
	nvrtx_sm75_give(lowering, scratch);
	nvrtx_sm75_give(lowering, pair);
	nvrtx_sm75_give(lowering, pair + 1U);

	/* Reports an emission past the code's room. */
	if (error != 0)
		return error;

	/* Succeeded: the quotient and the remainder are in their registers. */
	return 0;
}

/* Writes the magnitude of a signed register into another: -value where it is negative. */
static int
nvrtx_sm75_absolute(
	struct nvrtx_sm75_lowering *lowering,
	uint32_t value,
	uint32_t result)
{
	struct nvrtx_sm75_word word;
	struct nvrtx_sm75_src a;
	struct nvrtx_sm75_src zero;
	int error;

	zero = nvrtx_sm75_reg(NVRTX_SM75_RZ);

	/* P0: the value is negative. */
	a = nvrtx_sm75_reg(value);
	nvrtx_sm75_begin(&word);
	nvrtx_sm75_isetp(&word, NVRTX_SM75_P0, NVRTX_SM75_ICMP_LT, 1, &a, &zero);
	error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_FIXED_PREDICATE);
	if (error != 0)
		return error;

	/* The value, then its negation where P0 holds. */
	nvrtx_sm75_begin(&word);
	nvrtx_sm75_mov(&word, result, &a);
	error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_FIXED);
	if (error != 0)
		return error;

	error = nvrtx_sm75_negate_if_p0(lowering, result);
	if (error != 0)
		return error;

	/* Succeeded: the magnitude is in result. */
	return 0;
}

/* Negates a register in place where P0 holds. */
static int
nvrtx_sm75_negate_if_p0(
	struct nvrtx_sm75_lowering *lowering,
	uint32_t value)
{
	struct nvrtx_sm75_word word;
	struct nvrtx_sm75_src a;
	struct nvrtx_sm75_src zero;
	int error;

	zero = nvrtx_sm75_reg(NVRTX_SM75_RZ);

	/* @P0 IADD3 value, -value, RZ, RZ. */
	a = nvrtx_sm75_reg(value);
	a.neg = 1;
	nvrtx_sm75_begin(&word);
	nvrtx_sm75_iadd3(&word, value, &a, &zero, &zero);
	nvrtx_sm75_guard(&word, NVRTX_SM75_P0, 0);
	error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_FIXED);
	if (error != 0)
		return error;

	/* Succeeded: the register is negated where P0 holds. */
	return 0;
}

/*
 * Lowers an atomic on a storage buffer word (its address from the table)
 * or on a shared memory word (its offset), returning the old value; a
 * subtraction adds the negated value, a compare and exchange writes src[1]
 * where the word equals src[2].  A predicated one runs under P0.
 */
static int
nvrtx_sm75_lower_atomic(
	struct nvrtx_sm75_lowering *lowering,
	const struct drv_gpu_shader_ir_inst *instruction,
	uint32_t dst)
{
	struct nvrtx_sm75_word word;
	struct nvrtx_sm75_src a;
	struct nvrtx_sm75_src zero;
	uint32_t address;
	uint32_t data;
	uint32_t negated;
	uint32_t operation;
	uint32_t predicate;
	int is_signed;
	int shared;
	int error;

	/* The operation and its type. */
	is_signed = 0;
	switch (instruction->immediate) {
	case DRV_GPU_IR_ATOMIC_ADD:
	case DRV_GPU_IR_ATOMIC_SUB:
		operation = NVRTX_SM75_ATOM_ADD;
		break;
	case DRV_GPU_IR_ATOMIC_AND:
		operation = NVRTX_SM75_ATOM_AND;
		break;
	case DRV_GPU_IR_ATOMIC_OR:
		operation = NVRTX_SM75_ATOM_OR;
		break;
	case DRV_GPU_IR_ATOMIC_XOR:
		operation = NVRTX_SM75_ATOM_XOR;
		break;
	case DRV_GPU_IR_ATOMIC_XCHG:
	case DRV_GPU_IR_ATOMIC_CMPXCHG:
		operation = NVRTX_SM75_ATOM_EXCH;
		break;
	case DRV_GPU_IR_ATOMIC_SMIN:
		operation = NVRTX_SM75_ATOM_MIN;
		is_signed = 1;
		break;
	case DRV_GPU_IR_ATOMIC_SMAX:
		operation = NVRTX_SM75_ATOM_MAX;
		is_signed = 1;
		break;
	case DRV_GPU_IR_ATOMIC_UMIN:
		operation = NVRTX_SM75_ATOM_MIN;
		break;
	case DRV_GPU_IR_ATOMIC_UMAX:
		operation = NVRTX_SM75_ATOM_MAX;
		break;
	default:
		return nvrtx_sm75_refuse(lowering, EINVAL, "an atomic operation the IR does not define");
	}

	/* Where the word is: shared memory by its offset, or a storage buffer's word by its address. */
	shared = 0;
	if (instruction->location == DRV_GPU_IR_LOCATION_SHARED)
		shared = 1;
	address = lowering->reg[instruction->src[0]];
	if (shared == 0) {
		if (instruction->location >= lowering->ir->uniform_count || lowering->ir->uniforms[instruction->location].kind != DRV_GPU_IR_UNIFORM_STORAGE)
			return nvrtx_sm75_refuse(lowering, EINVAL, "an atomic names no storage buffer");
		error = nvrtx_sm75_storage_address(lowering, lowering->slot[instruction->location], address, &address);
		if (error != 0)
			return error;
	}

	/* A subtraction adds the negated value. */
	data = lowering->reg[instruction->src[1]];
	negated = NVRTX_SM75_RZ;
	error = 0;
	if (instruction->immediate == DRV_GPU_IR_ATOMIC_SUB) {
		error = nvrtx_sm75_take(lowering, &negated);
		if (error == 0) {
			a = nvrtx_sm75_reg(data);
			a.neg = 1;
			zero = nvrtx_sm75_reg(NVRTX_SM75_RZ);
			nvrtx_sm75_begin(&word);
			nvrtx_sm75_iadd3(&word, negated, &a, &zero, &zero);
			error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_FIXED);
			data = negated;
		}
	}

	/* P0 from the predicate, which follows the values. */
	if (error == 0 && instruction->component == DRV_GPU_IR_ATOMIC_PREDICATED) {
		predicate = instruction->src[2];
		if (instruction->immediate == DRV_GPU_IR_ATOMIC_CMPXCHG)
			predicate = instruction->src[3];
		error = nvrtx_sm75_predicate(lowering, predicate);
	}

	/* The atomic. */
	if (error == 0) {
		nvrtx_sm75_begin(&word);
		if (instruction->immediate == DRV_GPU_IR_ATOMIC_CMPXCHG) {
			nvrtx_sm75_atomic_cas(&word, shared, dst, address, lowering->reg[instruction->src[2]], data);
		} else {
			nvrtx_sm75_atomic(&word, shared, dst, address, data, operation, is_signed);
		}
		if (instruction->component == DRV_GPU_IR_ATOMIC_PREDICATED)
			nvrtx_sm75_guard(&word, NVRTX_SM75_P0, 0);
		error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_VARIABLE_WRITE);
	}

	/* The temporaries go back. */
	if (negated != NVRTX_SM75_RZ)
		nvrtx_sm75_give(lowering, negated);
	if (shared == 0) {
		nvrtx_sm75_give(lowering, address);
		nvrtx_sm75_give(lowering, address + 1U);
	}

	/* Reports an emission past the code's room. */
	if (error != 0)
		return error;

	/* Succeeded: the old value is in dst. */
	return 0;
}

/* Lowers a load or a store of a shared memory word, under P0 when predicated. */
static int
nvrtx_sm75_lower_shared(
	struct nvrtx_sm75_lowering *lowering,
	const struct drv_gpu_shader_ir_inst *instruction,
	uint32_t dst)
{
	struct nvrtx_sm75_word word;
	uint32_t predicate;
	uint32_t offset;
	int error;

	offset = lowering->reg[instruction->src[0]];

	/* P0 from the predicate of a predicated access. */
	if (instruction->component != 0U) {
		predicate = instruction->src[1];
		if (instruction->op == DRV_GPU_IR_STORE_SHARED)
			predicate = instruction->src[2];
		error = nvrtx_sm75_predicate(lowering, predicate);
		if (error != 0)
			return error;
	}

	/* The load or the store. */
	nvrtx_sm75_begin(&word);
	if (instruction->op == DRV_GPU_IR_LOAD_SHARED) {
		nvrtx_sm75_lds(&word, dst, offset);
	} else {
		nvrtx_sm75_sts(&word, offset, lowering->reg[instruction->src[1]]);
	}
	if (instruction->component != 0U)
		nvrtx_sm75_guard(&word, NVRTX_SM75_P0, 0);

	/* A load writes a result; a store only reads its sources. */
	if (instruction->op == DRV_GPU_IR_LOAD_SHARED) {
		error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_VARIABLE_WRITE);
	} else {
		error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_VARIABLE_STORE);
	}
	if (error != 0)
		return error;

	/* Succeeded: the access is made. */
	return 0;
}

/*
 * Lowers a workgroup barrier (after the memory barrier of global memory it
 * names; BAR.SYNC itself orders the workgroup's shared memory) and a memory
 * fence (MEMBAR at the GPU's scope for global memory, the workgroup's
 * otherwise).  Each is followed by a NOP of two cycles.
 *
 * Value referenced: the SM75 code the CUDA compiler emits (a NOP of two
 * cycles after an instruction with an execution latency), and Mesa 25.3.6
 * src/nouveau/compiler/nak/calc_instr_deps.rs (calc_delays: the NOP with a
 * delay of 2 after an instruction whose execution latency is above 1) and
 * src/nouveau/compiler/nak/sm70.rs (ShaderModel70::exec_latency: BAR and
 * MEMBAR).
 *
 * Procedure referenced: Mesa 25.3.6 src/nouveau/compiler/nak/from_nir.rs
 * (ShaderFromNir::parse_intrinsic, nir_intrinsic_barrier: the MEMBAR's
 * scope, the GPU's for global memory and the workgroup's otherwise).
 */
static int
nvrtx_sm75_lower_sync(
	struct nvrtx_sm75_lowering *lowering,
	const struct drv_gpu_shader_ir_inst *instruction)
{
	struct nvrtx_sm75_word word;
	uint32_t scope;
	int membar;
	int error;

	/* Whether a MEMBAR comes, and its scope. */
	scope = NVRTX_SM75_MEMBAR_CTA;
	if ((instruction->immediate & DRV_GPU_IR_FENCE_GLOBAL) != 0U)
		scope = NVRTX_SM75_MEMBAR_GPU;
	membar = 1;
	if (instruction->op == DRV_GPU_IR_BARRIER && scope != NVRTX_SM75_MEMBAR_GPU)
		membar = 0;

	/* The MEMBAR and its NOP. */
	if (membar != 0) {
		nvrtx_sm75_begin(&word);
		nvrtx_sm75_membar(&word, scope);
		error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_SYNC);
		if (error != 0)
			return error;

		nvrtx_sm75_begin(&word);
		nvrtx_sm75_nop(&word);
		error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_FIXED);
		if (error != 0)
			return error;
	}

	/* A fence is only the MEMBAR. */
	if (instruction->op == DRV_GPU_IR_FENCE)
		return 0;

	/* The barrier and its NOP. */
	nvrtx_sm75_begin(&word);
	nvrtx_sm75_bar(&word);
	error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_SYNC);
	if (error != 0)
		return error;

	nvrtx_sm75_begin(&word);
	nvrtx_sm75_nop(&word);
	error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_FIXED);
	if (error != 0)
		return error;

	/* Succeeded: the workgroup has met. */
	return 0;
}

/* Sets P0 from a Boolean value: it is not zero. */
static int
nvrtx_sm75_predicate(
	struct nvrtx_sm75_lowering *lowering,
	uint32_t value)
{
	struct nvrtx_sm75_word word;
	struct nvrtx_sm75_src a;
	struct nvrtx_sm75_src zero;
	int error;

	a = nvrtx_sm75_value(lowering, value);
	zero = nvrtx_sm75_reg(NVRTX_SM75_RZ);

	/* ISETP.NE.U32.AND P0, PT, value, RZ, PT. */
	nvrtx_sm75_begin(&word);
	nvrtx_sm75_isetp(&word, NVRTX_SM75_P0, NVRTX_SM75_ICMP_NE, 0, &a, &zero);
	error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_FIXED_PREDICATE);
	if (error != 0)
		return error;

	/* Succeeded: P0 holds the Boolean. */
	return 0;
}

/*
 * Makes every value a loop reads but that was made before the loop live
 * to the loop's end, since the next pass reads it again.  Checks that the
 * loops are nested properly.
 */
static int
nvrtx_sm75_extend_loops(
	struct nvrtx_sm75_lowering *lowering)
{
	const struct drv_gpu_shader_ir *ir;
	const struct drv_gpu_shader_ir_inst *instruction;
	uint32_t *first_def;
	uint32_t begins[NVRTX_SM75_LOOP_DEPTH];
	uint32_t sources[NVRTX_SM75_MAX_SOURCES];
	uint32_t depth;
	uint32_t begin;
	uint32_t index;
	uint32_t inner;
	uint32_t source;
	int defines;
	int count;

	ir = lowering->ir;

	/* Each value's first definition. */
	first_def = kern_calloc(ir->value_count + 1U, sizeof(*first_def));
	if (first_def == NULL)
		return ENOMEM;
	for (index = 0U; index <= ir->value_count; index++)
		first_def[index] = NVRTX_SM75_NOT_DEFINED;
	for (index = 0U; index < ir->instruction_count; index++) {
		instruction = &ir->instructions[index];
		defines = nvrtx_sm75_defines(instruction);
		if (defines != 0 && first_def[instruction->dst] == NVRTX_SM75_NOT_DEFINED)
			first_def[instruction->dst] = index;
	}

	/* Each loop, when its end is reached: the earlier values its body reads live to the end. */
	depth = 0U;
	for (index = 0U; index < ir->instruction_count; index++) {
		instruction = &ir->instructions[index];
		if (instruction->op == DRV_GPU_IR_LOOP_BEGIN) {
			if (depth == NVRTX_SM75_LOOP_DEPTH) {
				kern_free(first_def);
				lowering->index = index;
				return nvrtx_sm75_refuse(lowering, ENOTSUP, "loops nested deeper than the convergence barriers");
			}
			begins[depth] = index;
			depth++;
			continue;
		}
		if (instruction->op != DRV_GPU_IR_LOOP_END)
			continue;
		if (depth == 0U) {
			kern_free(first_def);
			lowering->index = index;
			return nvrtx_sm75_refuse(lowering, EINVAL, "a loop's end without its beginning");
		}
		depth--;
		begin = begins[depth];

		for (inner = begin + 1U; inner <= index; inner++) {
			count = nvrtx_sm75_sources(&ir->instructions[inner], sources);
			for (source = 0U; count > 0 && source < (uint32_t)count; source++) {
				if (first_def[sources[source]] >= begin)
					continue;
				if (lowering->last_use[sources[source]] < index + 1U)
					lowering->last_use[sources[source]] = index + 1U;
			}
		}
	}
	kern_free(first_def);

	/* Every loop must end. */
	if (depth != 0U)
		return nvrtx_sm75_refuse(lowering, EINVAL, "a loop without its end");

	/* Succeeded: the values live through their loops. */
	return 0;
}

/*
 * Prepares a graphics program: a fragment program's colour outputs take
 * R0 onward, four registers for each location it writes in ascending
 * order (the registers the hardware reads at EXIT), which no value may
 * take; a vertex program's store request starts empty.
 *
 * Procedure referenced: Mesa 25.3.6 src/nouveau/compiler/nak/from_nir.rs
 * (ShaderFromNir::parse_intrinsic, copy_fs_outputs_nv: the order of the
 * fragment outputs in OpRegOut, four for each location written, in
 * ascending order).
 */
static int
nvrtx_sm75_prepare_graphics(
	struct nvrtx_sm75_lowering *lowering)
{
	const struct drv_gpu_shader_ir *ir;
	const struct drv_gpu_shader_ir_inst *instruction;
	uint32_t written;
	uint32_t location;
	uint32_t index;
	uint32_t next;

	ir = lowering->ir;

	/* No colour location has registers yet, and nothing is stored. */
	for (location = 0U; location < NVRTX_SM75_COLOR_TARGETS; location++)
		lowering->color_reg[location] = NVRTX_SM75_NO_REG;
	lowering->inverse_w = NVRTX_SM75_NO_REG;
	lowering->store_first = 0xffU;
	lowering->store_last = 0U;

	/* Only a fragment program has colour outputs. */
	if (ir->stage != DRV_GPU_STAGE_FRAGMENT)
		return 0;

	/* The locations the program writes. */
	written = 0U;
	for (index = 0U; index < ir->instruction_count; index++) {
		instruction = &ir->instructions[index];
		if (instruction->op != DRV_GPU_IR_STORE_OUTPUT)
			continue;
		lowering->index = index;
		if (instruction->location >= NVRTX_SM75_COLOR_TARGETS || instruction->component > 3U)
			return nvrtx_sm75_refuse(lowering, ENOTSUP, "a fragment output other than a colour (depth, second colour)");
		written |= 1U << instruction->location;
	}
	lowering->index = 0U;

	/* Four registers for each, from R0, which stay taken. */
	next = 0U;
	for (location = 0U; location < NVRTX_SM75_COLOR_TARGETS; location++) {
		if ((written & (1U << location)) == 0U)
			continue;
		lowering->color_reg[location] = next;
		for (index = 0U; index < 4U; index++)
			lowering->taken[next + index] = 1U;
		next += 4U;
	}
	if (next > lowering->highest)
		lowering->highest = next;

	/* Succeeded: the outputs have their registers. */
	return 0;
}

/*
 * Starts a fragment program that interpolates in perspective: w at the
 * pixel (the position's w, passed screen-linear) and its reciprocal, which
 * every perspective input is multiplied by.
 *
 * Procedure referenced: Mesa 25.3.6
 * src/nouveau/compiler/nak_nir_lower_fs_inputs.c (load_frag_w and
 * lower_fs_input_intrin: the SM70 interpolation, w passed screen-linear,
 * then 1 / w).
 */
static int
nvrtx_sm75_prologue(
	struct nvrtx_sm75_lowering *lowering)
{
	const struct drv_gpu_shader_ir *ir;
	const struct drv_gpu_shader_ir_inst *instruction;
	const struct drv_gpu_shader_ir_io *input;
	struct nvrtx_sm75_word word;
	uint32_t index;
	int perspective;
	int error;

	ir = lowering->ir;

	/* Only a fragment program interpolates. */
	if (ir->stage != DRV_GPU_STAGE_FRAGMENT)
		return 0;

	/* Whether any input is read in perspective. */
	perspective = 0;
	for (index = 0U; index < ir->instruction_count; index++) {
		instruction = &ir->instructions[index];
		if (instruction->op != DRV_GPU_IR_LOAD_INPUT || instruction->location >= 64U)
			continue;
		input = nvrtx_sm75_input(ir, instruction->location);
		if (input != NULL && input->flat == 0U && input->noperspective == 0U)
			perspective = 1;
	}
	if (perspective == 0)
		return 0;

	/* w, then 1 / w in a register that stays taken. */
	error = nvrtx_sm75_take(lowering, &lowering->inverse_w);
	if (error != 0)
		return error;

	nvrtx_sm75_begin(&word);
	nvrtx_sm75_ipa(&word, lowering->inverse_w, NVRTX_SM75_ATTR_POSITION_W, NVRTX_SM75_IPA_PASS);
	error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_VARIABLE_WRITE);
	if (error != 0)
		return error;

	nvrtx_sm75_begin(&word);
	nvrtx_sm75_mufu(&word, lowering->inverse_w, NVRTX_SM75_MUFU_RCP, lowering->inverse_w);
	error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_VARIABLE_WRITE);
	if (error != 0)
		return error;

	/* Succeeded: 1 / w is ready. */
	nvrtx_sm75_sph_set(lowering->binary->sph, NVRTX_SM75_SPH_IMAP_AB_LOW + NVRTX_SM75_ATTR_POSITION_W / 4U, 1U, 1U);
	return 0;
}

/*
 * Lowers an input: a vertex program's attribute word (ALD), its vertex or
 * instance number; a fragment program's interpolated word (IPA, in
 * perspective multiplied by 1 / w), or the pixel's position.  The
 * header's input map records each.
 *
 * Procedure referenced: Mesa 25.3.6
 * src/nouveau/compiler/nak_nir_lower_fs_inputs.c (interp_fs_input: from
 * SM70 on, an input in perspective is passed, then multiplied by 1 / w).
 */
static int
nvrtx_sm75_lower_input(
	struct nvrtx_sm75_lowering *lowering,
	const struct drv_gpu_shader_ir_inst *instruction,
	uint32_t dst)
{
	const struct drv_gpu_shader_ir_io *input;
	struct nvrtx_sm75_word word;
	struct nvrtx_sm75_src a;
	struct nvrtx_sm75_src b;
	uint32_t *sph;
	uint32_t address;
	uint32_t slot;
	uint32_t mode;
	int error;

	sph = lowering->binary->sph;
	if (instruction->component > 3U)
		return nvrtx_sm75_refuse(lowering, EINVAL, "an input component past the fourth");

	/*
	 * A vertex program: a generic attribute, the vertex number or the
	 * instance number.
	 *
	 * Values referenced: Mesa 25.3.6 src/nouveau/compiler/nak/ir.rs
	 * (VtgIoInfo::mark_attrs: a bit of the input map for each word, the
	 * generic words from 0x080 and the C part of the system values from
	 * 0x2c0).
	 */
	if (lowering->ir->stage == DRV_GPU_STAGE_VERTEX) {
		if (instruction->location < 32U) {
			address = NVRTX_SM75_ATTR_GENERIC + instruction->location * 16U + instruction->component * 4U;
			nvrtx_sm75_sph_set(sph, NVRTX_SM75_SPH_IMAP_GENERIC_LOW + (address - NVRTX_SM75_ATTR_GENERIC) / 4U, 1U, 1U);
		} else if (instruction->location == DRV_GPU_SHADER_LOCATION_VERTEX_INDEX) {
			address = NVRTX_SM75_ATTR_VERTEX_ID;
			nvrtx_sm75_sph_set(sph, NVRTX_SM75_SPH_IMAP_C_VTG_LOW + (address - 0x2c0U) / 4U, 1U, 1U);
		} else if (instruction->location == DRV_GPU_SHADER_LOCATION_INSTANCE_INDEX) {
			address = NVRTX_SM75_ATTR_INSTANCE_ID;
			nvrtx_sm75_sph_set(sph, NVRTX_SM75_SPH_IMAP_C_VTG_LOW + (address - 0x2c0U) / 4U, 1U, 1U);
		} else {
			return nvrtx_sm75_refuse(lowering, ENOTSUP, "a vertex input not compiled yet");
		}

		nvrtx_sm75_begin(&word);
		nvrtx_sm75_ald(&word, dst, address);
		error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_VARIABLE_WRITE);
		if (error != 0)
			return error;
		return 0;
	}

	/* Only the fragment stage reads interpolated inputs. */
	if (lowering->ir->stage != DRV_GPU_STAGE_FRAGMENT)
		return nvrtx_sm75_refuse(lowering, ENOTSUP, "inputs of this stage are not compiled yet");

	/* The pixel's position: screen-linear, passed. */
	if (instruction->location == DRV_GPU_SHADER_LOCATION_FRAG_COORD) {
		address = NVRTX_SM75_ATTR_POSITION + instruction->component * 4U;
		nvrtx_sm75_sph_set(sph, NVRTX_SM75_SPH_IMAP_AB_LOW + address / 4U, 1U, 1U);
		nvrtx_sm75_begin(&word);
		nvrtx_sm75_ipa(&word, dst, address, NVRTX_SM75_IPA_PASS);
		error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_VARIABLE_WRITE);
		if (error != 0)
			return error;
		return 0;
	}

	/*
	 * A located input and its interpolation.
	 *
	 * Values referenced: Mesa 25.3.6 src/nouveau/compiler/nak/ir.rs
	 * (FragmentIoInfo::mark_attr_read) and
	 * src/nouveau/compiler/nak/sph.rs
	 * (ShaderProgramHeader::set_imap_vector_ps: two bits of the input map
	 * for each generic word).
	 */
	input = NULL;
	if (instruction->location < 32U)
		input = nvrtx_sm75_input(lowering->ir, instruction->location);
	if (input == NULL)
		return nvrtx_sm75_refuse(lowering, ENOTSUP, "a fragment input not compiled yet");

	address = NVRTX_SM75_ATTR_GENERIC + instruction->location * 16U + instruction->component * 4U;
	slot = (address - NVRTX_SM75_ATTR_GENERIC) / 4U;
	mode = NVRTX_SM75_IMAP_PERSPECTIVE;
	if (input->noperspective != 0U)
		mode = NVRTX_SM75_IMAP_SCREEN_LINEAR;
	if (input->flat != 0U)
		mode = NVRTX_SM75_IMAP_CONSTANT;
	nvrtx_sm75_sph_set(sph, NVRTX_SM75_SPH_IMAP_GENERIC_LOW + slot * 2U, 2U, mode);

	/* The interpolation: a flat input is the provoking vertex's, the others are passed. */
	nvrtx_sm75_begin(&word);
	if (mode == NVRTX_SM75_IMAP_CONSTANT) {
		nvrtx_sm75_ipa(&word, dst, address, NVRTX_SM75_IPA_CONSTANT);
	} else {
		nvrtx_sm75_ipa(&word, dst, address, NVRTX_SM75_IPA_PASS);
	}
	error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_VARIABLE_WRITE);
	if (error != 0)
		return error;

	/* In perspective, times 1 / w. */
	if (mode == NVRTX_SM75_IMAP_PERSPECTIVE) {
		a = nvrtx_sm75_reg(dst);
		b = nvrtx_sm75_reg(lowering->inverse_w);
		nvrtx_sm75_begin(&word);
		nvrtx_sm75_fmul(&word, dst, &a, &b);
		error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_FIXED);
		if (error != 0)
			return error;
	}

	/* Succeeded: the input is in dst. */
	return 0;
}

/*
 * Lowers an output: a vertex program's attribute word (AST: the position,
 * the point size or a generic attribute), recorded in the header's output
 * map and store request; a fragment program's colour word, moved into the
 * register the hardware reads it from at EXIT and recorded in the header's
 * targets.
 */
static int
nvrtx_sm75_lower_output(
	struct nvrtx_sm75_lowering *lowering,
	const struct drv_gpu_shader_ir_inst *instruction)
{
	struct nvrtx_sm75_word word;
	struct nvrtx_sm75_src value;
	uint32_t *sph;
	uint32_t address;
	int error;

	sph = lowering->binary->sph;
	value = nvrtx_sm75_value(lowering, instruction->src[0]);
	if (instruction->component > 3U)
		return nvrtx_sm75_refuse(lowering, EINVAL, "an output component past the fourth");

	/*
	 * A fragment program: the colour's register.
	 *
	 * Values referenced: Mesa 25.3.6 src/nouveau/compiler/nak/from_nir.rs
	 * (ShaderFromNir::parse_intrinsic, copy_fs_outputs_nv: writes_color, a
	 * bit for each component of each location) and
	 * src/nouveau/compiler/nak/sph.rs
	 * (ShaderProgramHeader::set_omap_targets).
	 */
	if (lowering->ir->stage == DRV_GPU_STAGE_FRAGMENT) {
		nvrtx_sm75_begin(&word);
		nvrtx_sm75_mov(&word, lowering->color_reg[instruction->location] + instruction->component, &value);
		error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_FIXED);
		if (error != 0)
			return error;
		nvrtx_sm75_sph_set(sph, NVRTX_SM75_SPH_OMAP_TARGETS_LOW + instruction->location * 4U + instruction->component, 1U, 1U);
		return 0;
	}

	/* Only the vertex stage stores attributes. */
	if (lowering->ir->stage != DRV_GPU_STAGE_VERTEX)
		return nvrtx_sm75_refuse(lowering, ENOTSUP, "outputs of this stage are not compiled yet");

	/*
	 * The attribute's address and its bit of the output map.
	 *
	 * Values referenced: Mesa 25.3.6 src/nouveau/compiler/nak/ir.rs
	 * (VtgIoInfo::mark_attrs: a bit of the output map for each word, the
	 * system values below 0x080 and the generic words from 0x080).
	 */
	if (instruction->location == DRV_GPU_IR_LOCATION_POSITION) {
		address = NVRTX_SM75_ATTR_POSITION + instruction->component * 4U;
		nvrtx_sm75_sph_set(sph, NVRTX_SM75_SPH_OMAP_AB_LOW + address / 4U, 1U, 1U);
	} else if (instruction->location == DRV_GPU_IR_LOCATION_POINT_SIZE) {
		address = NVRTX_SM75_ATTR_POINT_SIZE;
		nvrtx_sm75_sph_set(sph, NVRTX_SM75_SPH_OMAP_AB_LOW + address / 4U, 1U, 1U);
	} else if (instruction->location < 32U) {
		address = NVRTX_SM75_ATTR_GENERIC + instruction->location * 16U + instruction->component * 4U;
		nvrtx_sm75_sph_set(sph, NVRTX_SM75_SPH_OMAP_GENERIC_LOW + (address - NVRTX_SM75_ATTR_GENERIC) / 4U, 1U, 1U);
	} else {
		return nvrtx_sm75_refuse(lowering, ENOTSUP, "a vertex output not compiled yet");
	}

	/* The store, which reads its data register later. */
	nvrtx_sm75_mark_store(lowering, address);
	nvrtx_sm75_begin(&word);
	nvrtx_sm75_ast(&word, address, value.value);
	error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_VARIABLE_STORE);
	if (error != 0)
		return error;

	/* Succeeded: the attribute is written. */
	return 0;
}

/*
 * Lowers a 2D texture operation: the coordinates into a pair, the level of
 * detail and the constant texel offset (when there are any) into the
 * register or the pair after, TEX (a sample) or TLD (a fetch) with the
 * image's handle from constant buffer 0, and the four channels into two
 * pairs that become the four results' registers.  The level of detail of a
 * sample is implicit in a fragment program, zero in a vertex one, or the
 * instruction's bias or level.
 *
 * Procedure referenced: Mesa 25.3.6 src/nouveau/compiler/nak_nir_lower_tex.c
 * (lower_tex: the coordinates in the first source, the level of detail and
 * then the offset in the second) and src/nouveau/compiler/nak/sm70_encode.rs
 * (SM70Op for OpTex, encode: two destination registers and two source
 * registers).
 */
static int
nvrtx_sm75_lower_sample(
	struct nvrtx_sm75_lowering *lowering,
	const struct drv_gpu_shader_ir_inst *instruction)
{
	struct nvrtx_sm75_texture_operands operands;
	struct nvrtx_sm75_word word;
	struct nvrtx_sm75_src value;
	struct nvrtx_sm75_binary *binary;
	uint32_t coordinates;
	uint32_t extra;
	uint32_t extra_count;
	uint32_t first;
	uint32_t second;
	uint32_t texture;
	uint32_t index;
	int error;

	binary = lowering->binary;

	/* The image's texture handle. */
	texture = NVRTX_SM75_NO_SLOT;
	for (index = 0U; index < binary->texture_count; index++) {
		if (binary->textures[index].set == instruction->location && binary->textures[index].binding == instruction->immediate)
			texture = index;
	}
	if (texture == NVRTX_SM75_NO_SLOT)
		return nvrtx_sm75_refuse(lowering, EINVAL, "a sample names no sampled image");

	/* The results must be new values. */
	for (index = 0U; index < 4U; index++) {
		if (lowering->reg[instruction->dst + index] != NVRTX_SM75_NO_REG)
			return nvrtx_sm75_refuse(lowering, EINVAL, "a sample's result defined before");
	}

	/* What the operation passes. */
	error = nvrtx_sm75_texture_operands(lowering, instruction, &operands);
	if (error != 0)
		return error;

	/* The registers after the coordinates: the level of detail, the offset, both as a pair, or none. */
	extra_count = 0U;
	if (operands.lod != NVRTX_SM75_NOT_DEFINED)
		extra_count++;
	if (operands.has_offset != 0)
		extra_count++;

	/* The coordinates' pair. */
	error = nvrtx_sm75_take_pair(lowering, &coordinates);
	if (error != 0)
		return error;

	/* The level of detail's and the offset's register or pair. */
	extra = NVRTX_SM75_RZ;
	if (extra_count == 1U)
		error = nvrtx_sm75_take(lowering, &extra);
	if (extra_count == 2U)
		error = nvrtx_sm75_take_pair(lowering, &extra);
	if (error != 0) {
		nvrtx_sm75_give(lowering, coordinates);
		nvrtx_sm75_give(lowering, coordinates + 1U);
		return error;
	}

	/* The results' two pairs. */
	error = nvrtx_sm75_take_pair(lowering, &first);
	if (error == 0) {
		error = nvrtx_sm75_take_pair(lowering, &second);
		if (error != 0) {
			nvrtx_sm75_give(lowering, first);
			nvrtx_sm75_give(lowering, first + 1U);
		}
	}
	if (error != 0) {
		nvrtx_sm75_give(lowering, coordinates);
		nvrtx_sm75_give(lowering, coordinates + 1U);
		nvrtx_sm75_give(lowering, extra);
		if (extra_count == 2U)
			nvrtx_sm75_give(lowering, extra + 1U);
		return error;
	}

	/* The coordinates. */
	value = nvrtx_sm75_value(lowering, operands.u);
	nvrtx_sm75_begin(&word);
	nvrtx_sm75_mov(&word, coordinates, &value);
	error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_FIXED);
	if (error == 0) {
		value = nvrtx_sm75_value(lowering, operands.v);
		nvrtx_sm75_begin(&word);
		nvrtx_sm75_mov(&word, coordinates + 1U, &value);
		error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_FIXED);
	}

	/* The level of detail, first after the coordinates. */
	index = 0U;
	if (error == 0 && operands.lod != NVRTX_SM75_NOT_DEFINED) {
		value = nvrtx_sm75_value(lowering, operands.lod);
		nvrtx_sm75_begin(&word);
		nvrtx_sm75_mov(&word, extra, &value);
		error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_FIXED);
		index = 1U;
	}

	/* The offset, after the level of detail. */
	if (error == 0 && operands.has_offset != 0) {
		value = nvrtx_sm75_imm(operands.offset);
		nvrtx_sm75_begin(&word);
		nvrtx_sm75_mov(&word, extra + index, &value);
		error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_FIXED);
	}

	/* The sample or the fetch. */
	if (error == 0) {
		nvrtx_sm75_begin(&word);
		nvrtx_sm75_tex(&word, first, second, coordinates, extra, operands.mode, NVRTX_SM75_CB_ROOT, NVRTX_SM75_CB_TEXTURES + texture * 4U);
		if (operands.fetch != 0)
			nvrtx_sm75_tex_fetch(&word);
		if (operands.has_offset != 0)
			nvrtx_sm75_tex_offset(&word);
		error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_VARIABLE_WRITE);
	}

	/* The operands go back; the results' registers are the four values'. */
	nvrtx_sm75_give(lowering, coordinates);
	nvrtx_sm75_give(lowering, coordinates + 1U);
	nvrtx_sm75_give(lowering, extra);
	if (extra_count == 2U)
		nvrtx_sm75_give(lowering, extra + 1U);
	lowering->reg[instruction->dst] = (uint16_t)first;
	lowering->reg[instruction->dst + 1U] = (uint16_t)(first + 1U);
	lowering->reg[instruction->dst + 2U] = (uint16_t)second;
	lowering->reg[instruction->dst + 3U] = (uint16_t)(second + 1U);
	if (error != 0)
		return error;

	/* Succeeded: the channels are in the results' registers. */
	return 0;
}

/*
 * Finds what a texture operation passes: its coordinates, its level of
 * detail value and mode, whether it fetches, and its texel offset packed as
 * TEX takes it (u, v and r in four bits each).  A 2D sample of its own
 * operation keeps its offset as u in bits 11..8 and v in 7..4 of the
 * component; the general message keeps the message in bits 4..0 and the
 * offset shifted by 8.  The general message passes no image dimension, so
 * only the messages whose parameter count says 2D are taken: a sample
 * (u v), a biased or levelled one (bias or lod, u v), and a fetch (u v lod).
 *
 * Procedure referenced: Mesa 25.3.6 src/nouveau/compiler/nak_nir_lower_tex.c
 * (lower_tex: how the coordinates, level of detail and offset are packed into
 * registers, the offset four bits for each component with u lowest).
 */
static int
nvrtx_sm75_texture_operands(
	struct nvrtx_sm75_lowering *lowering,
	const struct drv_gpu_shader_ir_inst *instruction,
	struct nvrtx_sm75_texture_operands *operands)
{
	uint32_t message;
	uint32_t count;
	uint32_t offset;
	uint32_t first;

	/* No level of detail, no fetch, no offset. */
	operands->lod = NVRTX_SM75_NOT_DEFINED;
	operands->mode = NVRTX_SM75_TEX_LOD_AUTO;
	operands->fetch = 0;
	operands->has_offset = 0;
	operands->offset = 0U;

	/* The 2D samples of their own operations: the coordinates, then the bias or the level. */
	if (instruction->op != DRV_GPU_IR_TEXTURE) {
		operands->u = instruction->src[0];
		operands->v = instruction->src[1];
		if (instruction->op == DRV_GPU_IR_SAMPLE_LOD) {
			operands->lod = instruction->src[2];
			operands->mode = NVRTX_SM75_TEX_LOD_EXPLICIT;
		}
		if (instruction->op == DRV_GPU_IR_SAMPLE_BIAS) {
			operands->lod = instruction->src[2];
			operands->mode = NVRTX_SM75_TEX_LOD_BIAS;
		}

		/* u in bits 11..8 and v in 7..4 become u in 3..0 and v in 7..4. */
		offset = instruction->component;
		if (offset != 0U) {
			operands->has_offset = 1;
			operands->offset = ((offset >> 8) & 0xfU) | (offset & 0xf0U);
		}
	} else {
		/* The general message, its parameters from src[0]. */
		message = instruction->component & 0x1fU;
		count = instruction->src[1];
		first = instruction->src[0];
		if (message == DRV_GPU_IR_TEXTURE_SAMPLE && count == 2U) {
			operands->u = first;
			operands->v = first + 1U;
		} else if (message == DRV_GPU_IR_TEXTURE_SAMPLE_BIAS && count == 3U) {
			operands->lod = first;
			operands->mode = NVRTX_SM75_TEX_LOD_BIAS;
			operands->u = first + 1U;
			operands->v = first + 2U;
		} else if (message == DRV_GPU_IR_TEXTURE_SAMPLE_LOD && count == 3U) {
			operands->lod = first;
			operands->mode = NVRTX_SM75_TEX_LOD_EXPLICIT;
			operands->u = first + 1U;
			operands->v = first + 2U;
		} else if (message == DRV_GPU_IR_TEXTURE_LD && count == 3U) {
			operands->u = first;
			operands->v = first + 1U;
			operands->lod = first + 2U;
			operands->mode = NVRTX_SM75_TEX_LOD_EXPLICIT;
			operands->fetch = 1;
		} else {
			return nvrtx_sm75_refuse(lowering, ENOTSUP, "a texture message other than a 2D sample or fetch (array, cube, 3D, compare, gradients, size) is not compiled yet");
		}

		/* u in bits 11..8, v in 7..4 and r in 3..0 of the offset become u in 3..0, v in 7..4 and r in 11..8. */
		offset = (instruction->component >> 8) & 0xfffU;
		if (offset != 0U) {
			operands->has_offset = 1;
			operands->offset = ((offset >> 8) & 0xfU) | (offset & 0xf0U) | ((offset & 0xfU) << 8);
		}
	}

	/* Outside a fragment program there are no derivatives: an implicit level is level zero. */
	if (lowering->ir->stage != DRV_GPU_STAGE_FRAGMENT && operands->mode == NVRTX_SM75_TEX_LOD_AUTO)
		operands->mode = NVRTX_SM75_TEX_LOD_ZERO;

	/* Succeeded: the operands are found. */
	return 0;
}

/*
 * Lowers a discard of the fragments where the Boolean holds: @P0 KILL.
 *
 * Procedure referenced: Mesa 25.3.6 src/nouveau/compiler/nak/from_nir.rs
 * (ShaderFromNir::parse_intrinsic: nir_intrinsic_demote and
 * nir_intrinsic_demote_if) and src/nouveau/compiler/nak/sph.rs
 * (encode_header: set_kills_pixels).
 */
static int
nvrtx_sm75_lower_kill(
	struct nvrtx_sm75_lowering *lowering,
	const struct drv_gpu_shader_ir_inst *instruction)
{
	struct nvrtx_sm75_word word;
	int error;

	/* Only a fragment program discards. */
	if (lowering->ir->stage != DRV_GPU_STAGE_FRAGMENT)
		return nvrtx_sm75_refuse(lowering, EINVAL, "a discard outside a fragment program");

	/* P0, then the KILL under it. */
	error = nvrtx_sm75_predicate(lowering, instruction->src[0]);
	if (error != 0)
		return error;

	nvrtx_sm75_begin(&word);
	nvrtx_sm75_kill(&word);
	nvrtx_sm75_guard(&word, NVRTX_SM75_P0, 0);
	error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_CONTROL);
	if (error != 0)
		return error;

	/* Succeeded: the header says the program discards. */
	nvrtx_sm75_sph_set(lowering->binary->sph, NVRTX_SM75_SPH_KILLS_PIXELS, 1U, 1U);
	return 0;
}

/* Finds a fragment program's located input, or NULL. */
static const struct drv_gpu_shader_ir_io *
nvrtx_sm75_input(
	const struct drv_gpu_shader_ir *ir,
	uint32_t location)
{
	uint32_t index;

	/* The input at the location. */
	for (index = 0U; index < ir->input_count; index++) {
		if (ir->inputs[index].location == location)
			return &ir->inputs[index];
	}

	/* Succeeded in finding none. */
	return NULL;
}

/*
 * Completes a graphics program's header (version 4):
 * the type and version, the SASS version 1, a fragment program's
 * multiple-target bit and its input map's position w, the global memory
 * bits, a vertex program's store request.  The other maps were set as the
 * program was lowered.
 *
 * Procedure referenced: Mesa 25.3.6 src/nouveau/compiler/nak/sph.rs
 * (encode_header).
 *
 * Values referenced: Mesa 25.3.6 src/nouveau/compiler/nak/sph.rs
 * (encode_header: the SASS version and the fields set).
 */
static void
nvrtx_sm75_header(
	struct nvrtx_sm75_lowering *lowering)
{
	struct nvrtx_sm75_binary *binary;
	const struct drv_gpu_shader_ir *ir;
	uint32_t index;
	int global;
	int stores;

	binary = lowering->binary;
	ir = lowering->ir;

	/* A compute program has none. */
	if (ir->stage == DRV_GPU_STAGE_COMPUTE)
		return;
	binary->graphics = 1U;

	/* The type, the version and the stage. */
	if (ir->stage == DRV_GPU_STAGE_FRAGMENT) {
		nvrtx_sm75_sph_set(binary->sph, NVRTX_SM75_SPH_TYPE_LOW, 5U, NVRTX_SM75_SPH_TYPE_PS);
		nvrtx_sm75_sph_set(binary->sph, NVRTX_SM75_SPH_SHADER_TYPE_LOW, 4U, NVRTX_SM75_SPH_PIXEL);
		nvrtx_sm75_sph_set(binary->sph, NVRTX_SM75_SPH_MRT_ENABLE, 1U, 1U);

		/*
		 * The position's w is mapped in even when the program does not
		 * read it, as a trap (a shader program header error) follows
		 * without it.
		 *
		 * Value referenced: Mesa 25.3.6
		 * src/nouveau/compiler/nak/from_nir.rs (init_info_from_nir: bit
		 * 31 of a fragment program's sysvals_in.ab, the position's w in
		 * its input map).
		 */
		nvrtx_sm75_sph_set(binary->sph, NVRTX_SM75_SPH_IMAP_AB_LOW + NVRTX_SM75_ATTR_POSITION_W / 4U, 1U, 1U);
	} else {
		nvrtx_sm75_sph_set(binary->sph, NVRTX_SM75_SPH_TYPE_LOW, 5U, NVRTX_SM75_SPH_TYPE_VTG);
		nvrtx_sm75_sph_set(binary->sph, NVRTX_SM75_SPH_SHADER_TYPE_LOW, 4U, NVRTX_SM75_SPH_VERTEX);
		nvrtx_sm75_sph_set(binary->sph, NVRTX_SM75_SPH_STORE_REQ_START_LOW, 8U, lowering->store_first);
		nvrtx_sm75_sph_set(binary->sph, NVRTX_SM75_SPH_STORE_REQ_END_LOW, 8U, lowering->store_last);
	}
	nvrtx_sm75_sph_set(binary->sph, NVRTX_SM75_SPH_VERSION_LOW, 5U, NVRTX_SM75_SPH_VERSION);
	nvrtx_sm75_sph_set(binary->sph, NVRTX_SM75_SPH_SASS_VERSION_LOW, 4U, 1U);

	/* Whether it loads or stores global memory. */
	global = 0;
	stores = 0;
	for (index = 0U; index < ir->instruction_count; index++) {
		if (ir->instructions[index].op == DRV_GPU_IR_LOAD_STORAGE)
			global = 1;
		if (ir->instructions[index].op == DRV_GPU_IR_STORE_STORAGE || ir->instructions[index].op == DRV_GPU_IR_ATOMIC) {
			global = 1;
			stores = 1;
		}
	}
	nvrtx_sm75_sph_set(binary->sph, NVRTX_SM75_SPH_LOAD_OR_STORE, 1U, (uint32_t)global);
	nvrtx_sm75_sph_set(binary->sph, NVRTX_SM75_SPH_GLOBAL_STORE, 1U, (uint32_t)stores);
}

/* Sets a field of the header. */
static void
nvrtx_sm75_sph_set(
	uint32_t *sph,
	unsigned low,
	unsigned bits,
	uint32_t value)
{
	unsigned bit;
	uint32_t mask;

	/* Each bit of the field into its word. */
	for (bit = 0U; bit < bits; bit++) {
		mask = 1U << ((low + bit) % 32U);
		if (((value >> bit) & 1U) != 0U) {
			sph[(low + bit) / 32U] |= mask;
		} else {
			sph[(low + bit) / 32U] &= ~mask;
		}
	}
}

/*
 * Widens a vertex program's store request to an attribute word's address.
 *
 * Procedure referenced: Mesa 25.3.6 src/nouveau/compiler/nak/ir.rs
 * (VtgIoInfo::mark_store_req: the first and the last word, by address / 4).
 */
static void
nvrtx_sm75_mark_store(
	struct nvrtx_sm75_lowering *lowering,
	uint32_t address)
{
	/* The first and the last word stored. */
	if (address / 4U < lowering->store_first)
		lowering->store_first = address / 4U;
	if (address / 4U > lowering->store_last)
		lowering->store_last = address / 4U;
}

/* Reports nonzero for the texture operations, which define four results. */
static int
nvrtx_sm75_is_sample(
	enum drv_gpu_shader_ir_op op)
{
	/* The implicit, the explicit and the biased level of detail. */
	if (op == DRV_GPU_IR_SAMPLE)
		return 1;
	if (op == DRV_GPU_IR_SAMPLE_LOD)
		return 1;
	if (op == DRV_GPU_IR_SAMPLE_BIAS)
		return 1;

	/* The general sampler message. */
	if (op == DRV_GPU_IR_TEXTURE)
		return 1;

	/* Succeeded: another operation. */
	return 0;
}

/*
 * Computes a storage word's address into a new register pair: the
 * buffer's 64-bit address (LDC.64 from the table) plus the 32-bit byte
 * offset (IMAD.WIDE.U32 offset * 1 + address).
 */
static int
nvrtx_sm75_storage_address(
	struct nvrtx_sm75_lowering *lowering,
	uint32_t slot,
	uint32_t offset,
	uint32_t *address)
{
	struct nvrtx_sm75_word word;
	struct nvrtx_sm75_src a;
	struct nvrtx_sm75_src one;
	struct nvrtx_sm75_src base_pair;
	uint32_t base;
	int error;

	/* The pair the buffer's address is loaded into. */
	error = nvrtx_sm75_take_pair(lowering, &base);
	if (error != 0)
		return error;

	/* The pair the word's address goes into. */
	error = nvrtx_sm75_take_pair(lowering, address);
	if (error != 0) {
		nvrtx_sm75_give(lowering, base);
		nvrtx_sm75_give(lowering, base + 1U);
		return error;
	}

	/* The buffer's address from its entry. */
	nvrtx_sm75_begin(&word);
	nvrtx_sm75_ldc(&word, base, NVRTX_SM75_CB_ROOT, NVRTX_SM75_CB_STORAGE + slot * NVRTX_SM75_CB_STORAGE_ENTRY, 1);
	error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_VARIABLE_WRITE);

	/* The address plus the offset. */
	if (error == 0) {
		a = nvrtx_sm75_reg(offset);
		one = nvrtx_sm75_imm(1U);
		base_pair = nvrtx_sm75_reg(base);
		nvrtx_sm75_begin(&word);
		nvrtx_sm75_imad_wide(&word, *address, &a, &one, &base_pair);
		error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_FIXED);
	}

	/* The buffer's address is no longer needed. */
	nvrtx_sm75_give(lowering, base);
	nvrtx_sm75_give(lowering, base + 1U);

	/* An emission past the code's room gives the word's pair back too. */
	if (error != 0) {
		nvrtx_sm75_give(lowering, *address);
		nvrtx_sm75_give(lowering, *address + 1U);
		return error;
	}

	/* Succeeded: the pair holds the word's address. */
	return 0;
}

/*
 * Appends an instruction.  Its scheduling control is set when the program
 * is complete (nvrtx_sm75_finish); here the kind the lowering emits it as is
 * checked against how the dependence marking reads it back: a
 * variable-latency kind must be an instruction with a scoreboard, a fixed
 * one an instruction without, and a branch, convergence or barrier kind an
 * instruction that ends its block.
 */
static int
nvrtx_sm75_emit(
	struct nvrtx_sm75_lowering *lowering,
	struct nvrtx_sm75_word *word,
	int kind)
{
	struct nvrtx_sm75_operands operands;
	uint32_t *slot;
	int unknown;
	int variable;
	int ends_block;
	int global;

	/* The code has room for every expansion; running out is a bug of the lowering. */
	if (lowering->count >= lowering->capacity)
		return nvrtx_sm75_refuse(lowering, EINVAL, "the code outgrew its room");

	/* How the dependence marking reads the instruction. */
	unknown = nvrtx_sm75_operands(word->w, &operands);
	if (unknown != 0)
		return nvrtx_sm75_refuse(lowering, EINVAL, "the dependence marking does not know an instruction the lowering emitted");

	/* The kind agrees with it. */
	variable = 0;
	if (kind == NVRTX_SM75_VARIABLE_WRITE ||
	    kind == NVRTX_SM75_VARIABLE_STORE ||
	    kind == NVRTX_SM75_VARIABLE_SPECIAL)
		variable = 1;
	ends_block = 0;
	if (kind == NVRTX_SM75_CONTROL || kind == NVRTX_SM75_SYNC)
		ends_block = 1;
	if (ends_block != operands.boundary)
		return nvrtx_sm75_refuse(lowering, EINVAL, "an instruction emitted as a block's end that does not end one, or the reverse");
	if (ends_block == 0 &&
	    variable != 0 &&
	    operands.class_of != NVRTX_SM75_CLASS_DECOUPLED)
		return nvrtx_sm75_refuse(lowering, EINVAL, "an instruction emitted as variable-latency that has no scoreboard");
	if (ends_block == 0 &&
	    variable == 0 &&
	    operands.class_of == NVRTX_SM75_CLASS_DECOUPLED)
		return nvrtx_sm75_refuse(lowering, EINVAL, "an instruction emitted as fixed-latency that needs a scoreboard");

	/* A global access gets the shader model's memory order field. */
	global = nvrtx_sm75_is_global(word);
	if (global != 0)
		nvrtx_sm75_memory_order(word, lowering->sm);

	/* The four words into the code. */
	slot = lowering->binary->code + (size_t)lowering->count * NVRTX_SM75_INSTRUCTION_WORDS;
	slot[0] = word->w[0];
	slot[1] = word->w[1];
	slot[2] = word->w[2];
	slot[3] = word->w[3];
	lowering->count++;

	/* Succeeded: the instruction is in the code. */
	return 0;
}

/*
 * Ends the program: EXIT, a branch to itself (the fetch ahead never runs
 * past the end), and NOPs to a whole 128-byte line; then marks the
 * dependences of every instruction.  Records the code's size and the
 * register count.
 */
static int
nvrtx_sm75_finish(
	struct nvrtx_sm75_lowering *lowering)
{
	struct nvrtx_sm75_word word;
	const char *reason;
	uint32_t instruction;
	int error;

	/* EXIT. */
	nvrtx_sm75_begin(&word);
	nvrtx_sm75_exit(&word);
	error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_CONTROL);
	if (error != 0)
		return error;

	/* The branch to itself. */
	nvrtx_sm75_begin(&word);
	nvrtx_sm75_bra(&word, -1);
	error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_CONTROL);
	if (error != 0)
		return error;

	/* The padding. */
	while ((lowering->count % NVRTX_SM75_LINE_INSTRUCTIONS) != 0U) {
		nvrtx_sm75_begin(&word);
		nvrtx_sm75_nop(&word);
		error = nvrtx_sm75_emit(lowering, &word, NVRTX_SM75_FIXED);
		if (error != 0)
			return error;
	}

	/* The instructions of each block in an order that hides latency. */
	reason = NULL;
	instruction = 0U;
	error = nvrtx_sm75_reorder(lowering->binary->code, lowering->count, lowering->sm, &instruction, &reason);
	if (error != 0) {
		lowering->index = instruction;
		return nvrtx_sm75_refuse(lowering, EINVAL, reason);
	}

	/* The dependences of every instruction. */
	error = nvrtx_sm75_schedule(lowering->binary->code, lowering->count, lowering->sm, &instruction, &reason);
	if (error != 0) {
		lowering->index = instruction;
		return nvrtx_sm75_refuse(lowering, EINVAL, reason);
	}

	/* A graphics program's header. */
	nvrtx_sm75_header(lowering);

	/*
	 * The size and the registers (at least four, the hardware's two
	 * included).
	 *
	 * Values referenced: Mesa 25.3.6 src/nouveau/compiler/nak/api.rs
	 * (ShaderBin::new: num_gprs, at least 4) and
	 * src/nouveau/compiler/nak/sm70.rs (ShaderModel70::hw_reserved_gprs).
	 */
	lowering->binary->code_words = lowering->count * NVRTX_SM75_INSTRUCTION_WORDS;
	lowering->binary->gpr_count = lowering->highest + NVRTX_SM75_GPR_RESERVED;
	if (lowering->binary->gpr_count < NVRTX_SM75_GPR_MINIMUM)
		lowering->binary->gpr_count = NVRTX_SM75_GPR_MINIMUM;

	/* Succeeded: the program is complete. */
	return 0;
}

/*
 * Takes the free register given back longest ago, among the first
 * NVRTX_SM75_GPR_POOL or the ones in use so far, whichever are more; when
 * none of them is free, the lowest free one.
 */
static int
nvrtx_sm75_take(
	struct nvrtx_sm75_lowering *lowering,
	uint32_t *reg)
{
	uint32_t index;
	uint32_t limit;
	uint32_t best;

	/* The pool. */
	limit = lowering->highest;
	if (limit < NVRTX_SM75_GPR_POOL)
		limit = NVRTX_SM75_GPR_POOL;
	if (limit > NVRTX_SM75_GPR_USABLE)
		limit = NVRTX_SM75_GPR_USABLE;

	/* The free one of the pool given back longest ago, else the first free one at all. */
	best = NVRTX_SM75_GPR_USABLE;
	for (index = 0U; index < NVRTX_SM75_GPR_USABLE; index++) {
		if (lowering->taken[index] != 0U)
			continue;
		if (index >= limit && best != NVRTX_SM75_GPR_USABLE)
			break;
		if (best == NVRTX_SM75_GPR_USABLE || lowering->freed_at[index] < lowering->freed_at[best])
			best = index;
		if (index >= limit)
			break;
	}

	/* None is free: the compiler does not spill. */
	if (best == NVRTX_SM75_GPR_USABLE)
		return nvrtx_sm75_refuse(lowering, ENOTSUP, "the program needs more registers than there are (no spilling yet)");

	/* It is taken from here on. */
	lowering->taken[best] = 1U;
	if (best + 1U > lowering->highest)
		lowering->highest = best + 1U;
	*reg = best;

	/* Succeeded: the register is the caller's. */
	return 0;
}

/* Takes an even register whose odd neighbour is free too, the pair given back longest ago, as nvrtx_sm75_take() does. */
static int
nvrtx_sm75_take_pair(
	struct nvrtx_sm75_lowering *lowering,
	uint32_t *reg)
{
	uint32_t index;
	uint32_t limit;
	uint32_t best;
	uint32_t age;
	uint32_t best_age;

	/* The pool. */
	limit = lowering->highest;
	if (limit < NVRTX_SM75_GPR_POOL)
		limit = NVRTX_SM75_GPR_POOL;
	if (limit > NVRTX_SM75_GPR_USABLE)
		limit = NVRTX_SM75_GPR_USABLE;

	/* The free pair of the pool given back longest ago, else the first free pair at all. */
	best = NVRTX_SM75_GPR_USABLE;
	best_age = 0U;
	for (index = 0U; index + 1U < NVRTX_SM75_GPR_USABLE; index += 2U) {
		if (lowering->taken[index] != 0U)
			continue;
		if (lowering->taken[index + 1U] != 0U)
			continue;
		if (index >= limit && best != NVRTX_SM75_GPR_USABLE)
			break;

		/* The later of the two give-backs. */
		age = lowering->freed_at[index];
		if (lowering->freed_at[index + 1U] > age)
			age = lowering->freed_at[index + 1U];
		if (best == NVRTX_SM75_GPR_USABLE || age < best_age) {
			best = index;
			best_age = age;
		}
		if (index >= limit)
			break;
	}

	/* None is free: the compiler does not spill. */
	if (best == NVRTX_SM75_GPR_USABLE)
		return nvrtx_sm75_refuse(lowering, ENOTSUP, "the program needs more registers than there are (no spilling yet)");

	/* Both are taken from here on. */
	lowering->taken[best] = 1U;
	lowering->taken[best + 1U] = 1U;
	if (best + 2U > lowering->highest)
		lowering->highest = best + 2U;
	*reg = best;

	/* Succeeded: the pair is the caller's. */
	return 0;
}

/* Gives a register back. */
static void
nvrtx_sm75_give(
	struct nvrtx_sm75_lowering *lowering,
	uint32_t reg)
{
	/* RZ and out-of-range numbers are never taken. */
	if (reg >= NVRTX_SM75_GPR_USABLE)
		return;

	/* Free from here on, given back now. */
	lowering->taken[reg] = 0U;
	lowering->freed_at[reg] = lowering->count + 1U;
}

/*
 * Gives back the registers of the values an instruction read for the last
 * time, and of its result when nothing reads it.
 */
static void
nvrtx_sm75_retire(
	struct nvrtx_sm75_lowering *lowering,
	const struct drv_gpu_shader_ir_inst *instruction)
{
	uint32_t sources[NVRTX_SM75_MAX_SOURCES];
	uint32_t value;
	uint32_t source;
	int is_sample;
	int defines;
	int count;

	/* The sources read for the last time. */
	defines = nvrtx_sm75_defines(instruction);
	count = nvrtx_sm75_sources(instruction, sources);
	for (source = 0U; count > 0 && source < (uint32_t)count; source++) {
		value = sources[source];

		/* A value read later, already given back, or the result itself stays. */
		if (lowering->last_use[value] != lowering->index + 1U)
			continue;
		if (lowering->reg[value] == NVRTX_SM75_NO_REG)
			continue;
		if (defines != 0 && value == instruction->dst)
			continue;

		nvrtx_sm75_give(lowering, lowering->reg[value]);
		lowering->reg[value] = NVRTX_SM75_NO_REG;
	}

	/* A sample's results nothing reads afterwards go back at once. */
	is_sample = nvrtx_sm75_is_sample(instruction->op);
	for (source = 0U; is_sample != 0 && source < 4U; source++) {
		value = instruction->dst + source;
		if (lowering->last_use[value] > lowering->index + 1U)
			continue;
		if (lowering->reg[value] == NVRTX_SM75_NO_REG)
			continue;
		nvrtx_sm75_give(lowering, lowering->reg[value]);
		lowering->reg[value] = NVRTX_SM75_NO_REG;
	}

	/* A result nothing reads afterwards goes back at once. */
	if (defines == 0)
		return;
	value = instruction->dst;
	if (lowering->last_use[value] > lowering->index + 1U)
		return;
	if (lowering->reg[value] == NVRTX_SM75_NO_REG)
		return;

	nvrtx_sm75_give(lowering, lowering->reg[value]);
	lowering->reg[value] = NVRTX_SM75_NO_REG;
}

/* Reports a value's register as a source operand. */
static struct nvrtx_sm75_src
nvrtx_sm75_value(
	const struct nvrtx_sm75_lowering *lowering,
	uint32_t value)
{
	struct nvrtx_sm75_src operand;

	/* The register the value lives in; a value never defined reads as RZ. */
	operand = nvrtx_sm75_reg(NVRTX_SM75_RZ);
	if (lowering->reg[value] != NVRTX_SM75_NO_REG)
		operand = nvrtx_sm75_reg(lowering->reg[value]);

	/* Succeeded: the operand. */
	return operand;
}
