/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The vertical blank helpers of the one-screen path (vblank.c): the
 * evasion window of the selected screen, and the vertical blank and event
 * hooks of a panel run (their context is the run, struct
 * i915_lcd_kernel, and the modeset owner binds them into the run's
 * struct i915_lcd_emit).  The helpers the environment headers already
 * declare (drv_i915_lcd_ms_vblank_off() and the drv_i915_lcd_irq_*()
 * section helpers) are not repeated here.
 */

#ifndef DRIVERS_GPU_I915_DISPLAY_VBLANK_H
#define DRIVERS_GPU_I915_DISPLAY_VBLANK_H

#include "internal.h"

struct i915_display;

/*
 * ==== The vblank evasion window of the selected screen ====
 */

int drv_i915_lcd_modeset_evade_window(struct i915_display *display, int *min, int *max, int *vblank_start);

/*
 * ==== The vblank and event hooks of a panel run (ctx: struct i915_lcd_kernel) ====
 */

int drv_i915_lcd_kernel_vblank_get(void *ctx, int pipe);
void drv_i915_lcd_kernel_vblank_put(void *ctx, int pipe);
long drv_i915_lcd_kernel_vblank_sleep(void *ctx, int pipe, long ticks);
void drv_i915_lcd_kernel_arm_event(void *ctx, int pipe);
int drv_i915_lcd_kernel_wait_event(void *ctx, int pipe, unsigned timeout_ms);
void drv_i915_lcd_kernel_cancel_event(void *ctx, int pipe);

#endif /* DRIVERS_GPU_I915_DISPLAY_VBLANK_H */
