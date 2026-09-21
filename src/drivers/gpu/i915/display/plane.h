/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The universal plane of the one-screen path (plane.c).
 *
 * drv_i915_icl_hdr_plane_mask() takes no environment type.  The rest takes
 * the modeset environment's Linux types and is visible only to a
 * translation unit that has included modeset-internal.h first.  The
 * functions the environment headers already declare
 * (drv_i915_lcd_ms_plane_prepare(), _update(), _disable(), _min_cdclk(),
 * _update_flip() and _data_rates(), drv_i915_lcd_plane_disable_arm() and
 * i915_lcd_plane_get_hw_state()) are not repeated here.
 */

#ifndef DRIVERS_GPU_I915_DISPLAY_PLANE_H
#define DRIVERS_GPU_I915_DISPLAY_PLANE_H

#include "internal.h"

u8 drv_i915_icl_hdr_plane_mask(void);

#ifdef I915_DISPLAY_WORLD_MODESET

unsigned int drv_i915_adjusted_rate(const struct drm_rect *src, const struct drm_rect *dst, unsigned int rate);
unsigned int drv_i915_plane_pixel_rate(const struct intel_crtc_state *crtc_state, const struct intel_plane_state *plane_state);
unsigned int drv_i915_plane_data_rate(const struct intel_crtc_state *crtc_state, const struct intel_plane_state *plane_state, int color_plane);
int drv_i915_plane_emit(struct i915_lcd_world *world, struct i915_lcd_emit *emit, int pipe, int plane_id, u32 fourcc, u64 modifier, u32 width, u32 height, u32 pitch, u32 surf_ggtt_offset);

#endif /* I915_DISPLAY_WORLD_MODESET */

#endif /* DRIVERS_GPU_I915_DISPLAY_PLANE_H */
