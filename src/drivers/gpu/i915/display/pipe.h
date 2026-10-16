/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The pipe and transcoder of the one-screen path (pipe.c).
 *
 * Every function here takes the modeset environment's Linux types, so the
 * declarations are visible only to a translation unit that has included
 * modeset-internal.h first.  The functions the environment headers already
 * declare under their explicit names (drv_i915_lcd_intel_phy_is_tc() and
 * drv_i915_lcd_intel_port_to_phy()) are not repeated here.
 *
 * A function whose Linux text reached an object through a file-scope
 * pointer takes that object's owner as its first argument: the modeset
 * world (the only encoder of the screen, the interrupt section of the
 * update), or the takeover world (the registry's planes).
 */

#ifndef DRIVERS_GPU_I915_DISPLAY_PIPE_H
#define DRIVERS_GPU_I915_DISPLAY_PIPE_H

#include "internal.h"

struct i915_display;

#ifdef I915_DISPLAY_WORLD_MODESET

/*
 * ==== The link M/N values and the pipe clock ====
 */

void drv_i915_link_compute_m_n(u16 bits_per_pixel_x16, int nlanes, int pixel_clock, int link_clock, int bw_overhead, struct intel_link_m_n *m_n);

/*
 * ==== The PHY and the power domains of a port or a crtc ====
 */

enum intel_display_power_domain drv_i915_aux_power_domain(struct intel_digital_port *dig_port);

/*
 * ==== The transcoder enable and disable ====
 */


/*
 * ==== The pipe scalers ====
 */

void drv_i915_skl_scaler_disable(const struct intel_crtc_state *old_crtc_state);

/*
 * ==== The register words of the transcoder, recorded ====
 */

int drv_i915_display_emit_transcoder(struct i915_lcd_world *world, struct i915_lcd_emit *emit, const struct drm_display_mode *mode, const struct intel_link_m_n *m_n, int pipe, int cpu_transcoder, int src_w, int src_h);
int drv_i915_display_emit_cpu_transcoder(struct i915_lcd_world *world, struct i915_lcd_emit *emit, const struct drm_display_mode *mode, const struct intel_link_m_n *m_n, int pipe, int cpu_transcoder, int port_clock, int lanes, int pipe_bpp);

/*
 * ==== The synchronous update of a running pipe ====
 */


#endif /* I915_DISPLAY_WORLD_MODESET */

#endif /* DRIVERS_GPU_I915_DISPLAY_PIPE_H */
