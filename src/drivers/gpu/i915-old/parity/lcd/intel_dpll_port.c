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
#include "lcd_modeset_compat.h"

struct skl_wrpll_params {
	u32 dco_fraction;
	u32 dco_integer;
	u32 qdiv_ratio;
	u32 qdiv_mode;
	u32 kdiv;
	u32 pdiv;
	u32 central_freq;
};

/* zedBSD: forward declarations of the static functions of this file (generated; the keep-list is not in call order) */
static bool
ehl_combo_pll_div_frac_wa_needed(struct drm_i915_private *i915);
static int icl_calc_dp_combo_pll(struct intel_crtc_state *crtc_state,
				 struct skl_wrpll_params *pll_params);
static void icl_calc_dpll_state(struct drm_i915_private *i915,
				const struct skl_wrpll_params *pll_params,
				struct intel_dpll_hw_state *pll_state);
static i915_reg_t
intel_combo_pll_enable_reg(struct drm_i915_private *i915,
			   struct intel_shared_dpll *pll);
static void icl_pll_power_enable(struct drm_i915_private *i915,
				 struct intel_shared_dpll *pll,
				 i915_reg_t enable_reg);
static void icl_dpll_write(struct drm_i915_private *i915,
			   struct intel_shared_dpll *pll);
static void icl_pll_enable(struct drm_i915_private *i915,
			   struct intel_shared_dpll *pll,
			   i915_reg_t enable_reg);
static void adlp_cmtg_clock_gating_wa(struct drm_i915_private *i915, struct intel_shared_dpll *pll);
static void combo_pll_enable(struct drm_i915_private *i915,
			     struct intel_shared_dpll *pll);
static void icl_pll_disable(struct drm_i915_private *i915,
			    struct intel_shared_dpll *pll,
			    i915_reg_t enable_reg);
static void combo_pll_disable(struct drm_i915_private *i915,
			      struct intel_shared_dpll *pll);
static void _intel_enable_shared_dpll(struct drm_i915_private *i915,
				      struct intel_shared_dpll *pll);
static void _intel_disable_shared_dpll(struct drm_i915_private *i915,
				       struct intel_shared_dpll *pll);
static int icl_wrpll_ref_clock(struct drm_i915_private *i915);
static void icl_wrpll_get_multipliers(int bestdiv, int *pdiv,
				      int *qdiv, int *kdiv);
static void icl_wrpll_params_populate(struct skl_wrpll_params *params,
				      u32 dco_freq, u32 ref_freq,
				      int pdiv, int qdiv, int kdiv);
static int
icl_calc_wrpll(struct intel_crtc_state *crtc_state,
	       struct skl_wrpll_params *wrpll_params);
static unsigned long
intel_dpll_mask_all(struct drm_i915_private *i915);
static struct intel_shared_dpll *
intel_find_shared_dpll(struct intel_atomic_state *state,
		       const struct intel_crtc *crtc,
		       const struct intel_dpll_hw_state *pll_state,
		       unsigned long dpll_mask);
static void
intel_reference_shared_dpll_crtc(const struct intel_crtc *crtc,
				 const struct intel_shared_dpll *pll,
				 struct intel_shared_dpll_state *shared_dpll_state);
static void
intel_reference_shared_dpll(struct intel_atomic_state *state,
			    const struct intel_crtc *crtc,
			    const struct intel_shared_dpll *pll,
			    const struct intel_dpll_hw_state *pll_state);
static void intel_unreference_shared_dpll(struct intel_atomic_state *state,
					  const struct intel_crtc *crtc,
					  const struct intel_shared_dpll *pll);
static int icl_ddi_combo_pll_get_freq(struct drm_i915_private *i915,
				      const struct intel_shared_dpll *pll,
				      const struct intel_dpll_hw_state *pll_state);
static bool combo_pll_get_hw_state(struct drm_i915_private *i915,
				   struct intel_shared_dpll *pll,
				   struct intel_dpll_hw_state *hw_state);
static bool icl_pll_get_hw_state(struct drm_i915_private *i915,
				 struct intel_shared_dpll *pll,
				 struct intel_dpll_hw_state *hw_state,
				 i915_reg_t enable_reg);
static void readout_dpll_hw_state(struct drm_i915_private *i915,
				  struct intel_shared_dpll *pll);
static void sanitize_dpll_state(struct drm_i915_private *i915,
				struct intel_shared_dpll *pll);

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

static i915_reg_t
intel_combo_pll_enable_reg(struct drm_i915_private *i915,
			   struct intel_shared_dpll *pll)
{
	if (IS_DG1(i915))
		return DG1_DPLL_ENABLE(pll->info->id);
	else if ((IS_JASPERLAKE(i915) || IS_ELKHARTLAKE(i915)) &&
		 (pll->info->id == DPLL_ID_EHL_DPLL4))
		return MG_PLL_ENABLE(0);

	return ICL_DPLL_ENABLE(pll->info->id);
}

static void icl_pll_power_enable(struct drm_i915_private *i915,
				 struct intel_shared_dpll *pll,
				 i915_reg_t enable_reg)
{
	intel_de_rmw(i915, enable_reg, 0, PLL_POWER_ENABLE);

	/*
	 * The spec says we need to "wait" but it also says it should be
	 * immediate.
	 */
	if (intel_de_wait_for_set(i915, enable_reg, PLL_POWER_STATE, 1))
		drm_err(&i915->drm, "PLL %d Power not enabled\n",
			pll->info->id);
}

static void icl_dpll_write(struct drm_i915_private *i915,
			   struct intel_shared_dpll *pll)
{
	struct intel_dpll_hw_state *hw_state = &pll->state.hw_state;
	const enum intel_dpll_id id = pll->info->id;
	i915_reg_t cfgcr0_reg, cfgcr1_reg, div0_reg = INVALID_MMIO_REG;

	if (IS_ALDERLAKE_S(i915)) {
		cfgcr0_reg = ADLS_DPLL_CFGCR0(id);
		cfgcr1_reg = ADLS_DPLL_CFGCR1(id);
	} else if (IS_DG1(i915)) {
		cfgcr0_reg = DG1_DPLL_CFGCR0(id);
		cfgcr1_reg = DG1_DPLL_CFGCR1(id);
	} else if (IS_ROCKETLAKE(i915)) {
		cfgcr0_reg = RKL_DPLL_CFGCR0(id);
		cfgcr1_reg = RKL_DPLL_CFGCR1(id);
	} else if (DISPLAY_VER(i915) >= 12) {
		cfgcr0_reg = TGL_DPLL_CFGCR0(id);
		cfgcr1_reg = TGL_DPLL_CFGCR1(id);
		div0_reg = TGL_DPLL0_DIV0(id);
	} else {
		if ((IS_JASPERLAKE(i915) || IS_ELKHARTLAKE(i915)) &&
		    id == DPLL_ID_EHL_DPLL4) {
			cfgcr0_reg = ICL_DPLL_CFGCR0(4);
			cfgcr1_reg = ICL_DPLL_CFGCR1(4);
		} else {
			cfgcr0_reg = ICL_DPLL_CFGCR0(id);
			cfgcr1_reg = ICL_DPLL_CFGCR1(id);
		}
	}

	intel_de_write(i915, cfgcr0_reg, hw_state->cfgcr0);
	intel_de_write(i915, cfgcr1_reg, hw_state->cfgcr1);
	drm_WARN_ON_ONCE(&i915->drm, i915->display.vbt.override_afc_startup &&
			 !i915_mmio_reg_valid(div0_reg));
	if (i915->display.vbt.override_afc_startup &&
	    i915_mmio_reg_valid(div0_reg))
		intel_de_rmw(i915, div0_reg,
			     TGL_DPLL0_DIV0_AFC_STARTUP_MASK, hw_state->div0);
	intel_de_posting_read(i915, cfgcr1_reg);
}

static void icl_pll_enable(struct drm_i915_private *i915,
			   struct intel_shared_dpll *pll,
			   i915_reg_t enable_reg)
{
	intel_de_rmw(i915, enable_reg, 0, PLL_ENABLE);

	/* Timeout is actually 600us. */
	if (intel_de_wait_for_set(i915, enable_reg, PLL_LOCK, 1))
		drm_err(&i915->drm, "PLL %d not locked\n", pll->info->id);
}

static void adlp_cmtg_clock_gating_wa(struct drm_i915_private *i915, struct intel_shared_dpll *pll)
{
	u32 val;

	if (!(IS_ALDERLAKE_P(i915) && IS_DISPLAY_STEP(i915, STEP_A0, STEP_B0)) ||
	    pll->info->id != DPLL_ID_ICL_DPLL0)
		return;
	/*
	 * Wa_16011069516:adl-p[a0]
	 *
	 * All CMTG regs are unreliable until CMTG clock gating is disabled,
	 * so we can only assume the default TRANS_CMTG_CHICKEN reg value and
	 * sanity check this assumption with a double read, which presumably
	 * returns the correct value even with clock gating on.
	 *
	 * Instead of the usual place for workarounds we apply this one here,
	 * since TRANS_CMTG_CHICKEN is only accessible while DPLL0 is enabled.
	 */
	val = intel_de_read(i915, TRANS_CMTG_CHICKEN);
	val = intel_de_rmw(i915, TRANS_CMTG_CHICKEN, ~0, DISABLE_DPT_CLK_GATING);
	if (drm_WARN_ON(&i915->drm, val & ~DISABLE_DPT_CLK_GATING))
		drm_dbg_kms(&i915->drm, "Unexpected flags in TRANS_CMTG_CHICKEN: %08x\n", val);
}

static void combo_pll_enable(struct drm_i915_private *i915,
			     struct intel_shared_dpll *pll)
{
	i915_reg_t enable_reg = intel_combo_pll_enable_reg(i915, pll);

	icl_pll_power_enable(i915, pll, enable_reg);

	icl_dpll_write(i915, pll);

	/*
	 * DVFS pre sequence would be here, but in our driver the cdclk code
	 * paths should already be setting the appropriate voltage, hence we do
	 * nothing here.
	 */

	icl_pll_enable(i915, pll, enable_reg);

	adlp_cmtg_clock_gating_wa(i915, pll);

	/* DVFS post sequence would be here. See the comment above. */
}

static void icl_pll_disable(struct drm_i915_private *i915,
			    struct intel_shared_dpll *pll,
			    i915_reg_t enable_reg)
{
	/* The first steps are done by intel_ddi_post_disable(). */

	/*
	 * DVFS pre sequence would be here, but in our driver the cdclk code
	 * paths should already be setting the appropriate voltage, hence we do
	 * nothing here.
	 */

	intel_de_rmw(i915, enable_reg, PLL_ENABLE, 0);

	/* Timeout is actually 1us. */
	if (intel_de_wait_for_clear(i915, enable_reg, PLL_LOCK, 1))
		drm_err(&i915->drm, "PLL %d locked\n", pll->info->id);

	/* DVFS post sequence would be here. See the comment above. */

	intel_de_rmw(i915, enable_reg, PLL_POWER_ENABLE, 0);

	/*
	 * The spec says we need to "wait" but it also says it should be
	 * immediate.
	 */
	if (intel_de_wait_for_clear(i915, enable_reg, PLL_POWER_STATE, 1))
		drm_err(&i915->drm, "PLL %d Power not disabled\n",
			pll->info->id);
}

static void combo_pll_disable(struct drm_i915_private *i915,
			      struct intel_shared_dpll *pll)
{
	i915_reg_t enable_reg = intel_combo_pll_enable_reg(i915, pll);

	icl_pll_disable(i915, pll, enable_reg);
}

static void _intel_enable_shared_dpll(struct drm_i915_private *i915,
				      struct intel_shared_dpll *pll)
{
	if (pll->info->power_domain)
		pll->wakeref = intel_display_power_get(i915, pll->info->power_domain);

	pll->info->funcs->enable(i915, pll);
	pll->on = true;
}

/**
 * intel_enable_shared_dpll - enable a CRTC's shared DPLL
 * @crtc_state: CRTC, and its state, which has a shared DPLL
 *
 * Enable the shared DPLL used by @crtc.
 */
void intel_enable_shared_dpll(const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_i915_private *i915 = to_i915(crtc->base.dev);
	struct intel_shared_dpll *pll = crtc_state->shared_dpll;
	unsigned int pipe_mask = BIT(crtc->pipe);
	unsigned int old_mask;

	if (drm_WARN_ON(&i915->drm, pll == NULL))
		return;

	mutex_lock(&i915->display.dpll.lock);
	old_mask = pll->active_mask;

	if (drm_WARN_ON(&i915->drm, !(pll->state.pipe_mask & pipe_mask)) ||
	    drm_WARN_ON(&i915->drm, pll->active_mask & pipe_mask))
		goto out;

	pll->active_mask |= pipe_mask;

	drm_dbg_kms(&i915->drm,
		    "enable %s (active 0x%x, on? %d) for [CRTC:%d:%s]\n",
		    pll->info->name, pll->active_mask, pll->on,
		    crtc->base.base.id, crtc->base.name);

	if (old_mask) {
		drm_WARN_ON(&i915->drm, !pll->on);
		assert_shared_dpll_enabled(i915, pll);
		goto out;
	}
	drm_WARN_ON(&i915->drm, pll->on);

	drm_dbg_kms(&i915->drm, "enabling %s\n", pll->info->name);

	_intel_enable_shared_dpll(i915, pll);

out:
	mutex_unlock(&i915->display.dpll.lock);
}

static void _intel_disable_shared_dpll(struct drm_i915_private *i915,
				       struct intel_shared_dpll *pll)
{
	pll->info->funcs->disable(i915, pll);
	pll->on = false;

	if (pll->info->power_domain)
		intel_display_power_put(i915, pll->info->power_domain, pll->wakeref);
}

/**
 * intel_disable_shared_dpll - disable a CRTC's shared DPLL
 * @crtc_state: CRTC, and its state, which has a shared DPLL
 *
 * Disable the shared DPLL used by @crtc.
 */
void intel_disable_shared_dpll(const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_i915_private *i915 = to_i915(crtc->base.dev);
	struct intel_shared_dpll *pll = crtc_state->shared_dpll;
	unsigned int pipe_mask = BIT(crtc->pipe);

	/* PCH only available on ILK+ */
	if (DISPLAY_VER(i915) < 5)
		return;

	if (pll == NULL)
		return;

	mutex_lock(&i915->display.dpll.lock);
	if (drm_WARN(&i915->drm, !(pll->active_mask & pipe_mask),
		     "%s not used by [CRTC:%d:%s]\n", pll->info->name,
		     crtc->base.base.id, crtc->base.name))
		goto out;

	drm_dbg_kms(&i915->drm,
		    "disable %s (active 0x%x, on? %d) for [CRTC:%d:%s]\n",
		    pll->info->name, pll->active_mask, pll->on,
		    crtc->base.base.id, crtc->base.name);

	assert_shared_dpll_enabled(i915, pll);
	drm_WARN_ON(&i915->drm, !pll->on);

	pll->active_mask &= ~pipe_mask;
	if (pll->active_mask)
		goto out;

	drm_dbg_kms(&i915->drm, "disabling %s\n", pll->info->name);

	_intel_disable_shared_dpll(i915, pll);

out:
	mutex_unlock(&i915->display.dpll.lock);
}

static int icl_wrpll_ref_clock(struct drm_i915_private *i915)
{
	int ref_clock = i915->display.dpll.ref_clks.nssc;

	/*
	 * For ICL+, the spec states: if reference frequency is 38.4,
	 * use 19.2 because the DPLL automatically divides that by 2.
	 */
	if (ref_clock == 38400)
		ref_clock = 19200;

	return ref_clock;
}

static void icl_wrpll_get_multipliers(int bestdiv, int *pdiv,
				      int *qdiv, int *kdiv)
{
	/* even dividers */
	if (bestdiv % 2 == 0) {
		if (bestdiv == 2) {
			*pdiv = 2;
			*qdiv = 1;
			*kdiv = 1;
		} else if (bestdiv % 4 == 0) {
			*pdiv = 2;
			*qdiv = bestdiv / 4;
			*kdiv = 2;
		} else if (bestdiv % 6 == 0) {
			*pdiv = 3;
			*qdiv = bestdiv / 6;
			*kdiv = 2;
		} else if (bestdiv % 5 == 0) {
			*pdiv = 5;
			*qdiv = bestdiv / 10;
			*kdiv = 2;
		} else if (bestdiv % 14 == 0) {
			*pdiv = 7;
			*qdiv = bestdiv / 14;
			*kdiv = 2;
		}
	} else {
		if (bestdiv == 3 || bestdiv == 5 || bestdiv == 7) {
			*pdiv = bestdiv;
			*qdiv = 1;
			*kdiv = 1;
		} else { /* 9, 15, 21 */
			*pdiv = bestdiv / 3;
			*qdiv = 1;
			*kdiv = 3;
		}
	}
}

static void icl_wrpll_params_populate(struct skl_wrpll_params *params,
				      u32 dco_freq, u32 ref_freq,
				      int pdiv, int qdiv, int kdiv)
{
	u32 dco;

	switch (kdiv) {
	case 1:
		params->kdiv = 1;
		break;
	case 2:
		params->kdiv = 2;
		break;
	case 3:
		params->kdiv = 4;
		break;
	default:
		WARN(1, "Incorrect KDiv\n");
	}

	switch (pdiv) {
	case 2:
		params->pdiv = 1;
		break;
	case 3:
		params->pdiv = 2;
		break;
	case 5:
		params->pdiv = 4;
		break;
	case 7:
		params->pdiv = 8;
		break;
	default:
		WARN(1, "Incorrect PDiv\n");
	}

	WARN_ON(kdiv != 2 && qdiv != 1);

	params->qdiv_ratio = qdiv;
	params->qdiv_mode = (qdiv == 1) ? 0 : 1;

	dco = div_u64((u64)dco_freq << 15, ref_freq);

	params->dco_integer = dco >> 15;
	params->dco_fraction = dco & 0x7fff;
}

static int
icl_calc_wrpll(struct intel_crtc_state *crtc_state,
	       struct skl_wrpll_params *wrpll_params)
{
	struct drm_i915_private *i915 = to_i915(crtc_state->uapi.crtc->dev);
	int ref_clock = icl_wrpll_ref_clock(i915);
	u32 afe_clock = crtc_state->port_clock * 5;
	u32 dco_min = 7998000;
	u32 dco_max = 10000000;
	u32 dco_mid = (dco_min + dco_max) / 2;
	static const int dividers[] = {  2,  4,  6,  8, 10, 12,  14,  16,
					 18, 20, 24, 28, 30, 32,  36,  40,
					 42, 44, 48, 50, 52, 54,  56,  60,
					 64, 66, 68, 70, 72, 76,  78,  80,
					 84, 88, 90, 92, 96, 98, 100, 102,
					  3,  5,  7,  9, 15, 21 };
	u32 dco, best_dco = 0, dco_centrality = 0;
	u32 best_dco_centrality = U32_MAX; /* Spec meaning of 999999 MHz */
	int d, best_div = 0, pdiv = 0, qdiv = 0, kdiv = 0;

	for (d = 0; d < ARRAY_SIZE(dividers); d++) {
		dco = afe_clock * dividers[d];

		if (dco <= dco_max && dco >= dco_min) {
			dco_centrality = abs(dco - dco_mid);

			if (dco_centrality < best_dco_centrality) {
				best_dco_centrality = dco_centrality;
				best_div = dividers[d];
				best_dco = dco;
			}
		}
	}

	if (best_div == 0)
		return -EINVAL;

	icl_wrpll_get_multipliers(best_div, &pdiv, &qdiv, &kdiv);
	icl_wrpll_params_populate(wrpll_params, best_dco, ref_clock,
				  pdiv, qdiv, kdiv);

	return 0;
}

/**
 * intel_get_shared_dpll_by_id - get a DPLL given its id
 * @i915: i915 device instance
 * @id: pll id
 *
 * Returns:
 * A pointer to the DPLL with @id
 */
struct intel_shared_dpll *
intel_get_shared_dpll_by_id(struct drm_i915_private *i915,
			    enum intel_dpll_id id)
{
	struct intel_shared_dpll *pll;
	int i;

	for_each_shared_dpll(i915, pll, i) {
		if (pll->info->id == id)
			return pll;
	}

	MISSING_CASE(id);
	return NULL;
}

static unsigned long
intel_dpll_mask_all(struct drm_i915_private *i915)
{
	struct intel_shared_dpll *pll;
	unsigned long dpll_mask = 0;
	int i;

	for_each_shared_dpll(i915, pll, i) {
		drm_WARN_ON(&i915->drm, dpll_mask & BIT(pll->info->id));

		dpll_mask |= BIT(pll->info->id);
	}

	return dpll_mask;
}

static struct intel_shared_dpll *
intel_find_shared_dpll(struct intel_atomic_state *state,
		       const struct intel_crtc *crtc,
		       const struct intel_dpll_hw_state *pll_state,
		       unsigned long dpll_mask)
{
	struct drm_i915_private *i915 = to_i915(crtc->base.dev);
	unsigned long dpll_mask_all = intel_dpll_mask_all(i915);
	struct intel_shared_dpll_state *shared_dpll;
	struct intel_shared_dpll *unused_pll = NULL;
	enum intel_dpll_id id;

	shared_dpll = intel_atomic_get_shared_dpll_state(&state->base);

	drm_WARN_ON(&i915->drm, dpll_mask & ~dpll_mask_all);

	for_each_set_bit(id, &dpll_mask, fls(dpll_mask_all)) {
		struct intel_shared_dpll *pll;

		pll = intel_get_shared_dpll_by_id(i915, id);
		if (!pll)
			continue;

		/* Only want to check enabled timings first */
		if (shared_dpll[pll->index].pipe_mask == 0) {
			if (!unused_pll)
				unused_pll = pll;
			continue;
		}

		if (memcmp(pll_state,
			   &shared_dpll[pll->index].hw_state,
			   sizeof(*pll_state)) == 0) {
			drm_dbg_kms(&i915->drm,
				    "[CRTC:%d:%s] sharing existing %s (pipe mask 0x%x, active 0x%x)\n",
				    crtc->base.base.id, crtc->base.name,
				    pll->info->name,
				    shared_dpll[pll->index].pipe_mask,
				    pll->active_mask);
			return pll;
		}
	}

	/* Ok no matching timings, maybe there's a free one? */
	if (unused_pll) {
		drm_dbg_kms(&i915->drm, "[CRTC:%d:%s] allocated %s\n",
			    crtc->base.base.id, crtc->base.name,
			    unused_pll->info->name);
		return unused_pll;
	}

	return NULL;
}

/**
 * intel_reference_shared_dpll_crtc - Get a DPLL reference for a CRTC
 * @crtc: CRTC on which behalf the reference is taken
 * @pll: DPLL for which the reference is taken
 * @shared_dpll_state: the DPLL atomic state in which the reference is tracked
 *
 * Take a reference for @pll tracking the use of it by @crtc.
 */
static void
intel_reference_shared_dpll_crtc(const struct intel_crtc *crtc,
				 const struct intel_shared_dpll *pll,
				 struct intel_shared_dpll_state *shared_dpll_state)
{
	struct drm_i915_private *i915 = to_i915(crtc->base.dev);

	drm_WARN_ON(&i915->drm, (shared_dpll_state->pipe_mask & BIT(crtc->pipe)) != 0);

	shared_dpll_state->pipe_mask |= BIT(crtc->pipe);

	drm_dbg_kms(&i915->drm, "[CRTC:%d:%s] reserving %s\n",
		    crtc->base.base.id, crtc->base.name, pll->info->name);
}

static void
intel_reference_shared_dpll(struct intel_atomic_state *state,
			    const struct intel_crtc *crtc,
			    const struct intel_shared_dpll *pll,
			    const struct intel_dpll_hw_state *pll_state)
{
	struct intel_shared_dpll_state *shared_dpll;

	shared_dpll = intel_atomic_get_shared_dpll_state(&state->base);

	if (shared_dpll[pll->index].pipe_mask == 0)
		shared_dpll[pll->index].hw_state = *pll_state;

	intel_reference_shared_dpll_crtc(crtc, pll, &shared_dpll[pll->index]);
}

/**
 * intel_unreference_shared_dpll_crtc - Drop a DPLL reference for a CRTC
 * @crtc: CRTC on which behalf the reference is dropped
 * @pll: DPLL for which the reference is dropped
 * @shared_dpll_state: the DPLL atomic state in which the reference is tracked
 *
 * Drop a reference for @pll tracking the end of use of it by @crtc.
 */
void
intel_unreference_shared_dpll_crtc(const struct intel_crtc *crtc,
				   const struct intel_shared_dpll *pll,
				   struct intel_shared_dpll_state *shared_dpll_state)
{
	struct drm_i915_private *i915 = to_i915(crtc->base.dev);

	drm_WARN_ON(&i915->drm, (shared_dpll_state->pipe_mask & BIT(crtc->pipe)) == 0);

	shared_dpll_state->pipe_mask &= ~BIT(crtc->pipe);

	drm_dbg_kms(&i915->drm, "[CRTC:%d:%s] releasing %s\n",
		    crtc->base.base.id, crtc->base.name, pll->info->name);
}

static void intel_unreference_shared_dpll(struct intel_atomic_state *state,
					  const struct intel_crtc *crtc,
					  const struct intel_shared_dpll *pll)
{
	struct intel_shared_dpll_state *shared_dpll;

	shared_dpll = intel_atomic_get_shared_dpll_state(&state->base);

	intel_unreference_shared_dpll_crtc(crtc, pll, &shared_dpll[pll->index]);
}

static int icl_ddi_combo_pll_get_freq(struct drm_i915_private *i915,
				      const struct intel_shared_dpll *pll,
				      const struct intel_dpll_hw_state *pll_state)
{
	int ref_clock = icl_wrpll_ref_clock(i915);
	u32 dco_fraction;
	u32 p0, p1, p2, dco_freq;

	p0 = pll_state->cfgcr1 & DPLL_CFGCR1_PDIV_MASK;
	p2 = pll_state->cfgcr1 & DPLL_CFGCR1_KDIV_MASK;

	if (pll_state->cfgcr1 & DPLL_CFGCR1_QDIV_MODE(1))
		p1 = (pll_state->cfgcr1 & DPLL_CFGCR1_QDIV_RATIO_MASK) >>
			DPLL_CFGCR1_QDIV_RATIO_SHIFT;
	else
		p1 = 1;

	switch (p0) {
	case DPLL_CFGCR1_PDIV_2:
		p0 = 2;
		break;
	case DPLL_CFGCR1_PDIV_3:
		p0 = 3;
		break;
	case DPLL_CFGCR1_PDIV_5:
		p0 = 5;
		break;
	case DPLL_CFGCR1_PDIV_7:
		p0 = 7;
		break;
	}

	switch (p2) {
	case DPLL_CFGCR1_KDIV_1:
		p2 = 1;
		break;
	case DPLL_CFGCR1_KDIV_2:
		p2 = 2;
		break;
	case DPLL_CFGCR1_KDIV_3:
		p2 = 3;
		break;
	}

	dco_freq = (pll_state->cfgcr0 & DPLL_CFGCR0_DCO_INTEGER_MASK) *
		   ref_clock;

	dco_fraction = (pll_state->cfgcr0 & DPLL_CFGCR0_DCO_FRACTION_MASK) >>
		       DPLL_CFGCR0_DCO_FRACTION_SHIFT;

	if (ehl_combo_pll_div_frac_wa_needed(i915))
		dco_fraction *= 2;

	dco_freq += (dco_fraction * ref_clock) / 0x8000;

	if (drm_WARN_ON(&i915->drm, p0 == 0 || p1 == 0 || p2 == 0))
		return 0;

	return dco_freq / (p0 * p1 * p2 * 5);
}

/**
 * intel_dpll_get_freq - calculate the DPLL's output frequency
 * @i915: i915 device
 * @pll: DPLL for which to calculate the output frequency
 * @pll_state: DPLL state from which to calculate the output frequency
 *
 * Return the output frequency corresponding to @pll's passed in @pll_state.
 */
int intel_dpll_get_freq(struct drm_i915_private *i915,
			const struct intel_shared_dpll *pll,
			const struct intel_dpll_hw_state *pll_state)
{
	if (drm_WARN_ON(&i915->drm, !pll->info->funcs->get_freq))
		return 0;

	return pll->info->funcs->get_freq(i915, pll, pll_state);
}

/**
 * intel_dpll_get_hw_state - readout the DPLL's hardware state
 * @i915: i915 device
 * @pll: DPLL for which to calculate the output frequency
 * @hw_state: DPLL's hardware state
 *
 * Read out @pll's hardware state into @hw_state.
 */
bool intel_dpll_get_hw_state(struct drm_i915_private *i915,
			     struct intel_shared_dpll *pll,
			     struct intel_dpll_hw_state *hw_state)
{
	return pll->info->funcs->get_hw_state(i915, pll, hw_state);
}

static bool combo_pll_get_hw_state(struct drm_i915_private *i915,
				   struct intel_shared_dpll *pll,
				   struct intel_dpll_hw_state *hw_state)
{
	i915_reg_t enable_reg = intel_combo_pll_enable_reg(i915, pll);

	return icl_pll_get_hw_state(i915, pll, hw_state, enable_reg);
}

static bool icl_pll_get_hw_state(struct drm_i915_private *i915,
				 struct intel_shared_dpll *pll,
				 struct intel_dpll_hw_state *hw_state,
				 i915_reg_t enable_reg)
{
	const enum intel_dpll_id id = pll->info->id;
	intel_wakeref_t wakeref;
	bool ret = false;
	u32 val;

	wakeref = intel_display_power_get_if_enabled(i915,
						     POWER_DOMAIN_DISPLAY_CORE);
	if (!wakeref)
		return false;

	val = intel_de_read(i915, enable_reg);
	if (!(val & PLL_ENABLE))
		goto out;

	if (IS_ALDERLAKE_S(i915)) {
		hw_state->cfgcr0 = intel_de_read(i915, ADLS_DPLL_CFGCR0(id));
		hw_state->cfgcr1 = intel_de_read(i915, ADLS_DPLL_CFGCR1(id));
	} else if (IS_DG1(i915)) {
		hw_state->cfgcr0 = intel_de_read(i915, DG1_DPLL_CFGCR0(id));
		hw_state->cfgcr1 = intel_de_read(i915, DG1_DPLL_CFGCR1(id));
	} else if (IS_ROCKETLAKE(i915)) {
		hw_state->cfgcr0 = intel_de_read(i915,
						 RKL_DPLL_CFGCR0(id));
		hw_state->cfgcr1 = intel_de_read(i915,
						 RKL_DPLL_CFGCR1(id));
	} else if (DISPLAY_VER(i915) >= 12) {
		hw_state->cfgcr0 = intel_de_read(i915,
						 TGL_DPLL_CFGCR0(id));
		hw_state->cfgcr1 = intel_de_read(i915,
						 TGL_DPLL_CFGCR1(id));
		if (i915->display.vbt.override_afc_startup) {
			hw_state->div0 = intel_de_read(i915, TGL_DPLL0_DIV0(id));
			hw_state->div0 &= TGL_DPLL0_DIV0_AFC_STARTUP_MASK;
		}
	} else {
		if ((IS_JASPERLAKE(i915) || IS_ELKHARTLAKE(i915)) &&
		    id == DPLL_ID_EHL_DPLL4) {
			hw_state->cfgcr0 = intel_de_read(i915,
							 ICL_DPLL_CFGCR0(4));
			hw_state->cfgcr1 = intel_de_read(i915,
							 ICL_DPLL_CFGCR1(4));
		} else {
			hw_state->cfgcr0 = intel_de_read(i915,
							 ICL_DPLL_CFGCR0(id));
			hw_state->cfgcr1 = intel_de_read(i915,
							 ICL_DPLL_CFGCR1(id));
		}
	}

	ret = true;
out:
	intel_display_power_put(i915, POWER_DOMAIN_DISPLAY_CORE, wakeref);
	return ret;
}

static void readout_dpll_hw_state(struct drm_i915_private *i915,
				  struct intel_shared_dpll *pll)
{
	struct intel_crtc *crtc;

	pll->on = intel_dpll_get_hw_state(i915, pll, &pll->state.hw_state);

	if (pll->on && pll->info->power_domain)
		pll->wakeref = intel_display_power_get(i915, pll->info->power_domain);

	pll->state.pipe_mask = 0;
	for_each_intel_crtc(&i915->drm, crtc) {
		struct intel_crtc_state *crtc_state =
			to_intel_crtc_state(crtc->base.state);

		if (crtc_state->hw.active && crtc_state->shared_dpll == pll)
			intel_reference_shared_dpll_crtc(crtc, pll, &pll->state);
	}
	pll->active_mask = pll->state.pipe_mask;

	drm_dbg_kms(&i915->drm,
		    "%s hw state readout: pipe_mask 0x%x, on %i\n",
		    pll->info->name, pll->state.pipe_mask, pll->on);
}

void intel_dpll_readout_hw_state(struct drm_i915_private *i915)
{
	struct intel_shared_dpll *pll;
	int i;

	for_each_shared_dpll(i915, pll, i)
		readout_dpll_hw_state(i915, pll);
}

static void sanitize_dpll_state(struct drm_i915_private *i915,
				struct intel_shared_dpll *pll)
{
	if (!pll->on)
		return;

	adlp_cmtg_clock_gating_wa(i915, pll);

	if (pll->active_mask)
		return;

	drm_dbg_kms(&i915->drm,
		    "%s enabled but not in use, disabling\n",
		    pll->info->name);

	_intel_disable_shared_dpll(i915, pll);
}

void intel_dpll_sanitize_state(struct drm_i915_private *i915)
{
	struct intel_shared_dpll *pll;
	int i;

	for_each_shared_dpll(i915, pll, i)
		sanitize_dpll_state(i915, pll);
}


#include "parity_dpll_glue.inc"	/* zedBSD: entry point */
