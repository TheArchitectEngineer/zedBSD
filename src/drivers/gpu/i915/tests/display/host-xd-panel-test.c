/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host test of the xd panel (display/xd-panel.c) on the register
 * model (xd-fake-regs.c): the backlight PWM's period of a frequency on
 * the two raw clocks, the lowest duty of the VBT's minimum, the scaling
 * between ranges, the controllers a panel may use, the reading of the
 * firmware's PWM, and the exact writes of a start and a stop.
 */

#include "host-test.h"
#include "xd-fake-regs.h"

#include "../../display/xd-io.h"
#include "../../display/xd-panel.h"
#include "../../intel/display-regs-xd-south.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/*
 * The period of the 200 Hz PWM of the machines the display was written
 * on, in ticks of the 19.2 MHz raw clock: 96000, what the firmware
 * leaves in the period register.
 */
#define I915_XD_PANEL_TEST_PERIOD	0x17700U

/*
 * The window the checks share, reaching a fresh model each time
 * (i915_xd_panel_test_setup()).
 */
static struct i915_xd_io i915_xd_panel_test_io;

static void i915_xd_panel_test_setup(void);
static unsigned i915_xd_panel_test_writes(uint32_t *offsets, uint32_t *values, unsigned max);
static void i915_xd_panel_test_numbers(void);
static void i915_xd_panel_test_controllers(void);
static void i915_xd_panel_test_pwm(void);

/*
 * Runs every check of the xd panel and reports.
 */
int
main(void)
{
	int failures;

	/* Runs each group of checks. */
	i915_xd_panel_test_numbers();
	i915_xd_panel_test_controllers();
	i915_xd_panel_test_pwm();

	/* Reports the result. */
	failures = i915_host_report("host_xd_panel_test");
	if (failures != 0)
		return 1;

	/* Succeeded: every check passed. */
	return 0;
}

/* Starts a fresh model and the window on it. */
static void
i915_xd_panel_test_setup(void)
{
	struct i915_xd_io_hooks hooks;

	/* A fresh model and a window that reaches it. */
	i915_xd_fake_reset();
	i915_xd_fake_hooks(&hooks);
	drv_i915_xd_io_init(&i915_xd_panel_test_io, NULL, NULL);
	drv_i915_xd_io_set_hooks(&i915_xd_panel_test_io, &hooks);
}

/* Copies the writes of the window, oldest first; returns how many. */
static unsigned
i915_xd_panel_test_writes(
	uint32_t *offsets,
	uint32_t *values,
	unsigned max)
{
	struct i915_xd_io_record records[I915_XD_IO_RECORDS];
	unsigned count;
	unsigned index;
	unsigned kept;

	/* Takes the record and keeps the writes. */
	count = drv_i915_xd_io_records(&i915_xd_panel_test_io, records, I915_XD_IO_RECORDS);
	kept = 0U;
	for (index = 0U; index < count && kept < max; index++) {
		if (records[index].kind != I915_XD_IO_RECORD_WRITE)
			continue;

		offsets[kept] = records[index].offset;
		values[kept] = records[index].value;
		kept++;
	}

	/* Succeeded: the writes copied. */
	return kept;
}

/* The period of a frequency, the lowest duty and the scaling. */
static void
i915_xd_panel_test_numbers(void)
{
	uint32_t value;

	/* 19.2 MHz / 200 Hz is the firmware's period; 24 MHz gives 120000; 0 Hz none. */
	value = drv_i915_xd_backlight_period(19200U, 200U);
	i915_host_check(value == I915_XD_PANEL_TEST_PERIOD, "panel: 19.2 MHz / 200 Hz = 0x17700");
	value = drv_i915_xd_backlight_period(24000U, 200U);
	i915_host_check(value == 120000U, "panel: 24 MHz / 200 Hz = 120000");
	value = drv_i915_xd_backlight_period(19200U, 0U);
	i915_host_check(value == 0U, "panel: no frequency has no period");

	/* A period that does not divide evenly is rounded to the closest tick. */
	value = drv_i915_xd_backlight_period(19200U, 7U);
	i915_host_check(value == 2742857U, "panel: the period is rounded to the closest");

	/* The VBT's minimum 6 of 255 of 96000, rounded; 255 is cut to 64. */
	value = drv_i915_xd_backlight_min_duty(I915_XD_PANEL_TEST_PERIOD, 6U);
	i915_host_check(value == 2259U, "panel: the lowest duty of 6/255");
	value = drv_i915_xd_backlight_min_duty(I915_XD_PANEL_TEST_PERIOD, 255U);
	i915_host_check(value == 24094U, "panel: a minimum of 255/255 is cut to 64/255");

	/* Scaling: the ends, a value held inside its range, and the ranges that cannot scale. */
	value = drv_i915_xd_backlight_scale(100U, 0U, 100U, 2259U, 96000U);
	i915_host_check(value == 96000U, "panel: the top of the range scales to the top");
	value = drv_i915_xd_backlight_scale(50U, 0U, 100U, 0U, 96000U);
	i915_host_check(value == 48000U, "panel: the middle scales to the middle");
	value = drv_i915_xd_backlight_scale(500U, 0U, 100U, 0U, 96000U);
	i915_host_check(value == 96000U, "panel: a value above its range is held at the top");
	value = drv_i915_xd_backlight_scale(5U, 10U, 10U, 7U, 96000U);
	i915_host_check(value == 7U, "panel: an empty source range answers the target's bottom");
	value = drv_i915_xd_backlight_scale(5U, 0U, 10U, 9U, 8U);
	i915_host_check(value == 9U, "panel: a target range upside down answers its bottom");
}

/* The controllers a panel may use. */
static void
i915_xd_panel_test_controllers(void)
{
	int usable;

	/* Controller 0 of two is usable; controller 2 of two is not. */
	i915_xd_panel_test_setup();
	usable = drv_i915_xd_backlight_controller_usable(&i915_xd_panel_test_io, 0U, 2U, 1);
	i915_host_check(usable == 1 && i915_xd_fake_reads() == 0U, "panel: controller 0 is usable without a read");
	usable = drv_i915_xd_backlight_controller_usable(&i915_xd_panel_test_io, 2U, 2U, 0);
	i915_host_check(usable == 0, "panel: controller 2 of two is not");

	/* Controller 1 of a muxed display needs its pins in SOUTH_CHICKEN1. */
	usable = drv_i915_xd_backlight_controller_usable(&i915_xd_panel_test_io, 1U, 2U, 1);
	i915_host_check(usable == 0, "panel: controller 1 of a muxed display without its pins");
	i915_xd_fake_set(I915_XD_SOUTH_CHICKEN1, I915_XD_SOUTH_CHICKEN1_SECOND_PPS_IO);
	usable = drv_i915_xd_backlight_controller_usable(&i915_xd_panel_test_io, 1U, 2U, 1);
	i915_host_check(usable == 1, "panel: controller 1 of a muxed display with its pins");

	/* Controller 1 of a display that does not mux it is usable as it is. */
	i915_xd_panel_test_setup();
	usable = drv_i915_xd_backlight_controller_usable(&i915_xd_panel_test_io, 1U, 2U, 0);
	i915_host_check(usable == 1 && i915_xd_fake_reads() == 0U, "panel: controller 1 without muxing");
}

/* Reading the firmware's PWM, starting and stopping it. */
static void
i915_xd_panel_test_pwm(void)
{
	struct i915_xd_backlight pwm;
	uint32_t offsets[8];
	uint32_t values[8];
	uint32_t duty;
	unsigned count;
	int was_running;

	/* The firmware's PWM: running, inverted, with its period. */
	i915_xd_panel_test_setup();
	i915_xd_fake_set(I915_XD_SBLC_PWM_CTL(0U), I915_XD_SBLC_PWM_ENABLE | I915_XD_SBLC_PWM_POLARITY);
	i915_xd_fake_set(I915_XD_SBLC_PWM_FREQ(0U), I915_XD_PANEL_TEST_PERIOD);
	i915_xd_fake_set(I915_XD_SBLC_PWM_DUTY(0U), 0x1234U);
	drv_i915_xd_backlight_read(&pwm, &i915_xd_panel_test_io, 0U);
	duty = drv_i915_xd_backlight_duty(&pwm, &i915_xd_panel_test_io);
	i915_host_check(pwm.controller == 0U && pwm.period == I915_XD_PANEL_TEST_PERIOD &&
			pwm.active_low == 1 && pwm.running == 1 && duty == 0x1234U,
			"panel: the firmware's PWM is read: period, polarity, enable, duty");

	/* Starting a running PWM: stopped first, then the period, the duty, the polarity and the enable. */
	was_running = drv_i915_xd_backlight_enable(&pwm, &i915_xd_panel_test_io, 0x17700U);
	count = i915_xd_panel_test_writes(offsets, values, 8U);
	i915_host_check(was_running == 1 &&
			count == 5U &&
			offsets[0] == 0xc8250U && values[0] == I915_XD_SBLC_PWM_POLARITY &&
			offsets[1] == 0xc8254U && values[1] == 0x17700U &&
			offsets[2] == 0xc8258U && values[2] == 0x17700U &&
			offsets[3] == 0xc8250U && values[3] == I915_XD_SBLC_PWM_POLARITY &&
			offsets[4] == 0xc8250U && values[4] == (I915_XD_SBLC_PWM_POLARITY | I915_XD_SBLC_PWM_ENABLE),
			"panel: a running PWM is stopped, then started with the period and the duty");

	/* Starting a stopped PWM on controller 1: no stop, and the registers 0x100 up. */
	i915_xd_panel_test_setup();
	pwm.controller = 1U;
	pwm.period = I915_XD_PANEL_TEST_PERIOD;
	pwm.active_low = 0;
	pwm.running = 0;
	was_running = drv_i915_xd_backlight_enable(&pwm, &i915_xd_panel_test_io, 0x100U);
	count = i915_xd_panel_test_writes(offsets, values, 8U);
	i915_host_check(was_running == 0 &&
			count == 4U &&
			offsets[0] == 0xc8354U &&
			offsets[1] == 0xc8358U && values[1] == 0x100U &&
			offsets[2] == 0xc8350U && values[2] == 0U &&
			offsets[3] == 0xc8350U && values[3] == I915_XD_SBLC_PWM_ENABLE &&
			pwm.running == 1,
			"panel: a stopped PWM on controller 1 is started without a stop");

	/* Stopping: the duty, then the enable cleared with the polarity kept. */
	i915_xd_panel_test_setup();
	pwm.controller = 0U;
	i915_xd_fake_set(I915_XD_SBLC_PWM_CTL(0U), I915_XD_SBLC_PWM_ENABLE | I915_XD_SBLC_PWM_POLARITY);
	drv_i915_xd_backlight_disable(&pwm, &i915_xd_panel_test_io, 0U);
	count = i915_xd_panel_test_writes(offsets, values, 8U);
	i915_host_check(count == 2U &&
			offsets[0] == 0xc8258U && values[0] == 0U &&
			offsets[1] == 0xc8250U && values[1] == I915_XD_SBLC_PWM_POLARITY &&
			pwm.running == 0,
			"panel: a stop writes the duty, then clears only the enable");

	/* Setting the duty writes it alone. */
	i915_xd_panel_test_setup();
	drv_i915_xd_backlight_set_duty(&pwm, &i915_xd_panel_test_io, 48000U);
	count = i915_xd_panel_test_writes(offsets, values, 8U);
	i915_host_check(count == 1U && offsets[0] == 0xc8258U && values[0] == 48000U, "panel: the duty alone");
}
