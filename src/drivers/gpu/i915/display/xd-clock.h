/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The display clocks of the display 12 and 13 code (xd-clock.c): the CD
 * clock.
 *
 * The CD clock runs the display engine.  It comes from a PLL on the
 * reference clock (its ratio sets the PLL's rate) through a CD2X divider,
 * or, with the PLL off, it is the bypass clock, half the reference.  The
 * platform's table lists the CD clocks with their ratios; each CD clock
 * needs a voltage level, which the firmware is told after the change, as
 * it is asked before it whether the change may go ahead.
 *
 * At the display's start a CD clock the firmware left that is one of the
 * table's is kept; any other, a PLL that is off, or a bypass clock is
 * replaced by the table's lowest.  The platforms that can change the
 * PLL's ratio while it runs (display 13) crawl to a new one; the others,
 * and any change from a PLL that is off or in an unknown state, turn the
 * PLL off and on again.
 */

#ifndef DRIVERS_GPU_I915_DISPLAY_XD_CLOCK_H
#define DRIVERS_GPU_I915_DISPLAY_XD_CLOCK_H

#include <stdint.h>

#include "xd-io.h"
#include "xd-platform.h"

/* A PLL rate that is not known, which forces the PLL off and on again. */
#define I915_XD_CDCLK_VCO_UNKNOWN	0xffffffffU

/*
 * One state of the CD clock, in kHz: the reference, the PLL's rate (0
 * when it is off), the CD clock (0 when it must be set again), the bypass
 * clock, and the voltage level the CD clock needs.
 */
struct i915_xd_cdclk {
	uint32_t ref_khz;
	uint32_t vco_khz;
	uint32_t cdclk_khz;
	uint32_t bypass_khz;
	uint8_t voltage;
};

/*
 * What a change of the CD clock met, for the owner's log and
 * diagnostics: the state found before the start's check and whether the
 * start kept it or had to replace it, the state asked for, the
 * firmware's answers before (prepare) and after (voltage) the change,
 * whether the PLL and the control word were written, and the waits for
 * the PLL that ran out (its lock going, its lock coming, the crawl's
 * acknowledge).
 */
struct i915_xd_cdclk_report {
	struct i915_xd_cdclk before;
	int kept;
	int replaced;
	struct i915_xd_cdclk requested;
	int prepare_error;
	int voltage_error;
	int programmed;
	int unlock_late;
	int lock_late;
	int crawl_late;
};

void drv_i915_xd_cdclk_read(struct i915_xd_io *io, const struct i915_xd_platform *platform, struct i915_xd_cdclk *state);
uint8_t drv_i915_xd_cdclk_voltage(const struct i915_xd_platform *platform, uint32_t cdclk_khz);
uint32_t drv_i915_xd_cdclk_lowest(const struct i915_xd_platform *platform, uint32_t ref_khz, uint32_t min_khz);
uint32_t drv_i915_xd_cdclk_vco(const struct i915_xd_platform *platform, const struct i915_xd_cdclk *state, uint32_t cdclk_khz);
int drv_i915_xd_cdclk_legal(struct i915_xd_io *io, const struct i915_xd_platform *platform, const struct i915_xd_cdclk *state);
void drv_i915_xd_cdclk_set(struct i915_xd_io *io, const struct i915_xd_platform *platform, struct i915_xd_cdclk *state, const struct i915_xd_cdclk *target, struct i915_xd_cdclk_report *report);
void drv_i915_xd_cdclk_init(struct i915_xd_io *io, const struct i915_xd_platform *platform, struct i915_xd_cdclk *state, struct i915_xd_cdclk_report *report);
void drv_i915_xd_cdclk_uninit(struct i915_xd_io *io, const struct i915_xd_platform *platform, struct i915_xd_cdclk *state, struct i915_xd_cdclk_report *report);
uint32_t drv_i915_xd_cdclk_max(const struct i915_xd_cdclk *state);

#endif /* DRIVERS_GPU_I915_DISPLAY_XD_CLOCK_H */
