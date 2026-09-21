/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The DP AUX channel of the eDP port (aux.c): the DPCD hooks of the modeset
 * (struct i915_lcd_emit), whose context is the panel run (struct
 * i915_lcd_kernel).
 *
 * drv_i915_dp_aux_init() takes the DP environment's struct intel_dp and is
 * declared in dp-internal.h.
 */

#ifndef DRIVERS_GPU_I915_DISPLAY_AUX_H
#define DRIVERS_GPU_I915_DISPLAY_AUX_H

#include "internal.h"

long drv_i915_edp_emit_dpcd_read(void *ctx, unsigned offset, uint8_t *buf, size_t size);
long drv_i915_edp_emit_dpcd_write(void *ctx, unsigned offset, const uint8_t *buf, size_t size);
int drv_i915_edp_emit_read_dpcd_caps(void *ctx, uint8_t dpcd[15]);

#endif
