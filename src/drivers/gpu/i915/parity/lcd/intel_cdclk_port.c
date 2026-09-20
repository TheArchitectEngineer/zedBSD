/*
 * Copyright © 2006-2017 Intel Corporation
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
 * zedBSD WS031: generated from Linux v6.8.12 drivers/gpu/drm/i915/display/intel_cdclk.c
 * (sha256 e954ff508ced6525c83ced776ba26788cc18a2f8677aa4441697d5f9ccc38c48) by plan/ws031/handover/tools/port_lcd_calc.py.
 * The function bodies are the reference text.  Kept / changed:
 *  - kept: bxt_calc_cdclk, bxt_calc_cdclk_pll_vco, calc_voltage_level, tgl_calc_voltage_level,
 *    intel_pixel_rate_to_cdclk, intel_planes_min_cdclk, intel_crtc_compute_min_cdclk;
 *  - the includes are replaced by: lcd_compat.h, lcd_seq_compat.h, lcd_modeset_compat.h, lcd_plane_compat.h, lcd_wm_compat.h;
 *  - parity_cdclk_glue.inc (zedBSD code) is included at the end of the file.
 */

#include "lcd_compat.h"
#include "lcd_seq_compat.h"
#include "lcd_modeset_compat.h"
#include "lcd_plane_compat.h"
#include "lcd_wm_compat.h"

struct intel_cdclk_vals {
	u32 cdclk;
	u16 refclk;
	u16 waveform;
	u8 divider;	/* CD2X divider * 2 */
	u8 ratio;
};

static const struct intel_cdclk_vals icl_cdclk_table[] = {
	{ .refclk = 19200, .cdclk = 172800, .divider = 2, .ratio = 18 },
	{ .refclk = 19200, .cdclk = 192000, .divider = 2, .ratio = 20 },
	{ .refclk = 19200, .cdclk = 307200, .divider = 2, .ratio = 32 },
	{ .refclk = 19200, .cdclk = 326400, .divider = 4, .ratio = 68 },
	{ .refclk = 19200, .cdclk = 556800, .divider = 2, .ratio = 58 },
	{ .refclk = 19200, .cdclk = 652800, .divider = 2, .ratio = 68 },

	{ .refclk = 24000, .cdclk = 180000, .divider = 2, .ratio = 15 },
	{ .refclk = 24000, .cdclk = 192000, .divider = 2, .ratio = 16 },
	{ .refclk = 24000, .cdclk = 312000, .divider = 2, .ratio = 26 },
	{ .refclk = 24000, .cdclk = 324000, .divider = 4, .ratio = 54 },
	{ .refclk = 24000, .cdclk = 552000, .divider = 2, .ratio = 46 },
	{ .refclk = 24000, .cdclk = 648000, .divider = 2, .ratio = 54 },

	{ .refclk = 38400, .cdclk = 172800, .divider = 2, .ratio =  9 },
	{ .refclk = 38400, .cdclk = 192000, .divider = 2, .ratio = 10 },
	{ .refclk = 38400, .cdclk = 307200, .divider = 2, .ratio = 16 },
	{ .refclk = 38400, .cdclk = 326400, .divider = 4, .ratio = 34 },
	{ .refclk = 38400, .cdclk = 556800, .divider = 2, .ratio = 29 },
	{ .refclk = 38400, .cdclk = 652800, .divider = 2, .ratio = 34 },
	{}
};

static const struct intel_cdclk_vals adlp_cdclk_table[] = {
	{ .refclk = 19200, .cdclk = 172800, .divider = 3, .ratio = 27 },
	{ .refclk = 19200, .cdclk = 192000, .divider = 2, .ratio = 20 },
	{ .refclk = 19200, .cdclk = 307200, .divider = 2, .ratio = 32 },
	{ .refclk = 19200, .cdclk = 556800, .divider = 2, .ratio = 58 },
	{ .refclk = 19200, .cdclk = 652800, .divider = 2, .ratio = 68 },

	{ .refclk = 24000, .cdclk = 176000, .divider = 3, .ratio = 22 },
	{ .refclk = 24000, .cdclk = 192000, .divider = 2, .ratio = 16 },
	{ .refclk = 24000, .cdclk = 312000, .divider = 2, .ratio = 26 },
	{ .refclk = 24000, .cdclk = 552000, .divider = 2, .ratio = 46 },
	{ .refclk = 24000, .cdclk = 648000, .divider = 2, .ratio = 54 },

	{ .refclk = 38400, .cdclk = 179200, .divider = 3, .ratio = 14 },
	{ .refclk = 38400, .cdclk = 192000, .divider = 2, .ratio = 10 },
	{ .refclk = 38400, .cdclk = 307200, .divider = 2, .ratio = 16 },
	{ .refclk = 38400, .cdclk = 556800, .divider = 2, .ratio = 29 },
	{ .refclk = 38400, .cdclk = 652800, .divider = 2, .ratio = 34 },
	{}
};

/* zedBSD: forward declarations of the static functions of this file (generated; the keep-list is not in call order) */
static int bxt_calc_cdclk(struct drm_i915_private *dev_priv, int min_cdclk);
static int bxt_calc_cdclk_pll_vco(struct drm_i915_private *dev_priv, int cdclk);
static u8 calc_voltage_level(int cdclk, int num_voltage_levels,
			     const int voltage_level_max_cdclk[]);
static u8 tgl_calc_voltage_level(int cdclk);
static int intel_pixel_rate_to_cdclk(const struct intel_crtc_state *crtc_state);
static int intel_planes_min_cdclk(const struct intel_crtc_state *crtc_state);

static int bxt_calc_cdclk(struct drm_i915_private *dev_priv, int min_cdclk)
{
	const struct intel_cdclk_vals *table = dev_priv->display.cdclk.table;
	int i;

	for (i = 0; table[i].refclk; i++)
		if (table[i].refclk == dev_priv->display.cdclk.hw.ref &&
		    table[i].cdclk >= min_cdclk)
			return table[i].cdclk;

	drm_WARN(&dev_priv->drm, 1,
		 "Cannot satisfy minimum cdclk %d with refclk %u\n",
		 min_cdclk, dev_priv->display.cdclk.hw.ref);
	return 0;
}

static int bxt_calc_cdclk_pll_vco(struct drm_i915_private *dev_priv, int cdclk)
{
	const struct intel_cdclk_vals *table = dev_priv->display.cdclk.table;
	int i;

	if (cdclk == dev_priv->display.cdclk.hw.bypass)
		return 0;

	for (i = 0; table[i].refclk; i++)
		if (table[i].refclk == dev_priv->display.cdclk.hw.ref &&
		    table[i].cdclk == cdclk)
			return dev_priv->display.cdclk.hw.ref * table[i].ratio;

	drm_WARN(&dev_priv->drm, 1, "cdclk %d not valid for refclk %u\n",
		 cdclk, dev_priv->display.cdclk.hw.ref);
	return 0;
}

static u8 calc_voltage_level(int cdclk, int num_voltage_levels,
			     const int voltage_level_max_cdclk[])
{
	int voltage_level;

	for (voltage_level = 0; voltage_level < num_voltage_levels; voltage_level++) {
		if (cdclk <= voltage_level_max_cdclk[voltage_level])
			return voltage_level;
	}

	MISSING_CASE(cdclk);
	return num_voltage_levels - 1;
}

static u8 tgl_calc_voltage_level(int cdclk)
{
	static const int tgl_voltage_level_max_cdclk[] = {
		[0] = 312000,
		[1] = 326400,
		[2] = 556800,
		[3] = 652800,
	};

	return calc_voltage_level(cdclk,
				  ARRAY_SIZE(tgl_voltage_level_max_cdclk),
				  tgl_voltage_level_max_cdclk);
}

static int intel_pixel_rate_to_cdclk(const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *dev_priv = to_i915(crtc_state->uapi.crtc->dev);
	int pixel_rate = crtc_state->pixel_rate;

	if (DISPLAY_VER(dev_priv) >= 10)
		return DIV_ROUND_UP(pixel_rate, 2);
	else if (DISPLAY_VER(dev_priv) == 9 ||
		 IS_BROADWELL(dev_priv) || IS_HASWELL(dev_priv))
		return pixel_rate;
	else if (IS_CHERRYVIEW(dev_priv))
		return DIV_ROUND_UP(pixel_rate * 100, 95);
	else if (crtc_state->double_wide)
		return DIV_ROUND_UP(pixel_rate * 100, 90 * 2);
	else
		return DIV_ROUND_UP(pixel_rate * 100, 90);
}

static int intel_planes_min_cdclk(const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	struct intel_plane *plane;
	int min_cdclk = 0;

	for_each_intel_plane_on_crtc(&dev_priv->drm, crtc, plane)
		min_cdclk = max(crtc_state->min_cdclk[plane->id], min_cdclk);

	return min_cdclk;
}

int intel_crtc_compute_min_cdclk(const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *dev_priv =
		to_i915(crtc_state->uapi.crtc->dev);
	int min_cdclk;

	if (!crtc_state->hw.enable)
		return 0;

	min_cdclk = intel_pixel_rate_to_cdclk(crtc_state);

	/* pixel rate mustn't exceed 95% of cdclk with IPS on BDW */
	if (IS_BROADWELL(dev_priv) && hsw_crtc_state_ips_capable(crtc_state))
		min_cdclk = DIV_ROUND_UP(min_cdclk * 100, 95);

	/* BSpec says "Do not use DisplayPort with CDCLK less than 432 MHz,
	 * audio enabled, port width x4, and link rate HBR2 (5.4 GHz), or else
	 * there may be audio corruption or screen corruption." This cdclk
	 * restriction for GLK is 316.8 MHz.
	 */
	if (intel_crtc_has_dp_encoder(crtc_state) &&
	    crtc_state->has_audio &&
	    crtc_state->port_clock >= 540000 &&
	    crtc_state->lane_count == 4) {
		if (DISPLAY_VER(dev_priv) == 10) {
			/* Display WA #1145: glk */
			min_cdclk = max(316800, min_cdclk);
		} else if (DISPLAY_VER(dev_priv) == 9 || IS_BROADWELL(dev_priv)) {
			/* Display WA #1144: skl,bxt */
			min_cdclk = max(432000, min_cdclk);
		}
	}

	/*
	 * According to BSpec, "The CD clock frequency must be at least twice
	 * the frequency of the Azalia BCLK." and BCLK is 96 MHz by default.
	 */
	if (crtc_state->has_audio && DISPLAY_VER(dev_priv) >= 9)
		min_cdclk = max(2 * 96000, min_cdclk);

	/*
	 * "For DP audio configuration, cdclk frequency shall be set to
	 *  meet the following requirements:
	 *  DP Link Frequency(MHz) | Cdclk frequency(MHz)
	 *  270                    | 320 or higher
	 *  162                    | 200 or higher"
	 */
	if ((IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv)) &&
	    intel_crtc_has_dp_encoder(crtc_state) && crtc_state->has_audio)
		min_cdclk = max(crtc_state->port_clock, min_cdclk);

	/*
	 * On Valleyview some DSI panels lose (v|h)sync when the clock is lower
	 * than 320000KHz.
	 */
	if (intel_crtc_has_type(crtc_state, INTEL_OUTPUT_DSI) &&
	    IS_VALLEYVIEW(dev_priv))
		min_cdclk = max(320000, min_cdclk);

	/*
	 * On Geminilake once the CDCLK gets as low as 79200
	 * picture gets unstable, despite that values are
	 * correct for DSI PLL and DE PLL.
	 */
	if (intel_crtc_has_type(crtc_state, INTEL_OUTPUT_DSI) &&
	    IS_GEMINILAKE(dev_priv))
		min_cdclk = max(158400, min_cdclk);

	/* Account for additional needs from the planes */
	min_cdclk = max(intel_planes_min_cdclk(crtc_state), min_cdclk);

	if (crtc_state->dsc.compression_enable)
		min_cdclk = max(min_cdclk, intel_vdsc_min_cdclk(crtc_state));

	/*
	 * HACK. Currently for TGL/DG2 platforms we calculate
	 * min_cdclk initially based on pixel_rate divided
	 * by 2, accounting for also plane requirements,
	 * however in some cases the lowest possible CDCLK
	 * doesn't work and causing the underruns.
	 * Explicitly stating here that this seems to be currently
	 * rather a Hack, than final solution.
	 */
	if (IS_TIGERLAKE(dev_priv) || IS_DG2(dev_priv)) {
		/*
		 * Clamp to max_cdclk_freq in case pixel rate is higher,
		 * in order not to break an 8K, but still leave W/A at place.
		 */
		min_cdclk = max_t(int, min_cdclk,
				  min_t(int, crtc_state->pixel_rate,
					dev_priv->display.cdclk.max_cdclk_freq));
	}

	return min_cdclk;
}


#include "parity_cdclk_glue.inc"
