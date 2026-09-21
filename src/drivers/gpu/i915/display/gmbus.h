/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The GMBUS I2C buses of the hotplug path (gmbus.c).
 *
 * The buses are made on the first request for their pin and forgotten when
 * the hotplug path starts again.  The adapter lookup and the Linux GMBUS
 * functions take the hotplug environment's types and are declared in
 * hotplug-internal.h; only the neutral entry is declared here.
 */

#ifndef DRIVERS_GPU_I915_DISPLAY_GMBUS_H
#define DRIVERS_GPU_I915_DISPLAY_GMBUS_H

#include "internal.h"

void drv_i915_hpd_gmbus_forget(struct i915_hpd_world *world);

#endif /* DRIVERS_GPU_I915_DISPLAY_GMBUS_H */
