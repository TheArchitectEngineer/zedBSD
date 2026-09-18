/*
 * WS031 Linux-parity — the environment the reference's ENABLE-SEQUENCE callers are compiled in:
 * hsw_crtc_enable() (intel_display_port.c) and intel_ddi_pre_pll_enable() / intel_ddi_pre_enable() /
 * intel_ddi_pre_enable_dp() / tgl_ddi_pre_enable_dp() / intel_enable_ddi() / intel_enable_ddi_dp()
 * (intel_ddi_port.c).  zedBSD project code.
 *
 * The callers are reference text, so the ORDER is the reference's.  A callee that is ported runs for
 * real (its register operations land in the same list); a callee that is NOT ported is reported as a
 * named step at the position the reference calls it.  Nothing is dropped silently: a callee without a
 * definition here does not compile.
 */
#ifndef PARITY_LCD_SEQ_COMPAT_H
#define PARITY_LCD_SEQ_COMPAT_H

#define PARITY_LCD_STEP(i915, name) ((i915)->emit->step((i915)->emit->ctx, name))
/* the device behind the various objects the callers hold */
#define SEQ_I915_CRTC_STATE(cs) to_i915((cs)->uapi.crtc->dev)
#define SEQ_I915_ENCODER(e) to_i915((e)->base.dev)
struct intel_digital_port;

/* ---- hsw_crtc_enable(): its state accessors ---- */
struct intel_atomic_state { struct { struct drm_device *dev; } base; const struct intel_crtc_state *crtc_state, *old_crtc_state; };
#define intel_atomic_get_old_crtc_state(state, crtc) ((state)->old_crtc_state)
#define intel_atomic_get_new_crtc_state(state, crtc) ((state)->crtc_state)
#define intel_crtc_is_bigjoiner_slave(crtc_state) (0)          /* no big joiner in this configuration */
#define IS_BROADWELL(i915) 0
#define IS_GEMINILAKE(i915) 0
#define IS_BROXTON(i915) 0
#define HAS_DP20(i915) 0                                       /* IS_DG2 || display version >= 14 */
#define is_trans_port_sync_mode(crtc_state) (0)                /* no port sync: master_transcoder is INVALID and no slaves */

/*
 * ---- GUARDS: callees whose reference body begins with an early return that holds in the supported
 * configuration.  The reference's condition is kept here word for word in meaning (the function and the
 * condition are named); if it ever does NOT hold, the unported rest is reported as an ERROR -- the run is
 * then not a success -- instead of being passed over in silence.
 */
#define PARITY_LCD_GUARD(holds, what) do { if (!(holds)) parity_lcd_error("UNPORTED callee body reached: " what "\n"); } while (0)
/* icl_program_mg_dp_mode(): returns unless the PHY is Type-C (and not in TBT-alt mode) */
#define icl_program_mg_dp_mode(dig_port, cs) PARITY_LCD_GUARD(!intel_phy_is_tc(SEQ_I915_ENCODER(&(dig_port)->base), intel_port_to_phy(SEQ_I915_ENCODER(&(dig_port)->base), (dig_port)->base.port)), "icl_program_mg_dp_mode (Type-C PHY)")
/* intel_dp_configure_protocol_converter(): returns when DPCD_REV < 1.3, or the sink is not a branch device */
#define intel_dp_configure_protocol_converter(intel_dp, cs) PARITY_LCD_GUARD((intel_dp)->dpcd[DP_DPCD_REV] < 0x13 || !drm_dp_is_branch((intel_dp)->dpcd), "intel_dp_configure_protocol_converter (DPCD >= 1.3 branch device)")
/* DSC: every one of these returns at once when crtc_state->dsc.compression_enable is false */
#define intel_dp_sink_enable_decompression(state, connector, cs) PARITY_LCD_GUARD(!(cs)->dsc.compression_enable, "intel_dp_sink_enable_decompression (DSC)")
#define intel_dp_sink_disable_decompression(state, connector, cs) PARITY_LCD_GUARD(!(cs)->dsc.compression_enable, "intel_dp_sink_disable_decompression (DSC)")
#define intel_dsc_dp_pps_write(encoder, cs) PARITY_LCD_GUARD(!(cs)->dsc.compression_enable, "intel_dsc_dp_pps_write (DSC)")
#define intel_dsc_enable(cs) PARITY_LCD_GUARD(!(cs)->dsc.compression_enable, "intel_dsc_enable (DSC)")
#define intel_dsc_disable(cs) PARITY_LCD_GUARD(!(cs)->dsc.compression_enable, "intel_dsc_disable (DSC)")
/* intel_uncompressed_joiner_enable(): acts only with bigjoiner_pipes set */
#define intel_uncompressed_joiner_enable(cs) PARITY_LCD_GUARD((cs)->bigjoiner_pipes == 0, "intel_uncompressed_joiner_enable (big joiner)")
/* FEC: all three return at once when crtc_state->fec_enable is false */
#define intel_dp_sink_set_fec_ready(intel_dp, cs, enable) PARITY_LCD_GUARD(!(cs)->fec_enable, "intel_dp_sink_set_fec_ready (FEC)")
#define intel_ddi_enable_fec(encoder, cs) PARITY_LCD_GUARD(!(cs)->fec_enable, "intel_ddi_enable_fec (FEC)")
#define intel_ddi_wait_for_fec_status(encoder, cs, enabled) PARITY_LCD_GUARD(!(cs)->fec_enable, "intel_ddi_wait_for_fec_status (FEC)")
/* intel_dp_check_frl_training(): returns unless downstream_ports[2] has DP_PCON_SOURCE_CTL_MODE (a PCON) */
#define intel_dp_check_frl_training(intel_dp) PARITY_LCD_GUARD(!((intel_dp)->downstream_ports[2] & 0x20), "intel_dp_check_frl_training (PCON, DP_PCON_SOURCE_CTL_MODE = bit 5)")
/* intel_dp_pcon_dsc_configure(): returns unless the sink is an HDMI 2.1 PCON (intel_dp_is_hdmi_2_1_sink: the PCON caps) */
#define intel_dp_pcon_dsc_configure(intel_dp, cs) PARITY_LCD_GUARD(!drm_dp_is_branch((intel_dp)->dpcd), "intel_dp_pcon_dsc_configure (HDMI 2.1 PCON)")
/* skl_pfit_enable() / skl_scaler_disable(): nothing without the pipe scaler (pch_pfit.enabled / a scaler in use) */
#define skl_pfit_enable(cs) PARITY_LCD_GUARD(!(cs)->pch_pfit.enabled, "skl_pfit_enable (panel fitter)")
#define skl_scaler_disable(cs) PARITY_LCD_GUARD(!(cs)->pch_pfit.enabled, "skl_scaler_disable (panel fitter)")
/* intel_audio_sdp_split_update(): a register write only with HAS_DP20 */
#define intel_audio_sdp_split_update(cs) PARITY_LCD_GUARD(!HAS_DP20(SEQ_I915_CRTC_STATE(cs)), "intel_audio_sdp_split_update (DP 2.0)")
/* trans_port_sync_stop_link_train(): returns when crtc_state->sync_mode_slaves_mask is 0 */
#define trans_port_sync_stop_link_train(state, encoder, cs) PARITY_LCD_GUARD((cs)->sync_mode_slaves_mask == 0, "trans_port_sync_stop_link_train (port sync)")
/* intel_hdcp_enable() / _disable(): act only when the connector state asks for content protection */
#define intel_hdcp_enable(state, encoder, cs, conn) PARITY_LCD_GUARD((conn)->content_protection == 0, "intel_hdcp_enable (content protection requested)")
#define intel_hdcp_disable(connector) ((void)0)       /* returns at once when the connector has no HDCP shim (hdcp->shim == NULL): eDP registers none */
/* intel_tc_port_link_cancel_reset_work(): returns at once unless the PHY is Type-C */
#define intel_tc_port_link_cancel_reset_work(dig_port) PARITY_LCD_GUARD(!intel_phy_is_tc(SEQ_I915_ENCODER(&(dig_port)->base), intel_port_to_phy(SEQ_I915_ENCODER(&(dig_port)->base), (dig_port)->base.port)), "intel_tc_port_link_cancel_reset_work (Type-C port)")
/* colour: the LUT / CSC loaders are reached only with a LUT blob, a non-8-bit gamma mode or a CSC enable bit */
#define glk_load_degamma_lut(cs, blob) PARITY_LCD_GUARD((blob) == 0, "glk_load_degamma_lut (a degamma LUT)")
#define ilk_load_lut_8(cs, blob) PARITY_LCD_GUARD((blob) == 0, "ilk_load_lut_8 (a gamma LUT): returns at once without a blob")
#define icl_program_gamma_superfine_segment(cs) PARITY_LCD_GUARD(0, "icl_program_gamma_superfine_segment (12-bit multi-segment gamma)")
#define icl_program_gamma_multi_segment(cs) PARITY_LCD_GUARD(0, "icl_program_gamma_multi_segment (12-bit multi-segment gamma)")
#define ivb_load_lut_ext_max(cs) PARITY_LCD_GUARD(0, "ivb_load_lut_ext_max (10 / 12-bit gamma)")
#define glk_load_lut_ext2_max(cs) PARITY_LCD_GUARD(0, "glk_load_lut_ext2_max (10 / 12-bit gamma)")
#define bdw_load_lut_10(cs, blob, index) PARITY_LCD_GUARD(0, "bdw_load_lut_10 (10-bit gamma)")
#define PAL_PREC_INDEX_VALUE(x) (x)
#define ilk_update_pipe_csc(crtc, csc) PARITY_LCD_GUARD(0, "ilk_update_pipe_csc (a CTM)")
#define icl_update_output_csc(crtc, csc) PARITY_LCD_GUARD(0, "icl_update_output_csc (YCbCr output / limited range)")
#define intel_dsb_commit(dsb, wait) PARITY_LCD_GUARD(0, "intel_dsb_commit (a DSB)")
#define LEGACY_LUT_LENGTH 256
#define drm_color_lut_size(blob) ((int)((blob)->length / 8u))   /* sizeof(struct drm_color_lut): 4 x u16 */
/* intel_psr_disable(): returns when crtc_state->has_psr is false */
#define intel_psr_disable(intel_dp, cs) PARITY_LCD_GUARD(!(cs)->has_psr, "intel_psr_disable (PSR)")
/* drm_connector_update_privacy_screen(): returns when the connector has no privacy screen object */
#define drm_connector_update_privacy_screen(conn) ((void)0)
/* intel_write_dp_sdp(): returns unless the SDP type is set in crtc_state->infoframes.enable */
#define intel_write_dp_sdp(encoder, cs, type) PARITY_LCD_GUARD((cs)->infoframes.enable == 0, "intel_write_dp_sdp (an enabled SDP)")
#define HAS_DSC(i915) 1                               /* display version >= 11: only selects a bit to clear in intel_dp_set_infoframes */
#define DP_SDP_VSC 0x07
#define HDMI_PACKET_TYPE_GAMUT_METADATA 0x0a

/*
 * ---- DECIDED: callees that are deliberately not connected in the one-screen path.  Each leaves a
 * "(decided)" entry in the run log at the position the reference calls it, with the reason here.
 */
#define PARITY_LCD_DECIDED(i915, what) PARITY_LCD_STEP(i915, "(decided) " what)
/* intel_initial_watermarks(): calls display.funcs.wm->initial_watermarks, which skl_wm_funcs (display version 9+)
 * does not set: nothing happens there.  The watermarks are written by the plane update (skl_write_plane_wm). */
#define intel_initial_watermarks(state, crtc) ((void)0)
/* intel_set_cpu_fifo_underrun_reporting(): clears the sticky underrun bits of ICL_PIPESTATUS and unmasks the
 * pipe's underrun interrupt -- error REPORTING only.  The display interrupt is not wired in this path; the
 * LCD test reads the sticky bits itself after the observation window and logs them. */
#define intel_set_cpu_fifo_underrun_reporting(i915, pipe, enable) PARITY_LCD_DECIDED(i915, "intel_set_cpu_fifo_underrun_reporting: underrun IRQ not wired; PIPESTATUS is read by the test")
/* intel_crtc_vblank_on() / _off(): the DRM core's software vblank bookkeeping (drm_crtc_vblank_on / _off).  No
 * register is written there; nothing in this path waits on a DRM vblank event (frames are observed through the
 * pipe's frame counter / scanline). */
#define intel_crtc_vblank_on(cs) PARITY_LCD_DECIDED(SEQ_I915_CRTC_STATE(cs), "intel_crtc_vblank_on: DRM vblank bookkeeping, no hardware access")
#define intel_crtc_vblank_off(cs) PARITY_LCD_DECIDED(parity_lcd_cur_i915, "intel_crtc_vblank_off: DRM vblank bookkeeping, no hardware access")

/* ---- hsw_crtc_enable(): callees that are not ported ---- */
#define icl_ddi_bigjoiner_pre_enable(state, cs) PARITY_LCD_STEP(SEQ_I915_CRTC_STATE(cs), "icl_ddi_bigjoiner_pre_enable")
#define glk_pipe_scaler_clock_gating_wa(i915, pipe, enable) PARITY_LCD_STEP(i915, "glk_pipe_scaler_clock_gating_wa")
#define ilk_pfit_enable(cs) PARITY_LCD_STEP(SEQ_I915_CRTC_STATE(cs), "ilk_pfit_enable")
#define intel_disable_primary_plane(cs) PARITY_LCD_STEP(SEQ_I915_CRTC_STATE(cs), "intel_disable_primary_plane")
#define intel_crtc_wait_for_next_vblank(crtc) PARITY_LCD_STEP(to_i915((crtc)->base.dev), "intel_crtc_wait_for_next_vblank")
#define intel_crtc_for_pipe(i915, pipe) ((struct intel_crtc *)0)   /* Haswell workaround only */

/* the encoder hooks are called through these (zedBSD; the reference iterates the atomic state's connectors) */
void intel_encoders_pre_pll_enable(struct intel_atomic_state *state, struct intel_crtc *crtc);
void intel_encoders_pre_enable(struct intel_atomic_state *state, struct intel_crtc *crtc);
void intel_encoders_enable(struct intel_atomic_state *state, struct intel_crtc *crtc);

/* ---- the DDI callers: callees that are not ported ---- */
#define intel_tc_port_get_link(dig_port, lanes) PARITY_LCD_STEP(SEQ_I915_ENCODER(&(dig_port)->base), "intel_tc_port_get_link")
#define intel_ddi_update_active_dpll(state, encoder, crtc) PARITY_LCD_STEP(SEQ_I915_ENCODER(encoder), "intel_ddi_update_active_dpll")
#define intel_tc_port_set_fia_lane_count(dig_port, lanes) PARITY_LCD_STEP(SEQ_I915_ENCODER(&(dig_port)->base), "intel_tc_port_set_fia_lane_count")
#define bxt_ddi_phy_set_lane_optim_mask(encoder, mask) PARITY_LCD_STEP(SEQ_I915_ENCODER(encoder), "bxt_ddi_phy_set_lane_optim_mask")
#define intel_ddi_pre_enable_hdmi(state, encoder, cs, conn) PARITY_LCD_STEP(SEQ_I915_ENCODER(encoder), "intel_ddi_pre_enable_hdmi")
#define intel_enable_ddi_hdmi(state, encoder, cs, conn) PARITY_LCD_STEP(SEQ_I915_ENCODER(encoder), "intel_enable_ddi_hdmi")
#define intel_dp_has_hdmi_sink(intel_dp) (0)
#define intel_dp_128b132b_sdp_crc16(intel_dp, cs) PARITY_LCD_STEP(SEQ_I915_CRTC_STATE(cs), "intel_dp_128b132b_sdp_crc16")
#define PANEL_REPLAY_CONFIG 0
#define DP_PANEL_REPLAY_ENABLE 0
#define mtl_ddi_pre_enable_dp(state, encoder, cs, conn) PARITY_LCD_STEP(SEQ_I915_ENCODER(encoder), "mtl_ddi_pre_enable_dp")
#define hsw_ddi_pre_enable_dp(state, encoder, cs, conn) PARITY_LCD_STEP(SEQ_I915_ENCODER(encoder), "hsw_ddi_pre_enable_dp")
#define intel_ddi_config_transcoder_dp2(encoder, cs) PARITY_LCD_STEP(SEQ_I915_ENCODER(encoder), "intel_ddi_config_transcoder_dp2")
#define to_intel_connector(c) (c)

/* ---- the disable side: callees that are not ported ---- */
void intel_encoders_disable(struct intel_atomic_state *state, struct intel_crtc *crtc);
void intel_encoders_post_disable(struct intel_atomic_state *state, struct intel_crtc *crtc);
void intel_encoders_post_pll_disable(struct intel_atomic_state *state, struct intel_crtc *crtc);
#define intel_ddi_post_disable_hdmi(state, encoder, cs, conn) PARITY_LCD_STEP(parity_lcd_cur_i915, "intel_ddi_post_disable_hdmi")
#define intel_disable_ddi_hdmi(state, encoder, cs, conn) PARITY_LCD_STEP(parity_lcd_cur_i915, "intel_disable_ddi_hdmi")
#define ilk_pfit_disable(cs) PARITY_LCD_STEP(parity_lcd_cur_i915, "ilk_pfit_disable")
#define mtl_disable_ddi_buf(encoder, cs) PARITY_LCD_STEP(parity_lcd_cur_i915, "mtl_disable_ddi_buf")
#define adlp_tbt_to_dp_alt_switch_wa(encoder) PARITY_LCD_STEP(parity_lcd_cur_i915, "adlp_tbt_to_dp_alt_switch_wa")
#define intel_tc_port_put_link(dig_port) PARITY_LCD_STEP(parity_lcd_cur_i915, "intel_tc_port_put_link")
#define intel_crtc_max_vblank_count(cs) (0xffffffffu)             /* the hardware frame counter's range; used by a vblank-layer check only */


#endif /* PARITY_LCD_SEQ_COMPAT_H */
