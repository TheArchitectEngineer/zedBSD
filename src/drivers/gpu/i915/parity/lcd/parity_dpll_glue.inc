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

#include "lcd_seq_compat.h"
#include "lcd_modeset_compat.h"
#include "lcd_dp_compat.h"
#include "lcd_plane_compat.h"
#include "parity_lcd_modeset_int.h"

/*
 * The shared DPLL of the modeset object: the reference's adlp_plls[] entry for DPLL 0 / DPLL 1
 * ({ name, &combo_pll_funcs, id }, no power domain) with the two combo_pll_funcs members used here.
 * state.pipe_mask is what intel_reference_shared_dpll() leaves for the one crtc.
 */
static const struct intel_shared_dpll_funcs parity_combo_pll_funcs = {
	.enable = combo_pll_enable,
	.disable = combo_pll_disable,
};

void parity_lcd_ms_bind_pll(struct parity_lcd_modeset *ms, int dpll_id)
{
	ms->pll_info.name = dpll_id == DPLL_ID_ICL_DPLL1 ? "DPLL 1" : "DPLL 0";
	ms->pll_info.funcs = &parity_combo_pll_funcs;
	ms->pll_info.id = dpll_id == DPLL_ID_ICL_DPLL1 ? DPLL_ID_ICL_DPLL1 : DPLL_ID_ICL_DPLL0;
	ms->pll_info.power_domain = 0;
	ms->pll.info = &ms->pll_info;
	ms->pll.state.pipe_mask = (u8)BIT(ms->crtc.pipe);
	ms->crtc_state.shared_dpll = &ms->pll;
}
