/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The xd display of a device: its state and its driver stages.
 *
 * One struct i915_xd lives in each display.  It holds the register window
 * every xd part writes through, the platform description chosen for the
 * device, and what each part found; the parts the driver uses so far fill
 * it as the stages run.  This header uses no type of the older display
 * code, so the code below it can be compiled without that code's headers.
 */

#ifndef DRIVERS_GPU_I915_DISPLAY_XD_H
#define DRIVERS_GPU_I915_DISPLAY_XD_H

#include "xd-aux.h"
#include "xd-dmc.h"
#include "xd-io.h"
#include "xd-panel.h"
#include "xd-platform.h"
#include "xd-power.h"
#include "xd-takeover.h"
#include "xd-tc.h"
#include "xd-vbt.h"

#include <stddef.h>
#include <stdint.h>

struct i915_mmio;
struct mutex;

/*
 * The xd display state of one device.
 *
 * chosen is nonzero once a description of the device was found; it stays
 * zero on a device the xd code does not describe (Gen9, an unknown id),
 * whose display does not use the xd parts.  The OpRegion read is filled by
 * the OpRegion stage, the VBT parse (vbt_parsed nonzero) by the VBT stage.
 * The power well counts are prepared with the power domains and kept
 * under their lock; the DMC firmware is parsed and loaded by the DMC
 * loader's work.  The firmware's display is read once, before the first
 * display write, by the firmware check.  The Dekel PHY windows of the
 * Type-C ports are ready from the start.
 */
struct i915_xd {
	struct i915_xd_io io;
	struct i915_xd_choice choice;
	int chosen;
	struct i915_xd_opregion_read opregion;
	struct i915_xd_vbt vbt;
	int vbt_parsed;
	struct i915_xd_power power;
	struct i915_xd_dmc dmc;
	struct i915_xd_firmware firmware;
	struct i915_xd_dkl dkl;
};

void drv_i915_xd_start(struct i915_xd *xd, struct i915_mmio *mmio, struct mutex *mailbox_lock, uint16_t product, uint8_t revid);
void drv_i915_xd_south(struct i915_xd *xd, uint16_t product, uint8_t revid, uint16_t pch_id);
int drv_i915_xd_check_firmware(struct i915_xd *xd, uint64_t aperture_base, uint64_t aperture_size, uint32_t ggtt_pages, uint32_t driver_first_page, uint32_t asls);
int drv_i915_xd_read_opregion(struct i915_xd *xd, uint32_t asls, uint8_t *opregion_copy, uint8_t *vbt_copy, size_t vbt_room);

/*
 * What the takeover of the firmware's display did: the pipes it found
 * lit, the pipes it stopped, the pipes still lit afterwards, and the
 * error of the first pipe that could not be stopped (0 for none).
 */
struct i915_xd_takeover_outcome {
	unsigned lit;
	unsigned stopped;
	unsigned still_lit;
	int error;
};

int drv_i915_xd_takeover(struct i915_xd *xd, struct i915_xd_takeover_outcome *outcome);

#endif /* DRIVERS_GPU_I915_DISPLAY_XD_H */
