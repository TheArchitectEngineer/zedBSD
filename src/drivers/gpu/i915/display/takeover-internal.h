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
 * The takeover layer of the modeset environment.
 *
 * The Linux text that reads out, sanitizes and takes over the display the
 * firmware left running (intel_modeset_setup: the readout, the sanitize,
 * intel_crtc_disable_noatomic, plus the readout halves of the display,
 * DDI, DPLL, plane and watermark text) is compiled against this header,
 * which is layered on modeset-internal.h.
 *
 * It maps what that text reaches outside the ported set:
 *
 *   the walks over the device's crtcs, planes, encoders and connectors,
 *     which are the objects of the takeover registry (struct
 *     i915_n1_registry, built by the takeover runner);
 *   the power accessors of a readout, which take a reference only where the
 *     well is already on;
 *   the parts of other subsystems this path does not port, each reported
 *     as a named step, never a silent success.
 *
 * The Linux text found the registry through file-scope accessors, and the
 * device of the steps and power questions through a file-scope pointer.
 * The explicit forms here take the registry (struct i915_takeover_world),
 * the watermark context (struct i915_lcd_wm_ctx) and that device as
 * arguments.  A walk that needs a running index takes the index variable
 * as an argument too, since a loop may not declare it.
 *
 * Errors: as in modeset-internal.h (I915_LCD_EBUSY and I915_LCD_ENXIO are
 * the Linux numbers the takeover compares).
 */

#ifndef DRIVERS_GPU_I915_DISPLAY_TAKEOVER_INTERNAL_H
#define DRIVERS_GPU_I915_DISPLAY_TAKEOVER_INTERNAL_H

#include "internal.h"
#include "modeset-internal.h"

#ifndef I915_DISPLAY_WORLD_MODESET
#error "takeover-internal.h is layered on modeset-internal.h"
#endif

/* This translation unit also carries the takeover layer. */
#define I915_DISPLAY_WORLD_TAKEOVER 1

/*
 * ==== Macros and constants ====
 */

/* The pipes the takeover registry holds one crtc and one primary plane for. */
#define I915_N1_PIPES 4

/* ---- the walks over the registry ---- */

/*
 * The crtcs of the device (the Linux for_each_intel_crtc()): the registry's
 * crtcs, index 0 up to the first empty slot.
 */
#define I915_TAKEOVER_FOR_EACH_INTEL_CRTC(takeover, crtc, index) \
	for ((index) = 0u; ((crtc) = drv_i915_n1_crtc_at((takeover), (index))) != NULL; (index)++)

/* The encoders of the device (the Linux for_each_intel_encoder()): the registry's encoder. */
#define I915_TAKEOVER_FOR_EACH_INTEL_ENCODER(takeover, encoder, index) \
	for ((index) = 0u; ((encoder) = drv_i915_n1_encoder_at((takeover), (index))) != NULL; (index)++)

/* The DP encoders of the device (the Linux for_each_intel_dp()): every registry encoder. */
#define I915_TAKEOVER_FOR_EACH_INTEL_DP(takeover, encoder, index) \
	I915_TAKEOVER_FOR_EACH_INTEL_ENCODER(takeover, encoder, index)

/*
 * The crtcs of a pipe mask (the readout's Linux
 * for_each_intel_crtc_in_pipe_mask()): the registry's crtcs whose pipe is
 * in the mask.  This is the form that was in effect at the uses in the DDI,
 * display and modeset-setup text (in the DDI and display text the mask was
 * the literal 0, so the body never ran).
 */
#define I915_TAKEOVER_FOR_EACH_INTEL_CRTC_IN_PIPE_MASK(takeover, crtc, index, mask) \
	I915_TAKEOVER_FOR_EACH_INTEL_CRTC(takeover, crtc, index) \
		for_each_if((mask) & BIT((crtc)->pipe))

/* The encoders found on one crtc (the Linux for_each_encoder_on_crtc()). */
#define I915_TAKEOVER_FOR_EACH_ENCODER_ON_CRTC(takeover, on_crtc, encoder, index) \
	I915_TAKEOVER_FOR_EACH_INTEL_ENCODER(takeover, encoder, index) \
		for_each_if((encoder)->base.crtc == (on_crtc))

/* The planes of the device (the Linux for_each_intel_plane()): the registry's planes. */
#define I915_TAKEOVER_FOR_EACH_INTEL_PLANE(takeover, plane, index) \
	for ((index) = 0u; ((plane) = drv_i915_n1_plane_at((takeover), (index))) != NULL; (index)++)

/*
 * The planes of one crtc (the readout's Linux
 * for_each_intel_plane_on_crtc()): the registry holds one primary plane
 * per pipe.  This is the form that was in effect at every use.
 */
#define I915_TAKEOVER_FOR_EACH_INTEL_PLANE_ON_CRTC(takeover, crtc, plane) \
	for ((plane) = drv_i915_n1_plane_at((takeover), (unsigned)(crtc)->pipe); (plane) != NULL; (plane) = NULL)

/*
 * The planes of a plane mask (the Linux drm_for_each_plane_mask()): the
 * crtc has one plane in this path, the registry's primary of pipe A.  The
 * mask is not evaluated.
 */
#define I915_TAKEOVER_DRM_FOR_EACH_PLANE_MASK(takeover, plane) \
	for ((plane) = drv_i915_n1_primary_plane(takeover); (plane) != NULL; (plane) = NULL)

/*
 * The connectors of the device (the Linux for_each_intel_connector_iter()):
 * the registry's connector list, walked by the iterator's index.  The
 * objects belong to the runner, so the Linux reference counting has nothing
 * to count (an adaptation, not an unported path).
 */
#define I915_TAKEOVER_FOR_EACH_INTEL_CONNECTOR_ITER(takeover, connector, iter) \
	while (((connector) = drv_i915_n1_connector_at((takeover), (iter)->idx++)) != NULL)
#define I915_TAKEOVER_DRM_CONNECTOR_GET(connector) ((void)0)
#define I915_TAKEOVER_DRM_CONNECTOR_PUT(connector) ((void)0)

/* The crtc of a pipe (the readout's Linux intel_crtc_for_pipe()): the registry's crtc. */
#define I915_TAKEOVER_INTEL_CRTC_FOR_PIPE(takeover, i915, pipe) drv_i915_n1_crtc_for_pipe((takeover), (pipe))

/* ---- the atomic state of a readout ---- */

/* A crtc state from a DRM crtc state (the uapi half is its first member). */
#define to_intel_crtc_state(x) ((struct intel_crtc_state *)(x))

/*
 * The DRM atomic state is the intel atomic state in this path
 * (to_intel_atomic_state() is the identity); the runner owns the storage.
 */
#define drm_atomic_state intel_atomic_state

/*
 * The crtc state of a crtc in the takeover's atomic state (the readout's
 * Linux intel_atomic_get_crtc_state()): the registry's state for the crtc,
 * recorded as the old state of the disable that follows; outside a readout
 * the state of the watermark context.
 */
#define I915_TAKEOVER_INTEL_ATOMIC_GET_CRTC_STATE(takeover, wm, state, crtc) \
	drv_i915_n1_atomic_crtc_state((takeover), (wm), (state), (crtc))

/* The throw-away atomic state of the takeover (the Linux drm_atomic_state_alloc()). */
#define I915_TAKEOVER_DRM_ATOMIC_STATE_ALLOC(takeover, dev) drv_i915_n1_atomic_state((takeover), (dev))
#define drm_atomic_state_put(state) ((void)0)

/* The global states the noatomic disable clears: this path keeps them in the registry. */
#define to_intel_bw_state(x) ((struct intel_bw_state *)(x))
#define to_intel_cdclk_state(x) ((struct intel_cdclk_state *)(x))
#define to_intel_pmdemand_state(x) ((struct intel_pmdemand_state *)(x))
#define intel_atomic_get_bw_state(state) ((struct intel_bw_state *)0)
#define intel_atomic_get_cdclk_state(state) ((struct intel_cdclk_state *)0)

/* drm_atomic_state_helper.h: here only the link to the crtc matters. */
#define __drm_atomic_helper_crtc_state_reset(state, crtc_base) ((state)->crtc = (crtc_base))

/* The uapi copy of a LUT blob: a pointer here (no properties). */
#define drm_property_replace_blob(dst, src) (*(dst) = (src))

/* ---- power domains of a readout: a reference only where the well is already on ---- */

/* The readout's own mask lives inside the set (intel_display_power.h). */
#define I915_N1_SET_OF(m) ((struct intel_display_power_domain_set *)(void *)(m))

/*
 * The Linux intel_display_power_get_if_enabled(), _get_in_set_if_enabled()
 * and _put_all_in_set() of a readout.  The device is the one the readout
 * text reached through a file-scope pointer (cur_i915), which is not
 * always the device argument of the Linux call; the Linux device argument
 * was not evaluated.
 */
#define I915_TAKEOVER_INTEL_DISPLAY_POWER_GET_IF_ENABLED(cur_i915, domain) drv_i915_n1_power_get_if_enabled((cur_i915), (domain))
#define I915_TAKEOVER_INTEL_DISPLAY_POWER_GET_IN_SET_IF_ENABLED(cur_i915, set, domain) \
	(drv_i915_n1_power_get_in_set_if_enabled((cur_i915), (set), (domain)), true)
#define I915_TAKEOVER_INTEL_DISPLAY_POWER_PUT_ALL_IN_SET(cur_i915, set) drv_i915_n1_power_put_all_in_set((cur_i915), (set))

/*
 * The Linux with_intel_display_power_if_enabled(): the reference is taken
 * on the file-scope device (cur_i915) and given back on the Linux call's
 * device (i915), as the readout text did.
 */
#define I915_TAKEOVER_WITH_INTEL_DISPLAY_POWER_IF_ENABLED(cur_i915, i915, domain, wf) \
	for ((wf) = drv_i915_n1_power_get_if_enabled((cur_i915), (domain)); \
	     (wf) != 0; \
	     i915_n1_power_put((i915), (domain), (wf)), (wf) = 0)

/* The readout hands the crtc's own set; the modeset helpers take that set. */
#define intel_display_power_put_all_in_set_mask(i915, set, mask) intel_display_power_put_mask_in_set((i915), (set), (mask))

/* ---- the platform branches this machine never takes ---- */

/* No Broxton PHY. */
#define BXT_PHY_LANE_ENABLED 0u
#define BXT_PHY_LANE_POWERDOWN_ACK 0u
#define BXT_PHY_CMNLANE_POWERDOWN_ACK 0u
#define BXT_PHY_CTL(port) _MMIO(0)
#define bxt_ddi_phy_get_lane_lat_optim_mask(encoder) (0u)

/* No DSI panel on this machine. */
#define bxt_get_dsi_transcoder_state(crtc, cs, set) (false)

/* No Sandy Bridge. */
#define IS_SANDYBRIDGE(i915) (0)

/* One pipe per stream: no big joiner. */
#define enabled_bigjoiner_pipes(i915, master, slave) \
	do { \
		*(master) = 0u; \
		*(slave) = 0u; \
	} while (0)
#define get_bigjoiner_master_pipe(pipe, master_pipes, slave_pipes) (pipe)
#define intel_bigjoiner_adjust_pipe_src(cs) ((void)0)
#define intel_bigjoiner_get_config(cs) ((void)0)
#define intel_bigjoiner_num_pipes(cs) (1)
#define intel_bigjoiner_adjust_timings(cs, mode) ((void)0)

/* The transcoders A..D of this platform, and the walk over those of a mask. */
#define HAS_TRANSCODER(i915, tr) ((tr) >= 0 && (tr) <= 3)
#define for_each_cpu_transcoder_masked(i915, tr, mask) \
	for ((tr) = 0; (tr) <= 3; (tr)++) \
		for_each_if((mask) & BIT(tr))

/* One plane per crtc in this path. */
#define drm_plane_mask(plane) (1u << 0)

/* The plane type the sanitize keeps, and the crtc's bit in a device-wide mask. */
#define DRM_PLANE_TYPE_PRIMARY 1
#define drm_crtc_mask(crtc) (1u << (unsigned)to_intel_crtc(crtc)->pipe)

/* The object masks of a readout: this path numbers its objects by index. */
#define drm_connector_mask(connector) (1u << (connector)->index)
#define drm_encoder_mask(encoder) (1u << (encoder)->index)

/* linux/string_helpers.h */
#define str_enabled_disabled(v) ((v) ? "enabled" : "disabled")

/* Tells whether no bit of a bitmap is set (the Linux bitmap_empty()). */
#define bitmap_empty(bits, nbits) i915_n1_bitmap_empty(bits, nbits)

/* The encoders that carry a DP link (intel_display_types.h). */
#define intel_encoder_is_dp(encoder) ((encoder)->type == INTEL_OUTPUT_DP || (encoder)->type == INTEL_OUTPUT_EDP)

/* The DPLL readout of the pool (clock.c). */
#define intel_dpll_readout_hw_state drv_i915_n1_dpll_readout_hw_state

/* The plane disable of the takeover is this path's own text (plane.c). */
#define intel_plane_disable_arm(plane, cs) drv_i915_lcd_plane_disable_arm((plane), (cs))

/* The watermark readout of this platform (skl_wm_get_hw_state), static in its own file. */
#define intel_wm_get_hw_state(i915) drv_i915_lcd_wm_get_hw_state(i915)

/* The firmware notification of a sanitized encoder (opregion.c). */
#define intel_opregion_notify_encoder(encoder, enable) drv_i915_opregion_notify_encoder((int)(encoder)->port, (encoder)->type, (enable))

/* A debug line at notice level: this path has one log (the Linux drm_notice()). */
#define I915_TAKEOVER_DRM_NOTICE(dev, fmt, ...) I915_LCD_DRM_DBG_KMS(dev, fmt, ##__VA_ARGS__)

/* ---- subsystems this path does not port: each call is a named step ---- */

/*
 * Records an unported callee of the readout text as a named step on the
 * device the caller names (the readout text named it through a file-scope
 * pointer, which is not always the device argument of the Linux call).
 */
#define I915_N1_STEP(i915, name) I915_LCD_STEP(i915, name)

/*
 * XXX: UNPORTED -- the readout halves of the subsystems this path does not
 * program.  Each is a named step; what Linux does there:
 *   intel_lspcon_infoframes_enabled: the LSPCON's infoframe state (none here).
 *   intel_dsc_get_config: DSS_CTL1/2 and the PPS registers into
 *     crtc_state->dsc (this link carries uncompressed pixels).
 *   intel_vrr_get_config: TRANS_VRR_CTL / VMIN / VMAX / FLIPLINE into
 *     crtc_state->vrr (no variable refresh here).
 *   intel_ddi_mso_get_config: the eDP multi-SO splitter fields of PIPE_MISC2
 *     (this panel is one stream).
 *   intel_ddi_is_audio_enabled: AUDIO_OUTPUT_ENABLE of TRANS_DDI_FUNC_CTL.
 *   intel_hdmi_infoframes_enabled / _read_gcp_infoframe /
 *     intel_read_infoframe / intel_read_dp_sdp: VIDEO_DIP_CTL and the DIP
 *     data words read back into the frames the firmware is sending.
 *   intel_edp_fixup_vbt_bpp: the read-out pipe bpp compared with the VBT's
 *     edp_bpp, correcting the VBT copy.
 *   bdw_get_trans_port_sync_config: the port sync master of
 *     TRANS_DDI_FUNC_CTL2.
 *   is_trans_port_sync_master / intel_tc_port_link_needs_reset: port sync
 *     and the Type-C link reset (neither exists on this machine).
 *   intel_color_get_config: GAMMA_MODE / CSC_MODE and the LUTs.
 *   intel_psr_get_config: SRD_CTL / PSR2_CTL (no panel self refresh here).
 *   intel_audio_codec_get_config: the audio ELD / rate.
 *   intel_dp_sync_state: the DPCD of a firmware-trained link re-read
 *     (this path trains the link itself in the re-light).
 *   intel_tc_port_sanitize_mode / icl_set_active_port_dpll: Type-C only.
 *   hsw_ips_get_config / hsw_ips_disable: IPS (Haswell / Broadwell).
 *   ilk_get_pfit_config / skl_scaler_get_config: the panel fitter and the
 *     scalers (nothing is scaled here).
 *   drm_crtc_wait_one_vblank: the DRM vblank layer (not in this path).
 *   intel_set_memory_cxsr: CxSR (pre-Gen9 only).
 */
#define I915_TAKEOVER_INTEL_LSPCON_INFOFRAMES_ENABLED(i915, encoder, cs) (I915_N1_STEP(i915, "intel_lspcon_infoframes_enabled"), false)
#define I915_TAKEOVER_INTEL_DSC_GET_CONFIG(i915, cs) I915_N1_STEP(i915, "intel_dsc_get_config")
#define I915_TAKEOVER_INTEL_VRR_GET_CONFIG(i915, cs) I915_N1_STEP(i915, "intel_vrr_get_config")
#define I915_TAKEOVER_INTEL_DDI_MSO_GET_CONFIG(i915, encoder, cs) I915_N1_STEP(i915, "intel_ddi_mso_get_config")
#define I915_TAKEOVER_INTEL_DDI_IS_AUDIO_ENABLED(cur_i915, i915, tr) (I915_N1_STEP(cur_i915, "intel_ddi_is_audio_enabled"), false)
#define I915_TAKEOVER_INTEL_HDMI_INFOFRAMES_ENABLED(i915, encoder, cs) (I915_N1_STEP(i915, "intel_hdmi_infoframes_enabled"), 0u)
#define I915_TAKEOVER_INTEL_HDMI_READ_GCP_INFOFRAME(i915, encoder, cs) I915_N1_STEP(i915, "intel_hdmi_read_gcp_infoframe")
#define I915_TAKEOVER_INTEL_READ_INFOFRAME(i915, encoder, cs, type, frame) I915_N1_STEP(i915, "intel_read_infoframe")
#define I915_TAKEOVER_INTEL_READ_DP_SDP(i915, encoder, cs, type) I915_N1_STEP(i915, "intel_read_dp_sdp")
#define I915_TAKEOVER_INTEL_EDP_FIXUP_VBT_BPP(i915, encoder, bpp) I915_N1_STEP(i915, "intel_edp_fixup_vbt_bpp")
#define I915_TAKEOVER_BDW_GET_TRANS_PORT_SYNC_CONFIG(i915, cs) I915_N1_STEP(i915, "bdw_get_trans_port_sync_config")
#define I915_TAKEOVER_IS_TRANS_PORT_SYNC_MASTER(i915, cs) (I915_N1_STEP(i915, "is_trans_port_sync_master"), false)
#define I915_TAKEOVER_INTEL_TC_PORT_LINK_NEEDS_RESET(i915, dig_port) (I915_N1_STEP(i915, "intel_tc_port_link_needs_reset"), false)
#define I915_TAKEOVER_INTEL_COLOR_GET_CONFIG(i915, cs) I915_N1_STEP(i915, "intel_color_get_config")
#define I915_TAKEOVER_INTEL_PSR_GET_CONFIG(i915, encoder, cs) I915_N1_STEP(i915, "intel_psr_get_config")
#define I915_TAKEOVER_INTEL_AUDIO_CODEC_GET_CONFIG(i915, encoder, cs) I915_N1_STEP(i915, "intel_audio_codec_get_config")
#define I915_TAKEOVER_INTEL_DP_SYNC_STATE(i915, encoder, cs) I915_N1_STEP(i915, "intel_dp_sync_state")
#define I915_TAKEOVER_INTEL_TC_PORT_SANITIZE_MODE(i915, dig_port, cs) I915_N1_STEP(i915, "intel_tc_port_sanitize_mode")
#define I915_TAKEOVER_ICL_SET_ACTIVE_PORT_DPLL(i915, cs, port_dpll_id) I915_N1_STEP(i915, "icl_set_active_port_dpll")
#define I915_TAKEOVER_HSW_IPS_GET_CONFIG(i915, cs) I915_N1_STEP(i915, "hsw_ips_get_config")
#define I915_TAKEOVER_ILK_GET_PFIT_CONFIG(i915, cs) I915_N1_STEP(i915, "ilk_get_pfit_config")
#define I915_TAKEOVER_SKL_SCALER_GET_CONFIG(i915, cs) I915_N1_STEP(i915, "skl_scaler_get_config")
#define I915_TAKEOVER_DRM_CRTC_WAIT_ONE_VBLANK(i915, crtc) I915_N1_STEP(i915, "drm_crtc_wait_one_vblank")
#define I915_TAKEOVER_HSW_IPS_DISABLE(i915, cs) (I915_N1_STEP(i915, "hsw_ips_disable"), false)
#define I915_TAKEOVER_INTEL_SET_MEMORY_CXSR(cur_i915, i915, enable) (I915_N1_STEP(cur_i915, "intel_set_memory_cxsr"), false)

/*
 * XXX: UNPORTED -- the takeover's own callees of other subsystems.
 *   drm_atomic_add_affected_connectors: the DRM atomic core.
 *   __drm_atomic_helper_crtc_destroy_state / intel_crtc_free_hw_state:
 *     the state is the runner's storage, nothing is freed.
 *   intel_fbc_disable / intel_fbc_sanitize: FBC is not used.
 *   intel_update_watermarks: the pre-Gen9 watermark update.
 *   intel_pmdemand_update_port_clock / intel_pmdemand_update_phys_mask: the
 *     pm demand unit of display 14+; display 13 has nothing to write.
 *     pseudo-code of the reference (phys mask): phys_mask =
 *     pmdemand_state->active_combo_phys_mask; set / clear BIT(phy of
 *     encoder); store back.
 *   intel_init_fifo_underrun_reporting: the underrun bookkeeping, whose
 *     interrupts are not enabled here (an adaptation).  pseudo-code:
 *     crtc->cpu_fifo_underrun_disabled = !enable; for a PCH transcoder also
 *     crtc->pch_fifo_underrun_disabled = !enable.
 *   intel_crtc_state_dump: prints the whole state for debugging.
 *   intel_dsi_encoder_ports: the port mask of a DSI encoder (no DSI here).
 *   drm_crtc_vblank_reset: the DRM vblank layer's reset (not in this path).
 *   intel_power_domains_sanitize_state: drops the wells no crtc claims;
 *     this path releases its wells through its own domain set.
 */
#define I915_TAKEOVER_DRM_ATOMIC_ADD_AFFECTED_CONNECTORS(i915, state, crtc) (I915_N1_STEP(i915, "drm_atomic_add_affected_connectors"), 0)
#define I915_TAKEOVER___DRM_ATOMIC_HELPER_CRTC_DESTROY_STATE(i915, cs) I915_N1_STEP(i915, "__drm_atomic_helper_crtc_destroy_state")
#define I915_TAKEOVER_INTEL_CRTC_FREE_HW_STATE(i915, cs) I915_N1_STEP(i915, "intel_crtc_free_hw_state")
#define I915_TAKEOVER_INTEL_FBC_DISABLE(i915, crtc) I915_N1_STEP(i915, "intel_fbc_disable")
#define I915_TAKEOVER_INTEL_FBC_SANITIZE(cur_i915, i915) I915_N1_STEP(cur_i915, "intel_fbc_sanitize")
#define I915_TAKEOVER_INTEL_UPDATE_WATERMARKS(cur_i915, i915) I915_N1_STEP(cur_i915, "intel_update_watermarks")
#define I915_TAKEOVER_INTEL_PMDEMAND_UPDATE_PORT_CLOCK(cur_i915, i915, pmdemand, port, clock) I915_N1_STEP(cur_i915, "intel_pmdemand_update_port_clock")
#define I915_TAKEOVER_INTEL_PMDEMAND_UPDATE_PHYS_MASK(cur_i915, i915, encoder, pmdemand, set) I915_N1_STEP(cur_i915, "intel_pmdemand_update_phys_mask")
#define I915_TAKEOVER_INTEL_INIT_FIFO_UNDERRUN_REPORTING(cur_i915, i915, crtc, enable) I915_N1_STEP(cur_i915, "intel_init_fifo_underrun_reporting")
#define I915_TAKEOVER_INTEL_CRTC_STATE_DUMP(i915, cs, state, context) I915_N1_STEP(i915, "intel_crtc_state_dump")
#define I915_TAKEOVER_INTEL_DSI_ENCODER_PORTS(i915, encoder) (I915_N1_STEP(i915, "intel_dsi_encoder_ports"), 0u)
#define I915_TAKEOVER_DRM_CRTC_VBLANK_RESET(i915, crtc) I915_N1_STEP(i915, "drm_crtc_vblank_reset")
#define I915_TAKEOVER_INTEL_POWER_DOMAINS_SANITIZE_STATE(cur_i915, i915) I915_N1_STEP(cur_i915, "intel_power_domains_sanitize_state")

/*
 * Callees whose Linux body is DECIDED on this machine; the step is recorded
 * as decided, not as unresolved:
 *   intel_pch_sanitize(): its body runs only for HAS_PCH_IBX(); this
 *     machine has an ADL-P PCH.
 *   intel_pmdemand_init_pmdemand_params(): the pm demand unit exists from
 *     display 14; this display is 13.
 */
#define I915_TAKEOVER_INTEL_PCH_SANITIZE(cur_i915, i915) \
	I915_N1_STEP(cur_i915, "(decided) intel_pch_sanitize: HAS_PCH_IBX is false on this PCH")
#define I915_TAKEOVER_INTEL_PMDEMAND_INIT_PMDEMAND_PARAMS(cur_i915, i915, state) \
	I915_N1_STEP(cur_i915, "(decided) intel_pmdemand_init_pmdemand_params: display 13 has no pm demand unit")

/*
 * ==== Types ====
 */

/*
 * The Linux connector list iterator: the registry's connector list is
 * walked by index.
 */
struct drm_connector_list_iter {
	unsigned idx;
};

/*
 * The per-pipe part of the bandwidth state the takeover clears for a freed
 * pipe (intel_bw.c).
 */
struct intel_bw_state {
	unsigned int data_rate[I915_MAX_PIPES];
	u8 num_active_planes[I915_MAX_PIPES];
	bool force_check_qgv;		/* the QGV point is re-checked at the next bandwidth check */
};

/* The per-pipe part of the CDCLK state the takeover clears for a freed pipe (intel_cdclk.c). */
struct intel_cdclk_state {
	int min_cdclk[I915_MAX_PIPES];
	u8 min_voltage_level[I915_MAX_PIPES];
	u8 active_pipes;
};

/* The pm demand state's active pipes the takeover clears (intel_pmdemand.c). */
struct intel_pmdemand_state {
	u8 active_pipes;
};

/*
 * The takeover registry: the device's objects as the readout walks them --
 * one crtc and one primary plane per pipe, the encoder and connector of
 * the screen the DDI callers are bound to -- and the device-wide states the
 * takeover clears.
 *
 * The runner builds it from the same configuration the modeset object is
 * built from (clearing it first), marks it live for the readout, and the
 * walks answer nothing while it is not live.
 */
struct i915_n1_registry {
	/* The device the readout runs against. */
	struct drm_i915_private i915;

	/* One crtc per pipe. */
	struct intel_crtc crtc[I915_N1_PIPES];

	/* The state the readout fills for each crtc. */
	struct intel_crtc_state crtc_state[I915_N1_PIPES];

	/* One primary plane per pipe. */
	struct intel_plane plane[I915_N1_PIPES];

	/* The state the readout fills for each plane. */
	struct intel_plane_state plane_state[I915_N1_PIPES];

	/* The connector of the screen the DDI callers are bound to (the modeset object's own). */
	struct intel_connector *connector;

	/* The encoder of that screen. */
	struct intel_encoder *encoder;

	/* The connector's state. */
	struct drm_connector_state conn_state;

	/* The throw-away atomic state of the takeover's noatomic disable. */
	struct intel_atomic_state state;

	/* the device-wide states the takeover clears */
	struct intel_bw_state bw;
	struct intel_cdclk_state cdclk;
	struct intel_pmdemand_state pmdemand;
	struct intel_dbuf_state dbuf;

	/*
	 * the device vblank objects: the readout of an ACTIVE crtc writes the
	 * timestamping constants into dev->vblank[pipe] (drm_calc_timestamping_constants)
	 */
	struct drm_vblank_crtc vblank[I915_N1_PIPES];

	/* Nonzero while the registry is built and the walks answer with its objects. */
	int live;

	/* How many takeovers ran on the registry. */
	unsigned takeovers;

	/* The pipes still active after the last takeover (bit n = pipe n; 0 = all released). */
	unsigned still_active;
};

/*
 * The state of the takeover layer: the file-scope variables of the
 * takeover translation unit.
 *
 * The display root holds a pointer to one of these, allocated by the owner
 * of the takeover path.  It is used only by the modeset path, which runs on
 * the display owner's thread one operation at a time; no lock protects it.
 * A zeroed structure is the state at load time (the registry not live).
 */
struct i915_takeover_world {
	/*
	 * The registry of the display the firmware left running.  Cleared and
	 * built by the runner before a readout; live from then until the
	 * runner is done with it.
	 */
	struct i915_n1_registry n1;

	/*
	 * The display this world belongs to, set when the world is created.
	 * The registry walks read its register-trace switch through it.
	 */
	struct i915_display *display;
};

/*
 * ==== Forward declarations ====
 */

/*
 * The walks' accessors of the registry: the object at an index, or NULL
 * past the last one and whenever the registry is not live.
 */
struct intel_crtc *drv_i915_n1_crtc_at(struct i915_takeover_world *takeover, unsigned idx);
struct intel_crtc *drv_i915_n1_crtc_for_pipe(struct i915_takeover_world *takeover, enum pipe pipe);
struct intel_plane *drv_i915_n1_plane_at(struct i915_takeover_world *takeover, unsigned idx);
struct drm_plane *drv_i915_n1_primary_plane(struct i915_takeover_world *takeover);
struct intel_encoder *drv_i915_n1_encoder_at(struct i915_takeover_world *takeover, unsigned idx);
struct intel_connector *drv_i915_n1_connector_at(struct i915_takeover_world *takeover, unsigned idx);

/*
 * The state of a named crtc: the registry's while it is live, otherwise the
 * crtc state of the watermark context (NULL when there is none).
 */
struct intel_crtc_state *drv_i915_n1_crtc_state(struct i915_takeover_world *takeover, struct i915_lcd_wm_ctx *wm, const struct intel_crtc *crtc);

/*
 * The crtc state of a crtc in the takeover's atomic state; while the
 * registry is live it is also recorded as the old state of the disable.
 */
struct intel_crtc_state *drv_i915_n1_atomic_crtc_state(struct i915_takeover_world *takeover, struct i915_lcd_wm_ctx *wm, void *state, struct intel_crtc *crtc);

/* The registry's throw-away atomic state, reset for a new use. */
struct intel_atomic_state *drv_i915_n1_atomic_state(struct i915_takeover_world *takeover, struct drm_device *dev);

/*
 * The power accessors of a readout on the named device: a reference only
 * where the well is already on (0: the well is off).
 */
intel_wakeref_t drv_i915_n1_power_get_if_enabled(struct drm_i915_private *i915, enum intel_display_power_domain domain);
void drv_i915_n1_power_get_in_set_if_enabled(struct drm_i915_private *i915, struct intel_display_power_domain_set *set, enum intel_display_power_domain domain);
void drv_i915_n1_power_put_all_in_set(struct drm_i915_private *i915, struct intel_display_power_domain_set *set);

/* The generated readout functions the other files call. */
bool skl_ddb_allocation_overlaps(const struct skl_ddb_entry *ddb, const struct skl_ddb_entry *entries, int num_entries, int ignore_idx);
u32 skl_ddb_dbuf_slice_mask(struct drm_i915_private *i915, const struct skl_ddb_entry *entry);

/*
 * P1 exports: the DPLL and CDCLK readout functions of clock.c under the
 * names they are exported by.
 */
struct intel_shared_dpll *drv_i915_get_shared_dpll_by_id(struct drm_i915_private *i915, enum intel_dpll_id id);
bool drv_i915_dpll_get_hw_state(struct drm_i915_private *i915, struct intel_shared_dpll *pll, struct intel_dpll_hw_state *hw_state);
int drv_i915_dpll_get_freq(struct drm_i915_private *i915, const struct intel_shared_dpll *pll, const struct intel_dpll_hw_state *pll_state);
void drv_i915_unreference_shared_dpll_crtc(const struct intel_crtc *crtc, const struct intel_shared_dpll *pll, struct intel_shared_dpll_state *shared_dpll_state);
int drv_i915_crtc_compute_min_cdclk(struct i915_takeover_world *takeover, const struct intel_crtc_state *crtc_state);
void drv_i915_dpll_sanitize_state(struct drm_i915_private *i915);

/* The DPLL readout of the pool (intel_dpll_readout_hw_state()). */
void drv_i915_n1_dpll_readout_hw_state(struct i915_takeover_world *takeover, struct drm_i915_private *i915);

/* The plane disable of the takeover (plane.c). */
void drv_i915_lcd_plane_disable_arm(struct intel_plane *plane, const struct intel_crtc_state *crtc_state);

/* The watermark readout (skl_wm_get_hw_state), static in its own file. */
void drv_i915_lcd_wm_get_hw_state(struct drm_i915_private *i915);

/* The firmware notification of a sanitized encoder (opregion.c). */
int drv_i915_opregion_notify_encoder(int port, int output_type, int enable);

/* The plane readout hook, static in its own file (plane.c). */
bool (*i915_lcd_plane_get_hw_state(void))(struct intel_plane *plane, enum pipe *pipe);

/* What the rest of the modeset path lends the readout: its hooks. */
const struct intel_display_funcs *drv_i915_lcd_ms_display_funcs(void);
const struct intel_color_funcs *drv_i915_lcd_ms_color_funcs(void);
const struct drm_crtc_funcs *drv_i915_lcd_ms_crtc_funcs(void);

/*
 * The encoder and connector of the modeset object the named world's DDI
 * hooks are bound to (NULL: none bound).
 */
struct intel_encoder *drv_i915_lcd_ms_bound_encoder(struct i915_lcd_world *world);
struct intel_connector *drv_i915_lcd_ms_bound_connector(struct i915_lcd_world *world);

/* Binds a device view to the named world's shared-DPLL pool. */
void drv_i915_lcd_dpll_pool_bind(struct i915_lcd_world *world, struct drm_i915_private *i915);

/* Binds the readout hooks of an encoder, as intel_ddi_init() does. */
void drv_i915_lcd_ms_bind_readout(struct intel_encoder *encoder);

/*
 * ==== Inline helpers ====
 */

/*
 * Gives a display power reference back on the named device (the put of a
 * readout, as a function so it can sit in a loop header).
 */
static __inline void
i915_n1_power_put(
	struct drm_i915_private *i915,
	enum intel_display_power_domain domain,
	intel_wakeref_t wf)
{
	/* Gives the reference back through the device's backend. */
	i915_lcd_intel_display_power_put(i915, domain, wf);
}

/* Fills a rectangle from its origin and size (the Linux drm_rect_init()). */
static __inline void
i915_drm_rect_init(
	struct drm_rect *r,
	int x,
	int y,
	int w,
	int h)
{
	/* Stores the two corners. */
	r->x1 = x;
	r->y1 = y;
	r->x2 = x + w;
	r->y2 = y + h;
}

/*
 * Copies a mode into the uapi state (the Linux
 * drm_atomic_set_mode_for_crtc()): in this path a mode is a value, so it
 * cannot fail.
 */
static __inline int
i915_drm_atomic_set_mode_for_crtc(
	struct drm_crtc_state *state,
	const struct drm_display_mode *mode)
{
	/* Stores the mode by value. */
	state->mode = *mode;

	/* Succeeded: the uapi state carries the mode. */
	return 0;
}

/* Tells whether no bit of a bitmap is set (the Linux bitmap_empty()). */
static __inline bool
i915_n1_bitmap_empty(
	const unsigned long *bits,
	unsigned nbits)
{
	unsigned i;

	/* Looks for a word with a bit set. */
	for (i = 0u; i < (nbits + I915_BITS_PER_LONG - 1u) / I915_BITS_PER_LONG; i++) {
		if (bits[i] != 0ul)
			return false;
	}

	/* Succeeded: no bit is set. */
	return true;
}

/* Starts a walk over the registry's connectors (the Linux drm_connector_list_iter_begin()). */
static __inline void
i915_takeover_drm_connector_list_iter_begin(
	struct drm_connector_list_iter *iter)
{
	/* The walk starts at the first connector. */
	iter->idx = 0u;
}

/* Ends a walk over the registry's connectors (the Linux drm_connector_list_iter_end()). */
static __inline void
i915_takeover_drm_connector_list_iter_end(
	struct drm_connector_list_iter *iter)
{
	/* The walk holds no reference to give back. */
	UNUSED_PARAMETER(iter);
}

/* The encoder a connector is attached to (the Linux intel_attached_encoder()). */
static __inline struct intel_encoder *
i915_takeover_intel_attached_encoder(
	const struct intel_connector *connector)
{
	/* The attachment is recorded in the connector. */
	return connector->encoder;
}

/*
 * The DBUF state the readout reads and writes (the readout's Linux
 * to_intel_dbuf_state() and intel_atomic_get_dbuf_state()): the new DBUF
 * state of the watermark context.  The Linux argument (the global state
 * object or the atomic state) was not evaluated.
 */
static __inline struct intel_dbuf_state *
i915_takeover_intel_atomic_get_dbuf_state(
	struct i915_lcd_wm_ctx *wm)
{
	/* The device's DBUF state is the context's new state. */
	return &wm->new_dbuf;
}

/*
 * The two head-placement contracts this path depends on.  The Linux text
 * converts with container_of(), which does not care where the member sits;
 * this environment converts with a cast (to_intel_crtc_state(),
 * to_intel_plane_state()) and relies on to_intel_crtc(NULL) being NULL, so
 * these members must stay first.  A readout memset that overflowed into the
 * next object and a NULL crtc that came back non-NULL were both found this
 * way; a build that moves them fails here instead.
 */
_Static_assert(offsetof(struct intel_crtc_state, uapi) == 0,
	"intel_crtc_state.uapi must remain first: to_intel_crtc_state() is a cast in this path");
_Static_assert(offsetof(struct intel_crtc, base) == 0,
	"intel_crtc.base must remain first: the sanitize text relies on to_intel_crtc(NULL) == NULL");
_Static_assert(offsetof(struct intel_plane_state, uapi) == 0,
	"intel_plane_state.uapi must remain first: to_intel_plane_state() is a cast in this path");

#endif /* DRIVERS_GPU_I915_DISPLAY_TAKEOVER_INTERNAL_H */
