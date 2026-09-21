/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The pipe colour management of the one-screen path (color.c).
 *
 * Every function here takes the modeset environment's Linux types, so the
 * declarations are visible only to a translation unit that has included
 * modeset-internal.h first.  The colour hooks of the device
 * (drv_i915_lcd_ms_color_funcs()) and the colour check of the modeset
 * object (drv_i915_lcd_ms_color_check()) are declared by the environment
 * headers themselves (takeover-internal.h, modeset-internal.h).
 */

#ifndef DRIVERS_GPU_I915_DISPLAY_COLOR_H
#define DRIVERS_GPU_I915_DISPLAY_COLOR_H

#include "internal.h"

#ifdef I915_DISPLAY_WORLD_MODESET

void drv_i915_color_load_luts(const struct intel_crtc_state *crtc_state);
void drv_i915_color_commit_noarm(const struct intel_crtc_state *crtc_state);
void drv_i915_color_commit_arm(const struct intel_crtc_state *crtc_state);

#endif /* I915_DISPLAY_WORLD_MODESET */

#endif /* DRIVERS_GPU_I915_DISPLAY_COLOR_H */
