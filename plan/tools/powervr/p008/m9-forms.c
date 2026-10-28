/* M9: the USC operand forms of usc-encode.c (its lower, upper and destination layouts, random registers) against Mesa 26.2.5's generated pco_isa.h encoders.  Run by m9-forms.sh. */
#include "src/drivers/gpu/powervr/compiler/usc-encode.c"
#include "pco_isa.h"
#include <stdio.h>
#include <string.h>

static int fails, passes;
static unsigned seed = 1;
static unsigned rnd(unsigned bits){ seed = seed * 1103515245u + 12345u; return (seed >> 8) & ((1u << bits) - 1u); }

static void cmp(const char *what, const uint8_t *a, const uint8_t *b, unsigned n, const uint32_t *v)
{
	if (memcmp(a, b, n) == 0) { passes++; return; }
	fails++;
	if (fails < 30) {
		printf("FAIL %s v=%u,%u %u,%u %u,%u m%u ours", what, v[0], v[1], v[2], v[3], v[4], v[5], v[6]);
		for (unsigned i = 0; i < n; i++) printf(" %02x", a[i]);
		printf(" mesa");
		for (unsigned i = 0; i < n; i++) printf(" %02x", b[i]);
		printf("\n");
	}
}

int main(void)
{
	uint8_t ours[8], mesa[8];
	uint32_t v[8];
	unsigned t, f, n;
	const struct usc_operand_form *form;

	for (t = 0; t < 4000; t++) {
		/* Lower forms. */
		for (f = 0; f < 8; f++) {
			form = &lower_forms[f];
			memset(v, 0, sizeof(v));
			for (n = 0; n < 3; n++) {
				if (form->limit[2 * n] < 0) continue;
				v[2 * n] = rnd(form->limit[2 * n]);
				v[2 * n + 1] = rnd(form->limit[2 * n + 1]);
			}
			if (form->limit[6] == 2) v[6] = rnd(2); else if (form->limit[6] == 3) v[6] = rnd(3) % 6;
			memset(ours, 0, 8); memset(mesa, 0, 8);
			if (layout_place(&form->layout, v, ours) != 0) { printf("place %u\n", f); continue; }
			switch (f) {
			case 0: n = pco_src_1lo_1b6i_encode(mesa, .sb0 = v[0], .s0 = v[1]); break;
			case 1: n = pco_src_1lo_3b11i_2m_encode(mesa, .sb0 = v[0], .s0 = v[1], .is0 = v[6]); break;
			case 2: n = pco_src_2lo_1b6i_1b5i_encode(mesa, .sb0 = v[0], .s0 = v[1], .sb1 = v[2], .s1 = v[3]); break;
			case 3: n = pco_src_2lo_2b7i_2b7i_2m_encode(mesa, .sb0 = v[0], .s0 = v[1], .sb1 = v[2], .s1 = v[3], .is0 = v[6]); break;
			case 4: n = pco_src_2lo_3b11i_2b8i_3m_encode(mesa, .sb0 = v[0], .s0 = v[1], .sb1 = v[2], .s1 = v[3], .is0 = v[6]); break;
			case 5: n = pco_src_3lo_2b7i_2b7i_2b6i_2m_encode(mesa, .sb0 = v[0], .s0 = v[1], .sb1 = v[2], .s1 = v[3], .sb2 = v[4], .s2 = v[5], .is0 = v[6]); break;
			case 6: n = pco_src_3lo_3b8i_2b8i_3b8i_3m_encode(mesa, .sb0 = v[0], .s0 = v[1], .sb1 = v[2], .s1 = v[3], .sb2 = v[4], .s2 = v[5], .is0 = v[6]); break;
			default: n = pco_src_3lo_3b11i_2b8i_3b11i_3m_encode(mesa, .sb0 = v[0], .s0 = v[1], .sb1 = v[2], .s1 = v[3], .sb2 = v[4], .s2 = v[5], .is0 = v[6]); break;
			}
			if (n != form->layout.length) { printf("length lower %u: ours %u mesa %u\n", f, form->layout.length, n); fails++; }
			char w[32]; snprintf(w, sizeof w, "lower %u", f);
			cmp(w, ours, mesa, n, v);
		}
		/* Upper forms. */
		for (f = 0; f < 8; f++) {
			form = &upper_forms[f];
			memset(v, 0, sizeof(v));
			for (n = 0; n < 3; n++) {
				if (form->limit[2 * n] < 0) continue;
				v[2 * n] = rnd(form->limit[2 * n]);
				v[2 * n + 1] = rnd(form->limit[2 * n + 1]);
			}
			memset(ours, 0, 8); memset(mesa, 0, 8);
			if (layout_place(&form->layout, v, ours) != 0) { printf("place up %u\n", f); continue; }
			switch (f) {
			case 0: n = pco_src_1up_1b6i_encode(mesa, .sb3 = v[0], .s3 = v[1]); break;
			case 1: n = pco_src_1up_3b11i_encode(mesa, .sb3 = v[0], .s3 = v[1]); break;
			case 2: n = pco_src_2up_1b6i_1b5i_encode(mesa, .sb3 = v[0], .s3 = v[1], .sb4 = v[2], .s4 = v[3]); break;
			case 3: n = pco_src_2up_2b7i_2b7i_encode(mesa, .sb3 = v[0], .s3 = v[1], .sb4 = v[2], .s4 = v[3]); break;
			case 4: n = pco_src_2up_3b11i_2b8i_encode(mesa, .sb3 = v[0], .s3 = v[1], .sb4 = v[2], .s4 = v[3]); break;
			case 5: n = pco_src_3up_2b7i_2b7i_2b6i_encode(mesa, .sb3 = v[0], .s3 = v[1], .sb4 = v[2], .s4 = v[3], .sb5 = v[4], .s5 = v[5]); break;
			case 6: n = pco_src_3up_3b8i_2b8i_2b8i_encode(mesa, .sb3 = v[0], .s3 = v[1], .sb4 = v[2], .s4 = v[3], .sb5 = v[4], .s5 = v[5]); break;
			default: n = pco_src_3up_3b11i_2b8i_2b8i_encode(mesa, .sb3 = v[0], .s3 = v[1], .sb4 = v[2], .s4 = v[3], .sb5 = v[4], .s5 = v[5]); break;
			}
			if (n != form->layout.length) { printf("length upper %u: ours %u mesa %u\n", f, form->layout.length, n); fails++; }
			char w[32]; snprintf(w, sizeof w, "upper %u", f);
			cmp(w, ours, mesa, n, v);
		}
		/* Destination forms. */
		for (f = 0; f < 5; f++) {
			form = f < 2 ? &single_destination_forms[f] : &dual_destination_forms[f - 2];
			memset(v, 0, sizeof(v));
			for (n = 0; n < 2; n++) {
				if (form->limit[2 * n] < 0) continue;
				v[2 * n] = rnd(form->limit[2 * n]);
				v[2 * n + 1] = rnd(form->limit[2 * n + 1]);
			}
			memset(ours, 0, 8); memset(mesa, 0, 8);
			if (layout_place(&form->layout, v, ours) != 0) { printf("place dst %u\n", f); continue; }
			switch (f) {
			case 0: n = pco_dst_1_1b6i_encode(mesa, .dbN = v[0], .dN = v[1]); break;
			case 1: n = pco_dst_1_3b11i_encode(mesa, .dbN = v[0], .dN = v[1]); break;
			case 2: n = pco_dst_2_1b7i_1b6i_encode(mesa, .db0 = v[0], .d0 = v[1], .db1 = v[2], .d1 = v[3]); break;
			case 3: n = pco_dst_2_3b8i_3b8i_encode(mesa, .db0 = v[0], .d0 = v[1], .db1 = v[2], .d1 = v[3]); break;
			default: n = pco_dst_2_3b11i_3b11i_encode(mesa, .db0 = v[0], .d0 = v[1], .db1 = v[2], .d1 = v[3]); break;
			}
			if (n != form->layout.length) { printf("length dst %u: ours %u mesa %u\n", f, form->layout.length, n); fails++; }
			char w[32]; snprintf(w, sizeof w, "dst %u", f);
			cmp(w, ours, mesa, n, v);
		}
	}
	printf("m9 operand forms: %d passed, %d failed\n", passes, fails);
	return fails != 0;
}
