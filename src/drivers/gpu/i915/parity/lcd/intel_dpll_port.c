/*
 * Copyright © 2006-2016 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

/*
 * zedBSD WS031: generated from Linux v6.8.12 drivers/gpu/drm/i915/display/intel_dpll_mgr.c
 * (sha256 bc90f5855afccb4ef1c18f07b284c90fb67e27df96cf9d22545768d14abc7b00) by plan/ws031/handover/tools/port_lcd_calc.py.
 * The function bodies are the reference text.  Kept / changed:
 *  - kept, in the reference order: struct skl_wrpll_params, ehl_combo_pll_div_frac_wa_needed,
 *    struct icl_combo_pll_params, icl_dp_combo_pll_24MHz_values[], icl_dp_combo_pll_19_2MHz_values[],
 *    icl_calc_dp_combo_pll, icl_calc_dpll_state;
 *  - the includes are replaced by lcd_compat.h;
 *  - parity_dpll_glue.inc (zedBSD code) is included at the end of the file.
 */

#include "lcd_compat.h"	/* zedBSD: replaces the linux/ and i915 includes */

struct skl_wrpll_params {
	u32 dco_fraction;
	u32 dco_integer;
	u32 qdiv_ratio;
	u32 qdiv_mode;
	u32 kdiv;
	u32 pdiv;
	u32 central_freq;
};

/*
 * Display WA #22010492432: ehl, tgl, adl-s, adl-p
 * Program half of the nominal DCO divider fraction value.
 */
static bool
ehl_combo_pll_div_frac_wa_needed(struct drm_i915_private *i915)
{
	return ((IS_ELKHARTLAKE(i915) &&
		 IS_DISPLAY_STEP(i915, STEP_B0, STEP_FOREVER)) ||
		 IS_TIGERLAKE(i915) || IS_ALDERLAKE_S(i915) || IS_ALDERLAKE_P(i915)) &&
		 i915->display.dpll.ref_clks.nssc == 38400;
}

struct icl_combo_pll_params {
	int clock;
	struct skl_wrpll_params wrpll;
};

/*
 * These values alrea already adjusted: they're the bits we write to the
 * registers, not the logical values.
 */
static const struct icl_combo_pll_params icl_dp_combo_pll_24MHz_values[] = {
	{ 540000,
	  { .dco_integer = 0x151, .dco_fraction = 0x4000,		/* [0]: 5.4 */
	    .pdiv = 0x2 /* 3 */, .kdiv = 1, .qdiv_mode = 0, .qdiv_ratio = 0, }, },
	{ 270000,
	  { .dco_integer = 0x151, .dco_fraction = 0x4000,		/* [1]: 2.7 */
	    .pdiv = 0x2 /* 3 */, .kdiv = 2, .qdiv_mode = 0, .qdiv_ratio = 0, }, },
	{ 162000,
	  { .dco_integer = 0x151, .dco_fraction = 0x4000,		/* [2]: 1.62 */
	    .pdiv = 0x4 /* 5 */, .kdiv = 2, .qdiv_mode = 0, .qdiv_ratio = 0, }, },
	{ 324000,
	  { .dco_integer = 0x151, .dco_fraction = 0x4000,		/* [3]: 3.24 */
	    .pdiv = 0x4 /* 5 */, .kdiv = 1, .qdiv_mode = 0, .qdiv_ratio = 0, }, },
	{ 216000,
	  { .dco_integer = 0x168, .dco_fraction = 0x0000,		/* [4]: 2.16 */
	    .pdiv = 0x1 /* 2 */, .kdiv = 2, .qdiv_mode = 1, .qdiv_ratio = 2, }, },
	{ 432000,
	  { .dco_integer = 0x168, .dco_fraction = 0x0000,		/* [5]: 4.32 */
	    .pdiv = 0x1 /* 2 */, .kdiv = 2, .qdiv_mode = 0, .qdiv_ratio = 0, }, },
	{ 648000,
	  { .dco_integer = 0x195, .dco_fraction = 0x0000,		/* [6]: 6.48 */
	    .pdiv = 0x2 /* 3 */, .kdiv = 1, .qdiv_mode = 0, .qdiv_ratio = 0, }, },
	{ 810000,
	  { .dco_integer = 0x151, .dco_fraction = 0x4000,		/* [7]: 8.1 */
	    .pdiv = 0x1 /* 2 */, .kdiv = 1, .qdiv_mode = 0, .qdiv_ratio = 0, }, },
};


/* Also used for 38.4 MHz values. */
static const struct icl_combo_pll_params icl_dp_combo_pll_19_2MHz_values[] = {
	{ 540000,
	  { .dco_integer = 0x1A5, .dco_fraction = 0x7000,		/* [0]: 5.4 */
	    .pdiv = 0x2 /* 3 */, .kdiv = 1, .qdiv_mode = 0, .qdiv_ratio = 0, }, },
	{ 270000,
	  { .dco_integer = 0x1A5, .dco_fraction = 0x7000,		/* [1]: 2.7 */
	    .pdiv = 0x2 /* 3 */, .kdiv = 2, .qdiv_mode = 0, .qdiv_ratio = 0, }, },
	{ 162000,
	  { .dco_integer = 0x1A5, .dco_fraction = 0x7000,		/* [2]: 1.62 */
	    .pdiv = 0x4 /* 5 */, .kdiv = 2, .qdiv_mode = 0, .qdiv_ratio = 0, }, },
	{ 324000,
	  { .dco_integer = 0x1A5, .dco_fraction = 0x7000,		/* [3]: 3.24 */
	    .pdiv = 0x4 /* 5 */, .kdiv = 1, .qdiv_mode = 0, .qdiv_ratio = 0, }, },
	{ 216000,
	  { .dco_integer = 0x1C2, .dco_fraction = 0x0000,		/* [4]: 2.16 */
	    .pdiv = 0x1 /* 2 */, .kdiv = 2, .qdiv_mode = 1, .qdiv_ratio = 2, }, },
	{ 432000,
	  { .dco_integer = 0x1C2, .dco_fraction = 0x0000,		/* [5]: 4.32 */
	    .pdiv = 0x1 /* 2 */, .kdiv = 2, .qdiv_mode = 0, .qdiv_ratio = 0, }, },
	{ 648000,
	  { .dco_integer = 0x1FA, .dco_fraction = 0x2000,		/* [6]: 6.48 */
	    .pdiv = 0x2 /* 3 */, .kdiv = 1, .qdiv_mode = 0, .qdiv_ratio = 0, }, },
	{ 810000,
	  { .dco_integer = 0x1A5, .dco_fraction = 0x7000,		/* [7]: 8.1 */
	    .pdiv = 0x1 /* 2 */, .kdiv = 1, .qdiv_mode = 0, .qdiv_ratio = 0, }, },
};

static int icl_calc_dp_combo_pll(struct intel_crtc_state *crtc_state,
				 struct skl_wrpll_params *pll_params)
{
	struct drm_i915_private *i915 = to_i915(crtc_state->uapi.crtc->dev);
	const struct icl_combo_pll_params *params =
		i915->display.dpll.ref_clks.nssc == 24000 ?
		icl_dp_combo_pll_24MHz_values :
		icl_dp_combo_pll_19_2MHz_values;
	int clock = crtc_state->port_clock;
	int i;

	for (i = 0; i < ARRAY_SIZE(icl_dp_combo_pll_24MHz_values); i++) {
		if (clock == params[i].clock) {
			*pll_params = params[i].wrpll;
			return 0;
		}
	}

	MISSING_CASE(clock);
	return -EINVAL;
}

static void icl_calc_dpll_state(struct drm_i915_private *i915,
				const struct skl_wrpll_params *pll_params,
				struct intel_dpll_hw_state *pll_state)
{
	u32 dco_fraction = pll_params->dco_fraction;

	if (ehl_combo_pll_div_frac_wa_needed(i915))
		dco_fraction = DIV_ROUND_CLOSEST(dco_fraction, 2);

	pll_state->cfgcr0 = DPLL_CFGCR0_DCO_FRACTION(dco_fraction) |
			    pll_params->dco_integer;

	pll_state->cfgcr1 = DPLL_CFGCR1_QDIV_RATIO(pll_params->qdiv_ratio) |
			    DPLL_CFGCR1_QDIV_MODE(pll_params->qdiv_mode) |
			    DPLL_CFGCR1_KDIV(pll_params->kdiv) |
			    DPLL_CFGCR1_PDIV(pll_params->pdiv);

	if (DISPLAY_VER(i915) >= 12)
		pll_state->cfgcr1 |= TGL_DPLL_CFGCR1_CFSELOVRD_NORMAL_XTAL;
	else
		pll_state->cfgcr1 |= DPLL_CFGCR1_CENTRAL_FREQ_8400;

	if (i915->display.vbt.override_afc_startup)
		pll_state->div0 = TGL_DPLL0_DIV0_AFC_STARTUP(i915->display.vbt.override_afc_startup_val);
}

#include "parity_dpll_glue.inc"	/* zedBSD: entry point */
