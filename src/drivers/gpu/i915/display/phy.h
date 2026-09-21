/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The combo PHYs and their buffer translations (phy.c).
 *
 * The combo PHY initialization of the display core.  The lane power-up and
 * the buffer-translation hooks take types of the modeset environment and
 * are declared in modeset-internal.h.
 */

#ifndef DRIVERS_GPU_I915_DISPLAY_PHY_H
#define DRIVERS_GPU_I915_DISPLAY_PHY_H

#include "internal.h"

int drv_i915_combo_phy_init(struct i915_mmio *mmio, struct i915_trace *trace, unsigned *initialised_out);
int drv_i915_combo_phy_verify_state(struct i915_mmio *mmio, unsigned phy);
void drv_i915_combo_phy_init_one(struct i915_mmio *mmio, unsigned phy);

#endif
