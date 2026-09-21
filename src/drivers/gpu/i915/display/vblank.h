/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The scanline, frame counter and vblank helpers of the one-screen path
 * (vblank.c).
 *
 * The first group takes only neutral display types: the vblank evasion
 * window of the selected screen, and the vblank and event hooks of a panel
 * run (their context is the run, struct i915_lcd_kernel, and the modeset
 * owner binds them into the run's struct i915_lcd_emit).  The second group
 * takes the modeset environment's Linux types and is visible only to a
 * translation unit that has included modeset-internal.h first.  The hooks
 * the environment headers already declare (drv_i915_lcd_ms_crtc_funcs(),
 * drv_i915_lcd_ms_active_timings(), drv_i915_lcd_ms_evade_window(),
 * drv_i915_lcd_ms_vblank_off() and the drv_i915_lcd_irq_*() section
 * helpers) are not repeated here.
 */

#ifndef DRIVERS_GPU_I915_DISPLAY_VBLANK_H
#define DRIVERS_GPU_I915_DISPLAY_VBLANK_H

#include "internal.h"

struct i915_display;

/*
 * ==== The vblank evasion window of the selected screen ====
 */

int drv_i915_lcd_modeset_evade_window(struct i915_display *display, int *min, int *max, int *vblank_start);
unsigned long drv_i915_crtc_state_size_crtc_unit(void);

/*
 * ==== The vblank and event hooks of a panel run (ctx: struct i915_lcd_kernel) ====
 */

int drv_i915_lcd_kernel_vblank_get(void *ctx, int pipe);
void drv_i915_lcd_kernel_vblank_put(void *ctx, int pipe);
long drv_i915_lcd_kernel_vblank_sleep(void *ctx, int pipe, long ticks);
void drv_i915_lcd_kernel_arm_event(void *ctx, int pipe);
int drv_i915_lcd_kernel_wait_event(void *ctx, int pipe, unsigned timeout_ms);
void drv_i915_lcd_kernel_cancel_event(void *ctx, int pipe);

#ifdef I915_DISPLAY_WORLD_MODESET

/*
 * ==== The scanline and timing helpers (modeset environment) ====
 */

void drv_i915_wait_for_pipe_scanline_stopped(struct intel_crtc *crtc);
void drv_i915_wait_for_pipe_scanline_moving(struct intel_crtc *crtc);
int drv_i915_get_crtc_scanline(struct i915_lcd_world *world, struct intel_crtc *crtc);
void drv_i915_crtc_update_active_timings(const struct intel_crtc_state *crtc_state, bool vrr_enable);

#endif /* I915_DISPLAY_WORLD_MODESET */

#endif /* DRIVERS_GPU_I915_DISPLAY_VBLANK_H */
