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
 * enables itself.  RPS follows the load (ws075-p020): the GT's power
 * management interrupts say when the GT was busier or idler than the
 * thresholds, a work raises or lowers the requested frequency, and a client
 * waiting on work the GT has not started boosts it.
 *
 * The PCODE mailbox that RPS reads the efficient frequency from belongs to
 * power.c; this file only uses it.
 */

#ifndef DRIVERS_GPU_I915_GT_POWER_H
#define DRIVERS_GPU_I915_GT_POWER_H

#include <kern/lock.h>
#include <stdint.h>

#include "workqueue.h"

struct i915_mmio;
struct i915_gt_info;
struct i915_irq_dev;
struct mutex;

/*
 * The power modes the RPS thresholds are set for (Linux's LOW_POWER,
 * BETWEEN and HIGH_POWER): the higher the frequency, the sooner the GT
 * counts as busy enough to go up and the later as idle enough to go down.
 * NONE means no thresholds have been written yet.
 */
#define I915_RPS_POWER_NONE		(-1)
#define I915_RPS_POWER_LOW		0
#define I915_RPS_POWER_BETWEEN		1
#define I915_RPS_POWER_HIGH		2

/* The power management interrupt events of RPS (GEN6_PM_RP_*). */
#define I915_RPS_DOWN_TIMEOUT		(1U << 6)
#define I915_RPS_UP_THRESHOLD		(1U << 5)
#define I915_RPS_DOWN_THRESHOLD		(1U << 4)
#define I915_RPS_UP_EI_EXPIRED		(1U << 2)
#define I915_RPS_DOWN_EI_EXPIRED	(1U << 1)
#define I915_RPS_EVENTS			(I915_RPS_UP_EI_EXPIRED | I915_RPS_UP_THRESHOLD | I915_RPS_DOWN_EI_EXPIRED | \
					 I915_RPS_DOWN_THRESHOLD | I915_RPS_DOWN_TIMEOUT)

/* No frequency has been written yet. */
#define I915_RPS_FREQ_NONE		0xffffffffU

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
 * It is built by the GT table construction, changed by the enable step of
 * the GT resume, and driven from the start of the published node to its
 * stop: the interrupt handler hands the events to the work, which alone
 * requests frequencies from then on.  lock guards pm_iir, pm_imr, pm_ier and
 * waiters, and is taken with interrupts disabled.
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

	/*
	 * The range the work moves in (Linux's sysfs soft limits, which stay
	 * the whole range here), the frequency a boost jumps to (RP0), the
	 * frequency the driver last settled on and the one it last wrote
	 * (I915_RPS_FREQ_NONE before the first), and the last step the work
	 * took (its sign says up or down; consecutive steps double).
	 */
	uint32_t min_softlimit;
	uint32_t max_softlimit;
	uint32_t boost_freq;
	uint32_t cur_freq;
	uint32_t last_freq;
	int last_adj;

	/* The power mode of the thresholds written, and the busy percentages that go up and down. */
	int power_mode;
	uint32_t up_threshold;
	uint32_t down_threshold;

	/* The GT's command streamer clock in Hz, the unit of the evaluation intervals. */
	uint32_t clock_frequency;

	/* The interrupt events RPS follows (I915_RPS_UP_THRESHOLD and I915_RPS_DOWN_THRESHOLD). */
	uint32_t pm_events;

	/*
	 * The started part: the register access and the interrupt device the
	 * handler is registered with (NULL before the start), the events the
	 * handler saw that the work has not taken, the PM interrupt mask and
	 * enable as last written (the low half of GPM_WGBOXPERF's upper half),
	 * and how many clients wait on work the GT has not started.
	 */
	struct i915_mmio *mmio;
	struct i915_irq_dev *irq;
	struct spinlock lock;
	uint32_t pm_iir;
	uint32_t pm_imr;
	uint32_t pm_ier;
	unsigned waiters;

	/* The queue and the work that change the frequency; work_ready says they exist. */
	struct i915_workqueue workqueue;
	struct i915_work work;
	int work_ready;

	/* What happened, for the diagnostics: events, boosts, work runs and frequency changes. */
	volatile unsigned up_events;
	volatile unsigned down_events;
	volatile unsigned boosts;
	volatile unsigned work_runs;
	volatile unsigned changes;
};

void drv_i915_rc6_init(struct i915_rc6 *rc6, struct i915_mmio *mmio);
void drv_i915_gen11_rc6_enable(struct i915_rc6 *rc6, struct i915_mmio *mmio, const struct i915_gt_info *gt);
void drv_i915_rc6_sanitize(struct i915_rc6 *rc6, struct i915_mmio *mmio);
void drv_i915_rps_init(struct i915_rps *rps, struct mutex *sb_lock, struct i915_mmio *mmio);
void drv_i915_rps_enable(struct i915_rps *rps, struct i915_mmio *mmio, uint32_t clock_frequency);
void drv_i915_rps_sanitize(struct i915_rps *rps, struct i915_mmio *mmio);
int drv_i915_rps_start(struct i915_rps *rps, struct i915_irq_dev *irq, struct i915_mmio *mmio);
void drv_i915_rps_stop(struct i915_rps *rps);
void drv_i915_rps_irq(void *context, uint32_t pm_iir);
void drv_i915_rps_boost_begin(struct i915_rps *rps);
void drv_i915_rps_boost_end(struct i915_rps *rps);
uint32_t drv_i915_rps_next_freq(const struct i915_rps *rps, uint32_t pm_iir, int client_boost, int *adj);
uint32_t drv_i915_rps_pm_mask(const struct i915_rps *rps, uint32_t freq);
uint32_t drv_i915_rps_limits(const struct i915_rps *rps, uint32_t freq);
uint32_t drv_i915_rps_pm_interval(uint32_t clock_frequency, uint64_t ns);

#endif
