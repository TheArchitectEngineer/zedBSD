/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The DDI port of the one-screen modeset path (ddi.c).
 *
 * The encoder hooks of a combo-PHY DDI (the eDP and HDMI enable and
 * disable, the DP link-training hooks of the port, the clock and power
 * references of the port), its readout, the word recorder of the DDI
 * registers and the binding of the one encoder to a modeset object.
 *
 * Every function here takes the modeset environment's Linux types, so the
 * declarations are visible only to a translation unit that has included
 * modeset-internal.h first.  The bindings the environment headers already
 * declare are defined in ddi.c as well: drv_i915_lcd_ms_bind_encoder() and
 * drv_i915_lcd_hdmi_level_shift() (modeset-internal.h),
 * drv_i915_lcd_ms_bind_readout(), drv_i915_lcd_ms_bound_encoder() and
 * drv_i915_lcd_ms_bound_connector() (takeover-internal.h).
 */

#ifndef DRIVERS_GPU_I915_DISPLAY_DDI_H
#define DRIVERS_GPU_I915_DISPLAY_DDI_H

#include "internal.h"

#ifdef I915_DISPLAY_WORLD_MODESET

struct i915_lcd_world;
struct i915_takeover_world;

i915_reg_t drv_i915_hsw_chicken_trans_reg(struct drm_i915_private *i915, enum transcoder cpu_transcoder);
void drv_i915_ddi_compute_min_voltage_level(struct intel_crtc_state *crtc_state);
void drv_i915_ddi_sanitize_encoder_pll_mapping(struct i915_takeover_world *takeover, struct intel_encoder *encoder);
bool drv_i915_ddi_connector_get_hw_state(struct intel_connector *intel_connector);

void drv_i915_encoders_pre_pll_enable(struct intel_atomic_state *state, struct intel_crtc *crtc);
void drv_i915_encoders_pre_enable(struct intel_atomic_state *state, struct intel_crtc *crtc);
void drv_i915_encoders_enable(struct intel_atomic_state *state, struct intel_crtc *crtc);
void drv_i915_encoders_disable(struct intel_atomic_state *state, struct intel_crtc *crtc);
void drv_i915_encoders_post_disable(struct intel_atomic_state *state, struct intel_crtc *crtc);
void drv_i915_encoders_post_pll_disable(struct intel_atomic_state *state, struct intel_crtc *crtc);

int drv_i915_ddi_emit(struct i915_lcd_world *world, struct i915_lcd_emit *emit, const struct drm_display_mode *mode, int port, int pipe, int cpu_transcoder, int port_clock, int lanes, int pipe_bpp, u32 saved_port_bits, u32 *ddi_buf_ctl_value);
int drv_i915_lcd_ms_bound_port(struct i915_lcd_world *world);

#endif /* I915_DISPLAY_WORLD_MODESET */

#endif /* DRIVERS_GPU_I915_DISPLAY_DDI_H */
