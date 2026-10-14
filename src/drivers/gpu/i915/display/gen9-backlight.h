/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The panel's backlight of a Gen9 node (gen9-backlight.c, WS237 stage 2).
 *
 * The south display's PWM drives the eDP panel's backlight.  Its period
 * and its duty are offered as the kernel's backlight device
 * (/dev/backlight, kern/backlight.h), the same one the Settings app's
 * brightness uses on a Gen12 node.  The firmware's period is kept; only
 * the duty changes.
 */

#ifndef DRIVERS_GPU_I915_DISPLAY_GEN9_BACKLIGHT_H
#define DRIVERS_GPU_I915_DISPLAY_GEN9_BACKLIGHT_H

#include <stdint.h>

struct i915_device;

/*
 * The lowest duty a brightness of 1 % and up maps to, in 255ths of the
 * period (the VBT's minimum brightness scale), so the panel never goes
 * dark by a brightness change.
 */
#define I915_GEN9_BACKLIGHT_MIN_255	4U

/*
 * The PWM's registers as read: the control, and the period and the duty
 * (on a Sunrise Point PCH both are in the one register CTL2, which is
 * then given as period and the duty is taken from it).
 */
struct i915_skl_pwm_registers {
	/* Nonzero for a Cannon Point PCH (separate period and duty registers). */
	int cnp;

	/* The control: the enable and the polarity. */
	uint32_t control;

	/* CTL2 on Sunrise Point; the period register on Cannon Point. */
	uint32_t period;

	/* The duty register on Cannon Point (unused on Sunrise Point). */
	uint32_t duty;
};

/*
 * The PWM as the backlight uses it.
 */
struct i915_skl_pwm {
	/* Nonzero while the PWM runs. */
	int enabled;

	/* Nonzero when the PWM's output is inverted (active low). */
	int active_low;

	/* The period, the lowest duty a brightness maps to, and the duty now. */
	uint32_t max;
	uint32_t min;
	uint32_t duty;
};

/* Decodes the PWM's registers; EINVAL when the period is zero. */
int drv_i915_skl_pwm_decode(const struct i915_skl_pwm_registers *registers, struct i915_skl_pwm *pwm);

/* Reports the duty a brightness (a percentage) maps to. */
uint32_t drv_i915_skl_pwm_duty(const struct i915_skl_pwm *pwm, uint32_t percent);

/* Reports the brightness (a percentage) a duty maps to. */
uint32_t drv_i915_skl_pwm_percent(const struct i915_skl_pwm *pwm, uint32_t duty);

/* Offers the panel's backlight when the step asks for it and the PWM runs. */
void drv_i915_gen9_backlight_start(struct i915_device *device);

/* Withdraws the backlight device. */
void drv_i915_gen9_backlight_stop(struct i915_device *device);

/* Turns the panel's light off for a sleep (whatever the step), and back on after it (WS237 p005). */
void drv_i915_gen9_backlight_suspend(struct i915_device *device);
void drv_i915_gen9_backlight_resume(struct i915_device *device);

#endif /* DRIVERS_GPU_I915_DISPLAY_GEN9_BACKLIGHT_H */
