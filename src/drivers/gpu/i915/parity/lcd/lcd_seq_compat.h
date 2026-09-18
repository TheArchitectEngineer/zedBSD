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
struct drm_i915_private *parity_lcd_seq_dp_i915(const struct intel_dp *intel_dp);

/* ---- hsw_crtc_enable(): its state accessors ---- */
struct intel_atomic_state { struct { struct drm_device *dev; } base; const struct intel_crtc_state *crtc_state; };
#define intel_atomic_get_new_crtc_state(state, crtc) ((state)->crtc_state)
#define intel_crtc_is_bigjoiner_slave(crtc_state) (0)          /* no big joiner in this configuration */
#define IS_BROADWELL(i915) 0
#define IS_GEMINILAKE(i915) 0
#define IS_BROXTON(i915) 0
#define HAS_DP20(i915) 0                                       /* IS_DG2 || display version >= 14 */
#define is_trans_port_sync_mode(crtc_state) (0)                /* no port sync: master_transcoder is INVALID and no slaves */

/* ---- hsw_crtc_enable(): callees that are not ported ---- */
#define intel_dmc_enable_pipe(i915, pipe) PARITY_LCD_STEP(i915, "intel_dmc_enable_pipe")
#define icl_ddi_bigjoiner_pre_enable(state, cs) PARITY_LCD_STEP(SEQ_I915_CRTC_STATE(cs), "icl_ddi_bigjoiner_pre_enable")
#define intel_enable_shared_dpll(cs) PARITY_LCD_STEP(SEQ_I915_CRTC_STATE(cs), "intel_enable_shared_dpll")
#define intel_dsc_enable(cs) PARITY_LCD_STEP(SEQ_I915_CRTC_STATE(cs), "intel_dsc_enable")
#define intel_uncompressed_joiner_enable(cs) PARITY_LCD_STEP(SEQ_I915_CRTC_STATE(cs), "intel_uncompressed_joiner_enable")
#define bdw_set_pipe_misc(cs) PARITY_LCD_STEP(SEQ_I915_CRTC_STATE(cs), "bdw_set_pipe_misc")
#define glk_pipe_scaler_clock_gating_wa(i915, pipe, enable) PARITY_LCD_STEP(i915, "glk_pipe_scaler_clock_gating_wa")
#define skl_pfit_enable(cs) PARITY_LCD_STEP(SEQ_I915_CRTC_STATE(cs), "skl_pfit_enable")
#define ilk_pfit_enable(cs) PARITY_LCD_STEP(SEQ_I915_CRTC_STATE(cs), "ilk_pfit_enable")
#define intel_color_load_luts(cs) PARITY_LCD_STEP(SEQ_I915_CRTC_STATE(cs), "intel_color_load_luts")
#define intel_color_commit_noarm(cs) PARITY_LCD_STEP(SEQ_I915_CRTC_STATE(cs), "intel_color_commit_noarm")
#define intel_color_commit_arm(cs) PARITY_LCD_STEP(SEQ_I915_CRTC_STATE(cs), "intel_color_commit_arm")
#define intel_disable_primary_plane(cs) PARITY_LCD_STEP(SEQ_I915_CRTC_STATE(cs), "intel_disable_primary_plane")
#define hsw_set_linetime_wm(cs) PARITY_LCD_STEP(SEQ_I915_CRTC_STATE(cs), "hsw_set_linetime_wm")
#define icl_set_pipe_chicken(cs) PARITY_LCD_STEP(SEQ_I915_CRTC_STATE(cs), "icl_set_pipe_chicken")
#define intel_initial_watermarks(state, crtc) PARITY_LCD_STEP(to_i915((crtc)->base.dev), "intel_initial_watermarks")
#define intel_crtc_vblank_on(cs) PARITY_LCD_STEP(SEQ_I915_CRTC_STATE(cs), "intel_crtc_vblank_on")
#define intel_crtc_wait_for_next_vblank(crtc) PARITY_LCD_STEP(to_i915((crtc)->base.dev), "intel_crtc_wait_for_next_vblank")
#define intel_crtc_for_pipe(i915, pipe) ((struct intel_crtc *)0)   /* Haswell workaround only */

/* the encoder hooks are called through these (zedBSD; the reference iterates the atomic state's connectors) */
void intel_encoders_pre_pll_enable(struct intel_atomic_state *state, struct intel_crtc *crtc);
void intel_encoders_pre_enable(struct intel_atomic_state *state, struct intel_crtc *crtc);
void intel_encoders_enable(struct intel_atomic_state *state, struct intel_crtc *crtc);

/* ---- the DDI callers: callees that are not ported ---- */
#define intel_tc_port_get_link(dig_port, lanes) PARITY_LCD_STEP(SEQ_I915_ENCODER(&(dig_port)->base), "intel_tc_port_get_link")
#define intel_ddi_update_active_dpll(state, encoder, crtc) PARITY_LCD_STEP(SEQ_I915_ENCODER(encoder), "intel_ddi_update_active_dpll")
#define main_link_aux_power_domain_get(dig_port, cs) PARITY_LCD_STEP(SEQ_I915_ENCODER(&(dig_port)->base), "main_link_aux_power_domain_get")
#define intel_tc_port_set_fia_lane_count(dig_port, lanes) PARITY_LCD_STEP(SEQ_I915_ENCODER(&(dig_port)->base), "intel_tc_port_set_fia_lane_count")
#define bxt_ddi_phy_set_lane_optim_mask(encoder, mask) PARITY_LCD_STEP(SEQ_I915_ENCODER(encoder), "bxt_ddi_phy_set_lane_optim_mask")
#define intel_set_cpu_fifo_underrun_reporting(i915, pipe, enable) PARITY_LCD_STEP(i915, "intel_set_cpu_fifo_underrun_reporting")
#define intel_ddi_pre_enable_hdmi(state, encoder, cs, conn) PARITY_LCD_STEP(SEQ_I915_ENCODER(encoder), "intel_ddi_pre_enable_hdmi")
#define intel_enable_ddi_hdmi(state, encoder, cs, conn) PARITY_LCD_STEP(SEQ_I915_ENCODER(encoder), "intel_enable_ddi_hdmi")
#define intel_dp_has_hdmi_sink(intel_dp) (0)
#define intel_dp_128b132b_sdp_crc16(intel_dp, cs) PARITY_LCD_STEP(SEQ_I915_CRTC_STATE(cs), "intel_dp_128b132b_sdp_crc16")
#define drm_dp_dpcd_writeb(aux, reg, value) (0)                   /* only reached with HAS_DP20 */
#define PANEL_REPLAY_CONFIG 0
#define DP_PANEL_REPLAY_ENABLE 0
#define mtl_ddi_pre_enable_dp(state, encoder, cs, conn) PARITY_LCD_STEP(SEQ_I915_ENCODER(encoder), "mtl_ddi_pre_enable_dp")
#define hsw_ddi_pre_enable_dp(state, encoder, cs, conn) PARITY_LCD_STEP(SEQ_I915_ENCODER(encoder), "hsw_ddi_pre_enable_dp")
#define intel_dp_set_link_params(intel_dp, rate, lanes) PARITY_LCD_STEP(parity_lcd_seq_dp_i915(intel_dp), "intel_dp_set_link_params")
#define intel_pps_on(intel_dp) PARITY_LCD_STEP(parity_lcd_seq_dp_i915(intel_dp), "intel_pps_on")
#define intel_ddi_enable_clock(encoder, cs) PARITY_LCD_STEP(SEQ_I915_ENCODER(encoder), "intel_ddi_enable_clock")
#define intel_display_power_get(i915, domain) (PARITY_LCD_STEP(i915, "intel_display_power_get(ddi_io_power_domain)"), 1)
#define icl_program_mg_dp_mode(dig_port, cs) PARITY_LCD_STEP(SEQ_I915_ENCODER(&(dig_port)->base), "icl_program_mg_dp_mode")
#define intel_ddi_enable_transcoder_clock(encoder, cs) PARITY_LCD_STEP(SEQ_I915_ENCODER(encoder), "intel_ddi_enable_transcoder_clock")
#define intel_ddi_config_transcoder_dp2(encoder, cs) PARITY_LCD_STEP(SEQ_I915_ENCODER(encoder), "intel_ddi_config_transcoder_dp2")
#define intel_ddi_power_up_lanes(encoder, cs) PARITY_LCD_STEP(SEQ_I915_ENCODER(encoder), "intel_ddi_power_up_lanes")
#define intel_ddi_mso_configure(cs) PARITY_LCD_STEP(SEQ_I915_CRTC_STATE(cs), "intel_ddi_mso_configure")
#define DP_SET_POWER_D0 0x1
#define intel_dp_set_power(intel_dp, mode) PARITY_LCD_STEP(parity_lcd_seq_dp_i915(intel_dp), "intel_dp_set_power(D0)")
#define intel_dp_configure_protocol_converter(intel_dp, cs) PARITY_LCD_STEP(parity_lcd_seq_dp_i915(intel_dp), "intel_dp_configure_protocol_converter")
#define to_intel_connector(c) (c)
#define intel_dp_sink_enable_decompression(state, connector, cs) PARITY_LCD_STEP(SEQ_I915_CRTC_STATE(cs), "intel_dp_sink_enable_decompression")
#define intel_dp_sink_set_fec_ready(intel_dp, cs, enable) PARITY_LCD_STEP(parity_lcd_seq_dp_i915(intel_dp), "intel_dp_sink_set_fec_ready")
#define intel_dp_check_frl_training(intel_dp) PARITY_LCD_STEP(parity_lcd_seq_dp_i915(intel_dp), "intel_dp_check_frl_training")
#define intel_dp_pcon_dsc_configure(intel_dp, cs) PARITY_LCD_STEP(parity_lcd_seq_dp_i915(intel_dp), "intel_dp_pcon_dsc_configure")
#define intel_dp_start_link_train(intel_dp, cs) PARITY_LCD_STEP(parity_lcd_seq_dp_i915(intel_dp), "intel_dp_start_link_train")
#define intel_dp_stop_link_train(intel_dp, cs) PARITY_LCD_STEP(parity_lcd_seq_dp_i915(intel_dp), "intel_dp_stop_link_train")
#define intel_ddi_enable_fec(encoder, cs) PARITY_LCD_STEP(SEQ_I915_ENCODER(encoder), "intel_ddi_enable_fec")
#define intel_dsc_dp_pps_write(encoder, cs) PARITY_LCD_STEP(SEQ_I915_ENCODER(encoder), "intel_dsc_dp_pps_write")
#define intel_audio_sdp_split_update(cs) PARITY_LCD_STEP(SEQ_I915_CRTC_STATE(cs), "intel_audio_sdp_split_update")
#define intel_enable_transcoder(cs) PARITY_LCD_STEP(SEQ_I915_CRTC_STATE(cs), "intel_enable_transcoder")
#define intel_ddi_wait_for_fec_status(encoder, cs, enabled) PARITY_LCD_STEP(SEQ_I915_ENCODER(encoder), "intel_ddi_wait_for_fec_status")
#define intel_hdcp_enable(state, encoder, cs, conn) PARITY_LCD_STEP(SEQ_I915_ENCODER(encoder), "intel_hdcp_enable")
#define drm_connector_update_privacy_screen(conn) PARITY_LCD_STEP(SEQ_I915_ENCODER(encoder), "drm_connector_update_privacy_screen")
#define intel_edp_backlight_on(cs, conn) PARITY_LCD_STEP(SEQ_I915_CRTC_STATE(cs), "intel_edp_backlight_on")
#define intel_dp_set_infoframes(encoder, enable, cs, conn) PARITY_LCD_STEP(SEQ_I915_ENCODER(encoder), "intel_dp_set_infoframes")
#define trans_port_sync_stop_link_train(state, encoder, cs) PARITY_LCD_STEP(SEQ_I915_ENCODER(encoder), "trans_port_sync_stop_link_train")

#endif /* PARITY_LCD_SEQ_COMPAT_H */
