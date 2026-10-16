/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The display clocks of the display 12 and 13 code (see xd-clock.h).
 */

#include "xd-clock.h"
#include "xd-io.h"
#include "xd-platform.h"

#include "../intel/display-regs-xd.h"

#include <uapi/errno.h>
#include <stddef.h>
#include <stdint.h>

/*
 * How long the CD clock PLL may take to lose its lock, to lock, and to
 * acknowledge a crawl.
 *
 * Values referenced: Linux v6.8.12 drivers/gpu/drm/i915/display/intel_cdclk.c
 * (icl_cdclk_pll_disable, icl_cdclk_pll_enable, adlp_cdclk_pll_crawl).
 */
#define I915_XD_CDCLK_PLL_US		1000U

/*
 * How the firmware is asked whether a CD clock change may go ahead: each
 * request may take 500 us, the requests are repeated 10 us apart for
 * 3 ms, then for 50 ms more; the voltage level is told with one request
 * of 500 us.
 *
 * Values referenced: Linux v6.8.12 drivers/gpu/drm/i915/intel_pcode.c
 * (skl_pcode_request, __snb_pcode_rw, snb_pcode_write), and
 * drivers/gpu/drm/i915/display/intel_cdclk.c (bxt_set_cdclk: the base of
 * 3 ms).
 */
#define I915_XD_CDCLK_MAILBOX_US	500U
#define I915_XD_CDCLK_PREPARE_GAP_US	10U
#define I915_XD_CDCLK_PREPARE_US	3000U
#define I915_XD_CDCLK_PREPARE_MORE_US	50000U

/*
 * The highest CD clock of display 11 on: on a 24 MHz reference, and on
 * the others.
 *
 * Values referenced: Linux v6.8.12 drivers/gpu/drm/i915/display/intel_cdclk.c
 * (intel_update_max_cdclk).
 */
#define I915_XD_CDCLK_MAX_24MHZ		648000U
#define I915_XD_CDCLK_MAX		652800U

static uint32_t i915_xd_clock_div_round(uint32_t a, uint32_t b);
static uint32_t i915_xd_cdclk_divider(uint32_t cdclk_khz, uint32_t vco_khz);
static uint32_t i915_xd_cdclk_decimal(uint32_t cdclk_khz);
static int i915_xd_cdclk_prepare(struct i915_xd_io *io);
static void i915_xd_cdclk_pll_off(struct i915_xd_io *io, struct i915_xd_cdclk *state, struct i915_xd_cdclk_report *report);
static void i915_xd_cdclk_pll_on(struct i915_xd_io *io, struct i915_xd_cdclk *state, uint32_t vco_khz, struct i915_xd_cdclk_report *report);
static void i915_xd_cdclk_pll_crawl(struct i915_xd_io *io, struct i915_xd_cdclk *state, uint32_t vco_khz, struct i915_xd_cdclk_report *report);
static void i915_xd_cdclk_program(struct i915_xd_io *io, const struct i915_xd_platform *platform, struct i915_xd_cdclk *state, const struct i915_xd_cdclk *target, struct i915_xd_cdclk_report *report);
static void i915_xd_cdclk_report_clear(struct i915_xd_cdclk_report *report);

/*
 * Reads the CD clock the hardware runs: the reference from DSSM, the
 * PLL's rate when it is on and locked (its ratio times the reference),
 * the bypass clock (half the reference), and the CD clock (the PLL's rate
 * through the CD2X divider, or the bypass clock).  The voltage level is
 * not readable; the one the CD clock needs is assumed.
 *
 * Procedure referenced: Linux v6.8.12 drivers/gpu/drm/i915/display/intel_cdclk.c
 * (bxt_get_cdclk, bxt_de_pll_readout, icl_readout_refclk).
 */
void
drv_i915_xd_cdclk_read(
	struct i915_xd_io *io,
	const struct i915_xd_platform *platform,
	struct i915_xd_cdclk *state)
{
	uint32_t dssm;
	uint32_t pll;
	uint32_t control;
	uint32_t divider_x2;

	/* The reference clock; a code DSSM does not define reads as 24 MHz. */
	dssm = (drv_i915_xd_io_read(io, I915_XD_DSSM) & I915_XD_DSSM_REFCLK) >> 29;
	switch (dssm) {
	case I915_XD_DSSM_REFCLK_19P2:
		state->ref_khz = 19200U;
		break;
	case I915_XD_DSSM_REFCLK_38P4:
		state->ref_khz = 38400U;
		break;
	default:
		state->ref_khz = 24000U;
		break;
	}

	/* The PLL's rate, 0 when it is off or not locked. */
	pll = drv_i915_xd_io_read(io, I915_XD_CDCLK_PLL_ENABLE);
	state->vco_khz = 0U;
	if ((pll & I915_XD_CDCLK_PLL_ON) != 0U && (pll & I915_XD_CDCLK_PLL_LOCK) != 0U)
		state->vco_khz = (pll & I915_XD_CDCLK_PLL_RATIO) * state->ref_khz;

	/* The bypass clock is half the reference, and is the CD clock while the PLL is off. */
	state->bypass_khz = state->ref_khz / 2U;
	state->cdclk_khz = state->bypass_khz;

	/* The CD clock through the CD2X divider (given here doubled: 1, 1.5, 2, 4). */
	if (state->vco_khz != 0U) {
		control = drv_i915_xd_io_read(io, I915_XD_CDCLK_CTL);
		switch ((control & I915_XD_CDCLK_CD2X_DIV) >> 22) {
		case I915_XD_CDCLK_CD2X_DIV_1_5:
			divider_x2 = 3U;
			break;
		case I915_XD_CDCLK_CD2X_DIV_2:
			divider_x2 = 4U;
			break;
		case I915_XD_CDCLK_CD2X_DIV_4:
			divider_x2 = 8U;
			break;
		default:
			divider_x2 = 2U;
			break;
		}
		state->cdclk_khz = i915_xd_clock_div_round(state->vco_khz, divider_x2);
	}

	/* The voltage level the CD clock needs. */
	state->voltage = drv_i915_xd_cdclk_voltage(platform, state->cdclk_khz);
}

/*
 * Reports the voltage level a CD clock needs: the first level whose
 * highest CD clock covers it, or the top level.
 *
 * Procedure referenced: Linux v6.8.12 drivers/gpu/drm/i915/display/intel_cdclk.c
 * (tgl_calc_voltage_level, calc_voltage_level).
 */
uint8_t
drv_i915_xd_cdclk_voltage(
	const struct i915_xd_platform *platform,
	uint32_t cdclk_khz)
{
	unsigned level;

	/* Walks the levels from the lowest. */
	for (level = 0U; level < platform->nvoltage; level++) {
		if (cdclk_khz <= platform->voltage_steps[level])
			return (uint8_t)level;
	}

	/* A platform without levels has only level 0. */
	if (platform->nvoltage == 0U)
		return 0U;

	/* Succeeded: a CD clock above every level takes the top one. */
	return (uint8_t)(platform->nvoltage - 1U);
}

/*
 * Reports the lowest CD clock of the platform's table on a reference
 * that is at least a minimum, or 0 when none is.
 *
 * Procedure referenced: Linux v6.8.12 drivers/gpu/drm/i915/display/intel_cdclk.c
 * (bxt_calc_cdclk).
 */
uint32_t
drv_i915_xd_cdclk_lowest(
	const struct i915_xd_platform *platform,
	uint32_t ref_khz,
	uint32_t min_khz)
{
	unsigned row;

	/* The table lists each reference's CD clocks from the lowest. */
	for (row = 0U; row < platform->ncdclk; row++) {
		if (platform->cdclk[row].ref_khz != ref_khz)
			continue;
		if (platform->cdclk[row].cdclk_khz < min_khz)
			continue;

		/* Succeeded: the first one fast enough. */
		return platform->cdclk[row].cdclk_khz;
	}

	/* No CD clock of the reference is fast enough. */
	return 0U;
}

/*
 * Reports the PLL rate a CD clock of the table needs on the state's
 * reference: 0 for the bypass clock, and 0 for a CD clock the table does
 * not have.
 *
 * Procedure referenced: Linux v6.8.12 drivers/gpu/drm/i915/display/intel_cdclk.c
 * (bxt_calc_cdclk_pll_vco).
 */
uint32_t
drv_i915_xd_cdclk_vco(
	const struct i915_xd_platform *platform,
	const struct i915_xd_cdclk *state,
	uint32_t cdclk_khz)
{
	unsigned row;

	/* The bypass clock runs with the PLL off. */
	if (cdclk_khz == state->bypass_khz)
		return 0U;

	/* Looks the CD clock up for the reference. */
	for (row = 0U; row < platform->ncdclk; row++) {
		if (platform->cdclk[row].ref_khz != state->ref_khz)
			continue;
		if (platform->cdclk[row].cdclk_khz != cdclk_khz)
			continue;

		/* Succeeded: the ratio times the reference. */
		return state->ref_khz * platform->cdclk[row].ratio;
	}

	/* The table has no such CD clock. */
	return 0U;
}

/*
 * Tells whether a CD clock read from the hardware may be kept (1 or 0):
 * the PLL runs, the CD clock is not the bypass clock, it is one of the
 * table's with the PLL rate the table gives it, and the control word
 * holds its divider and decimal frequency (the pipe the last change
 * waited for is not compared).
 *
 * Procedure referenced: Linux v6.8.12 drivers/gpu/drm/i915/display/intel_cdclk.c
 * (bxt_sanitize_cdclk).
 */
int
drv_i915_xd_cdclk_legal(
	struct i915_xd_io *io,
	const struct i915_xd_platform *platform,
	const struct i915_xd_cdclk *state)
{
	uint32_t control;
	uint32_t expected;
	uint32_t cdclk;
	uint32_t vco;

	/* A PLL that is off, or the bypass clock, is replaced. */
	if (state->vco_khz == 0U)
		return 0;
	if (state->cdclk_khz == state->bypass_khz)
		return 0;

	/* The CD clock must be one of the table's. */
	cdclk = drv_i915_xd_cdclk_lowest(platform, state->ref_khz, state->cdclk_khz);
	if (cdclk != state->cdclk_khz)
		return 0;

	/* The PLL must run at the rate the table gives it. */
	vco = drv_i915_xd_cdclk_vco(platform, state, cdclk);
	if (vco != state->vco_khz)
		return 0;

	/* The control word must hold its divider and decimal frequency, whatever pipe it waited for. */
	control = drv_i915_xd_io_read(io, I915_XD_CDCLK_CTL) & ~I915_XD_CDCLK_CD2X_PIPE_FIELD;
	expected = i915_xd_cdclk_decimal(cdclk) | i915_xd_cdclk_divider(cdclk, vco);
	if (control != expected)
		return 0;

	/* Succeeded: the CD clock may be kept. */
	return 1;
}

/*
 * Changes the CD clock to a target: asks the firmware whether it may (a
 * refusal changes nothing), changes the PLL and writes the control word
 * (synchronized to no pipe), tells the firmware the voltage level (a
 * refusal leaves the hardware changed and the state as it was, the PLL's
 * rate excepted), then reads the CD clock back with the voltage level
 * asked for.
 *
 * Procedure referenced: Linux v6.8.12 drivers/gpu/drm/i915/display/intel_cdclk.c
 * (bxt_set_cdclk).
 */
void
drv_i915_xd_cdclk_set(
	struct i915_xd_io *io,
	const struct i915_xd_platform *platform,
	struct i915_xd_cdclk *state,
	const struct i915_xd_cdclk *target,
	struct i915_xd_cdclk_report *report)
{
	uint32_t data;
	int error;

	/* Asks the firmware whether the change may go ahead. */
	report->requested = *target;
	error = i915_xd_cdclk_prepare(io);
	report->prepare_error = error;
	if (error != 0)
		return;

	/* Changes the PLL and writes the control word. */
	i915_xd_cdclk_program(io, platform, state, target, report);

	/* Tells the firmware the voltage level the new CD clock needs. */
	data = target->voltage;
	error = drv_i915_xd_io_mailbox(io, I915_XD_MAILBOX_CDCLK_CONTROL, &data, NULL, I915_XD_CDCLK_MAILBOX_US);
	report->voltage_error = error;
	if (error != 0)
		return;

	/* Reads the CD clock back; the voltage level is the one asked for. */
	drv_i915_xd_cdclk_read(io, platform, state);
	state->voltage = target->voltage;
}

/*
 * Brings the CD clock up at the display's start: keeps the one the
 * firmware left when it may be kept, and otherwise sets the table's
 * lowest.
 *
 * Procedure referenced: Linux v6.8.12 drivers/gpu/drm/i915/display/intel_cdclk.c
 * (bxt_cdclk_init_hw, bxt_sanitize_cdclk).
 */
void
drv_i915_xd_cdclk_init(
	struct i915_xd_io *io,
	const struct i915_xd_platform *platform,
	struct i915_xd_cdclk *state,
	struct i915_xd_cdclk_report *report)
{
	struct i915_xd_cdclk target;
	int legal;

	/* Reads what the firmware left, and keeps it when it may be kept. */
	i915_xd_cdclk_report_clear(report);
	drv_i915_xd_cdclk_read(io, platform, state);
	report->before = *state;
	legal = drv_i915_xd_cdclk_legal(io, platform, state);
	if (legal) {
		report->kept = 1;
		return;
	}

	/* Forgets it: the PLL in an unknown state is turned off and on again. */
	report->replaced = 1;
	state->cdclk_khz = 0U;
	state->vco_khz = I915_XD_CDCLK_VCO_UNKNOWN;

	/* Sets the table's lowest CD clock. */
	target = *state;
	target.cdclk_khz = drv_i915_xd_cdclk_lowest(platform, state->ref_khz, 0U);
	target.vco_khz = drv_i915_xd_cdclk_vco(platform, state, target.cdclk_khz);
	target.voltage = drv_i915_xd_cdclk_voltage(platform, target.cdclk_khz);
	drv_i915_xd_cdclk_set(io, platform, state, &target, report);
}

/*
 * Takes the CD clock down before a suspend: the bypass clock with the
 * PLL off.
 *
 * Procedure referenced: Linux v6.8.12 drivers/gpu/drm/i915/display/intel_cdclk.c
 * (bxt_cdclk_uninit_hw).
 */
void
drv_i915_xd_cdclk_uninit(
	struct i915_xd_io *io,
	const struct i915_xd_platform *platform,
	struct i915_xd_cdclk *state,
	struct i915_xd_cdclk_report *report)
{
	struct i915_xd_cdclk target;

	/* The bypass clock with the PLL off, at the voltage level it needs. */
	i915_xd_cdclk_report_clear(report);
	target = *state;
	target.cdclk_khz = target.bypass_khz;
	target.vco_khz = 0U;
	target.voltage = drv_i915_xd_cdclk_voltage(platform, target.cdclk_khz);

	/* Sets it. */
	drv_i915_xd_cdclk_set(io, platform, state, &target, report);
}

/*
 * Reports the highest CD clock the display may use on the state's
 * reference.
 *
 * Procedure referenced: Linux v6.8.12 drivers/gpu/drm/i915/display/intel_cdclk.c
 * (intel_update_max_cdclk).
 */
uint32_t
drv_i915_xd_cdclk_max(
	const struct i915_xd_cdclk *state)
{
	/* A 24 MHz reference reaches a little less. */
	if (state->ref_khz == 24000U)
		return I915_XD_CDCLK_MAX_24MHZ;

	/* Succeeded: the 19.2 and 38.4 MHz references' highest. */
	return I915_XD_CDCLK_MAX;
}

/* Divides, rounding to the closest. */
static uint32_t
i915_xd_clock_div_round(
	uint32_t a,
	uint32_t b)
{
	/* Half the divisor added before the truncating division. */
	return (a + b / 2U) / b;
}

/*
 * Reports the CD2X divider field of a CD clock on a PLL rate: 1.5, 2 or
 * 4 by their ratio, and 1 for any other (the bypass clock with the PLL
 * off among them).
 *
 * Procedure referenced: Linux v6.8.12 drivers/gpu/drm/i915/display/intel_cdclk.c
 * (bxt_cdclk_cd2x_div_sel).
 */
static uint32_t
i915_xd_cdclk_divider(
	uint32_t cdclk_khz,
	uint32_t vco_khz)
{
	uint32_t ratio;

	/* A CD clock of 0 has no ratio. */
	if (cdclk_khz == 0U)
		return I915_XD_CDCLK_CD2X_DIV_1 << 22;

	/* Picks the divider by the ratio of the PLL rate to the CD clock. */
	ratio = i915_xd_clock_div_round(vco_khz, cdclk_khz);
	switch (ratio) {
	case 3U:
		return I915_XD_CDCLK_CD2X_DIV_1_5 << 22;
	case 4U:
		return I915_XD_CDCLK_CD2X_DIV_2 << 22;
	case 8U:
		return I915_XD_CDCLK_CD2X_DIV_4 << 22;
	default:
		break;
	}

	/* Succeeded: any other ratio divides by 1. */
	return I915_XD_CDCLK_CD2X_DIV_1 << 22;
}

/*
 * Reports the decimal frequency field of a CD clock: (the CD clock less
 * 1 MHz) in 0.5 MHz, rounded to the closest.
 *
 * Procedure referenced: Linux v6.8.12 drivers/gpu/drm/i915/display/intel_cdclk.c
 * (skl_cdclk_decimal).
 */
static uint32_t
i915_xd_cdclk_decimal(
	uint32_t cdclk_khz)
{
	/* A CD clock below 1 MHz has no decimal. */
	if (cdclk_khz < 1000U)
		return 0U;

	/* Succeeded: the field. */
	return i915_xd_clock_div_round(cdclk_khz - 1000U, 500U) & I915_XD_CDCLK_DECIMAL;
}

/*
 * Asks the firmware whether a CD clock change may go ahead: the request
 * is repeated until the firmware answers that it is ready, for 3 ms with
 * 10 us between the requests and then 50 ms more.  Returns 0, the last
 * request's mailbox error, or ETIMEDOUT when the firmware never answered
 * ready.
 *
 * Procedure referenced: Linux v6.8.12 drivers/gpu/drm/i915/intel_pcode.c
 * (skl_pcode_request, skl_pcode_try_request).
 */
static int
i915_xd_cdclk_prepare(
	struct i915_xd_io *io)
{
	uint32_t data;
	unsigned waited_us;
	int delayed;
	int error;

	/* Asks until the answer is ready or the time is spent. */
	waited_us = 0U;
	error = 0;
	for (;;) {
		/* One request; a busy mailbox, a refusal or an answer that is not ready asks again. */
		data = I915_XD_MAILBOX_CDCLK_PREPARE;
		error = drv_i915_xd_io_mailbox(io, I915_XD_MAILBOX_CDCLK_CONTROL, &data, NULL, I915_XD_CDCLK_MAILBOX_US);
		if (error == 0 && (data & I915_XD_MAILBOX_CDCLK_READY) == I915_XD_MAILBOX_CDCLK_READY)
			return 0;

		/* Stops once the whole time is spent. */
		if (waited_us >= I915_XD_CDCLK_PREPARE_US + I915_XD_CDCLK_PREPARE_MORE_US)
			break;

		/* Waits before the next request; a time base that fails stops at once. */
		delayed = drv_i915_xd_io_delay(io, I915_XD_CDCLK_PREPARE_GAP_US);
		if (delayed != 0)
			return EIO;
		waited_us += I915_XD_CDCLK_PREPARE_GAP_US;
	}

	/* Reports the mailbox's error, or that the firmware never answered ready. */
	if (error != 0)
		return error;

	/* The firmware answered, but never that it was ready. */
	return ETIMEDOUT;
}

/*
 * Turns the CD clock PLL off and waits for its lock to go.
 *
 * Procedure referenced: Linux v6.8.12 drivers/gpu/drm/i915/display/intel_cdclk.c
 * (icl_cdclk_pll_disable).
 */
static void
i915_xd_cdclk_pll_off(
	struct i915_xd_io *io,
	struct i915_xd_cdclk *state,
	struct i915_xd_cdclk_report *report)
{
	int error;

	/* Clears the enable and waits for the lock to drop. */
	(void)drv_i915_xd_io_rmw(io, I915_XD_CDCLK_PLL_ENABLE, I915_XD_CDCLK_PLL_ON, 0U);
	error = drv_i915_xd_io_poll(io, I915_XD_CDCLK_PLL_ENABLE, I915_XD_CDCLK_PLL_LOCK, 0U, I915_XD_CDCLK_PLL_US, NULL);
	if (error != 0)
		report->unlock_late = 1;

	/* The PLL is off. */
	state->vco_khz = 0U;
}

/*
 * Turns the CD clock PLL on at a rate: the ratio first, then the ratio
 * with the enable, and waits for the lock.
 *
 * Procedure referenced: Linux v6.8.12 drivers/gpu/drm/i915/display/intel_cdclk.c
 * (icl_cdclk_pll_enable).
 */
static void
i915_xd_cdclk_pll_on(
	struct i915_xd_io *io,
	struct i915_xd_cdclk *state,
	uint32_t vco_khz,
	struct i915_xd_cdclk_report *report)
{
	uint32_t ratio;
	int error;

	/* The ratio, then the ratio with the enable. */
	ratio = i915_xd_clock_div_round(vco_khz, state->ref_khz) & I915_XD_CDCLK_PLL_RATIO;
	drv_i915_xd_io_write(io, I915_XD_CDCLK_PLL_ENABLE, ratio);
	drv_i915_xd_io_write(io, I915_XD_CDCLK_PLL_ENABLE, ratio | I915_XD_CDCLK_PLL_ON);

	/* Waits for the lock. */
	error = drv_i915_xd_io_poll(io, I915_XD_CDCLK_PLL_ENABLE, I915_XD_CDCLK_PLL_LOCK, I915_XD_CDCLK_PLL_LOCK, I915_XD_CDCLK_PLL_US, NULL);
	if (error != 0)
		report->lock_late = 1;

	/* The PLL runs at the rate. */
	state->vco_khz = vco_khz;
}

/*
 * Moves the running CD clock PLL to another rate without turning it off:
 * the new ratio with the enable, then the change request, a wait for the
 * lock and the acknowledge, and the request withdrawn.
 *
 * Procedure referenced: Linux v6.8.12 drivers/gpu/drm/i915/display/intel_cdclk.c
 * (adlp_cdclk_pll_crawl).
 */
static void
i915_xd_cdclk_pll_crawl(
	struct i915_xd_io *io,
	struct i915_xd_cdclk *state,
	uint32_t vco_khz,
	struct i915_xd_cdclk_report *report)
{
	uint32_t value;
	int error;

	/* The new ratio with the PLL still on, then the change request. */
	value = (i915_xd_clock_div_round(vco_khz, state->ref_khz) & I915_XD_CDCLK_PLL_RATIO) | I915_XD_CDCLK_PLL_ON;
	drv_i915_xd_io_write(io, I915_XD_CDCLK_PLL_ENABLE, value);
	value |= I915_XD_CDCLK_PLL_FREQ_REQUEST;
	drv_i915_xd_io_write(io, I915_XD_CDCLK_PLL_ENABLE, value);

	/* Waits for the lock and the acknowledge. */
	error = drv_i915_xd_io_poll(io, I915_XD_CDCLK_PLL_ENABLE, I915_XD_CDCLK_PLL_LOCK | I915_XD_CDCLK_PLL_FREQ_ACK,
		I915_XD_CDCLK_PLL_LOCK | I915_XD_CDCLK_PLL_FREQ_ACK, I915_XD_CDCLK_PLL_US, NULL);
	if (error != 0)
		report->crawl_late = 1;

	/* Withdraws the request; the PLL runs at the rate. */
	value &= ~I915_XD_CDCLK_PLL_FREQ_REQUEST;
	drv_i915_xd_io_write(io, I915_XD_CDCLK_PLL_ENABLE, value);
	state->vco_khz = vco_khz;
}

/*
 * Changes the PLL to the target's rate (a crawl between two running
 * rates where the platform can, otherwise off and on again) and writes
 * the control word: the divider, no pipe to wait for, and the decimal
 * frequency.
 *
 * Procedure referenced: Linux v6.8.12 drivers/gpu/drm/i915/display/intel_cdclk.c
 * (_bxt_set_cdclk, icl_cdclk_pll_update).
 */
static void
i915_xd_cdclk_program(
	struct i915_xd_io *io,
	const struct i915_xd_platform *platform,
	struct i915_xd_cdclk *state,
	const struct i915_xd_cdclk *target,
	struct i915_xd_cdclk_report *report)
{
	uint32_t control;
	int crawl;

	/* A crawl needs the platform's support and two running, known rates. */
	report->programmed = 1;
	crawl = 0;
	if (platform->cdclk_crawl &&
	    state->vco_khz != 0U &&
	    state->vco_khz != I915_XD_CDCLK_VCO_UNKNOWN &&
	    target->vco_khz != 0U)
		crawl = 1;

	/* Moves the PLL to the target's rate. */
	if (crawl) {
		if (state->vco_khz != target->vco_khz)
			i915_xd_cdclk_pll_crawl(io, state, target->vco_khz, report);
	} else {
		/* A PLL at another rate goes off first; then it comes on at the target's, unless that is off. */
		if (state->vco_khz != 0U && state->vco_khz != target->vco_khz)
			i915_xd_cdclk_pll_off(io, state, report);
		if (state->vco_khz != target->vco_khz)
			i915_xd_cdclk_pll_on(io, state, target->vco_khz, report);
	}

	/* The control word: the divider, no pipe to wait for, the decimal frequency. */
	control = i915_xd_cdclk_divider(target->cdclk_khz, target->vco_khz);
	control |= I915_XD_CDCLK_CD2X_PIPE_NONE;
	control |= i915_xd_cdclk_decimal(target->cdclk_khz);
	drv_i915_xd_io_write(io, I915_XD_CDCLK_CTL, control);
}

/* Starts a CD clock change's report with nothing in it. */
static void
i915_xd_cdclk_report_clear(
	struct i915_xd_cdclk_report *report)
{
	/* Nothing found, asked or seen yet. */
	report->before.ref_khz = 0U;
	report->before.vco_khz = 0U;
	report->before.cdclk_khz = 0U;
	report->before.bypass_khz = 0U;
	report->before.voltage = 0U;
	report->kept = 0;
	report->replaced = 0;
	report->requested = report->before;
	report->prepare_error = 0;
	report->voltage_error = 0;
	report->programmed = 0;
	report->unlock_late = 0;
	report->lock_late = 0;
	report->crawl_late = 0;
}
