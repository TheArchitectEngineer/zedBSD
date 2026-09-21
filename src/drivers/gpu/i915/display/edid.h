/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The display modes of the one-screen modeset path (edid.c).
 *
 * The panel's preferred mode from its EDID's detailed timings, and the mode
 * helpers the pipe, vblank and watermark text use (copy, the crtc timings,
 * the visible size).
 *
 * Every function here takes the modeset environment's Linux types, so the
 * declarations are visible only to a translation unit that has included
 * modeset-internal.h first.  drv_i915_lcd_drm_mode_set_name() is declared
 * by modeset-internal.h itself.
 */

#ifndef DRIVERS_GPU_I915_DISPLAY_EDID_H
#define DRIVERS_GPU_I915_DISPLAY_EDID_H

#include "internal.h"

#ifdef I915_DISPLAY_WORLD_MODESET

struct i915_lcd_world;

void drv_i915_drm_mode_copy(struct drm_display_mode *dst, const struct drm_display_mode *src);
void drv_i915_drm_mode_init(struct drm_display_mode *dst, const struct drm_display_mode *src);
void drv_i915_drm_mode_get_hv_timing(const struct drm_display_mode *mode, int *hdisplay, int *vdisplay);
void drv_i915_drm_mode_set_crtcinfo(struct drm_display_mode *p, int adjust_flags);
int drv_i915_edid_preferred_mode(struct i915_lcd_world *world, const u8 *edid128, struct drm_display_mode *mode, unsigned *index);

#endif /* I915_DISPLAY_WORLD_MODESET */

#endif /* DRIVERS_GPU_I915_DISPLAY_EDID_H */
