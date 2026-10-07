/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The display's external DP ports (dp-ext-kern.c): the Type-C ports bound
 * to the DP environment's AUX transfer, and the probe of their sinks that
 * the hotplug path's DP detection asks.
 */

#ifndef DRIVERS_GPU_I915_DISPLAY_DP_EXT_KERN_H
#define DRIVERS_GPU_I915_DISPLAY_DP_EXT_KERN_H

#include "internal.h"
#include "dp-ext.h"

void drv_i915_dp_ext_start(struct i915_display *display);
int drv_i915_dp_ext_pulse(struct i915_display *display, int port);
enum i915_dp_ext_status drv_i915_dp_ext_probe(struct i915_display *display, int port, uint8_t *edid, size_t edid_size, unsigned *edid_bytes);

/*
 * Gives the DPCD access of an external DP port's sink (the Type-C port of
 * a DDI port) for a modeset object of that sink: NULL when the external
 * ports are not bound or the port is not a declared Type-C port.  It
 * stays valid for the device's lifetime and refuses once the ports stop.
 */
const struct i915_lcd_aux_emit *drv_i915_dp_ext_aux_emit(struct i915_display *display, int port);

#endif
