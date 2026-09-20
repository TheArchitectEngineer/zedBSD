/*
 * WS031 Linux-parity -- zedBSD glue at the end of intel_cdclk_port.c: the reference's bxt_modeset_calc_cdclk()
 * (intel_compute_min_cdclk + bxt_compute_min_voltage_level + bxt_calc_cdclk / _pll_vco + the voltage level)
 * reduced to the one crtc of the modeset, followed by intel_cdclk_changed()'s comparison with the state the
 * normal initialisation left.  Nothing is programmed here: when the required state equals the current one the
 * reference's intel_set_cdclk_pre/post_plane_update() return without touching the hardware, and that is the
 * only case the modeset accepts.
 */
#include "lcd_dp_compat.h"
#include "parity_lcd_modeset_int.h"

int parity_lcd_ms_cdclk_check(struct parity_lcd_modeset *ms)
{
	struct drm_i915_private *i915 = &ms->i915;
	const struct intel_crtc_state *cs = &ms->crtc_state;
	int min_cdclk, cdclk, vco, level;

	/* intel_init_cdclk_hooks(): ADL-P past display stepping B0, not RPL-U */
	i915->display.cdclk.table = adlp_cdclk_table;

	/* intel_compute_min_cdclk(): the crtc's need, the bandwidth need (no forced minimum) */
	min_cdclk = intel_crtc_compute_min_cdclk(cs);
	if (min_cdclk < 0)
		return min_cdclk;
	ms->cdclk.crtc_min = min_cdclk;
	ms->cdclk.bw_min = parity_lcd_ms_bw_min_cdclk(ms);
	min_cdclk = max(ms->cdclk.bw_min, min_cdclk);
	if (min_cdclk > (int)i915->display.cdclk.max_cdclk_freq)
		return -EINVAL;

	/* bxt_modeset_calc_cdclk() */
	cdclk = bxt_calc_cdclk(i915, min_cdclk);
	vco = bxt_calc_cdclk_pll_vco(i915, cdclk);
	level = max_t(int, cs->min_voltage_level, tgl_calc_voltage_level(cdclk));
	if (cdclk == 0 || vco == 0)
		return -EINVAL;
	ms->cdclk.min_cdclk = min_cdclk;
	ms->cdclk.cdclk = cdclk;
	ms->cdclk.vco = vco;
	ms->cdclk.voltage_level = level;
	/* intel_cdclk_changed(): intel_cdclk_needs_modeset() (cdclk / vco / ref differ) or the voltage level differs */
	ms->cdclk.change_needed = cdclk != (int)i915->display.cdclk.hw.cdclk || vco != (int)i915->display.cdclk.hw.vco ||
		level != (int)i915->display.cdclk.hw.voltage_level;
	return 0;
}
