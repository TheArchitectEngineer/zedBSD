/*
 * WS031 Linux-parity — zedBSD glue at the end of intel_dpll_port.c: the combo PLL
 * words for a DP port clock, through the reference's table lookup
 * (icl_calc_dp_combo_pll) and state packing (icl_calc_dpll_state, which applies
 * Display WA #22010492432 -- half the DCO fraction -- when the reference clock is
 * 38.4 MHz on this platform).
 */
int parity_icl_dp_combo_pll(int port_clock, int ref_nssc, u32 *cfgcr0, u32 *cfgcr1, u32 *div0)
{
	static struct drm_i915_private i915;
	struct drm_crtc crtc;
	struct intel_crtc_state crtc_state;
	struct skl_wrpll_params params;
	struct intel_dpll_hw_state hw;
	int rc;

	memset(&i915, 0, sizeof(i915));
	memset(&crtc_state, 0, sizeof(crtc_state));
	memset(&params, 0, sizeof(params));
	memset(&hw, 0, sizeof(hw));
	i915.display.dpll.ref_clks.nssc = ref_nssc;
	crtc.dev = &i915.drm;
	crtc_state.uapi.crtc = &crtc;
	crtc_state.port_clock = port_clock;

	rc = icl_calc_dp_combo_pll(&crtc_state, &params);
	if (rc != 0)
		return rc;
	icl_calc_dpll_state(&i915, &params, &hw);
	*cfgcr0 = hw.cfgcr0;
	*cfgcr1 = hw.cfgcr1;
	*div0 = hw.div0;
	return 0;
}
