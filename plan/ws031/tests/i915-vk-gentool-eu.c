/*
 * WS031 E-128: every emitter of the EU encoder, written out for Mesa's gentool to judge.
 * run-vk-gentool-test.sh disassembles the result (no validation error may appear), assembles the
 * disassembly again and requires the very same bytes: a stray or missing bit cannot survive both.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void *kern_calloc(size_t count, size_t size) { return calloc(count, size); }
void kern_free(void *pointer) { free(pointer); }

#include "../../../src/drivers/gpu/i915/compiler/eu.c"

int
main(int argc, char **argv)
{
	struct i915_eu_buf b;
	const uint32_t *words;
	size_t bytes;
	FILE *f;

	if (argc != 2)
		return 2;
	drv_i915_eu_init(&b);
	drv_i915_eu_mov(&b, drv_i915_eu_grf(16U), drv_i915_eu_imm_f(0x3edc28f6U));
	drv_i915_eu_mov(&b, drv_i915_eu_grf(17U), drv_i915_eu_grf(3U));
	drv_i915_eu_mov(&b, drv_i915_eu_grf(18U), drv_i915_eu_grf_scalar(2U, 4U));
	drv_i915_eu_mov(&b, drv_i915_eu_grf(19U), drv_i915_eu_negate(drv_i915_eu_grf(17U)));
	drv_i915_eu_mov(&b, drv_i915_eu_grf_ud(127U), drv_i915_eu_grf_ud(1U));
	{
		struct i915_eu_reg d = drv_i915_eu_grf_ud(100U);

		d.type = 6U;
		drv_i915_eu_mov(&b, d, drv_i915_eu_imm_d(0U));
	}
	drv_i915_eu_alu2(&b, I915_EU_ADD, drv_i915_eu_grf(20U), drv_i915_eu_grf(16U), drv_i915_eu_grf(17U));
	drv_i915_eu_alu2(&b, I915_EU_ADD, drv_i915_eu_grf(21U), drv_i915_eu_grf(16U), drv_i915_eu_negate(drv_i915_eu_grf(17U)));
	drv_i915_eu_alu2(&b, I915_EU_MUL, drv_i915_eu_grf(22U), drv_i915_eu_grf_scalar(4U, 12U), drv_i915_eu_grf(3U));
	drv_i915_eu_alu2(&b, I915_EU_MUL, drv_i915_eu_grf(23U), drv_i915_eu_grf(22U), drv_i915_eu_imm_f(0x3f99999aU));
	drv_i915_eu_math(&b, I915_EU_MATH_SIN, drv_i915_eu_grf(24U), drv_i915_eu_grf(23U), drv_i915_eu_null());
	drv_i915_eu_math(&b, I915_EU_MATH_COS, drv_i915_eu_grf(25U), drv_i915_eu_grf(23U), drv_i915_eu_null());
	drv_i915_eu_math(&b, I915_EU_MATH_RSQ, drv_i915_eu_grf(26U), drv_i915_eu_grf(23U), drv_i915_eu_null());
	/* sampler: u in r30, v in r31, the reply in r40..r43; binding table entry 1, sampler 0 */
	drv_i915_eu_send(&b, drv_i915_eu_grf(40U), drv_i915_eu_grf(30U), drv_i915_eu_grf(31U), 2U, 0x02420001U, 0x00000040U, 0, 0);
	/* URB write: handles in r1, eight registers from r100 */
	drv_i915_eu_send(&b, drv_i915_eu_null(), drv_i915_eu_grf(1U), drv_i915_eu_grf(100U), 6U, 0x02080007U, 0x00000200U, 0, 0);
	/* URB write, slot 2, end of thread */
	drv_i915_eu_send(&b, drv_i915_eu_null(), drv_i915_eu_grf(127U), drv_i915_eu_grf(123U), 6U, 0x02080027U, 0x00000100U, 0, 1);
	/* render-target write, end of thread */
	drv_i915_eu_send(&b, drv_i915_eu_null(), drv_i915_eu_grf(124U), drv_i915_eu_null(), 5U, 0x08031400U, 0U, 1, 1);
	/* p014 stage C: comparisons, selections, logic, rounding, abs, the flag load, the math functions, a masked write */
	drv_i915_eu_cmp(&b, I915_EU_COND_LT, I915_EU_FLAG_F0_0, 0, drv_i915_eu_grf(27U), drv_i915_eu_grf(16U), drv_i915_eu_grf(17U));
	drv_i915_eu_cmp(&b, I915_EU_COND_GE, I915_EU_FLAG_F0_0, 0, drv_i915_eu_grf(28U), drv_i915_eu_grf(16U), drv_i915_eu_grf(17U));
	drv_i915_eu_cmp(&b, I915_EU_COND_EQ, I915_EU_FLAG_F0_0, 0, drv_i915_eu_grf(29U), drv_i915_eu_grf(16U), drv_i915_eu_grf(17U));
	drv_i915_eu_cmp(&b, I915_EU_COND_NE, I915_EU_FLAG_F0_0, 0, drv_i915_eu_grf(30U), drv_i915_eu_grf(16U), drv_i915_eu_grf(17U));
	{
		struct i915_eu_reg n = drv_i915_eu_null();

		n.type = 6U;
		drv_i915_eu_cmp(&b, I915_EU_COND_NE, I915_EU_FLAG_F0_0, 0, n, drv_i915_eu_grf_d(27U), drv_i915_eu_imm_d(0U));
		drv_i915_eu_select(&b, I915_EU_FLAG_F0_0, drv_i915_eu_grf_d(31U), drv_i915_eu_grf_d(16U), drv_i915_eu_grf_d(17U));
		drv_i915_eu_flag_load(&b, I915_EU_FLAG_F1_0, 1U, 28U);
		drv_i915_eu_cmp(&b, I915_EU_COND_EQ, I915_EU_FLAG_F1_0, 1, n, drv_i915_eu_grf_d(28U), drv_i915_eu_imm_d(0U));
	}
	drv_i915_eu_minmax(&b, I915_EU_COND_LT, drv_i915_eu_grf(32U), drv_i915_eu_grf(16U), drv_i915_eu_grf(17U));
	drv_i915_eu_minmax(&b, I915_EU_COND_GE, drv_i915_eu_grf(33U), drv_i915_eu_grf(16U), drv_i915_eu_grf(17U));
	drv_i915_eu_alu2(&b, I915_EU_AND, drv_i915_eu_grf_d(34U), drv_i915_eu_grf_d(27U), drv_i915_eu_grf_d(28U));
	drv_i915_eu_alu2(&b, I915_EU_OR, drv_i915_eu_grf_d(35U), drv_i915_eu_grf_d(27U), drv_i915_eu_grf_d(28U));
	drv_i915_eu_alu1(&b, I915_EU_NOT, drv_i915_eu_grf_d(36U), drv_i915_eu_grf_d(27U));
	drv_i915_eu_alu1(&b, I915_EU_RNDD, drv_i915_eu_grf(37U), drv_i915_eu_grf(16U));
	drv_i915_eu_alu1(&b, I915_EU_FRC, drv_i915_eu_grf(38U), drv_i915_eu_grf(16U));
	drv_i915_eu_mov(&b, drv_i915_eu_grf(39U), drv_i915_eu_abs(drv_i915_eu_grf(16U)));
	drv_i915_eu_math(&b, I915_EU_MATH_INV, drv_i915_eu_grf(44U), drv_i915_eu_grf(16U), drv_i915_eu_null());
	drv_i915_eu_math(&b, I915_EU_MATH_SQRT, drv_i915_eu_grf(45U), drv_i915_eu_grf(16U), drv_i915_eu_null());
	drv_i915_eu_math(&b, I915_EU_MATH_LOG, drv_i915_eu_grf(46U), drv_i915_eu_grf(16U), drv_i915_eu_null());
	drv_i915_eu_math(&b, I915_EU_MATH_EXP, drv_i915_eu_grf(47U), drv_i915_eu_grf(16U), drv_i915_eu_null());
	/* p014 stage E: integer arithmetic, shifts, conversions, the 32 x 16-bit multiply halves, masked ALU, WHILE */
	{
		uint32_t top = drv_i915_eu_position(&b);
		struct i915_eu_reg n = drv_i915_eu_null();

		drv_i915_eu_alu2(&b, I915_EU_ADD, drv_i915_eu_grf_d(48U), drv_i915_eu_grf_d(16U), drv_i915_eu_negate(drv_i915_eu_grf_d(17U)));
		drv_i915_eu_alu2(&b, I915_EU_XOR, drv_i915_eu_grf_d(49U), drv_i915_eu_grf_d(16U), drv_i915_eu_grf_d(17U));
		drv_i915_eu_alu2(&b, I915_EU_SHL, drv_i915_eu_grf_d(50U), drv_i915_eu_grf_d(16U), drv_i915_eu_grf_d(17U));
		drv_i915_eu_alu2(&b, I915_EU_SHR, drv_i915_eu_grf_ud(51U), drv_i915_eu_grf_ud(16U), drv_i915_eu_imm_ud(5U));
		drv_i915_eu_alu2(&b, I915_EU_ASR, drv_i915_eu_grf_d(52U), drv_i915_eu_grf_d(16U), drv_i915_eu_grf_d(17U));
		drv_i915_eu_alu2(&b, I915_EU_MUL, drv_i915_eu_grf_d(53U), drv_i915_eu_grf_d(16U), drv_i915_eu_grf_uw_half(17U, 0));
		drv_i915_eu_alu2(&b, I915_EU_MUL, drv_i915_eu_grf_d(54U), drv_i915_eu_grf_d(16U), drv_i915_eu_grf_uw_half(17U, 1));
		drv_i915_eu_alu1(&b, I915_EU_RNDZ, drv_i915_eu_grf(55U), drv_i915_eu_grf(16U));
		drv_i915_eu_mov(&b, drv_i915_eu_grf_d(56U), drv_i915_eu_grf(16U));
		drv_i915_eu_mov(&b, drv_i915_eu_grf(57U), drv_i915_eu_grf_d(16U));
		drv_i915_eu_mov(&b, drv_i915_eu_grf_ud(58U), drv_i915_eu_grf(16U));
		drv_i915_eu_mov(&b, drv_i915_eu_grf(59U), drv_i915_eu_grf_ud(16U));
		drv_i915_eu_cmp(&b, I915_EU_COND_GE, I915_EU_FLAG_F0_0, 0, drv_i915_eu_grf_d(60U), drv_i915_eu_grf_ud(16U), drv_i915_eu_grf_ud(17U));
		drv_i915_eu_alu2_masked(&b, I915_EU_FLAG_F0_0, I915_EU_ADD, drv_i915_eu_grf_d(61U), drv_i915_eu_grf_d(61U), drv_i915_eu_negate(drv_i915_eu_grf_d(17U)));
		drv_i915_eu_alu2_masked(&b, I915_EU_FLAG_F0_0, I915_EU_OR, drv_i915_eu_grf_d(62U), drv_i915_eu_grf_d(62U), drv_i915_eu_imm_d(0x80000000U));
		n.type = 6U;
		drv_i915_eu_cmp(&b, I915_EU_COND_NE, I915_EU_FLAG_F0_0, 0, n, drv_i915_eu_grf_d(60U), drv_i915_eu_imm_d(0U));
		drv_i915_eu_while(&b, I915_EU_FLAG_F0_0, top);
	}
	/* p014 stage E2: the integer division in the math box (signed and unsigned), round to even */
	drv_i915_eu_math(&b, I915_EU_MATH_INT_QUOTIENT, drv_i915_eu_grf_d(63U), drv_i915_eu_grf_d(16U), drv_i915_eu_grf_d(17U));
	drv_i915_eu_math(&b, I915_EU_MATH_INT_REMAINDER, drv_i915_eu_grf_d(64U), drv_i915_eu_grf_d(16U), drv_i915_eu_grf_d(17U));
	drv_i915_eu_math(&b, I915_EU_MATH_INT_QUOTIENT, drv_i915_eu_grf_ud(65U), drv_i915_eu_grf_ud(16U), drv_i915_eu_grf_ud(17U));
	drv_i915_eu_math(&b, I915_EU_MATH_INT_REMAINDER, drv_i915_eu_grf_ud(66U), drv_i915_eu_grf_ud(16U), drv_i915_eu_grf_ud(17U));
	drv_i915_eu_alu1(&b, I915_EU_RNDE, drv_i915_eu_grf(67U), drv_i915_eu_grf(16U));
	/*
	 * p014 stage E3: the scratch header (cleared on all channels, the size and base copied out of r0 by SIMD1 AND),
	 * its offset (SIMD1 MOV), an OWord block write of r20 under the mask and a read into r21 outside it
	 */
	{
		struct i915_eu_reg dword = drv_i915_eu_grf_ud(95U);
		struct i915_eu_reg payload = drv_i915_eu_grf_scalar(0U, 12U);

		drv_i915_eu_mov_all(&b, drv_i915_eu_grf_ud(95U), drv_i915_eu_imm_ud(0U));
		dword.subnr = 12U;
		payload.type = EU_TYPE_UD;
		drv_i915_eu_alu2_scalar(&b, I915_EU_AND, dword, payload, drv_i915_eu_imm_ud(0x0000000fU));
		dword.subnr = 20U;
		payload.subnr = 20U;
		drv_i915_eu_alu2_scalar(&b, I915_EU_AND, dword, payload, drv_i915_eu_imm_ud(0xfffffc00U));
		dword.subnr = 8U;
		drv_i915_eu_mov_scalar(&b, dword, drv_i915_eu_imm_ud(6U));
		drv_i915_eu_send(&b, drv_i915_eu_null(), drv_i915_eu_grf_ud(95U), drv_i915_eu_grf_ud(20U), 10U, 0x020a02fdU, 0x00000040U, 0, 0);
		drv_i915_eu_send_all(&b, drv_i915_eu_grf_ud(21U), drv_i915_eu_grf_ud(95U), drv_i915_eu_null(), 10U, 0x021802fdU, 0U);
	}
	/* render-target write predicated on the discard flag, end of thread */
	drv_i915_eu_send_masked(&b, I915_EU_FLAG_F1_0, drv_i915_eu_null(), drv_i915_eu_grf(124U), drv_i915_eu_null(), 5U, 0x08031400U, 0U, 1, 1);
	if (b.error != 0)
		return 1;
	words = drv_i915_eu_data(&b, &bytes);
	f = fopen(argv[1], "wb");
	fwrite(words, 1, bytes, f);
	fclose(f);
	drv_i915_eu_free(&b);
	return 0;
}
