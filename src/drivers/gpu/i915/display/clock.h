/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The display's CD clock (clock.c): its state over the xd clock, read,
 * started, changed and stopped.
 */

#ifndef DRIVERS_GPU_I915_DISPLAY_CLOCK_H
#define DRIVERS_GPU_I915_DISPLAY_CLOCK_H

#include "internal.h"

struct i915_xd;

void drv_i915_init_cdclk_hooks(struct i915_cdclk_dev *cd, struct i915_xd *xd);
void drv_i915_bxt_get_cdclk(struct i915_cdclk_dev *cd, struct i915_cdclk_config *cfg);
void drv_i915_update_cdclk(struct i915_cdclk_dev *cd);
void drv_i915_cdclk_init_hw(struct i915_cdclk_dev *cd);
void drv_i915_cdclk_uninit_hw(struct i915_cdclk_dev *cd);
uint32_t drv_i915_max_cdclk_freq(const struct i915_cdclk_dev *cd);

#endif
