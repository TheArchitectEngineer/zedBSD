/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The HDMI detection of the hotplug path (hdmi.c).
 *
 * The forced detect of an HDMI connector and the EDID it reads over its
 * GMBUS adapter.  The connector callbacks take the hotplug environment's
 * types and are declared in hotplug-internal.h; the EDID records, which the
 * hotplug path and the tests read, are declared here.
 */

#ifndef DRIVERS_GPU_I915_DISPLAY_HDMI_H
#define DRIVERS_GPU_I915_DISPLAY_HDMI_H

#include "internal.h"

void drv_i915_hpd_edid_info(struct i915_hpd_world *world, struct i915_hpd_edid_info *out);
const uint8_t *drv_i915_hpd_edid_bytes(struct i915_hpd_world *world, unsigned idx, unsigned *size);
void drv_i915_hpd_edid_forget(struct i915_hpd_world *world);

#endif /* DRIVERS_GPU_I915_DISPLAY_HDMI_H */
