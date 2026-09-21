/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2020 Intel Corporation
 */

/*
 * zedBSD WS031: copied from Linux v6.8.12 drivers/gpu/drm/i915/display/intel_pps.h
 * (sha256 a8e8ad1455ad552081eb82897badf09722cc5bdb8aa543c3f90ff4eb5d54dff4) by tools/port_dp_aux_pps.py.
 * Change: the <linux/types.h> and intel_wakeref.h includes are removed.
 */

/*
 * zedBSD display rebuild: moved from src/drivers/gpu/i915/parity/dp/intel_pps.h of the
 * pre-rebuild tree (commit 7e7ff337) without change to any definition;
 * the include guard __INTEL_PPS_H__ was renamed.  The provenance above still applies and names the generator.
 */

#ifndef DRIVERS_GPU_I915_DATA_DISPLAY_INTEL_PPS_INC
#define DRIVERS_GPU_I915_DATA_DISPLAY_INTEL_PPS_INC

/* zedBSD: types and intel_wakeref_t come from dp_compat.h, which includes this file */

enum pipe;
struct drm_i915_private;
struct intel_connector;
struct intel_crtc_state;
struct intel_dp;
struct intel_encoder;

intel_wakeref_t intel_pps_lock(struct intel_dp *intel_dp);
intel_wakeref_t intel_pps_unlock(struct intel_dp *intel_dp, intel_wakeref_t wakeref);

#define with_intel_pps_lock(dp, wf)						\
	for ((wf) = intel_pps_lock(dp); (wf); (wf) = intel_pps_unlock((dp), (wf)))

void intel_pps_backlight_on(struct intel_dp *intel_dp);
void intel_pps_backlight_off(struct intel_dp *intel_dp);
void intel_pps_backlight_power(struct intel_connector *connector, bool enable);

bool intel_pps_vdd_on_unlocked(struct intel_dp *intel_dp);
void intel_pps_vdd_off_unlocked(struct intel_dp *intel_dp, bool sync);
void intel_pps_on_unlocked(struct intel_dp *intel_dp);
void intel_pps_off_unlocked(struct intel_dp *intel_dp);
void intel_pps_check_power_unlocked(struct intel_dp *intel_dp);

void intel_pps_vdd_on(struct intel_dp *intel_dp);
void intel_pps_on(struct intel_dp *intel_dp);
void intel_pps_off(struct intel_dp *intel_dp);
void intel_pps_vdd_off_sync(struct intel_dp *intel_dp);
bool intel_pps_have_panel_power_or_vdd(struct intel_dp *intel_dp);
void intel_pps_wait_power_cycle(struct intel_dp *intel_dp);

bool intel_pps_init(struct intel_dp *intel_dp);
void intel_pps_init_late(struct intel_dp *intel_dp);
void intel_pps_encoder_reset(struct intel_dp *intel_dp);
void intel_pps_reset_all(struct drm_i915_private *i915);

void vlv_pps_init(struct intel_encoder *encoder,
		  const struct intel_crtc_state *crtc_state);

void intel_pps_unlock_regs_wa(struct drm_i915_private *i915);
void intel_pps_setup(struct drm_i915_private *i915);

void assert_pps_unlocked(struct drm_i915_private *i915, enum pipe pipe);

#endif /* DRIVERS_GPU_I915_DATA_DISPLAY_INTEL_PPS_INC */
