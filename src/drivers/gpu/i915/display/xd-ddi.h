/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The DDIs of the display 12 and 13 code (xd-ddi.c): the combo PHYs.
 *
 * A combo PHY drives the lanes of one DDI.  It is brought up once, at the
 * display's start (and again after DC states that lose it): its IO
 * compensation is powered, its duty cycle corrector set to run once, its
 * process and voltage reference values written, the compensation started
 * (on the PHYs that own the compensation resistor the reference current
 * first) and its common lane allowed to power down.  A PHY whose state
 * already reads as brought up, as the firmware leaves PHY A, is not
 * written.  Before a suspend every PHY's compensation is stopped.
 *
 * A port powers up only the lanes its link uses.  Its signal levels come
 * from the platform's swing table of its kind (DisplayPort by rate, eDP by
 * rate and by the VBT's low swing or HOBL setting, HDMI): each lane takes
 * the row of the level the sink asked for, with the load generator of the
 * lane, and the change is latched by the training enable.
 */

#ifndef DRIVERS_GPU_I915_DISPLAY_XD_DDI_H
#define DRIVERS_GPU_I915_DISPLAY_XD_DDI_H

#include <stdint.h>

#include "xd-io.h"
#include "xd-platform.h"

/* The kinds of output a swing table serves. */
#define I915_XD_OUTPUT_DP		0U
#define I915_XD_OUTPUT_EDP		1U
#define I915_XD_OUTPUT_HDMI		2U

/* The VBT's eDP settings that choose a swing table: the low swing, and HOBL. */
#define I915_XD_SWING_LOW		0x1U
#define I915_XD_SWING_HOBL		0x2U

/*
 * The signal levels of one combo port: its PHY, its kind, the link rate
 * and lane count, the swing table, and the table row of each lane.
 */
struct i915_xd_combo_levels {
	unsigned phy;
	unsigned kind;
	uint32_t rate_khz;
	unsigned lanes;
	const struct i915_xd_combo_swing_table *table;
	uint8_t level[4];
};

int drv_i915_xd_combo_phy_ready(struct i915_xd_io *io, const struct i915_xd_platform *platform, unsigned phy);
unsigned drv_i915_xd_combo_phy_init(struct i915_xd_io *io, const struct i915_xd_platform *platform);
unsigned drv_i915_xd_combo_phy_uninit(struct i915_xd_io *io, const struct i915_xd_platform *platform);
void drv_i915_xd_combo_lanes(struct i915_xd_io *io, unsigned phy, unsigned lanes, int reversed);
const struct i915_xd_combo_swing_table *drv_i915_xd_combo_swing_table(const struct i915_xd_platform *platform, unsigned kind, uint32_t rate_khz, unsigned vbt);
int drv_i915_xd_combo_swing_is_hobl(const struct i915_xd_platform *platform, const struct i915_xd_combo_swing_table *table);
void drv_i915_xd_combo_signal_levels(struct i915_xd_io *io, const struct i915_xd_platform *platform, const struct i915_xd_combo_levels *levels);

#endif /* DRIVERS_GPU_I915_DISPLAY_XD_DDI_H */
