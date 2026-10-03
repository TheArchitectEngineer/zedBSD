/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2020-2021 Intel Corporation
 */

/*
 * zedBSD WS031: copied from Linux v6.8.12 drivers/gpu/drm/i915/display/intel_dp_aux.h
 * (sha256 ea6e014ed7f352388bfed847de49c15b780ef0f770aa0803a7497eb079326767) by tools/port_dp_aux_pps.py.
 * Change: the <linux/types.h> include is removed.
 */

#ifndef __INTEL_DP_AUX_H__
#define __INTEL_DP_AUX_H__

/* zedBSD: types come from dp_compat.h, which includes this file */

enum aux_ch;
struct drm_i915_private;
struct intel_dp;
struct intel_encoder;

void intel_dp_aux_fini(struct intel_dp *intel_dp);
void intel_dp_aux_init(struct intel_dp *intel_dp);

enum aux_ch intel_dp_aux_ch(struct intel_encoder *encoder);

void intel_dp_aux_irq_handler(struct drm_i915_private *i915);
u32 intel_dp_aux_pack(const u8 *src, int src_bytes);
int intel_dp_aux_fw_sync_len(struct intel_dp *intel_dp);

#endif /* __INTEL_DP_AUX_H__ */
