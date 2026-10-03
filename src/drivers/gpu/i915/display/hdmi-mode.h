/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The HDMI mode half of the one-screen modeset path (hdmi-mode.c).
 *
 * The infoframe hook of an HDMI port, the DP dual-mode adaptor's TMDS
 * output switch and the sink's scrambling and TMDS clock ratio.
 *
 * Every function here takes the modeset environment's Linux types, so the
 * declarations are visible only to a translation unit that has included
 * modeset-internal.h first.
 */

#ifndef DRIVERS_GPU_I915_DISPLAY_HDMI_MODE_H
#define DRIVERS_GPU_I915_DISPLAY_HDMI_MODE_H

#include "internal.h"

#ifdef I915_DISPLAY_WORLD_MODESET

void drv_i915_dp_dual_mode_set_tmds_output(struct intel_hdmi *hdmi, bool enable);
bool drv_i915_hdmi_handle_sink_scrambling(struct intel_encoder *encoder, struct drm_connector *connector, bool high_tmds_clock_ratio, bool scrambling);
void (*drv_i915_lcd_hdmi_set_infoframes(void))(struct intel_encoder *encoder, bool enable, const struct intel_crtc_state *crtc_state, const struct drm_connector_state *conn_state);
void drv_i915_lcd_hdmi_tmds_output(struct intel_hdmi *hdmi, bool enable);

#endif /* I915_DISPLAY_WORLD_MODESET */

#endif /* DRIVERS_GPU_I915_DISPLAY_HDMI_MODE_H */
