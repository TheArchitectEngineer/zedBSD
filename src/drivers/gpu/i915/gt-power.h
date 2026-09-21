/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * GT power management: RC6 and RPS.
 *
 * RC6 lets the GT drop into its low-power state when idle, and render and
 * media power gating let the idle units switch off.  RPS picks the GT clock.
 * Both follow the Linux intel_rc6.c and intel_rps.c paths that Alder Lake-P
 * takes with the GuC disabled: the driver writes the thresholds and the
 * enables itself, and requests a fixed frequency.
 *
 * The PCODE mailbox that RPS reads the efficient frequency from belongs to
 * power.c; this file only uses it.
 */

#ifndef DRIVERS_GPU_I915_GT_POWER_H
#define DRIVERS_GPU_I915_GT_POWER_H

#include <stdint.h>

struct i915_mmio;
struct i915_gt_info;
struct mutex;

/*
 * The RC6 state of one GT.
 *
 * It is built by the GT table construction and changed by the sanitize and
 * enable steps of the GT resume; the device start is its only user.
 */
struct i915_rc6 {
	/* Nonzero when the platform supports RC6 at all. */
	int supported;

	/* Nonzero once RC6 and power gating have been turned on. */
	int enabled;

	/* The RC_CONTROL value the enable writes. */
	uint32_t ctl_enable;

	/* The PG_ENABLE value the enable writes. */
	uint32_t pg_enable;

	/*
	 * Nonzero when the enable left RC6 and power gating off.  Nothing in
	 * the driver sets it; it stays in the state so the start report keeps
	 * the field it has always printed.
	 */
	int wa_disabled;
};

/*
 * The frequency limits and state of one GT, in 16.67 MHz units.
 *
 * It is built by the GT table construction and changed by the enable step
 * of the GT resume.
 */
struct i915_rps {
	/* The highest, the sustainable and the lowest frequency the fuses allow. */
	uint32_t rp0_freq;
	uint32_t rp1_freq;
	uint32_t min_freq;

	/* The range the driver requests frequencies from. */
	uint32_t max_freq;

	/* The most power-efficient frequency. */
	uint32_t efficient_freq;

	/* Nonzero once a frequency has been requested. */
	int enabled;

	/* Nonzero when the PCODE reported the efficient frequency. */
	int pcode_ok;
};

void drv_i915_rc6_init(struct i915_rc6 *rc6, struct i915_mmio *mmio);
void drv_i915_gen11_rc6_enable(struct i915_rc6 *rc6, struct i915_mmio *mmio, const struct i915_gt_info *gt);
void drv_i915_rc6_sanitize(struct i915_rc6 *rc6, struct i915_mmio *mmio);
void drv_i915_rps_init(struct i915_rps *rps, struct mutex *sb_lock, struct i915_mmio *mmio);
void drv_i915_rps_enable(struct i915_rps *rps, struct i915_mmio *mmio);
void drv_i915_rps_sanitize(struct i915_rps *rps, struct i915_mmio *mmio);

#endif
