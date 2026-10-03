// SPDX-License-Identifier: MIT
/*
 * Copyright © 2020 Intel Corporation
 *
 */

/*
 * zedBSD WS031: generated from Linux v6.8.12 drivers/gpu/drm/i915/display/intel_vrr.c
 * (sha256 03f4500bc7cbcd488c3ec0a69478effb494268d4f9bb4b888c5ea07318d7773e) by plan/ws031/handover/tools/port_lcd_calc.py.
 * The function bodies are the reference text.  Kept / changed:
 *  - kept: trans_vrr_ctl, intel_vrr_set_transcoder_timings;
 *  - the includes are replaced by lcd_compat.h (register writes go through its emit hook).
 */

#include "lcd_compat.h"	/* zedBSD: replaces the linux/, drm/ and i915 includes */
#include "lcd_trans_regs.h"
#include "lcd_ddi_regs.h"

static u32 trans_vrr_ctl(const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *i915 = to_i915(crtc_state->uapi.crtc->dev);

	if (DISPLAY_VER(i915) >= 13)
		return VRR_CTL_IGN_MAX_SHIFT | VRR_CTL_FLIP_LINE_EN |
			XELPD_VRR_CTL_VRR_GUARDBAND(crtc_state->vrr.guardband);
	else
		return VRR_CTL_IGN_MAX_SHIFT | VRR_CTL_FLIP_LINE_EN |
			VRR_CTL_PIPELINE_FULL(crtc_state->vrr.pipeline_full) |
			VRR_CTL_PIPELINE_FULL_OVERRIDE;
}

void intel_vrr_set_transcoder_timings(const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *dev_priv = to_i915(crtc_state->uapi.crtc->dev);
	enum transcoder cpu_transcoder = crtc_state->cpu_transcoder;

	/*
	 * This bit seems to have two meanings depending on the platform:
	 * TGL: generate VRR "safe window" for DSB vblank waits
	 * ADL/DG2: make TRANS_SET_CONTEXT_LATENCY effective with VRR
	 */
	if (IS_DISPLAY_VER(dev_priv, 12, 13))
		intel_de_rmw(dev_priv, CHICKEN_TRANS(cpu_transcoder),
			     0, PIPE_VBLANK_WITH_DELAY);

	if (!crtc_state->vrr.flipline) {
		intel_de_write(dev_priv, TRANS_VRR_CTL(cpu_transcoder), 0);
		return;
	}

	intel_de_write(dev_priv, TRANS_VRR_VMIN(cpu_transcoder), crtc_state->vrr.vmin - 1);
	intel_de_write(dev_priv, TRANS_VRR_VMAX(cpu_transcoder), crtc_state->vrr.vmax - 1);
	intel_de_write(dev_priv, TRANS_VRR_CTL(cpu_transcoder), trans_vrr_ctl(crtc_state));
	intel_de_write(dev_priv, TRANS_VRR_FLIPLINE(cpu_transcoder), crtc_state->vrr.flipline - 1);
}

