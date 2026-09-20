/*
 * WS031 Linux-parity — zedBSD glue at the end of intel_dpll_port.c: the combo PLL
 * words for a DP port clock, through the reference's table lookup
 * (icl_calc_dp_combo_pll) and state packing (icl_calc_dpll_state, which applies
 * Display WA #22010492432 -- half the DCO fraction -- when the reference clock is
 * 38.4 MHz on this platform).
 */
/*
 * The HDMI TMDS clock's PLL state: icl_calc_wrpll() + icl_calc_dpll_state(), the reference's own text.
 * `port_clock` is the TMDS clock in kHz; `ref_nssc` the reference clock (38.4 MHz on this platform).
 */
int parity_icl_hdmi_wrpll(int port_clock, int ref_nssc, u32 *cfgcr0, u32 *cfgcr1, u32 *div0)
{
	struct drm_i915_private i915;
	struct intel_crtc crtc;
	struct intel_crtc_state cs;
	struct skl_wrpll_params params;
	struct intel_dpll_hw_state hw;
	int rc;

	memset(&i915, 0, sizeof(i915));
	memset(&crtc, 0, sizeof(crtc));
	memset(&cs, 0, sizeof(cs));
	memset(&params, 0, sizeof(params));
	memset(&hw, 0, sizeof(hw));
	i915.display.dpll.ref_clks.nssc = ref_nssc;
	crtc.base.dev = &i915.drm;
	cs.uapi.crtc = &crtc.base;
	cs.port_clock = port_clock;
	rc = icl_calc_wrpll(&cs, &params);
	if (rc != 0)
		return rc;
	icl_calc_dpll_state(&i915, &params, &hw);
	*cfgcr0 = hw.cfgcr0; *cfgcr1 = hw.cfgcr1; *div0 = hw.div0;
	return 0;
}

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

/*
 * The device's two combo PLLs (intel_dpll_init_clock_hook / intel_shared_dpll_init for ADL-P: DPLL 0 and
 * DPLL 1).  One pool for the whole device: two modeset objects that want the same hardware state share the
 * same object, and its active_mask counts the pipes that drive it -- which is what keeps one screen's stop
 * from turning off a PLL the other screen still uses.
 */
static struct intel_shared_dpll parity_lcd_dpll_pool[2];
static struct dpll_info parity_lcd_dpll_pool_info[2];
static struct intel_shared_dpll_state parity_lcd_dpll_pool_state[2];
static int parity_lcd_dpll_pool_inited;

struct intel_shared_dpll_state *parity_lcd_shared_dpll_state(void)
{
	return parity_lcd_dpll_pool_state;
}

static void parity_lcd_dpll_pool_init(struct parity_lcd_modeset *ms)
{
	int i;

	if (!parity_lcd_dpll_pool_inited) {
		for (i = 0; i < 2; i++) {
			memset(&parity_lcd_dpll_pool[i], 0, sizeof(parity_lcd_dpll_pool[i]));
			memset(&parity_lcd_dpll_pool_state[i], 0, sizeof(parity_lcd_dpll_pool_state[i]));
			parity_lcd_dpll_pool_info[i].name = i == DPLL_ID_ICL_DPLL1 ? "DPLL 1" : "DPLL 0";
			parity_lcd_dpll_pool_info[i].funcs = &parity_combo_pll_funcs;
			parity_lcd_dpll_pool_info[i].id = i == DPLL_ID_ICL_DPLL1 ? DPLL_ID_ICL_DPLL1 : DPLL_ID_ICL_DPLL0;
			parity_lcd_dpll_pool_info[i].power_domain = 0;
			parity_lcd_dpll_pool[i].info = &parity_lcd_dpll_pool_info[i];
			parity_lcd_dpll_pool[i].index = (enum intel_dpll_id)i;
		}
		parity_lcd_dpll_pool_inited = 1;
	}
	ms->i915.display.dpll.shared_dplls = parity_lcd_dpll_pool;
	ms->i915.display.dpll.num_shared_dpll = 2;
}

/*
 * icl_get_combo_phy_dpll()'s allocation half: the PLL state was computed by the caller
 * (parity_lcd_compute / parity_icl_hdmi_wrpll); here the reference picks the object that carries it.
 * Returns the DPLL id, or a negative errno when both PLLs are taken by other pipes.
 */
/* the pipe's reference on every PLL of the pool goes back (intel_release_shared_dplls) */
void parity_lcd_ms_release_pipe(enum pipe pipe)
{
	int i;

	for (i = 0; i < 2; i++) {
		parity_lcd_dpll_pool_state[i].pipe_mask &= (u8)~BIT(pipe);
		parity_lcd_dpll_pool[i].state.pipe_mask &= (u8)~BIT(pipe);
	}
}

/* the device is (re)created: the pool starts empty, as intel_shared_dpll_init() leaves it */
void parity_lcd_dplls_reset(void)
{
	int i;

	parity_lcd_dpll_pool_inited = 0;
	for (i = 0; i < 2; i++) {
		memset(&parity_lcd_dpll_pool[i], 0, sizeof(parity_lcd_dpll_pool[i]));
		memset(&parity_lcd_dpll_pool_state[i], 0, sizeof(parity_lcd_dpll_pool_state[i]));
	}
}

int parity_lcd_ms_alloc_pll(struct parity_lcd_modeset *ms, const struct intel_dpll_hw_state *hw_state)
{
	struct intel_shared_dpll *pll;

	parity_lcd_dpll_pool_init(ms);
	parity_lcd_ms_release_pipe(ms->crtc.pipe);
	pll = intel_find_shared_dpll(&ms->state, &ms->crtc, hw_state,
		BIT(DPLL_ID_ICL_DPLL0) | BIT(DPLL_ID_ICL_DPLL1));
	if (pll == NULL)
		return -EINVAL;
	intel_reference_shared_dpll(&ms->state, &ms->crtc, pll, hw_state);
	ms->crtc_state.shared_dpll = pll;
	pll->state = parity_lcd_dpll_pool_state[pll->index];   /* the object carries the new state */
	return (int)pll->info->id;
}

/* intel_release_shared_dplls(): the crtc gives its reference back (the object stays for the other pipe) */
void parity_lcd_ms_release_pll(struct parity_lcd_modeset *ms)
{
	struct intel_shared_dpll *pll = ms->crtc_state.shared_dpll;

	if (pll == NULL)
		return;
	intel_unreference_shared_dpll(&ms->state, &ms->crtc, pll);
	pll->state.pipe_mask = parity_lcd_dpll_pool_state[pll->index].pipe_mask;
}

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
