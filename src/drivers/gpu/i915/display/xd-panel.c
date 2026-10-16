/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The built-in panel of the display 12 and 13 code (see xd-panel.h).
 */

#include "xd-panel.h"
#include "xd-io.h"

#include "../intel/display-regs-xd-south.h"

#include <stddef.h>
#include <stdint.h>

/* The top of the VBT's minimum brightness scale. */
#define I915_XD_BACKLIGHT_VBT_SCALE	255U

/*
 * Tells whether a backlight controller can drive the panel.
 *
 * The south display has controllers controllers.  On the south displays
 * that share the second controller's pins with other functions (muxed
 * nonzero), the second controller is usable only when those pins are
 * given to the second panel power sequencer.
 *
 * Procedure referenced: Linux v6.8.12
 * drivers/gpu/drm/i915/display/intel_backlight.c
 * (cnp_backlight_controller_is_valid).
 */
int
drv_i915_xd_backlight_controller_usable(
	struct i915_xd_io *io,
	unsigned controller,
	unsigned controllers,
	int muxed)
{
	uint32_t chicken;

	/* A controller the south display does not have. */
	if (controller >= controllers)
		return 0;

	/* The second controller of a muxed display needs its pins. */
	if (controller == 1U && muxed != 0) {
		chicken = drv_i915_xd_io_read(io, I915_XD_SOUTH_CHICKEN1);
		if ((chicken & I915_XD_SOUTH_CHICKEN1_SECOND_PPS_IO) == 0U)
			return 0;
	}

	/* Succeeded: the controller can drive the panel. */
	return 1;
}

/*
 * Reads how the firmware left a controller: its period, its polarity and
 * whether it runs.
 */
void
drv_i915_xd_backlight_read(
	struct i915_xd_backlight *backlight,
	struct i915_xd_io *io,
	unsigned controller)
{
	uint32_t control;

	/* The polarity and the enable. */
	backlight->controller = controller;
	control = drv_i915_xd_io_read(io, I915_XD_SBLC_PWM_CTL(controller));
	backlight->active_low = 0;
	if ((control & I915_XD_SBLC_PWM_POLARITY) != 0U)
		backlight->active_low = 1;
	backlight->running = 0;
	if ((control & I915_XD_SBLC_PWM_ENABLE) != 0U)
		backlight->running = 1;

	/* The period, 0 when nobody programmed one. */
	backlight->period = drv_i915_xd_io_read(io, I915_XD_SBLC_PWM_FREQ(controller));
}

/*
 * Returns the period of a PWM frequency: the raw clock ticks of one
 * period, rounded to the closest; 0 for a frequency of 0.
 *
 * Procedure referenced: Linux v6.8.12
 * drivers/gpu/drm/i915/display/intel_backlight.c (cnp_hz_to_pwm).
 */
uint32_t
drv_i915_xd_backlight_period(
	unsigned rawclk_khz,
	unsigned hz)
{
	uint64_t ticks;

	/* No frequency has no period. */
	if (hz == 0U)
		return 0U;

	/* The raw clock's ticks per second shared out over the periods. */
	ticks = (uint64_t)rawclk_khz * 1000U;
	ticks = (ticks + hz / 2U) / hz;

	/* Succeeded: the period. */
	return (uint32_t)ticks;
}

/*
 * Returns the lowest duty of a period: the VBT's minimum brightness, a
 * share of 255, at most 64 of 255.
 *
 * Procedure referenced: Linux v6.8.12
 * drivers/gpu/drm/i915/display/intel_backlight.c (get_backlight_min_vbt).
 */
uint32_t
drv_i915_xd_backlight_min_duty(
	uint32_t period,
	unsigned vbt_min)
{
	uint32_t duty;

	/* A minimum above a quarter is cut to it. */
	if (vbt_min > I915_XD_BACKLIGHT_MIN_LIMIT)
		vbt_min = I915_XD_BACKLIGHT_MIN_LIMIT;

	/* The share of the period. */
	duty = drv_i915_xd_backlight_scale(vbt_min, 0U, I915_XD_BACKLIGHT_VBT_SCALE, 0U, period);

	/* Succeeded: the lowest duty. */
	return duty;
}

/*
 * Moves a value from one range to another, rounded to the closest.
 *
 * The value is first held inside its range.  An empty source range or a
 * target range upside down answers the target's bottom; the caller
 * reports those.
 */
uint32_t
drv_i915_xd_backlight_scale(
	uint32_t value,
	uint32_t from_min,
	uint32_t from_max,
	uint32_t to_min,
	uint32_t to_max)
{
	uint64_t moved;

	/* Ranges that cannot be scaled answer the bottom. */
	if (from_min >= from_max)
		return to_min;
	if (to_min > to_max)
		return to_min;

	/* Holds the value inside its range. */
	if (value < from_min)
		value = from_min;
	if (value > from_max)
		value = from_max;

	/* Scales in 64 bits, so the product does not overflow. */
	moved = (uint64_t)(value - from_min) * (to_max - to_min);
	moved = (moved + (from_max - from_min) / 2U) / (from_max - from_min);
	moved += to_min;

	/* Succeeded: the value in the target range. */
	return (uint32_t)moved;
}

/*
 * Reads the duty the controller drives.
 */
uint32_t
drv_i915_xd_backlight_duty(
	const struct i915_xd_backlight *backlight,
	struct i915_xd_io *io)
{
	uint32_t duty;

	/* Reads the duty register. */
	duty = drv_i915_xd_io_read(io, I915_XD_SBLC_PWM_DUTY(backlight->controller));

	/* Succeeded: the duty. */
	return duty;
}

/*
 * Sets the duty the controller drives; a running PWM takes it at once.
 */
void
drv_i915_xd_backlight_set_duty(
	const struct i915_xd_backlight *backlight,
	struct i915_xd_io *io,
	uint32_t duty)
{
	/* Writes the duty register. */
	drv_i915_xd_io_write(io, I915_XD_SBLC_PWM_DUTY(backlight->controller), duty);
}

/*
 * Starts the PWM with a duty: a PWM still running is stopped first, then
 * the period and the duty are set, the polarity is set and pushed out,
 * and the PWM is started.  Returns 1 when the PWM had to be stopped
 * first, 0 otherwise.
 *
 * Procedure referenced: Linux v6.8.12
 * drivers/gpu/drm/i915/display/intel_backlight.c (cnp_enable_backlight).
 */
int
drv_i915_xd_backlight_enable(
	struct i915_xd_backlight *backlight,
	struct i915_xd_io *io,
	uint32_t duty)
{
	uint32_t control_offset;
	uint32_t control;
	int was_running;

	/* Stops a PWM that still runs, so the new period takes effect cleanly. */
	control_offset = I915_XD_SBLC_PWM_CTL(backlight->controller);
	control = drv_i915_xd_io_read(io, control_offset);
	was_running = 0;
	if ((control & I915_XD_SBLC_PWM_ENABLE) != 0U) {
		was_running = 1;
		drv_i915_xd_io_write(io, control_offset, control & ~I915_XD_SBLC_PWM_ENABLE);
	}

	/* The period, then the duty. */
	drv_i915_xd_io_write(io, I915_XD_SBLC_PWM_FREQ(backlight->controller), backlight->period);
	drv_i915_xd_io_write(io, I915_XD_SBLC_PWM_DUTY(backlight->controller), duty);

	/* The polarity alone, pushed out before the enable. */
	control = 0U;
	if (backlight->active_low != 0)
		control |= I915_XD_SBLC_PWM_POLARITY;
	drv_i915_xd_io_write(io, control_offset, control);
	drv_i915_xd_io_posting_read(io, control_offset);

	/* Starts the PWM. */
	drv_i915_xd_io_write(io, control_offset, control | I915_XD_SBLC_PWM_ENABLE);
	backlight->running = 1;

	/* Succeeded: tells whether the PWM was found running. */
	return was_running;
}

/*
 * Stops the PWM after setting its duty.
 *
 * Procedure referenced: Linux v6.8.12
 * drivers/gpu/drm/i915/display/intel_backlight.c (cnp_disable_backlight).
 */
void
drv_i915_xd_backlight_disable(
	struct i915_xd_backlight *backlight,
	struct i915_xd_io *io,
	uint32_t duty)
{
	/* The duty, then the enable cleared. */
	drv_i915_xd_io_write(io, I915_XD_SBLC_PWM_DUTY(backlight->controller), duty);
	(void)drv_i915_xd_io_rmw(io, I915_XD_SBLC_PWM_CTL(backlight->controller), I915_XD_SBLC_PWM_ENABLE, 0U);
	backlight->running = 0;
}
