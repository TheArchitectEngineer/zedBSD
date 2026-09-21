/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The DP link of the one-screen modeset path (dp.c).
 *
 * The link rate and bandwidth arithmetic, the sink power state, the source
 * OUI, the eDP backlight enable, the DP infoframe enables, and the 8b/10b
 * link training with its DPCD link-status and LTTPR helpers.
 *
 * Every function here takes the modeset environment's Linux types, so the
 * declarations are visible only to a translation unit that has included
 * modeset-internal.h first.
 */

#ifndef DRIVERS_GPU_I915_DISPLAY_DP_H
#define DRIVERS_GPU_I915_DISPLAY_DP_H

#include "internal.h"

#ifdef I915_DISPLAY_WORLD_MODESET

bool drv_i915_drm_dp_channel_eq_ok(const u8 link_status[DP_LINK_STATUS_SIZE], int lane_count);
bool drv_i915_drm_dp_clock_recovery_ok(const u8 link_status[DP_LINK_STATUS_SIZE], int lane_count);
int drv_i915_drm_dp_dpcd_read_phy_link_status(struct drm_dp_aux *aux, enum drm_dp_phy dp_phy, u8 link_status[DP_LINK_STATUS_SIZE]);

bool drv_i915_dp_is_edp(struct intel_dp *intel_dp);
bool drv_i915_dp_is_uhbr(const struct intel_crtc_state *crtc_state);
int drv_i915_dp_link_required(int pixel_clock, int bpp);
int drv_i915_dp_link_symbol_size(int rate);
int drv_i915_dp_link_symbol_clock(int rate);
int drv_i915_dp_effective_data_rate(int pixel_clock, int bpp_x16, int bw_overhead);
int drv_i915_dp_max_data_rate(int max_link_rate, int max_lanes);
bool drv_i915_dp_needs_vsc_sdp(const struct intel_crtc_state *crtc_state, const struct drm_connector_state *conn_state);
void drv_i915_dp_set_infoframes(struct intel_encoder *encoder, bool enable, const struct intel_crtc_state *crtc_state, const struct drm_connector_state *conn_state);
void drv_i915_dp_set_link_params(struct intel_dp *intel_dp, int link_rate, int lane_count);
void drv_i915_dp_set_power(struct intel_dp *intel_dp, u8 mode);
void drv_i915_edp_backlight_on(const struct intel_crtc_state *crtc_state, const struct drm_connector_state *conn_state);
void drv_i915_edp_backlight_off(const struct drm_connector_state *old_conn_state);

void drv_i915_dp_start_link_train(struct intel_dp *intel_dp, const struct intel_crtc_state *crtc_state);
void drv_i915_dp_stop_link_train(struct intel_dp *intel_dp, const struct intel_crtc_state *crtc_state);

#endif /* I915_DISPLAY_WORLD_MODESET */

#endif /* DRIVERS_GPU_I915_DISPLAY_DP_H */
