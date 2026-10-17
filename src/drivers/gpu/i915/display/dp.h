/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The DP link of the one-screen modeset path (dp.c).
 *
 * The sink power state, the source OUI, the eDP backlight enable, the DP
 * infoframe enables, and the link training and link status of the xd
 * DisplayPort code (xd-dp.c) on the environment's port.
 *
 * Every function here takes the modeset environment's Linux types, so the
 * declarations are visible only to a translation unit that has included
 * modeset-internal.h first.
 */

#ifndef DRIVERS_GPU_I915_DISPLAY_DP_H
#define DRIVERS_GPU_I915_DISPLAY_DP_H

#include "internal.h"

#ifdef I915_DISPLAY_WORLD_MODESET


bool drv_i915_dp_needs_vsc_sdp(const struct intel_crtc_state *crtc_state, const struct drm_connector_state *conn_state);


#endif /* I915_DISPLAY_WORLD_MODESET */

#endif /* DRIVERS_GPU_I915_DISPLAY_DP_H */
