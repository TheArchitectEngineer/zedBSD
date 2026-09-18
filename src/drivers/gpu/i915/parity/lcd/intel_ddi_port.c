/*
 * Copyright © 2012 Intel Corporation
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
 *
 * Authors:
 *    Eugeni Dodonov <eugeni.dodonov@intel.com>
 *
 */

/*
 * zedBSD WS031: generated from Linux v6.8.12 drivers/gpu/drm/i915/display/intel_ddi.c
 * (sha256 6a40473be6b095d154b6d81ecd90b7a3ec3d9f4f5c3b96d0680b3f5a56d57f23) by plan/ws031/handover/tools/port_lcd_calc.py.
 * The function bodies are the reference text.  Kept / changed:
 *  - kept: ddi_buf_phy_link_rate, intel_ddi_init_dp_buf_reg, intel_ddi_set_dp_msa,
 *    bdw_trans_port_sync_master_select, intel_ddi_transcoder_func_reg_val_get,
 *    intel_ddi_enable_transcoder_func, intel_ddi_config_transcoder_func, hsw_chicken_trans_reg, and the DP
 *    enable-sequence callers tgl_ddi_pre_enable_dp, intel_ddi_pre_enable_dp, intel_ddi_pre_enable,
 *    intel_enable_ddi_dp, intel_enable_ddi, intel_ddi_pre_pll_enable (their unported callees are named steps,
 *    lcd_seq_compat.h);
 *  - the includes are replaced by lcd_compat.h (register writes go through its emit hook);
 *  - parity_ddi_emit_glue.inc (zedBSD code) is included at the end of the file.
 */

#include "lcd_compat.h"	/* zedBSD: replaces the linux/, drm/ and i915 includes */
#include "lcd_trans_regs.h"
#include "lcd_ddi_regs.h"
#include "lcd_dp_msa.h"
#include "lcd_seq_compat.h"

static u32 ddi_buf_phy_link_rate(int port_clock)
{
	switch (port_clock) {
	case 162000:
		return DDI_BUF_PHY_LINK_RATE(0);
	case 216000:
		return DDI_BUF_PHY_LINK_RATE(4);
	case 243000:
		return DDI_BUF_PHY_LINK_RATE(5);
	case 270000:
		return DDI_BUF_PHY_LINK_RATE(1);
	case 324000:
		return DDI_BUF_PHY_LINK_RATE(6);
	case 432000:
		return DDI_BUF_PHY_LINK_RATE(7);
	case 540000:
		return DDI_BUF_PHY_LINK_RATE(2);
	case 810000:
		return DDI_BUF_PHY_LINK_RATE(3);
	default:
		MISSING_CASE(port_clock);
		return DDI_BUF_PHY_LINK_RATE(0);
	}
}

static void intel_ddi_init_dp_buf_reg(struct intel_encoder *encoder,
				      const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	struct intel_dp *intel_dp = enc_to_intel_dp(encoder);
	struct intel_digital_port *dig_port = enc_to_dig_port(encoder);
	enum phy phy = intel_port_to_phy(i915, encoder->port);

	/* DDI_BUF_CTL_ENABLE will be set by intel_ddi_prepare_link_retrain() later */
	intel_dp->DP = dig_port->saved_port_bits |
		DDI_PORT_WIDTH(crtc_state->lane_count) |
		DDI_BUF_TRANS_SELECT(0);

	if (DISPLAY_VER(i915) >= 14) {
		if (intel_dp_is_uhbr(crtc_state))
			intel_dp->DP |= DDI_BUF_PORT_DATA_40BIT;
		else
			intel_dp->DP |= DDI_BUF_PORT_DATA_10BIT;
	}

	if (IS_ALDERLAKE_P(i915) && intel_phy_is_tc(i915, phy)) {
		intel_dp->DP |= ddi_buf_phy_link_rate(crtc_state->port_clock);
		if (!intel_tc_port_in_tbt_alt_mode(dig_port))
			intel_dp->DP |= DDI_BUF_CTL_TC_PHY_OWNERSHIP;
	}
}

void intel_ddi_set_dp_msa(const struct intel_crtc_state *crtc_state,
			  const struct drm_connector_state *conn_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	enum transcoder cpu_transcoder = crtc_state->cpu_transcoder;
	u32 temp;

	if (!intel_crtc_has_dp_encoder(crtc_state))
		return;

	drm_WARN_ON(&dev_priv->drm, transcoder_is_dsi(cpu_transcoder));

	temp = DP_MSA_MISC_SYNC_CLOCK;

	switch (crtc_state->pipe_bpp) {
	case 18:
		temp |= DP_MSA_MISC_6_BPC;
		break;
	case 24:
		temp |= DP_MSA_MISC_8_BPC;
		break;
	case 30:
		temp |= DP_MSA_MISC_10_BPC;
		break;
	case 36:
		temp |= DP_MSA_MISC_12_BPC;
		break;
	default:
		MISSING_CASE(crtc_state->pipe_bpp);
		break;
	}

	/* nonsense combination */
	drm_WARN_ON(&dev_priv->drm, crtc_state->limited_color_range &&
		    crtc_state->output_format != INTEL_OUTPUT_FORMAT_RGB);

	if (crtc_state->limited_color_range)
		temp |= DP_MSA_MISC_COLOR_CEA_RGB;

	/*
	 * As per DP 1.2 spec section 2.3.4.3 while sending
	 * YCBCR 444 signals we should program MSA MISC1/0 fields with
	 * colorspace information.
	 */
	if (crtc_state->output_format == INTEL_OUTPUT_FORMAT_YCBCR444)
		temp |= DP_MSA_MISC_COLOR_YCBCR_444_BT709;

	/*
	 * As per DP 1.4a spec section 2.2.4.3 [MSA Field for Indication
	 * of Color Encoding Format and Content Color Gamut] while sending
	 * YCBCR 420, HDR BT.2020 signals we should program MSA MISC1 fields
	 * which indicate VSC SDP for the Pixel Encoding/Colorimetry Format.
	 */
	if (intel_dp_needs_vsc_sdp(crtc_state, conn_state))
		temp |= DP_MSA_MISC_COLOR_VSC_SDP;

	intel_de_write(dev_priv, TRANS_MSA_MISC(cpu_transcoder), temp);
}

static u32 bdw_trans_port_sync_master_select(enum transcoder master_transcoder)
{
	if (master_transcoder == TRANSCODER_EDP)
		return 0;
	else
		return master_transcoder + 1;
}

/*
 * Returns the TRANS_DDI_FUNC_CTL value based on CRTC state.
 *
 * Only intended to be used by intel_ddi_enable_transcoder_func() and
 * intel_ddi_config_transcoder_func().
 */
static u32
intel_ddi_transcoder_func_reg_val_get(struct intel_encoder *encoder,
				      const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	enum pipe pipe = crtc->pipe;
	enum transcoder cpu_transcoder = crtc_state->cpu_transcoder;
	enum port port = encoder->port;
	u32 temp;

	/* Enable TRANS_DDI_FUNC_CTL for the pipe to work in HDMI mode */
	temp = TRANS_DDI_FUNC_ENABLE;
	if (DISPLAY_VER(dev_priv) >= 12)
		temp |= TGL_TRANS_DDI_SELECT_PORT(port);
	else
		temp |= TRANS_DDI_SELECT_PORT(port);

	switch (crtc_state->pipe_bpp) {
	default:
		MISSING_CASE(crtc_state->pipe_bpp);
		fallthrough;
	case 18:
		temp |= TRANS_DDI_BPC_6;
		break;
	case 24:
		temp |= TRANS_DDI_BPC_8;
		break;
	case 30:
		temp |= TRANS_DDI_BPC_10;
		break;
	case 36:
		temp |= TRANS_DDI_BPC_12;
		break;
	}

	if (crtc_state->hw.adjusted_mode.flags & DRM_MODE_FLAG_PVSYNC)
		temp |= TRANS_DDI_PVSYNC;
	if (crtc_state->hw.adjusted_mode.flags & DRM_MODE_FLAG_PHSYNC)
		temp |= TRANS_DDI_PHSYNC;

	if (cpu_transcoder == TRANSCODER_EDP) {
		switch (pipe) {
		default:
			MISSING_CASE(pipe);
			fallthrough;
		case PIPE_A:
			/* On Haswell, can only use the always-on power well for
			 * eDP when not using the panel fitter, and when not
			 * using motion blur mitigation (which we don't
			 * support). */
			if (crtc_state->pch_pfit.force_thru)
				temp |= TRANS_DDI_EDP_INPUT_A_ONOFF;
			else
				temp |= TRANS_DDI_EDP_INPUT_A_ON;
			break;
		case PIPE_B:
			temp |= TRANS_DDI_EDP_INPUT_B_ONOFF;
			break;
		case PIPE_C:
			temp |= TRANS_DDI_EDP_INPUT_C_ONOFF;
			break;
		}
	}

	if (intel_crtc_has_type(crtc_state, INTEL_OUTPUT_HDMI)) {
		if (crtc_state->has_hdmi_sink)
			temp |= TRANS_DDI_MODE_SELECT_HDMI;
		else
			temp |= TRANS_DDI_MODE_SELECT_DVI;

		if (crtc_state->hdmi_scrambling)
			temp |= TRANS_DDI_HDMI_SCRAMBLING;
		if (crtc_state->hdmi_high_tmds_clock_ratio)
			temp |= TRANS_DDI_HIGH_TMDS_CHAR_RATE;
		if (DISPLAY_VER(dev_priv) >= 14)
			temp |= TRANS_DDI_PORT_WIDTH(crtc_state->lane_count);
	} else if (intel_crtc_has_type(crtc_state, INTEL_OUTPUT_ANALOG)) {
		temp |= TRANS_DDI_MODE_SELECT_FDI_OR_128B132B;
		temp |= (crtc_state->fdi_lanes - 1) << 1;
	} else if (intel_crtc_has_type(crtc_state, INTEL_OUTPUT_DP_MST)) {
		if (intel_dp_is_uhbr(crtc_state))
			temp |= TRANS_DDI_MODE_SELECT_FDI_OR_128B132B;
		else
			temp |= TRANS_DDI_MODE_SELECT_DP_MST;
		temp |= DDI_PORT_WIDTH(crtc_state->lane_count);

		if (DISPLAY_VER(dev_priv) >= 12) {
			enum transcoder master;

			master = crtc_state->mst_master_transcoder;
			drm_WARN_ON(&dev_priv->drm,
				    master == INVALID_TRANSCODER);
			temp |= TRANS_DDI_MST_TRANSPORT_SELECT(master);
		}
	} else {
		temp |= TRANS_DDI_MODE_SELECT_DP_SST;
		temp |= DDI_PORT_WIDTH(crtc_state->lane_count);
	}

	if (IS_DISPLAY_VER(dev_priv, 8, 10) &&
	    crtc_state->master_transcoder != INVALID_TRANSCODER) {
		u8 master_select =
			bdw_trans_port_sync_master_select(crtc_state->master_transcoder);

		temp |= TRANS_DDI_PORT_SYNC_ENABLE |
			TRANS_DDI_PORT_SYNC_MASTER_SELECT(master_select);
	}

	return temp;
}

void intel_ddi_enable_transcoder_func(struct intel_encoder *encoder,
				      const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	enum transcoder cpu_transcoder = crtc_state->cpu_transcoder;

	if (DISPLAY_VER(dev_priv) >= 11) {
		enum transcoder master_transcoder = crtc_state->master_transcoder;
		u32 ctl2 = 0;

		if (master_transcoder != INVALID_TRANSCODER) {
			u8 master_select =
				bdw_trans_port_sync_master_select(master_transcoder);

			ctl2 |= PORT_SYNC_MODE_ENABLE |
				PORT_SYNC_MODE_MASTER_SELECT(master_select);
		}

		intel_de_write(dev_priv,
			       TRANS_DDI_FUNC_CTL2(cpu_transcoder), ctl2);
	}

	intel_de_write(dev_priv, TRANS_DDI_FUNC_CTL(cpu_transcoder),
		       intel_ddi_transcoder_func_reg_val_get(encoder,
							     crtc_state));
}

/*
 * Same as intel_ddi_enable_transcoder_func(), but it does not set the enable
 * bit.
 */
static void
intel_ddi_config_transcoder_func(struct intel_encoder *encoder,
				 const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	enum transcoder cpu_transcoder = crtc_state->cpu_transcoder;
	u32 ctl;

	ctl = intel_ddi_transcoder_func_reg_val_get(encoder, crtc_state);
	ctl &= ~TRANS_DDI_FUNC_ENABLE;
	intel_de_write(dev_priv, TRANS_DDI_FUNC_CTL(cpu_transcoder), ctl);
}

/* FIXME bad home for this function */
i915_reg_t hsw_chicken_trans_reg(struct drm_i915_private *i915,
				 enum transcoder cpu_transcoder)
{
	return DISPLAY_VER(i915) >= 14 ?
		MTL_CHICKEN_TRANS(cpu_transcoder) :
		CHICKEN_TRANS(cpu_transcoder);
}

static void tgl_ddi_pre_enable_dp(struct intel_atomic_state *state,
				  struct intel_encoder *encoder,
				  const struct intel_crtc_state *crtc_state,
				  const struct drm_connector_state *conn_state)
{
	struct intel_dp *intel_dp = enc_to_intel_dp(encoder);
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_digital_port *dig_port = enc_to_dig_port(encoder);
	bool is_mst = intel_crtc_has_type(crtc_state, INTEL_OUTPUT_DP_MST);

	intel_dp_set_link_params(intel_dp,
				 crtc_state->port_clock,
				 crtc_state->lane_count);

	/*
	 * We only configure what the register value will be here.  Actual
	 * enabling happens during link training farther down.
	 */
	intel_ddi_init_dp_buf_reg(encoder, crtc_state);

	/*
	 * 1. Enable Power Wells
	 *
	 * This was handled at the beginning of intel_atomic_commit_tail(),
	 * before we called down into this function.
	 */

	/* 2. Enable Panel Power if PPS is required */
	intel_pps_on(intel_dp);

	/*
	 * 3. For non-TBT Type-C ports, set FIA lane count
	 * (DFLEXDPSP.DPX4TXLATC)
	 *
	 * This was done before tgl_ddi_pre_enable_dp by
	 * hsw_crtc_enable()->intel_encoders_pre_pll_enable().
	 */

	/*
	 * 4. Enable the port PLL.
	 *
	 * The PLL enabling itself was already done before this function by
	 * hsw_crtc_enable()->intel_enable_shared_dpll().  We need only
	 * configure the PLL to port mapping here.
	 */
	intel_ddi_enable_clock(encoder, crtc_state);

	/* 5. If IO power is controlled through PWR_WELL_CTL, Enable IO Power */
	if (!intel_tc_port_in_tbt_alt_mode(dig_port)) {
		drm_WARN_ON(&dev_priv->drm, dig_port->ddi_io_wakeref);
		dig_port->ddi_io_wakeref = intel_display_power_get(dev_priv,
								   dig_port->ddi_io_power_domain);
	}

	/* 6. Program DP_MODE */
	icl_program_mg_dp_mode(dig_port, crtc_state);

	/*
	 * 7. The rest of the below are substeps under the bspec's "Enable and
	 * Train Display Port" step.  Note that steps that are specific to
	 * MST will be handled by intel_mst_pre_enable_dp() before/after it
	 * calls into this function.  Also intel_mst_pre_enable_dp() only calls
	 * us when active_mst_links==0, so any steps designated for "single
	 * stream or multi-stream master transcoder" can just be performed
	 * unconditionally here.
	 */

	/*
	 * 7.a Configure Transcoder Clock Select to direct the Port clock to the
	 * Transcoder.
	 */
	intel_ddi_enable_transcoder_clock(encoder, crtc_state);

	if (HAS_DP20(dev_priv))
		intel_ddi_config_transcoder_dp2(encoder, crtc_state);

	/*
	 * 7.b Configure TRANS_DDI_FUNC_CTL DDI Select, DDI Mode Select & MST
	 * Transport Select
	 */
	intel_ddi_config_transcoder_func(encoder, crtc_state);

	/*
	 * 7.c Configure & enable DP_TP_CTL with link training pattern 1
	 * selected
	 *
	 * This will be handled by the intel_dp_start_link_train() farther
	 * down this function.
	 */

	/* 7.e Configure voltage swing and related IO settings */
	encoder->set_signal_levels(encoder, crtc_state);

	/*
	 * 7.f Combo PHY: Configure PORT_CL_DW10 Static Power Down to power up
	 * the used lanes of the DDI.
	 */
	intel_ddi_power_up_lanes(encoder, crtc_state);

	/*
	 * 7.g Program CoG/MSO configuration bits in DSS_CTL1 if selected.
	 */
	intel_ddi_mso_configure(crtc_state);

	if (!is_mst)
		intel_dp_set_power(intel_dp, DP_SET_POWER_D0);

	intel_dp_configure_protocol_converter(intel_dp, crtc_state);
	if (!is_mst)
		intel_dp_sink_enable_decompression(state,
						   to_intel_connector(conn_state->connector),
						   crtc_state);
	/*
	 * DDI FEC: "anticipates enabling FEC encoding sets the FEC_READY bit
	 * in the FEC_CONFIGURATION register to 1 before initiating link
	 * training
	 */
	intel_dp_sink_set_fec_ready(intel_dp, crtc_state, true);

	intel_dp_check_frl_training(intel_dp);
	intel_dp_pcon_dsc_configure(intel_dp, crtc_state);

	/*
	 * 7.i Follow DisplayPort specification training sequence (see notes for
	 *     failure handling)
	 * 7.j If DisplayPort multi-stream - Set DP_TP_CTL link training to Idle
	 *     Pattern, wait for 5 idle patterns (DP_TP_STATUS Min_Idles_Sent)
	 *     (timeout after 800 us)
	 */
	intel_dp_start_link_train(intel_dp, crtc_state);

	/* 7.k Set DP_TP_CTL link training to Normal */
	if (!is_trans_port_sync_mode(crtc_state))
		intel_dp_stop_link_train(intel_dp, crtc_state);

	/* 7.l Configure and enable FEC if needed */
	intel_ddi_enable_fec(encoder, crtc_state);

	if (!is_mst)
		intel_dsc_dp_pps_write(encoder, crtc_state);
}

static void intel_ddi_pre_enable_dp(struct intel_atomic_state *state,
				    struct intel_encoder *encoder,
				    const struct intel_crtc_state *crtc_state,
				    const struct drm_connector_state *conn_state)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_dp *intel_dp = enc_to_intel_dp(encoder);

	if (HAS_DP20(dev_priv)) {
		intel_dp_128b132b_sdp_crc16(enc_to_intel_dp(encoder),
					    crtc_state);
		if (crtc_state->has_panel_replay)
			drm_dp_dpcd_writeb(&intel_dp->aux, PANEL_REPLAY_CONFIG,
					   DP_PANEL_REPLAY_ENABLE);
	}

	if (DISPLAY_VER(dev_priv) >= 14)
		mtl_ddi_pre_enable_dp(state, encoder, crtc_state, conn_state);
	else if (DISPLAY_VER(dev_priv) >= 12)
		tgl_ddi_pre_enable_dp(state, encoder, crtc_state, conn_state);
	else
		hsw_ddi_pre_enable_dp(state, encoder, crtc_state, conn_state);

	/* MST will call a setting of MSA after an allocating of Virtual Channel
	 * from MST encoder pre_enable callback.
	 */
	if (!intel_crtc_has_type(crtc_state, INTEL_OUTPUT_DP_MST))
		intel_ddi_set_dp_msa(crtc_state, conn_state);
}

static void intel_ddi_pre_enable(struct intel_atomic_state *state,
				 struct intel_encoder *encoder,
				 const struct intel_crtc_state *crtc_state,
				 const struct drm_connector_state *conn_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	enum pipe pipe = crtc->pipe;

	/*
	 * When called from DP MST code:
	 * - conn_state will be NULL
	 * - encoder will be the main encoder (ie. mst->primary)
	 * - the main connector associated with this port
	 *   won't be active or linked to a crtc
	 * - crtc_state will be the state of the first stream to
	 *   be activated on this port, and it may not be the same
	 *   stream that will be deactivated last, but each stream
	 *   should have a state that is identical when it comes to
	 *   the DP link parameteres
	 */

	drm_WARN_ON(&dev_priv->drm, crtc_state->has_pch_encoder);

	intel_set_cpu_fifo_underrun_reporting(dev_priv, pipe, true);

	if (intel_crtc_has_type(crtc_state, INTEL_OUTPUT_HDMI)) {
		intel_ddi_pre_enable_hdmi(state, encoder, crtc_state,
					  conn_state);
	} else {
		struct intel_digital_port *dig_port = enc_to_dig_port(encoder);

		intel_ddi_pre_enable_dp(state, encoder, crtc_state,
					conn_state);

		/* FIXME precompute everything properly */
		/* FIXME how do we turn infoframes off again? */
		if (dig_port->lspcon.active && intel_dp_has_hdmi_sink(&dig_port->dp))
			dig_port->set_infoframes(encoder,
						 crtc_state->has_infoframe,
						 crtc_state, conn_state);
	}
}

static void intel_enable_ddi_dp(struct intel_atomic_state *state,
				struct intel_encoder *encoder,
				const struct intel_crtc_state *crtc_state,
				const struct drm_connector_state *conn_state)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_dp *intel_dp = enc_to_intel_dp(encoder);
	struct intel_digital_port *dig_port = enc_to_dig_port(encoder);
	enum port port = encoder->port;

	if (port == PORT_A && DISPLAY_VER(dev_priv) < 9)
		intel_dp_stop_link_train(intel_dp, crtc_state);

	drm_connector_update_privacy_screen(conn_state);
	intel_edp_backlight_on(crtc_state, conn_state);

	if (!dig_port->lspcon.active || intel_dp_has_hdmi_sink(&dig_port->dp))
		intel_dp_set_infoframes(encoder, true, crtc_state, conn_state);

	trans_port_sync_stop_link_train(state, encoder, crtc_state);
}

static void intel_enable_ddi(struct intel_atomic_state *state,
			     struct intel_encoder *encoder,
			     const struct intel_crtc_state *crtc_state,
			     const struct drm_connector_state *conn_state)
{
	drm_WARN_ON(state->base.dev, crtc_state->has_pch_encoder);

	if (!intel_crtc_is_bigjoiner_slave(crtc_state))
		intel_ddi_enable_transcoder_func(encoder, crtc_state);

	/* Enable/Disable DP2.0 SDP split config before transcoder */
	intel_audio_sdp_split_update(crtc_state);

	intel_enable_transcoder(crtc_state);

	intel_ddi_wait_for_fec_status(encoder, crtc_state, true);

	intel_crtc_vblank_on(crtc_state);

	if (intel_crtc_has_type(crtc_state, INTEL_OUTPUT_HDMI))
		intel_enable_ddi_hdmi(state, encoder, crtc_state, conn_state);
	else
		intel_enable_ddi_dp(state, encoder, crtc_state, conn_state);

	intel_hdcp_enable(state, encoder, crtc_state, conn_state);

}

static void
intel_ddi_pre_pll_enable(struct intel_atomic_state *state,
			 struct intel_encoder *encoder,
			 const struct intel_crtc_state *crtc_state,
			 const struct drm_connector_state *conn_state)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_digital_port *dig_port = enc_to_dig_port(encoder);
	enum phy phy = intel_port_to_phy(dev_priv, encoder->port);
	bool is_tc_port = intel_phy_is_tc(dev_priv, phy);

	if (is_tc_port) {
		struct intel_crtc *master_crtc =
			to_intel_crtc(crtc_state->uapi.crtc);

		intel_tc_port_get_link(dig_port, crtc_state->lane_count);
		intel_ddi_update_active_dpll(state, encoder, master_crtc);
	}

	main_link_aux_power_domain_get(dig_port, crtc_state);

	if (is_tc_port && !intel_tc_port_in_tbt_alt_mode(dig_port))
		/*
		 * Program the lane count for static/dynamic connections on
		 * Type-C ports.  Skip this step for TBT.
		 */
		intel_tc_port_set_fia_lane_count(dig_port, crtc_state->lane_count);
	else if (IS_GEMINILAKE(dev_priv) || IS_BROXTON(dev_priv))
		bxt_ddi_phy_set_lane_optim_mask(encoder,
						crtc_state->lane_lat_optim_mask);
}


#include "parity_ddi_emit_glue.inc"	/* zedBSD: builds the encoder / crtc state and records the words */
