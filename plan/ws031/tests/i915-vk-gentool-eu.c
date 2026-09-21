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

#include "../../../src/drivers/gpu/i915/vk/eu.c"

int
main(int argc, char **argv)
{
	struct i915_vk_eu_buf b;
	const uint32_t *words;
	size_t bytes;
	FILE *f;

	if (argc != 2)
		return 2;
	i915_vk_eu_init(&b);
	i915_vk_eu_mov(&b, i915_vk_eu_grf(16U), i915_vk_eu_imm_f(0x3edc28f6U));
	i915_vk_eu_mov(&b, i915_vk_eu_grf(17U), i915_vk_eu_grf(3U));
	i915_vk_eu_mov(&b, i915_vk_eu_grf(18U), i915_vk_eu_grf_scalar(2U, 4U));
	i915_vk_eu_mov(&b, i915_vk_eu_grf(19U), i915_vk_eu_negate(i915_vk_eu_grf(17U)));
	i915_vk_eu_mov(&b, i915_vk_eu_grf_ud(127U), i915_vk_eu_grf_ud(1U));
	{
		struct i915_vk_eu_reg d = i915_vk_eu_grf_ud(100U);

		d.type = 6U;
		i915_vk_eu_mov(&b, d, i915_vk_eu_imm_d(0U));
	}
	i915_vk_eu_alu2(&b, I915_VK_EU_ADD, i915_vk_eu_grf(20U), i915_vk_eu_grf(16U), i915_vk_eu_grf(17U));
	i915_vk_eu_alu2(&b, I915_VK_EU_ADD, i915_vk_eu_grf(21U), i915_vk_eu_grf(16U), i915_vk_eu_negate(i915_vk_eu_grf(17U)));
	i915_vk_eu_alu2(&b, I915_VK_EU_MUL, i915_vk_eu_grf(22U), i915_vk_eu_grf_scalar(4U, 12U), i915_vk_eu_grf(3U));
	i915_vk_eu_alu2(&b, I915_VK_EU_MUL, i915_vk_eu_grf(23U), i915_vk_eu_grf(22U), i915_vk_eu_imm_f(0x3f99999aU));
	i915_vk_eu_math(&b, I915_VK_EU_MATH_SIN, i915_vk_eu_grf(24U), i915_vk_eu_grf(23U), i915_vk_eu_null());
	i915_vk_eu_math(&b, I915_VK_EU_MATH_COS, i915_vk_eu_grf(25U), i915_vk_eu_grf(23U), i915_vk_eu_null());
	i915_vk_eu_math(&b, I915_VK_EU_MATH_RSQ, i915_vk_eu_grf(26U), i915_vk_eu_grf(23U), i915_vk_eu_null());
	/* sampler: u in r30, v in r31, the reply in r40..r43; binding table entry 1, sampler 0 */
	i915_vk_eu_send(&b, i915_vk_eu_grf(40U), i915_vk_eu_grf(30U), i915_vk_eu_grf(31U), 2U, 0x02420001U, 0x00000040U, 0, 0);
	/* URB write: handles in r1, eight registers from r100 */
	i915_vk_eu_send(&b, i915_vk_eu_null(), i915_vk_eu_grf(1U), i915_vk_eu_grf(100U), 6U, 0x02080007U, 0x00000200U, 0, 0);
	/* URB write, slot 2, end of thread */
	i915_vk_eu_send(&b, i915_vk_eu_null(), i915_vk_eu_grf(127U), i915_vk_eu_grf(123U), 6U, 0x02080027U, 0x00000100U, 0, 1);
	/* render-target write, end of thread */
	i915_vk_eu_send(&b, i915_vk_eu_null(), i915_vk_eu_grf(124U), i915_vk_eu_null(), 5U, 0x08031400U, 0U, 1, 1);
	if (b.error != 0)
		return 1;
	words = i915_vk_eu_data(&b, &bytes);
	f = fopen(argv[1], "wb");
	fwrite(words, 1, bytes, f);
	fclose(f);
	i915_vk_eu_free(&b);
	return 0;
}
