/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The eDP panel power sequencer (panel.c): the panel hook of the modeset
 * (struct i915_lcd_emit), whose context is the panel run (struct
 * i915_lcd_kernel).
 *
 * The power sequencer functions (drv_i915_pps_*) take the DP environment's
 * struct intel_dp and are declared in dp-internal.h.
 */

#ifndef DRIVERS_GPU_I915_DISPLAY_PANEL_H
#define DRIVERS_GPU_I915_DISPLAY_PANEL_H

#include "internal.h"

int drv_i915_edp_emit_panel(void *ctx, int op);

#endif
