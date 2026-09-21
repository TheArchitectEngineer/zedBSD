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
	if (b.error != 0)
		return 1;
	words = drv_i915_eu_data(&b, &bytes);
	f = fopen(argv[1], "wb");
	fwrite(words, 1, bytes, f);
	fclose(f);
	drv_i915_eu_free(&b);
	return 0;
}
