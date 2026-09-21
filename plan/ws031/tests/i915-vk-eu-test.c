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

#include "../../../src/drivers/gpu/i915/vk/eu.c"

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
	struct i915_vk_eu_buf buffer;
	const uint32_t *code;
	size_t bytes;

	i915_vk_eu_init(&buffer);

	/* mov g2, g1 */
	i915_vk_eu_mov(&buffer, i915_vk_eu_grf(2U), i915_vk_eu_grf(1U));
	/* add g3, g1, g2 */
	i915_vk_eu_alu2(&buffer, I915_VK_EU_ADD, i915_vk_eu_grf(3U), i915_vk_eu_grf(1U), i915_vk_eu_grf(2U));
	/* mov g4, 1.0f */
	i915_vk_eu_mov(&buffer, i915_vk_eu_grf(4U), i915_vk_eu_imm_f(0x3F800000U));
	/* math.sin g5, g1 */
	i915_vk_eu_math(&buffer, I915_VK_EU_MATH_SIN, i915_vk_eu_grf(5U), i915_vk_eu_grf(1U), i915_vk_eu_null());
	/* nop */
	i915_vk_eu_nop(&buffer);

	assert(buffer.error == 0);
	code = i915_vk_eu_data(&buffer, &bytes);
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

	i915_vk_eu_free(&buffer);
	assert(fixture_live == 0U);
	printf("i915 vk eu host test PASS\n");
	return 0;
}
