/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Type and constant definitions derived from the Linux kernel (drivers/gpu/drm/i915/display/intel_display_power.h),
 * which carries the following notice.
 *
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2019 Intel Corporation
 */

/*
 * Type and constant definitions derived from the Linux kernel (drivers/gpu/drm/i915/display/intel_display_types.h),
 * which carries the following notice.
 *
 * Copyright (c) 2006 Dave Airlie <airlied@linux.ie>
 * Copyright (c) 2007-2008 Intel Corporation
 *   Jesse Barnes <jesse.barnes@intel.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

/*
 * What the older modeset text still takes from the takeover layer: the
 * takeover world it passes along, the shared DPLL and CDCLK functions of
 * clock.c, and the colour and crtc hooks of the modeset object.  The
 * firmware's display itself is read, sanitized and stopped by the xd
 * parts (xd-takeover.c, xd-display.c).
 */

#ifndef DRIVERS_GPU_I915_DISPLAY_TAKEOVER_INTERNAL_H
#define DRIVERS_GPU_I915_DISPLAY_TAKEOVER_INTERNAL_H

#include "internal.h"
#include "modeset-internal.h"

#ifndef I915_DISPLAY_WORLD_MODESET
#error "takeover-internal.h is layered on modeset-internal.h"
#endif

/* A crtc state from a DRM crtc state (the uapi half is its first member). */
#define to_intel_crtc_state(x) ((struct intel_crtc_state *)(x))

/*
 * The takeover world: the context the older modeset text passes along.
 * It names its display; the display root holds a pointer to it.
 */
struct i915_takeover_world {
	/* The display this world belongs to, set when the world is created. */
	struct i915_display *display;
};

/* The shared DPLL and CDCLK functions of clock.c. */
struct intel_shared_dpll *drv_i915_get_shared_dpll_by_id(struct drm_i915_private *i915, enum intel_dpll_id id);
void drv_i915_unreference_shared_dpll_crtc(const struct intel_crtc *crtc, const struct intel_shared_dpll *pll, struct intel_shared_dpll_state *shared_dpll_state);

/* The colour and crtc hooks of the modeset object. */
const struct intel_color_funcs *drv_i915_lcd_ms_color_funcs(void);

/* Binds a device view to the named world's shared-DPLL pool. */
void drv_i915_lcd_dpll_pool_bind(struct i915_lcd_world *world, struct drm_i915_private *i915);

/*
 * The head-placement contracts the casts depend on: to_intel_crtc_state()
 * and to_intel_plane_state() are casts, and to_intel_crtc(NULL) must be
 * NULL, so these members must stay first.
 */
_Static_assert(offsetof(struct intel_crtc_state, uapi) == 0,
	"intel_crtc_state.uapi must remain first: to_intel_crtc_state() is a cast in this path");
_Static_assert(offsetof(struct intel_crtc, base) == 0,
	"intel_crtc.base must remain first: to_intel_crtc(NULL) must be NULL");
_Static_assert(offsetof(struct intel_plane_state, uapi) == 0,
	"intel_plane_state.uapi must remain first: to_intel_plane_state() is a cast in this path");

#endif /* DRIVERS_GPU_I915_DISPLAY_TAKEOVER_INTERNAL_H */
