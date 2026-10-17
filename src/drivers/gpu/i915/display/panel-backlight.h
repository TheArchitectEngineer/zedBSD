/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The panel backlight of the one-screen path (panel-backlight.c).
 *
 * The brightness, the OpRegion (ACPI) brightness request and the
 * backlight power of the selected screen, each answering with an
 * I915_LCD_MS_* result.  The modeset object's own hooks
 * (drv_i915_lcd_ms_set_brightness(), _set_acpi(), _backlight_power() and
 * _user_level()) take the modeset environment's types and are declared by
 * modeset-internal.h.
 */

#ifndef DRIVERS_GPU_I915_DISPLAY_PANEL_BACKLIGHT_H
#define DRIVERS_GPU_I915_DISPLAY_PANEL_BACKLIGHT_H

#include "internal.h"

struct i915_display;

/*
 * ==== The backlight of the selected screen ====
 */


#endif /* DRIVERS_GPU_I915_DISPLAY_PANEL_BACKLIGHT_H */
