/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * GT power management: RC6 and RPS (see gt-power.h).
 *
 * Every register access here goes through the held accessors: the device
 * start holds every GT forcewake domain across the GT initialization and
 * resume that call these functions.
 */

#include "i915.h"
#include "gt-power.h"
#include "mmio.h"
#include "power.h"
#include "device-info.h"

#include <kern/klog.h>

#include <stddef.h>
#include <stdint.h>

#include "data/i915-gt-power.inc"

/*
 * Prepares the RC6 state and makes sure RC6 is off.
 *
 * Follows Linux intel_rc6_init(): RC6 must stay disabled until the GT is
 * ready for it.
 */
void
drv_i915_rc6_init(
	struct i915_rc6 *rc6,
	struct i915_mmio *mmio)
{
	/*
	 * rc6_supported(): the platform has RC6 and is neither a virtual GPU
	 * nor a mock.  The GEN9_LP, MTL and media-A-step exclusions do not
	 * apply to Alder Lake-P.
	 */
	rc6->supported = 1;
	rc6->enabled = 0;
	rc6->ctl_enable = 0U;
	rc6->pg_enable = 0U;
	rc6->wa_disabled = 0;

	/* Sanitizes RC6: it is disabled before the driver is ready for it. */
	drv_i915_write32(mmio, GEN6_RC_CONTROL, 0U);
}

/*
 * Programs the RC6 thresholds and turns on RC6 and power gating.
 *
 * Follows Linux gen11_rc6_enable() with the GuC's RC6 disabled, so the
 * driver owns RC_CONTROL.
 */
void
drv_i915_gen11_rc6_enable(
	struct i915_rc6 *rc6,
	struct i915_mmio *mmio,
	const struct i915_gt_info *gt)
{
	uint32_t pg_enable;
	unsigned vcs;
	unsigned engine_index;

	/* A GT without RC6 is left alone. */
	if (rc6->supported == 0)
		return;

	/* Programs the RC6 wake, evaluation and idle thresholds. */
	drv_i915_write32(mmio, GEN6_RC6_WAKE_RATE_LIMIT, (54U << 16) | 85U);
	drv_i915_write32(mmio, GEN10_MEDIA_WAKE_RATE_LIMIT, 150U);
	drv_i915_write32(mmio, GEN6_RC_EVALUATION_INTERVAL, 125000U);
	drv_i915_write32(mmio, GEN6_RC_IDLE_HYSTERSIS, 25U);

	/* Gives every engine the same idle time before it may power down. */
	for (engine_index = 0U; engine_index < gt->num_engines; engine_index++)
		drv_i915_write32(mmio, RING_MAX_IDLE(gt->engines[engine_index].mmio_base), 10U);

	/* Programs the GuC idle count, the RC sleep and the RC6 threshold. */
	drv_i915_write32(mmio, GUC_MAX_IDLE_COUNT, 0xaU);
	drv_i915_write32(mmio, GEN6_RC_SLEEP, 0U);
	drv_i915_write32(mmio, GEN6_RC6_THRESHOLD, 50000U);

	/* Programs the coarse power gating hysteresis of render and media. */
	drv_i915_write32(mmio, GEN9_MEDIA_PG_IDLE_HYSTERESIS, 60U);
	drv_i915_write32(mmio, GEN9_RENDER_PG_IDLE_HYSTERESIS, 60U);

	/* Without the GuC's RC6 the driver itself enables RC6. */
	rc6->ctl_enable = GEN6_RC_CTL_RC6_ENABLE;

	/* Render, media and media sampler power gating are always enabled. */
	pg_enable = GEN9_RENDER_PG_ENABLE | GEN9_MEDIA_PG_ENABLE | GEN11_MEDIA_SAMPLER_PG_ENABLE;

	/*
	 * Graphics version 12 other than DG1 also gates the HCP and MFX units
	 * of every video decode engine that exists.
	 */
	for (vcs = 0U; vcs < I915_MAX_VCS; vcs++) {
		/* Looks for the video decode engine of this instance. */
		for (engine_index = 0U; engine_index < gt->num_engines; engine_index++) {
			/* Only a video decode engine has HCP and MFX units. */
			if (gt->engines[engine_index].class != I915_VIDEO_DECODE_CLASS)
				continue;

			/* Another instance is gated by its own bits. */
			if ((unsigned)gt->engines[engine_index].instance != vcs)
				continue;

			/* Gates this instance's HCP and MFX units. */
			pg_enable |= VDN_HCP_POWERGATE_ENABLE(vcs) | VDN_MFX_POWERGATE_ENABLE(vcs);
			break;
		}
	}

	/* Records the gating mask the enable writes, for the start report. */
	rc6->pg_enable = pg_enable;

	/* Turns on power gating, then RC6. */
	drv_i915_write32(mmio, GEN9_PG_ENABLE, pg_enable);
	drv_i915_write32(mmio, GEN6_RC_CONTROL, rc6->ctl_enable);

	/* The GT now enters RC6 on its own whenever it idles. */
	rc6->enabled = 1;
}

/*
 * Turns RC6 and power gating off before the GT is reinitialized.
 *
 * Follows Linux intel_rc6_sanitize() -> __intel_rc6_disable() with the GuC
 * off, which goes straight to the registers.
 */
void
drv_i915_rc6_sanitize(
	struct i915_rc6 *rc6,
	struct i915_mmio *mmio)
{
	/* An RC6 still enabled here would be an unbalanced suspend and resume. */
	rc6->enabled = 0;

	/* A GT without RC6 has nothing to turn off. */
	if (rc6->supported == 0)
		return;

	/* Turns off power gating and RC6, and clears the RC state. */
	drv_i915_write32(mmio, GEN9_PG_ENABLE, 0U);
	drv_i915_write32(mmio, GEN6_RC_CONTROL, 0U);
	drv_i915_write32(mmio, GEN6_RC_STATE, 0U);
}

/*
 * Reads the GT frequency limits and the efficient frequency.
 *
 * Follows Linux intel_rps_init() -> __gen6_rps_get_freq_caps() for a
 * graphics version 11 or later part that is not GEN9_LP.
 */
void
drv_i915_rps_init(
	struct i915_rps *rps,
	struct mutex *sb_lock,
	struct i915_mmio *mmio)
{
	uint32_t capabilities;
	uint32_t frequency_info;
	uint32_t ddcc;
	uint32_t ddcc_high;
	uint32_t efficient;
	int pcode_error;

	ddcc = 0U;
	ddcc_high = 0U;

	/* Reads RP0 and the minimum from the capabilities, RP1 from the frequency info. */
	capabilities = drv_i915_read32(mmio, GEN6_RP_STATE_CAP);
	rps->rp0_freq = (capabilities >> 0) & 0xffU;
	frequency_info = drv_i915_read32(mmio, GEN10_FREQ_INFO_REC);
	rps->rp1_freq = (frequency_info & RPE_MASK) >> 8;
	rps->min_freq = (capabilities >> 16) & 0xffU;

	/* The capabilities are in 50 MHz units; the driver counts in 16.67 MHz. */
	rps->rp0_freq *= GEN9_FREQ_SCALER;
	rps->rp1_freq *= GEN9_FREQ_SCALER;
	rps->min_freq *= GEN9_FREQ_SCALER;

	/* Requests may use the whole fused range; RP1 is efficient until PCODE says otherwise. */
	rps->max_freq = rps->rp0_freq;
	rps->efficient_freq = rps->rp1_freq;

	/* Asks the PCODE for the efficient frequency. */
	pcode_error = drv_i915_pcode_read(sb_lock, mmio, HSW_PCODE_DYNAMIC_DUTY_CYCLE_CONTROL, &ddcc, &ddcc_high);
	rps->pcode_ok = 0;
	if (pcode_error == 0)
		rps->pcode_ok = 1;

	/* Takes the PCODE's efficient frequency, clamped into the fused range. */
	if (rps->pcode_ok != 0) {
		efficient = ((ddcc >> 8) & 0xffU) * GEN9_FREQ_SCALER;

		/* The efficient frequency is never below the minimum. */
		if (efficient < rps->min_freq)
			efficient = rps->min_freq;

		/* Nor above the maximum. */
		if (efficient > rps->max_freq)
			efficient = rps->max_freq;

		rps->efficient_freq = efficient;
	}

	/* No frequency has been requested yet. */
	rps->enabled = 0;
}

/*
 * Requests the minimum frequency and starts frequency management.
 *
 * Follows Linux intel_rps_enable() -> gen9_rps_enable() and rps_reset().
 */
void
drv_i915_rps_enable(
	struct i915_rps *rps,
	struct i915_mmio *mmio)
{
	/* A GT with no room between its limits has nothing to reclock. */
	if (rps->max_freq <= rps->min_freq)
		return;

	/*
	 * Programs the idle hysteresis.  The graphics version is not 9, so
	 * GEN6_RC_VIDEO_FREQ is not written.
	 */
	drv_i915_write32(mmio, GEN6_RP_IDLE_HYSTERSIS, 0xaU);

	/* Requests the minimum frequency, as rps_reset() -> gen6_rps_set() does. */
	drv_i915_write32(mmio, GEN6_RPNSWREQ, GEN9_FREQUENCY(rps->min_freq));

	/* A frequency is now requested. */
	rps->enabled = 1;
}

/*
 * Masks the RPS interrupts before the GT is reinitialized.
 *
 * Follows Linux intel_rps_sanitize() -> rps_disable_interrupts().
 */
void
drv_i915_rps_sanitize(
	struct i915_rps *rps,
	struct i915_mmio *mmio)
{
	UNUSED_PARAMETER(rps);

	/*
	 * PMINTRMSK takes rps_pm_sanitize_mask(~0); pm_intrmsk_mbz is 0 on
	 * graphics version 11 and later (the REDIRECT_TO_GUC bit is gen8 to
	 * gen10).  The GPM IER and IMR updates that follow leave the values
	 * the interrupt setup programmed (IER 0, IMR ~0) unchanged, so they
	 * write nothing.
	 */
	drv_i915_write32(mmio, GEN6_PMINTRMSK, 0xffffffffU);
}
