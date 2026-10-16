/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The universal plane of the one-screen path (plane.c).
 *
 * Every function here takes the modeset environment's Linux types and is visible only to a
 * translation unit that has included modeset-internal.h first.  The
 * functions the environment headers already declare
 * (drv_i915_lcd_ms_plane_prepare(), _update(), _disable(), _min_cdclk(),
 * _update_flip() and _data_rates(), drv_i915_lcd_plane_disable_arm() and
 * i915_lcd_plane_get_hw_state()) are not repeated here.
 */

#ifndef DRIVERS_GPU_I915_DISPLAY_PLANE_H
#define DRIVERS_GPU_I915_DISPLAY_PLANE_H

#include "internal.h"


#ifdef I915_DISPLAY_WORLD_MODESET

int drv_i915_plane_emit(struct i915_lcd_world *world, struct i915_lcd_emit *emit, int pipe, int plane_id, u32 fourcc, u64 modifier, u32 width, u32 height, u32 pitch, u32 surf_ggtt_offset);

#endif /* I915_DISPLAY_WORLD_MODESET */

#endif /* DRIVERS_GPU_I915_DISPLAY_PLANE_H */
