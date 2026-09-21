/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The panel backlight of the one-screen path (panel-backlight.c).
 *
 * The first group takes only neutral display types: the brightness, the
 * OpRegion (ACPI) brightness request and the backlight power of the
 * selected screen, each answering with an I915_LCD_MS_* result.  The
 * second group takes the modeset environment's Linux types and is visible
 * only to a translation unit that has included modeset-internal.h first.
 * The hooks the environment header already declares
 * (drv_i915_lcd_ms_backlight_setup(), _set_brightness(), _set_acpi(),
 * _backlight_power() and _user_level()) are not repeated here.
 */

#ifndef DRIVERS_GPU_I915_DISPLAY_PANEL_BACKLIGHT_H
#define DRIVERS_GPU_I915_DISPLAY_PANEL_BACKLIGHT_H

#include "internal.h"

struct i915_display;

/*
 * ==== The backlight of the selected screen ====
 */

int drv_i915_lcd_modeset_brightness(struct i915_display *display, uint32_t user_level, uint32_t user_max);
int drv_i915_lcd_modeset_backlight_acpi(struct i915_display *display, uint32_t level, uint32_t max);
int drv_i915_lcd_modeset_backlight(struct i915_display *display, int on);

#ifdef I915_DISPLAY_WORLD_MODESET

/*
 * ==== The Linux backlight text (modeset environment) ====
 */

void drv_i915_backlight_enable(const struct intel_crtc_state *crtc_state, const struct drm_connector_state *conn_state);
void drv_i915_backlight_disable(const struct drm_connector_state *old_conn_state);
void drv_i915_backlight_set_acpi(const struct drm_connector_state *conn_state, u32 user_level, u32 user_max);
u32 drv_i915_backlight_level_to_pwm(struct intel_connector *connector, u32 val);
u32 drv_i915_backlight_level_from_pwm(struct intel_connector *connector, u32 val);

#endif /* I915_DISPLAY_WORLD_MODESET */

#endif /* DRIVERS_GPU_I915_DISPLAY_PANEL_BACKLIGHT_H */
