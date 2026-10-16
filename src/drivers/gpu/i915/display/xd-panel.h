/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The built-in panel of the display 12 and 13 code (xd-panel.c): the
 * backlight PWM of the south display.
 *
 * The PWM runs from the raw clock of the south display.  Its period, in
 * raw clock ticks, sets the frequency the panel's backlight driver sees;
 * its duty, in the same ticks, sets the brightness.  A display may have
 * two controllers; the panel's is named by the VBT.  The caller holds
 * the backlight lock around every call.
 */

#ifndef DRIVERS_GPU_I915_DISPLAY_XD_PANEL_H
#define DRIVERS_GPU_I915_DISPLAY_XD_PANEL_H

#include <stdint.h>

#include "xd-io.h"

/*
 * The largest VBT minimum brightness taken as it is, of 255: a larger
 * one (some machines say 255) is cut to this quarter of the range, so the
 * lowest brightness is never the highest.
 */
#define I915_XD_BACKLIGHT_MIN_LIMIT	64U

/*
 * The backlight PWM of one panel as it was found: the controller, the
 * period (0 when nobody programmed one), whether the PWM is inverted
 * (active low) and whether it runs.
 */
struct i915_xd_backlight {
	unsigned controller;
	uint32_t period;
	int active_low;
	int running;
};

int drv_i915_xd_backlight_controller_usable(struct i915_xd_io *io, unsigned controller, unsigned controllers, int muxed);
void drv_i915_xd_backlight_read(struct i915_xd_backlight *backlight, struct i915_xd_io *io, unsigned controller);
uint32_t drv_i915_xd_backlight_period(unsigned rawclk_khz, unsigned hz);
uint32_t drv_i915_xd_backlight_min_duty(uint32_t period, unsigned vbt_min);
uint32_t drv_i915_xd_backlight_scale(uint32_t value, uint32_t from_min, uint32_t from_max, uint32_t to_min, uint32_t to_max);
uint32_t drv_i915_xd_backlight_duty(const struct i915_xd_backlight *backlight, struct i915_xd_io *io);
void drv_i915_xd_backlight_set_duty(const struct i915_xd_backlight *backlight, struct i915_xd_io *io, uint32_t duty);
int drv_i915_xd_backlight_enable(struct i915_xd_backlight *backlight, struct i915_xd_io *io, uint32_t duty);
void drv_i915_xd_backlight_disable(struct i915_xd_backlight *backlight, struct i915_xd_io *io, uint32_t duty);

#endif /* DRIVERS_GPU_I915_DISPLAY_XD_PANEL_H */
