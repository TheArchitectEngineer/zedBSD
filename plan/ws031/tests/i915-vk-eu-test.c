/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Host fixture for the Gen12 EU encoder (p005). Encodes core instructions and
 * checks that each control and operand field lands in the transcribed bit
 * range. Full ISA semantics (SWSB, three-source and send operands) are
 * completed on hardware; this fixture verifies field placement.
 */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned fixture_live;

void *
kern_calloc(size_t count, size_t size)
{
	void *pointer = calloc(count, size);
	if (pointer != NULL)
		fixture_live++;
	return pointer;
}

void
kern_free(void *pointer)
{
	if (pointer != NULL)
		fixture_live--;
	free(pointer);
}

#include "../../../src/drivers/gpu/i915/compiler/eu.c"

/* Reads the inclusive bit range [high:low] from one instruction's four words. */
static uint32_t
field(const uint32_t *inst, unsigned high, unsigned low)
{
	uint32_t value = 0U;
	unsigned position;

	for (position = low; position <= high; position++) {
		uint32_t bit = (inst[position / 32U] >> (position % 32U)) & 1U;
		value |= bit << (position - low);
	}
	return value;
}

int
main(void)
{
	struct i915_eu_buf buffer;
	struct i915_eu_reg null;
	const uint32_t *code;
	size_t bytes;

	drv_i915_eu_init(&buffer);

	/* mov g2, g1 */
	drv_i915_eu_mov(&buffer, drv_i915_eu_grf(2U), drv_i915_eu_grf(1U));
	/* add g3, g1, g2 */
	drv_i915_eu_alu2(&buffer, I915_EU_ADD, drv_i915_eu_grf(3U), drv_i915_eu_grf(1U), drv_i915_eu_grf(2U));
	/* mov g4, 1.0f */
	drv_i915_eu_mov(&buffer, drv_i915_eu_grf(4U), drv_i915_eu_imm_f(0x3F800000U));
	/* math.sin g5, g1 */
	drv_i915_eu_math(&buffer, I915_EU_MATH_SIN, drv_i915_eu_grf(5U), drv_i915_eu_grf(1U), drv_i915_eu_null());
	/* nop */
	drv_i915_eu_nop(&buffer);

	assert(buffer.error == 0);
	code = drv_i915_eu_data(&buffer, &bytes);
	/* five instructions and the sync.nop the encoder puts after the out-of-order MATH (E-128) */
	assert(bytes == 6U * 4U * sizeof(uint32_t));

	/* mov: opcode 97, SIMD8, dst g2, src0 g1 as a general register. */
	assert(field(code + 0, EU_OPCODE_HI, EU_OPCODE_LO) == EU_OP_MOV);
	assert(field(code + 0, EU_EXEC_SIZE_HI, EU_EXEC_SIZE_LO) == EU_EXEC_SIZE_8);
	assert(field(code + 0, EU_DST_REG_NR_HI, EU_DST_REG_NR_LO) == 2U);
	assert(field(code + 0, EU_SRC0_REG_NR_HI, EU_SRC0_REG_NR_LO) == 1U);
	assert(field(code + 0, EU_SRC0_IS_IMM_BIT, EU_SRC0_IS_IMM_BIT) == 0U);

	/* add: opcode 64, dst g3, src0 g1, src1 g2. */
	assert(field(code + 4, EU_OPCODE_HI, EU_OPCODE_LO) == EU_OP_ADD);
	assert(field(code + 4, EU_DST_REG_NR_HI, EU_DST_REG_NR_LO) == 3U);
	assert(field(code + 4, EU_SRC0_REG_NR_HI, EU_SRC0_REG_NR_LO) == 1U);
	assert(field(code + 4, EU_SRC1_REG_NR_HI, EU_SRC1_REG_NR_LO) == 2U);

	/* mov immediate: is_imm set and the float bits in the last word. */
	assert(field(code + 8, EU_SRC0_IS_IMM_BIT, EU_SRC0_IS_IMM_BIT) == 1U);
	assert(code[8 + 3] == 0x3F800000U);

	/* math: opcode 56, function selector sine (6); it names itself with token 0 and waits for the MOV before it */
	assert(field(code + 12, EU_OPCODE_HI, EU_OPCODE_LO) == EU_OP_MATH);
	assert(field(code + 12, EU_MATH_FUNCTION_HI, EU_MATH_FUNCTION_LO) == EU_MATH_SIN);
	assert(field(code + 12, EU_SWSB_HI, EU_SWSB_LO) == EU_SWSB_REGDIST_SET(1U, 0U));
	assert(field(code + 16, EU_OPCODE_HI, EU_OPCODE_LO) == EU_OP_SYNC);
	assert(field(code + 16, EU_SWSB_HI, EU_SWSB_LO) == EU_SWSB_SYNC_DST(0U));

	/* the scoreboard: nothing before the first instruction, the one before for every later in-order one */
	assert(field(code + 0, EU_SWSB_HI, EU_SWSB_LO) == 0U);
	assert(field(code + 4, EU_SWSB_HI, EU_SWSB_LO) == EU_SWSB_REGDIST(1U));

	/* float is type 10, a SIMD8 source region is <8;8,1> = vstride 4, width 3, hstride 1 */
	assert(field(code + 0, EU_DST_REG_TYPE_HI, EU_DST_REG_TYPE_LO) == 10U);
	assert(field(code + 0, EU_SRC0_VSTRIDE_HI, EU_SRC0_VSTRIDE_LO) == 4U);
	assert(field(code + 0, EU_SRC0_WIDTH_HI, EU_SRC0_WIDTH_LO) == 3U);
	assert(field(code + 0, EU_SRC0_HSTRIDE_HI, EU_SRC0_HSTRIDE_LO) == 1U);

	/* nop: opcode 96. */
	assert(field(code + 20, EU_OPCODE_HI, EU_OPCODE_LO) == EU_OP_NOP);

	drv_i915_eu_free(&buffer);

	/*
	 * p014 stage C: the comparison, selection, logic, rounding and flag emitters.  Each field is
	 * read back from its transcribed position (brw_eu_inst.h, Gen12 column).
	 */
	drv_i915_eu_init(&buffer);
	/* cmp.l.f0.0 g5:F g1:F g2:F */
	drv_i915_eu_cmp(&buffer, I915_EU_COND_LT, I915_EU_FLAG_F0_0, 0, drv_i915_eu_grf(5U), drv_i915_eu_grf(1U), drv_i915_eu_grf(2U));
	/* (+f1.0) cmp.z.f1.0 null:D g6:D 0:D -- the discard */
	null = drv_i915_eu_null();
	null.type = EU_TYPE_D;
	drv_i915_eu_cmp(&buffer, I915_EU_COND_EQ, I915_EU_FLAG_F1_0, 1, null, drv_i915_eu_grf_d(6U), drv_i915_eu_imm_d(0U));
	/* sel.ge g7:F g1:F g2:F (max) and (+f0.1) sel g8:D g3:D g4:D */
	drv_i915_eu_minmax(&buffer, I915_EU_COND_GE, drv_i915_eu_grf(7U), drv_i915_eu_grf(1U), drv_i915_eu_grf(2U));
	drv_i915_eu_select(&buffer, I915_EU_FLAG_F0_1, drv_i915_eu_grf_d(8U), drv_i915_eu_grf_d(3U), drv_i915_eu_grf_d(4U));
	/* and / or / not on Booleans; rndd, frc; mov with abs */
	drv_i915_eu_alu2(&buffer, I915_EU_AND, drv_i915_eu_grf_d(9U), drv_i915_eu_grf_d(3U), drv_i915_eu_grf_d(4U));
	drv_i915_eu_alu2(&buffer, I915_EU_OR, drv_i915_eu_grf_d(10U), drv_i915_eu_grf_d(3U), drv_i915_eu_grf_d(4U));
	drv_i915_eu_alu1(&buffer, I915_EU_NOT, drv_i915_eu_grf_d(11U), drv_i915_eu_grf_d(3U));
	drv_i915_eu_alu1(&buffer, I915_EU_RNDD, drv_i915_eu_grf(12U), drv_i915_eu_grf(1U));
	drv_i915_eu_alu1(&buffer, I915_EU_FRC, drv_i915_eu_grf(13U), drv_i915_eu_grf(1U));
	drv_i915_eu_mov(&buffer, drv_i915_eu_grf(14U), drv_i915_eu_abs(drv_i915_eu_grf(1U)));
	/* mov(1) f1.0:UW g1.14<0,1,0>:UW {NoMask}: the dispatch mask, low word of dword 7 */
	drv_i915_eu_flag_load(&buffer, I915_EU_FLAG_F1_0, 1U, 28U);
	/* math.log, math.exp */
	drv_i915_eu_math(&buffer, I915_EU_MATH_LOG, drv_i915_eu_grf(15U), drv_i915_eu_grf(1U), drv_i915_eu_null());
	drv_i915_eu_math(&buffer, I915_EU_MATH_EXP, drv_i915_eu_grf(16U), drv_i915_eu_grf(1U), drv_i915_eu_null());
	/* (+f1.0) sendc: the render-target write of a shader that discards */
	drv_i915_eu_send_masked(&buffer, I915_EU_FLAG_F1_0, drv_i915_eu_null(), drv_i915_eu_grf(124U), drv_i915_eu_null(),
		5U, 0x08031400U, 0U, 1, 1);
	assert(buffer.error == 0);
	code = drv_i915_eu_data(&buffer, &bytes);
	/* 14 instructions and the two sync.nops after the two MATHs */
	assert(bytes == 16U * 16U);

	/* cmp: opcode 112, cond L (5) at 95:92, flag f0.0, not predicated; dst register g5 */
	assert(field(code + 0, EU_OPCODE_HI, EU_OPCODE_LO) == EU_OP_CMP);
	assert(field(code + 0, EU_COND_MODIFIER_HI, EU_COND_MODIFIER_LO) == EU_COND_L);
	assert(field(code + 0, EU_FLAG_REG_NR_BIT, EU_FLAG_REG_NR_BIT) == 0U);
	assert(field(code + 0, EU_FLAG_SUBREG_NR_BIT, EU_FLAG_SUBREG_NR_BIT) == 0U);
	assert(field(code + 0, EU_PRED_CONTROL_HI, EU_PRED_CONTROL_LO) == 0U);
	assert(field(code + 0, EU_DST_REG_NR_HI, EU_DST_REG_NR_LO) == 5U);

	/* the discard: cond Z (1), flag f1.0 written AND read (predicate normal), null destination, D type, imm 0 */
	assert(field(code + 4, EU_OPCODE_HI, EU_OPCODE_LO) == EU_OP_CMP);
	assert(field(code + 4, EU_COND_MODIFIER_HI, EU_COND_MODIFIER_LO) == EU_COND_Z);
	assert(field(code + 4, EU_FLAG_REG_NR_BIT, EU_FLAG_REG_NR_BIT) == 1U);
	assert(field(code + 4, EU_FLAG_SUBREG_NR_BIT, EU_FLAG_SUBREG_NR_BIT) == 0U);
	assert(field(code + 4, EU_PRED_CONTROL_HI, EU_PRED_CONTROL_LO) == EU_PREDICATE_NORMAL);
	assert(field(code + 4, EU_PRED_INV_BIT, EU_PRED_INV_BIT) == 0U);
	assert(field(code + 4, EU_DST_REG_FILE_BIT, EU_DST_REG_FILE_BIT) == 0U);
	assert(field(code + 4, EU_SRC0_REG_TYPE_HI, EU_SRC0_REG_TYPE_LO) == EU_TYPE_D);
	assert(field(code + 4, EU_SRC1_IS_IMM_BIT, EU_SRC1_IS_IMM_BIT) == 1U && code[4 + 3] == 0U);

	/* sel.ge: opcode 98, cond GE (4), no predicate; (+f0.1) sel: predicate on f0.1, no cond */
	assert(field(code + 8, EU_OPCODE_HI, EU_OPCODE_LO) == EU_OP_SEL);
	assert(field(code + 8, EU_COND_MODIFIER_HI, EU_COND_MODIFIER_LO) == EU_COND_GE);
	assert(field(code + 8, EU_PRED_CONTROL_HI, EU_PRED_CONTROL_LO) == 0U);
	assert(field(code + 12, EU_OPCODE_HI, EU_OPCODE_LO) == EU_OP_SEL);
	assert(field(code + 12, EU_COND_MODIFIER_HI, EU_COND_MODIFIER_LO) == 0U);
	assert(field(code + 12, EU_PRED_CONTROL_HI, EU_PRED_CONTROL_LO) == EU_PREDICATE_NORMAL);
	assert(field(code + 12, EU_FLAG_REG_NR_BIT, EU_FLAG_REG_NR_BIT) == 0U);
	assert(field(code + 12, EU_FLAG_SUBREG_NR_BIT, EU_FLAG_SUBREG_NR_BIT) == 1U);
	assert(field(code + 12, EU_DST_REG_TYPE_HI, EU_DST_REG_TYPE_LO) == EU_TYPE_D);

	/* and 101, or 102, not 100, rndd 69, frc 67; the abs modifier at bit 44 */
	assert(field(code + 16, EU_OPCODE_HI, EU_OPCODE_LO) == EU_OP_AND);
	assert(field(code + 20, EU_OPCODE_HI, EU_OPCODE_LO) == EU_OP_OR);
	assert(field(code + 24, EU_OPCODE_HI, EU_OPCODE_LO) == EU_OP_NOT);
	assert(field(code + 28, EU_OPCODE_HI, EU_OPCODE_LO) == EU_OP_RNDD);
	assert(field(code + 32, EU_OPCODE_HI, EU_OPCODE_LO) == EU_OP_FRC);
	assert(field(code + 36, EU_OPCODE_HI, EU_OPCODE_LO) == EU_OP_MOV);
	assert(field(code + 36, EU_SRC0_ABS_BIT, EU_SRC0_ABS_BIT) == 1U);
	assert(field(code + 36, EU_SRC0_NEGATE_BIT, EU_SRC0_NEGATE_BIT) == 0U);
	assert(field(code + 32, EU_SRC0_ABS_BIT, EU_SRC0_ABS_BIT) == 0U);

	/* the flag load: SIMD1, NoMask, dst ARF f1 (0x31) subregister 0 as UW, src r1 byte 28 <0;1,0> UW */
	assert(field(code + 40, EU_OPCODE_HI, EU_OPCODE_LO) == EU_OP_MOV);
	assert(field(code + 40, EU_EXEC_SIZE_HI, EU_EXEC_SIZE_LO) == EU_EXEC_SIZE_1);
	assert(field(code + 40, EU_NO_MASK_BIT, EU_NO_MASK_BIT) == 1U);
	assert(field(code + 40, EU_DST_REG_FILE_BIT, EU_DST_REG_FILE_BIT) == 0U);
	assert(field(code + 40, EU_DST_REG_NR_HI, EU_DST_REG_NR_LO) == EU_ARF_FLAG + 1U);
	assert(field(code + 40, EU_DST_REG_TYPE_HI, EU_DST_REG_TYPE_LO) == EU_TYPE_UW);
	assert(field(code + 40, EU_SRC0_REG_NR_HI, EU_SRC0_REG_NR_LO) == 1U);
	assert(field(code + 40, EU_SRC0_SUBREG_HI, EU_SRC0_SUBREG_LO) == 28U);
	assert(field(code + 40, EU_SRC0_REG_TYPE_HI, EU_SRC0_REG_TYPE_LO) == EU_TYPE_UW);

	/* math.log (2) and math.exp (3), each followed by its sync.nop */
	assert(field(code + 44, EU_MATH_FUNCTION_HI, EU_MATH_FUNCTION_LO) == EU_MATH_LOG);
	assert(field(code + 48, EU_OPCODE_HI, EU_OPCODE_LO) == EU_OP_SYNC);
	assert(field(code + 52, EU_MATH_FUNCTION_HI, EU_MATH_FUNCTION_LO) == EU_MATH_EXP);

	/* the masked render-target write: SENDC, EOT, predicated on f1.0 */
	assert(field(code + 60, EU_OPCODE_HI, EU_OPCODE_LO) == EU_OP_SENDC);
	assert(field(code + 60, EU_SEND_EOT_BIT, EU_SEND_EOT_BIT) == 1U);
	assert(field(code + 60, EU_PRED_CONTROL_HI, EU_PRED_CONTROL_LO) == EU_PREDICATE_NORMAL);
	assert(field(code + 60, EU_FLAG_REG_NR_BIT, EU_FLAG_REG_NR_BIT) == 1U);
	assert(field(code + 60, EU_FLAG_SUBREG_NR_BIT, EU_FLAG_SUBREG_NR_BIT) == 0U);
	drv_i915_eu_free(&buffer);

	/* refused: a minimum / maximum form other than L / GE, an immediate first source, a flag past f1.1 */
	drv_i915_eu_init(&buffer);
	drv_i915_eu_minmax(&buffer, I915_EU_COND_EQ, drv_i915_eu_grf(7U), drv_i915_eu_grf(1U), drv_i915_eu_grf(2U));
	assert(buffer.error != 0);
	drv_i915_eu_free(&buffer);
	drv_i915_eu_init(&buffer);
	drv_i915_eu_cmp(&buffer, I915_EU_COND_LT, I915_EU_FLAG_F0_0, 0, drv_i915_eu_grf(5U), drv_i915_eu_imm_f(0U), drv_i915_eu_grf(2U));
	assert(buffer.error != 0);
	drv_i915_eu_free(&buffer);
	drv_i915_eu_init(&buffer);
	drv_i915_eu_select(&buffer, I915_EU_FLAG_COUNT, drv_i915_eu_grf(5U), drv_i915_eu_grf(1U), drv_i915_eu_grf(2U));
	assert(buffer.error != 0);
	drv_i915_eu_free(&buffer);

	assert(fixture_live == 0U);
	printf("i915 vk eu host test PASS\n");
	return 0;
}
