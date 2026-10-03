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
 * declare under their explicit names (drv_i915_lcd_intel_phy_is_tc(),
 * drv_i915_lcd_intel_port_to_phy(), drv_i915_lcd_ms_crtc_enable(),
 * drv_i915_lcd_ms_crtc_disable() and drv_i915_lcd_ms_display_funcs()) are
 * not repeated here.
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
void drv_i915_cpu_transcoder_get_m1_n1(struct intel_crtc *crtc, enum transcoder transcoder, struct intel_link_m_n *m_n);
void drv_i915_cpu_transcoder_get_m2_n2(struct intel_crtc *crtc, enum transcoder transcoder, struct intel_link_m_n *m_n);
int drv_i915_crtc_dotclock(const struct intel_crtc_state *pipe_config);

/*
 * ==== The PHY and the power domains of a port or a crtc ====
 */

bool drv_i915_phy_is_combo(struct drm_i915_private *dev_priv, enum phy phy);
enum intel_display_power_domain drv_i915_aux_power_domain(struct intel_digital_port *dig_port);
void drv_i915_modeset_get_crtc_power_domains(struct i915_lcd_world *world, struct intel_crtc_state *crtc_state, struct intel_power_domain_mask *old_domains);
void drv_i915_modeset_put_crtc_power_domains(struct intel_crtc *crtc, struct intel_power_domain_mask *domains);

/*
 * ==== The transcoder enable and disable ====
 */

void drv_i915_enable_transcoder(const struct intel_crtc_state *new_crtc_state);
void drv_i915_disable_transcoder(const struct intel_crtc_state *old_crtc_state);

/*
 * ==== The readout of a running pipe ====
 */

void drv_i915_crtc_state_reset(struct intel_crtc_state *crtc_state, struct intel_crtc *crtc);
bool drv_i915_crtc_get_pipe_config(struct intel_crtc_state *crtc_state);
void drv_i915_encoder_get_config(struct intel_encoder *encoder, struct intel_crtc_state *crtc_state);
void drv_i915_set_plane_visible(struct intel_crtc_state *crtc_state, struct intel_plane_state *plane_state, bool visible);
void drv_i915_plane_fixup_bitmasks(struct i915_takeover_world *takeover, struct intel_crtc_state *crtc_state);
void drv_i915_plane_disable_noatomic(struct i915_takeover_world *takeover, struct intel_crtc *crtc, struct intel_plane *plane);

/*
 * ==== The register words of the transcoder, recorded ====
 */

int drv_i915_display_emit_transcoder(struct i915_lcd_world *world, struct i915_lcd_emit *emit, const struct drm_display_mode *mode, const struct intel_link_m_n *m_n, int pipe, int cpu_transcoder, int src_w, int src_h);
int drv_i915_display_emit_cpu_transcoder(struct i915_lcd_world *world, struct i915_lcd_emit *emit, const struct drm_display_mode *mode, const struct intel_link_m_n *m_n, int pipe, int cpu_transcoder, int port_clock, int lanes, int pipe_bpp);

/*
 * ==== The synchronous update of a running pipe ====
 */

int drv_i915_usecs_to_scanlines(const struct drm_display_mode *adjusted_mode, int usecs);
void drv_i915_crtc_vblank_evade_scanlines(struct intel_atomic_state *state, struct intel_crtc *crtc, int *min, int *max, int *vblank_start);
void drv_i915_pipe_update_start(struct i915_lcd_world *world, struct intel_atomic_state *state, struct intel_crtc *crtc);
void drv_i915_pipe_update_end(struct i915_lcd_world *world, struct intel_atomic_state *state, struct intel_crtc *crtc);
void drv_i915_crtc_wait_for_next_vblank(struct intel_crtc *crtc);

#endif /* I915_DISPLAY_WORLD_MODESET */

#endif /* DRIVERS_GPU_I915_DISPLAY_PIPE_H */
