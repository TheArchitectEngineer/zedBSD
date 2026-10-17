/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The DP environment's world (dp-sink.c): the eDP bring-up up to the
 * sink's DPCD and EDID on the older code, which the older host tests run,
 * and the DPCD and I2C-over-AUX helpers the external DisplayPort ports use.
 * The display's own eDP device and the kernel backend are edp-device.c's.
 *
 * Every declaration here takes world-neutral types only.  The functions the
 * Linux text of the DP environment calls with its own types are declared in
 * dp-internal.h.
 *
 * The bring-up functions return the result contract of struct
 * i915_edp_result: 0, or a negative Linux errno (-I915_EDP_E*), because those
 * values are logged and compared with the Linux results.
 */

#ifndef DRIVERS_GPU_I915_DISPLAY_DP_SINK_H
#define DRIVERS_GPU_I915_DISPLAY_DP_SINK_H

#include "internal.h"
#include "edp-device.h"

int drv_i915_dp_world_create(struct i915_display *display);
void drv_i915_dp_world_destroy(struct i915_display *display);
void drv_i915_dp_ext_stop_of(struct i915_edp_device *dev);


#endif
