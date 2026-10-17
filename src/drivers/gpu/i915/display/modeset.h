/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The modeset world of a display (modeset.c).
 *
 * The display version and the xd platform description the older display
 * code asks for, and the reports of that code that take no world.  Only
 * neutral display types appear here; the functions that take the older
 * code's Linux types are declared in modeset-internal.h.
 */

#ifndef DRIVERS_GPU_I915_DISPLAY_MODESET_H
#define DRIVERS_GPU_I915_DISPLAY_MODESET_H

#include "internal.h"

struct i915_display;
struct i915_xd_platform;

/*
 * ==== The modeset world ====
 */

/* Allocates and frees the modeset world (display->lcd_world); create returns 0, ENOMEM or EBUSY. */
int drv_i915_lcd_world_create(struct i915_display *display);
void drv_i915_lcd_world_destroy(struct i915_display *display);

/* Sets and reports the display version of the device (12 = Tiger Lake, 13 = ADL-P class). */
void drv_i915_lcd_set_display_ver(struct i915_display *display, int ver);
int drv_i915_lcd_display_ver(void);

/*
 * Sets and reports the xd platform description the modeset's clocks read
 * (the display's; before one is set, the first description of the
 * display version).
 */
void drv_i915_lcd_set_xd_platform(struct i915_display *display, const struct i915_xd_platform *platform);
const struct i915_xd_platform *drv_i915_lcd_xd_platform(void);

/* Records an anomaly the backend found (a power well kept on, a broken time base) as an error of the screen whose operation runs. */
void drv_i915_lcd_backend_fault(const char *what);

/* Names the power domain of one power use of a screen's output: a screen backend's power_domain hook (pipe.c). */
int drv_i915_lcd_power_domain_of(void *ctx, unsigned use, unsigned index, int thunderbolt);

#endif /* DRIVERS_GPU_I915_DISPLAY_MODESET_H */
