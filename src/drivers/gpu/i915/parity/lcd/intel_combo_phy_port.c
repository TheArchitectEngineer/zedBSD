// SPDX-License-Identifier: MIT
/*
 * Copyright © 2018 Intel Corporation
 */

/*
 * zedBSD WS031: generated from Linux v6.8.12 drivers/gpu/drm/i915/display/intel_combo_phy.c
 * (sha256 d62d20fe268d280dd185b0ce1b186c1082ebb8788b0d2ca0a29784fe935d3c55) by plan/ws031/handover/tools/port_lcd_calc.py.
 * The function bodies are the reference text.  Kept / changed:
 *  - kept: intel_combo_phy_power_up_lanes;
 *  - the includes are replaced by: lcd_compat.h, lcd_modeset_compat.h;
 */

#include "lcd_compat.h"
#include "lcd_modeset_compat.h"

void intel_combo_phy_power_up_lanes(struct drm_i915_private *dev_priv,
				    enum phy phy, bool is_dsi,
				    int lane_count, bool lane_reversal)
{
	u8 lane_mask;

	if (is_dsi) {
		drm_WARN_ON(&dev_priv->drm, lane_reversal);

		switch (lane_count) {
		case 1:
			lane_mask = PWR_DOWN_LN_3_1_0;
			break;
		case 2:
			lane_mask = PWR_DOWN_LN_3_1;
			break;
		case 3:
			lane_mask = PWR_DOWN_LN_3;
			break;
		default:
			MISSING_CASE(lane_count);
			fallthrough;
		case 4:
			lane_mask = PWR_UP_ALL_LANES;
			break;
		}
	} else {
		switch (lane_count) {
		case 1:
			lane_mask = lane_reversal ? PWR_DOWN_LN_2_1_0 :
						    PWR_DOWN_LN_3_2_1;
			break;
		case 2:
			lane_mask = lane_reversal ? PWR_DOWN_LN_1_0 :
						    PWR_DOWN_LN_3_2;
			break;
		default:
			MISSING_CASE(lane_count);
			fallthrough;
		case 4:
			lane_mask = PWR_UP_ALL_LANES;
			break;
		}
	}

	intel_de_rmw(dev_priv, ICL_PORT_CL_DW10(phy),
		     PWR_DOWN_LN_MASK, lane_mask);
}

