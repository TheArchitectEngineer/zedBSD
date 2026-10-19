/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The JH7110's clock and reset generators (SYS, STG and AON).
 *
 * A device's node names its clocks and resets by the phandle of a
 * generator and the generator's number, as the clocks and resets
 * properties do.  The calls here look those up and switch the gates, set
 * the dividers and move the resets of the clocks and resets the board's
 * drivers use.  A clock the table does not know is refused, so a driver
 * cannot stop a clock it does not own, in particular the display's, the
 * CPUs', the memory's or the console's.
 *
 * Rates are computed from the PLL settings in the SYS system control
 * block and the dividers and multiplexers on the way; the PLLs are only
 * read, never changed.
 */

#ifndef KERN_DRIVERS_VF2_CRG_H
#define KERN_DRIVERS_VF2_CRG_H

#include <stdint.h>

#include <drivers/generic/fdt.h>

/*
 * One of the three generators.
 *
 * The numbers index the generators' records; COUNT is not a generator.
 */
enum drv_vf2_crg {
	DRV_VF2_CRG_SYS = 0,
	DRV_VF2_CRG_STG = 1,
	DRV_VF2_CRG_AON = 2,
	DRV_VF2_CRG_COUNT = 3
};

/*
 * One clock: its generator and the generator's number for it.
 *
 * The number is the one the device tree's clocks property gives.
 */
struct drv_vf2_clock {
	enum drv_vf2_crg crg;
	uint32_t index;
};

/*
 * One reset line: its generator and the generator's number for it.
 *
 * The number is the one the device tree's resets property gives.
 */
struct drv_vf2_reset {
	enum drv_vf2_crg crg;
	uint32_t index;
};

/*
 * Finds the three generators in the device tree and maps them.
 *
 * Touches no register.  drv_vf2_syscon_init() must have run, for the PLL
 * settings.  Reports ENODEV when the SYS generator is missing.
 */
int drv_vf2_crg_init(const struct drv_fdt *fdt);

/*
 * Finds a clock of a device by its clock-names entry.
 *
 * A NULL name takes the first entry of the clocks property.  ENOENT means
 * the device lacks the clock; ENODEV that its phandle names no generator.
 */
int drv_vf2_clock_lookup(const struct drv_fdt *fdt, uint32_t node, const char *name,
			 struct drv_vf2_clock *clock);

/*
 * Starts a clock and every gate above it on its current path.
 *
 * A gate already open is left alone, so this may be repeated.
 */
int drv_vf2_clock_enable(const struct drv_vf2_clock *clock);

/*
 * Stops a clock's own gate.
 *
 * The gates above it stay open.  A bus clock other devices share is
 * refused with EBUSY.
 */
int drv_vf2_clock_disable(const struct drv_vf2_clock *clock);

/*
 * Computes a clock's rate in hertz from the registers.
 *
 * ENOTSUP means the table does not know the clock's source.
 */
int drv_vf2_clock_get_rate(const struct drv_vf2_clock *clock, uint64_t *rate);

/*
 * Computes the rate in hertz of one of the three PLLs (0 to 2).
 *
 * A powered-down PLL reports zero.
 */
int drv_vf2_pll_get_rate(unsigned pll, uint64_t *rate);

/*
 * Sets a clock's divider so that its rate is as close as possible to the
 * requested one without exceeding it.
 *
 * Only clocks whose divider a driver may own accept this; others report
 * ENOTSUP.  ERANGE means even the largest divider exceeds the rate.
 */
int drv_vf2_clock_set_rate(const struct drv_vf2_clock *clock, uint64_t rate);

/*
 * Applies a device's assigned-clocks and assigned-clock-rates.
 *
 * A rate of zero leaves its clock alone.  ENOENT means the device has no
 * assigned clocks.
 */
int drv_vf2_clock_apply_assigned(const struct drv_fdt *fdt, uint32_t node);

/*
 * Finds a reset line of a device by its reset-names entry.
 *
 * A NULL name takes the first entry of the resets property.
 */
int drv_vf2_reset_lookup(const struct drv_fdt *fdt, uint32_t node, const char *name,
			 struct drv_vf2_reset *reset);

/*
 * Holds a device in reset.
 */
int drv_vf2_reset_assert(const struct drv_vf2_reset *reset);

/*
 * Releases a device from reset and waits until the generator reports it
 * released.
 *
 * ETIMEDOUT means the status did not follow, usually because the
 * device's clock is stopped.
 */
int drv_vf2_reset_deassert(const struct drv_vf2_reset *reset);

/*
 * Reports whether the generator's status shows a line released.
 */
int drv_vf2_reset_released(const struct drv_vf2_reset *reset, int *released);

#endif
