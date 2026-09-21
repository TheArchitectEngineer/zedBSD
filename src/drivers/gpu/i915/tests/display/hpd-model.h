/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The register model of the hotplug path (hpd-model.c).
 *
 * A model test starts the hotplug path with this model in place of the
 * MMIO BAR.  The model answers the south display's hotplug status and
 * control registers and a GMBUS controller with a DDC sink behind it,
 * through the checkpoints the production hotplug code calls for a model
 * instance.  The test delivers synthetic interrupts into the Linux handler
 * and runs the armed storm re-enable work on demand.  The header is
 * neutral: the model tests include it without the hotplug environment.
 */

#ifndef DRIVERS_GPU_I915_TESTS_DISPLAY_HPD_MODEL_H
#define DRIVERS_GPU_I915_TESTS_DISPLAY_HPD_MODEL_H

#include <stdint.h>

struct i915_display;

/*
 * The registers of one model instance.
 *
 * The test owns it for the length of the instance and sets the status
 * fields and the DDC sink; the hotplug path reads and writes it through
 * the checkpoints.
 */
struct i915_hpd_fake_regs {
	/* SDEISR, SHOTPLUG_CTL_DDI and SHOTPLUG_CTL_TC. */
	uint32_t sdeisr, shotplug_ddi, shotplug_tc;

	/* How many read-modify-writes the path made, and the last value written back. */
	unsigned rmw_writes;
	uint32_t last_rmw_write;

	/* The DDC bus behind GMBUS: a sink answering at 0x50 with ddc_edid, or nobody (NAK). */
	int ddc_present;
	const uint8_t *ddc_edid;
	unsigned ddc_edid_len;

	/* GMBUS0..5 as written, and the state of the transfer in progress. */
	uint32_t gmbus[6];
	unsigned gm_ptr, gm_left, gm_reads, gm_naks;
	int gm_nak, gm_active;
};

/* Delivers a synthetic SDE interrupt to a running model instance, with interrupts disabled. */
void drv_i915_test_hpd_model_irq(struct i915_display *display, uint32_t sde_iir);

/* Reports whether a model instance may run: 0 once a hardware instance ran in this boot. */
int drv_i915_test_hpd_model_allowed(struct i915_display *display);

/* Runs the armed storm re-enable work now: 1 when it ran, 0 when idle, ENOENT when never armed. */
int drv_i915_test_hpd_flush_reenable(struct i915_display *display);

#endif
