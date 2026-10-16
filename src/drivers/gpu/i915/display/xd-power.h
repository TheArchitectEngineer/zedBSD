/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The power wells of the display 12 and 13 code (xd-power.c).
 *
 * The display hardware is split into power wells, each switched through
 * a request bit and reporting through a state bit of one of three
 * controls (the main wells, the AUX wells, the DDI IO wells).  The
 * platform's description lists the wells with what each one needs: the
 * power gate whose fuses it waits for, whether it holds the VGA plane,
 * the pipes whose interrupt registers it holds, and whether it is the
 * Thunderbolt AUX well of a Type-C port.
 *
 * Around a well whose loss clears pipe interrupt registers the owner's
 * hooks restore the interrupts after it comes up and drain them before
 * it goes down; a drain that fails keeps the well on.  A well that holds
 * the VGA plane has the VGA registers rewritten after it comes up.  The
 * AUX well of a Type-C port in DisplayPort or legacy mode waits for its
 * PHY's microcontroller through the owner's hook.  The caller serializes
 * the calls (the power domains' lock).
 */

#ifndef DRIVERS_GPU_I915_DISPLAY_XD_POWER_H
#define DRIVERS_GPU_I915_DISPLAY_XD_POWER_H

#include <stdint.h>

#include "xd-io.h"
#include "xd-platform.h"

/*
 * The owner's answers around a well's change.
 *
 * pipes_up restores the interrupts of the pipes in pipe_mask after their
 * well came up; pipes_down disables and drains them before it goes down
 * and reports nonzero when the drain failed (the well then stays on).
 * vga_reset rewrites the VGA registers after the well that holds the VGA
 * plane came up.  tc_health waits for the microcontroller of a Type-C
 * port's PHY (0 for the first port) after its AUX well came up, and
 * reports 0, ETIMEDOUT (only noted) or EIO (the time base failed).  Any
 * hook may be NULL.
 */
struct i915_xd_power_hooks {
	void (*pipes_up)(void *context, unsigned pipe_mask);
	int (*pipes_down)(void *context, unsigned pipe_mask);
	void (*vga_reset)(void *context);
	int (*tc_health)(void *context, unsigned tc_port);
	void *context;
};

/*
 * What a well's change met on the way that did not stop it: the state
 * bit that did not follow the request in time (on enable: the well may
 * still come up; a Thunderbolt AUX well with its tunnel down never
 * reports), and the power gates (a bit each) whose fuses were not
 * distributed in time.
 */
struct i915_xd_well_report {
	int state_late;
	uint32_t fuses_late;
};

int drv_i915_xd_power_find(const struct i915_xd_platform *platform, unsigned control, unsigned index);
int drv_i915_xd_power_well_enable(struct i915_xd_io *io, const struct i915_xd_platform *platform, unsigned well, const struct i915_xd_power_hooks *hooks, struct i915_xd_well_report *report);
int drv_i915_xd_power_well_disable(struct i915_xd_io *io, const struct i915_xd_platform *platform, unsigned well, const struct i915_xd_power_hooks *hooks, struct i915_xd_well_report *report);
int drv_i915_xd_power_well_owned(struct i915_xd_io *io, const struct i915_xd_platform *platform, unsigned well);
void drv_i915_xd_power_well_adopt(struct i915_xd_io *io, const struct i915_xd_platform *platform, unsigned well);

#endif /* DRIVERS_GPU_I915_DISPLAY_XD_POWER_H */
