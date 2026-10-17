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
struct i915_dp_world *drv_i915_edp_world_of(struct i915_edp_device *dev);
void drv_i915_dp_ext_stop_of(struct i915_edp_device *dev);

int drv_i915_edp_begin(struct i915_dp_world *world, struct i915_dp_env *env, const struct i915_edp_config *cfg, struct i915_edp_result *res);
int drv_i915_edp_init_late(struct i915_dp_world *world, const struct i915_edp_config *final_cfg, struct i915_edp_result *res);
void drv_i915_edp_work_run(struct i915_dp_world *world, int which);
void drv_i915_edp_snapshot(struct i915_dp_world *world, struct i915_edp_result *res);
int drv_i915_edp_end(struct i915_dp_world *world, struct i915_edp_result *res);
long drv_i915_edp_dpcd_read(struct i915_dp_world *world, unsigned offset, uint8_t *buf, size_t size);
long drv_i915_edp_dpcd_write(struct i915_dp_world *world, unsigned offset, const uint8_t *buf, size_t size);
int drv_i915_edp_read_dpcd_caps(struct i915_dp_world *world, uint8_t dpcd[15]);
int drv_i915_edp_panel_op(struct i915_dp_world *world, int op);

#endif
