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
#include "lcd_modeset_compat.h"
#include "lcd_dp_compat.h"

/* zedBSD: forward declarations of the static functions of this file (generated; the keep-list is not in call order) */
static u32 ddi_buf_phy_link_rate(int port_clock);
static void intel_ddi_init_dp_buf_reg(struct intel_encoder *encoder,
				      const struct intel_crtc_state *crtc_state);
static u32 bdw_trans_port_sync_master_select(enum transcoder master_transcoder);
static u32
intel_ddi_transcoder_func_reg_val_get(struct intel_encoder *encoder,
				      const struct intel_crtc_state *crtc_state);
static void
intel_ddi_config_transcoder_func(struct intel_encoder *encoder,
				 const struct intel_crtc_state *crtc_state);
static void tgl_ddi_pre_enable_dp(struct intel_atomic_state *state,
				  struct intel_encoder *encoder,
				  const struct intel_crtc_state *crtc_state,
				  const struct drm_connector_state *conn_state);
static void intel_ddi_pre_enable_dp(struct intel_atomic_state *state,
				    struct intel_encoder *encoder,
				    const struct intel_crtc_state *crtc_state,
				    const struct drm_connector_state *conn_state);
static void intel_ddi_pre_enable(struct intel_atomic_state *state,
				 struct intel_encoder *encoder,
				 const struct intel_crtc_state *crtc_state,
				 const struct drm_connector_state *conn_state);
static void intel_enable_ddi_dp(struct intel_atomic_state *state,
				struct intel_encoder *encoder,
				const struct intel_crtc_state *crtc_state,
				const struct drm_connector_state *conn_state);
static void intel_enable_ddi(struct intel_atomic_state *state,
			     struct intel_encoder *encoder,
			     const struct intel_crtc_state *crtc_state,
			     const struct drm_connector_state *conn_state);
static void
intel_ddi_pre_pll_enable(struct intel_atomic_state *state,
			 struct intel_encoder *encoder,
			 const struct intel_crtc_state *crtc_state,
			 const struct drm_connector_state *conn_state);
static u8 intel_ddi_dp_voltage_max(struct intel_dp *intel_dp,
				   const struct intel_crtc_state *crtc_state);
static u8 intel_ddi_dp_preemph_max(struct intel_dp *intel_dp);
static void _icl_ddi_enable_clock(struct drm_i915_private *i915, i915_reg_t reg,
				  u32 clk_sel_mask, u32 clk_sel, u32 clk_off);
static void _icl_ddi_disable_clock(struct drm_i915_private *i915, i915_reg_t reg,
				   u32 clk_off);
static void icl_ddi_combo_enable_clock(struct intel_encoder *encoder,
				       const struct intel_crtc_state *crtc_state);
static void icl_ddi_combo_disable_clock(struct intel_encoder *encoder);
static enum intel_display_power_domain
intel_ddi_main_link_aux_domain(struct intel_digital_port *dig_port,
			       const struct intel_crtc_state *crtc_state);
static void
main_link_aux_power_domain_get(struct intel_digital_port *dig_port,
			       const struct intel_crtc_state *crtc_state);
static void
main_link_aux_power_domain_put(struct intel_digital_port *dig_port,
			       const struct intel_crtc_state *crtc_state);
static int intel_ddi_dp_level(struct intel_dp *intel_dp,
			      const struct intel_crtc_state *crtc_state,
			      int lane);
static u32 icl_combo_phy_loadgen_select(const struct intel_crtc_state *crtc_state,
					int lane);
static void icl_ddi_combo_vswing_program(struct intel_encoder *encoder,
					 const struct intel_crtc_state *crtc_state);
static void icl_combo_phy_set_signal_levels(struct intel_encoder *encoder,
					    const struct intel_crtc_state *crtc_state);
static int translate_signal_level(struct intel_dp *intel_dp,
				  u8 signal_levels);
static void intel_ddi_power_up_lanes(struct intel_encoder *encoder,
				     const struct intel_crtc_state *crtc_state);
static void intel_ddi_mso_configure(const struct intel_crtc_state *crtc_state);
static enum transcoder
tgl_dp_tp_transcoder(const struct intel_crtc_state *crtc_state);
static void intel_wait_ddi_buf_active(struct drm_i915_private *dev_priv,
				      enum port port);
static void intel_ddi_prepare_link_retrain(struct intel_dp *intel_dp,
					   const struct intel_crtc_state *crtc_state);
static void intel_ddi_set_link_train(struct intel_dp *intel_dp,
				     const struct intel_crtc_state *crtc_state,
				     u8 dp_train_pat);
static void intel_ddi_set_idle_link_train(struct intel_dp *intel_dp,
					  const struct intel_crtc_state *crtc_state);
static void intel_ddi_disable_fec(struct intel_encoder *encoder,
				  const struct intel_crtc_state *crtc_state);
static void disable_ddi_buf(struct intel_encoder *encoder,
			    const struct intel_crtc_state *crtc_state);
static void intel_disable_ddi_buf(struct intel_encoder *encoder,
				  const struct intel_crtc_state *crtc_state);
static void intel_dp_sink_set_msa_timing_par_ignore_state(struct intel_dp *intel_dp,
							  const struct intel_crtc_state *crtc_state,
							  bool enable);
static void intel_disable_ddi_dp(struct intel_atomic_state *state,
				 struct intel_encoder *encoder,
				 const struct intel_crtc_state *old_crtc_state,
				 const struct drm_connector_state *old_conn_state);
static void intel_disable_ddi(struct intel_atomic_state *state,
			      struct intel_encoder *encoder,
			      const struct intel_crtc_state *old_crtc_state,
			      const struct drm_connector_state *old_conn_state);
static void intel_ddi_post_disable_dp(struct intel_atomic_state *state,
				      struct intel_encoder *encoder,
				      const struct intel_crtc_state *old_crtc_state,
				      const struct drm_connector_state *old_conn_state);
static void intel_ddi_post_disable(struct intel_atomic_state *state,
				   struct intel_encoder *encoder,
				   const struct intel_crtc_state *old_crtc_state,
				   const struct drm_connector_state *old_conn_state);
static void intel_ddi_post_pll_disable(struct intel_atomic_state *state,
				       struct intel_encoder *encoder,
				       const struct intel_crtc_state *old_crtc_state,
				       const struct drm_connector_state *old_conn_state);
static int icl_ddi_min_voltage_level(const struct intel_crtc_state *crtc_state);
static int jsl_ddi_min_voltage_level(const struct intel_crtc_state *crtc_state);
static int tgl_ddi_min_voltage_level(const struct intel_crtc_state *crtc_state);
static int intel_ddi_hdmi_level(struct intel_encoder *encoder,
				const struct intel_ddi_buf_trans *trans);
static void intel_ddi_pre_enable_hdmi(struct intel_atomic_state *state,
				      struct intel_encoder *encoder,
				      const struct intel_crtc_state *crtc_state,
				      const struct drm_connector_state *conn_state);
static void intel_enable_ddi_hdmi(struct intel_atomic_state *state,
				  struct intel_encoder *encoder,
				  const struct intel_crtc_state *crtc_state,
				  const struct drm_connector_state *conn_state);
static void intel_disable_ddi_hdmi(struct intel_atomic_state *state,
				   struct intel_encoder *encoder,
				   const struct intel_crtc_state *old_crtc_state,
				   const struct drm_connector_state *old_conn_state);
static void intel_ddi_post_disable_hdmi(struct intel_atomic_state *state,
					struct intel_encoder *encoder,
					const struct intel_crtc_state *old_crtc_state,
					const struct drm_connector_state *old_conn_state);
static void intel_ddi_get_encoder_pipes(struct intel_encoder *encoder,
					u8 *pipe_mask, bool *is_dp_mst);
static void intel_ddi_read_func_ctl(struct intel_encoder *encoder,
				    struct intel_crtc_state *pipe_config);
static void ddi_dotclock_get(struct intel_crtc_state *pipe_config);
static void intel_ddi_get_config(struct intel_encoder *encoder,
				 struct intel_crtc_state *pipe_config);
static struct intel_shared_dpll *
_icl_ddi_get_pll(struct drm_i915_private *i915, i915_reg_t reg,
		 u32 clk_sel_mask, u32 clk_sel_shift);
static void icl_ddi_combo_get_config(struct intel_encoder *encoder,
				     struct intel_crtc_state *crtc_state);
static void intel_ddi_sync_state(struct intel_encoder *encoder,
				 const struct intel_crtc_state *crtc_state);
static void intel_ddi_get_power_domains(struct intel_encoder *encoder,
					struct intel_crtc_state *crtc_state);
static bool _icl_ddi_is_clock_enabled(struct drm_i915_private *i915, i915_reg_t reg,
				      u32 clk_off);
static bool icl_ddi_combo_is_clock_enabled(struct intel_encoder *encoder);

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

static const u8 index_to_dp_signal_levels[] = {
	[0] = DP_TRAIN_VOLTAGE_SWING_LEVEL_0 | DP_TRAIN_PRE_EMPH_LEVEL_0,
	[1] = DP_TRAIN_VOLTAGE_SWING_LEVEL_0 | DP_TRAIN_PRE_EMPH_LEVEL_1,
	[2] = DP_TRAIN_VOLTAGE_SWING_LEVEL_0 | DP_TRAIN_PRE_EMPH_LEVEL_2,
	[3] = DP_TRAIN_VOLTAGE_SWING_LEVEL_0 | DP_TRAIN_PRE_EMPH_LEVEL_3,
	[4] = DP_TRAIN_VOLTAGE_SWING_LEVEL_1 | DP_TRAIN_PRE_EMPH_LEVEL_0,
	[5] = DP_TRAIN_VOLTAGE_SWING_LEVEL_1 | DP_TRAIN_PRE_EMPH_LEVEL_1,
	[6] = DP_TRAIN_VOLTAGE_SWING_LEVEL_1 | DP_TRAIN_PRE_EMPH_LEVEL_2,
	[7] = DP_TRAIN_VOLTAGE_SWING_LEVEL_2 | DP_TRAIN_PRE_EMPH_LEVEL_0,
	[8] = DP_TRAIN_VOLTAGE_SWING_LEVEL_2 | DP_TRAIN_PRE_EMPH_LEVEL_1,
	[9] = DP_TRAIN_VOLTAGE_SWING_LEVEL_3 | DP_TRAIN_PRE_EMPH_LEVEL_0,
};

static u8 intel_ddi_dp_voltage_max(struct intel_dp *intel_dp,
				   const struct intel_crtc_state *crtc_state)
{
	struct intel_encoder *encoder = &dp_to_dig_port(intel_dp)->base;
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	int n_entries;

	encoder->get_buf_trans(encoder, crtc_state, &n_entries);

	if (drm_WARN_ON(&dev_priv->drm, n_entries < 1))
		n_entries = 1;
	if (drm_WARN_ON(&dev_priv->drm,
			n_entries > ARRAY_SIZE(index_to_dp_signal_levels)))
		n_entries = ARRAY_SIZE(index_to_dp_signal_levels);

	return index_to_dp_signal_levels[n_entries - 1] &
		DP_TRAIN_VOLTAGE_SWING_MASK;
}

/*
 * We assume that the full set of pre-emphasis values can be
 * used on all DDI platforms. Should that change we need to
 * rethink this code.
 */
static u8 intel_ddi_dp_preemph_max(struct intel_dp *intel_dp)
{
	return DP_TRAIN_PRE_EMPH_LEVEL_3;
}

void intel_ddi_enable_clock(struct intel_encoder *encoder,
			    const struct intel_crtc_state *crtc_state)
{
	if (encoder->enable_clock)
		encoder->enable_clock(encoder, crtc_state);
}

void intel_ddi_disable_clock(struct intel_encoder *encoder)
{
	if (encoder->disable_clock)
		encoder->disable_clock(encoder);
}

static void _icl_ddi_enable_clock(struct drm_i915_private *i915, i915_reg_t reg,
				  u32 clk_sel_mask, u32 clk_sel, u32 clk_off)
{
	mutex_lock(&i915->display.dpll.lock);

	intel_de_rmw(i915, reg, clk_sel_mask, clk_sel);

	/*
	 * "This step and the step before must be
	 *  done with separate register writes."
	 */
	intel_de_rmw(i915, reg, clk_off, 0);

	mutex_unlock(&i915->display.dpll.lock);
}

static void _icl_ddi_disable_clock(struct drm_i915_private *i915, i915_reg_t reg,
				   u32 clk_off)
{
	mutex_lock(&i915->display.dpll.lock);

	intel_de_rmw(i915, reg, 0, clk_off);

	mutex_unlock(&i915->display.dpll.lock);
}

static void icl_ddi_combo_enable_clock(struct intel_encoder *encoder,
				       const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	const struct intel_shared_dpll *pll = crtc_state->shared_dpll;
	enum phy phy = intel_port_to_phy(i915, encoder->port);

	if (drm_WARN_ON(&i915->drm, !pll))
		return;

	_icl_ddi_enable_clock(i915, ICL_DPCLKA_CFGCR0,
			      ICL_DPCLKA_CFGCR0_DDI_CLK_SEL_MASK(phy),
			      ICL_DPCLKA_CFGCR0_DDI_CLK_SEL(pll->info->id, phy),
			      ICL_DPCLKA_CFGCR0_DDI_CLK_OFF(phy));
}

static void icl_ddi_combo_disable_clock(struct intel_encoder *encoder)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	enum phy phy = intel_port_to_phy(i915, encoder->port);

	_icl_ddi_disable_clock(i915, ICL_DPCLKA_CFGCR0,
			       ICL_DPCLKA_CFGCR0_DDI_CLK_OFF(phy));
}

static enum intel_display_power_domain
intel_ddi_main_link_aux_domain(struct intel_digital_port *dig_port,
			       const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *i915 = to_i915(dig_port->base.base.dev);
	enum phy phy = intel_port_to_phy(i915, dig_port->base.port);

	/*
	 * ICL+ HW requires corresponding AUX IOs to be powered up for PSR with
	 * DC states enabled at the same time, while for driver initiated AUX
	 * transfers we need the same AUX IOs to be powered but with DC states
	 * disabled. Accordingly use the AUX_IO_<port> power domain here which
	 * leaves DC states enabled.
	 *
	 * Before MTL TypeC PHYs (in all TypeC modes and both DP/HDMI) also require
	 * AUX IO to be enabled, but all these require DC_OFF to be enabled as
	 * well, so we can acquire a wider AUX_<port> power domain reference
	 * instead of a specific AUX_IO_<port> reference without powering up any
	 * extra wells.
	 */
	if (intel_psr_needs_aux_io_power(&dig_port->base, crtc_state))
		return intel_display_power_aux_io_domain(i915, dig_port->aux_ch);
	else if (DISPLAY_VER(i915) < 14 &&
		 (intel_crtc_has_dp_encoder(crtc_state) ||
		  intel_phy_is_tc(i915, phy)))
		return intel_aux_power_domain(dig_port);
	else
		return POWER_DOMAIN_INVALID;
}

static void
main_link_aux_power_domain_get(struct intel_digital_port *dig_port,
			       const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *i915 = to_i915(dig_port->base.base.dev);
	enum intel_display_power_domain domain =
		intel_ddi_main_link_aux_domain(dig_port, crtc_state);

	drm_WARN_ON(&i915->drm, dig_port->aux_wakeref);

	if (domain == POWER_DOMAIN_INVALID)
		return;

	dig_port->aux_wakeref = intel_display_power_get(i915, domain);
}

static void
main_link_aux_power_domain_put(struct intel_digital_port *dig_port,
			       const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *i915 = to_i915(dig_port->base.base.dev);
	enum intel_display_power_domain domain =
		intel_ddi_main_link_aux_domain(dig_port, crtc_state);
	intel_wakeref_t wf;

	wf = fetch_and_zero(&dig_port->aux_wakeref);
	if (!wf)
		return;

	intel_display_power_put(i915, domain, wf);
}

void intel_ddi_enable_transcoder_clock(struct intel_encoder *encoder,
				       const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	enum transcoder cpu_transcoder = crtc_state->cpu_transcoder;
	enum phy phy = intel_port_to_phy(dev_priv, encoder->port);
	u32 val;

	if (cpu_transcoder == TRANSCODER_EDP)
		return;

	if (DISPLAY_VER(dev_priv) >= 13)
		val = TGL_TRANS_CLK_SEL_PORT(phy);
	else if (DISPLAY_VER(dev_priv) >= 12)
		val = TGL_TRANS_CLK_SEL_PORT(encoder->port);
	else
		val = TRANS_CLK_SEL_PORT(encoder->port);

	intel_de_write(dev_priv, TRANS_CLK_SEL(cpu_transcoder), val);
}

void intel_ddi_disable_transcoder_clock(const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *dev_priv = to_i915(crtc_state->uapi.crtc->dev);
	enum transcoder cpu_transcoder = crtc_state->cpu_transcoder;
	u32 val;

	if (cpu_transcoder == TRANSCODER_EDP)
		return;

	if (DISPLAY_VER(dev_priv) >= 12)
		val = TGL_TRANS_CLK_SEL_DISABLED;
	else
		val = TRANS_CLK_SEL_DISABLED;

	intel_de_write(dev_priv, TRANS_CLK_SEL(cpu_transcoder), val);
}

static int intel_ddi_dp_level(struct intel_dp *intel_dp,
			      const struct intel_crtc_state *crtc_state,
			      int lane)
{
	u8 train_set = intel_dp->train_set[lane];

	if (intel_dp_is_uhbr(crtc_state)) {
		return train_set & DP_TX_FFE_PRESET_VALUE_MASK;
	} else {
		u8 signal_levels = train_set & (DP_TRAIN_VOLTAGE_SWING_MASK |
						DP_TRAIN_PRE_EMPHASIS_MASK);

		return translate_signal_level(intel_dp, signal_levels);
	}
}

int intel_ddi_level(struct intel_encoder *encoder,
		    const struct intel_crtc_state *crtc_state,
		    int lane)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	const struct intel_ddi_buf_trans *trans;
	int level, n_entries;

	trans = encoder->get_buf_trans(encoder, crtc_state, &n_entries);
	if (drm_WARN_ON_ONCE(&i915->drm, !trans))
		return 0;

	if (intel_crtc_has_type(crtc_state, INTEL_OUTPUT_HDMI))
		level = intel_ddi_hdmi_level(encoder, trans);
	else
		level = intel_ddi_dp_level(enc_to_intel_dp(encoder), crtc_state,
					   lane);

	if (drm_WARN_ON_ONCE(&i915->drm, level >= n_entries))
		level = n_entries - 1;

	return level;
}

static u32 icl_combo_phy_loadgen_select(const struct intel_crtc_state *crtc_state,
					int lane)
{
	if (crtc_state->port_clock > 600000)
		return 0;

	if (crtc_state->lane_count == 4)
		return lane >= 1 ? LOADGEN_SELECT : 0;
	else
		return lane == 1 || lane == 2 ? LOADGEN_SELECT : 0;
}

static void icl_ddi_combo_vswing_program(struct intel_encoder *encoder,
					 const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	const struct intel_ddi_buf_trans *trans;
	enum phy phy = intel_port_to_phy(dev_priv, encoder->port);
	int n_entries, ln;
	u32 val;

	trans = encoder->get_buf_trans(encoder, crtc_state, &n_entries);
	if (drm_WARN_ON_ONCE(&dev_priv->drm, !trans))
		return;

	if (intel_crtc_has_type(crtc_state, INTEL_OUTPUT_EDP)) {
		struct intel_dp *intel_dp = enc_to_intel_dp(encoder);

		val = EDP4K2K_MODE_OVRD_EN | EDP4K2K_MODE_OVRD_OPTIMIZED;
		intel_dp->hobl_active = is_hobl_buf_trans(trans);
		intel_de_rmw(dev_priv, ICL_PORT_CL_DW10(phy), val,
			     intel_dp->hobl_active ? val : 0);
	}

	/* Set PORT_TX_DW5 */
	val = intel_de_read(dev_priv, ICL_PORT_TX_DW5_LN(0, phy));
	val &= ~(SCALING_MODE_SEL_MASK | RTERM_SELECT_MASK |
		  TAP2_DISABLE | TAP3_DISABLE);
	val |= SCALING_MODE_SEL(0x2);
	val |= RTERM_SELECT(0x6);
	val |= TAP3_DISABLE;
	intel_de_write(dev_priv, ICL_PORT_TX_DW5_GRP(phy), val);

	/* Program PORT_TX_DW2 */
	for (ln = 0; ln < 4; ln++) {
		int level = intel_ddi_level(encoder, crtc_state, ln);

		intel_de_rmw(dev_priv, ICL_PORT_TX_DW2_LN(ln, phy),
			     SWING_SEL_UPPER_MASK | SWING_SEL_LOWER_MASK | RCOMP_SCALAR_MASK,
			     SWING_SEL_UPPER(trans->entries[level].icl.dw2_swing_sel) |
			     SWING_SEL_LOWER(trans->entries[level].icl.dw2_swing_sel) |
			     RCOMP_SCALAR(0x98));
	}

	/* Program PORT_TX_DW4 */
	/* We cannot write to GRP. It would overwrite individual loadgen. */
	for (ln = 0; ln < 4; ln++) {
		int level = intel_ddi_level(encoder, crtc_state, ln);

		intel_de_rmw(dev_priv, ICL_PORT_TX_DW4_LN(ln, phy),
			     POST_CURSOR_1_MASK | POST_CURSOR_2_MASK | CURSOR_COEFF_MASK,
			     POST_CURSOR_1(trans->entries[level].icl.dw4_post_cursor_1) |
			     POST_CURSOR_2(trans->entries[level].icl.dw4_post_cursor_2) |
			     CURSOR_COEFF(trans->entries[level].icl.dw4_cursor_coeff));
	}

	/* Program PORT_TX_DW7 */
	for (ln = 0; ln < 4; ln++) {
		int level = intel_ddi_level(encoder, crtc_state, ln);

		intel_de_rmw(dev_priv, ICL_PORT_TX_DW7_LN(ln, phy),
			     N_SCALAR_MASK,
			     N_SCALAR(trans->entries[level].icl.dw7_n_scalar));
	}
}

static void icl_combo_phy_set_signal_levels(struct intel_encoder *encoder,
					    const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	enum phy phy = intel_port_to_phy(dev_priv, encoder->port);
	u32 val;
	int ln;

	/*
	 * 1. If port type is eDP or DP,
	 * set PORT_PCS_DW1 cmnkeeper_enable to 1b,
	 * else clear to 0b.
	 */
	val = intel_de_read(dev_priv, ICL_PORT_PCS_DW1_LN(0, phy));
	if (intel_crtc_has_type(crtc_state, INTEL_OUTPUT_HDMI))
		val &= ~COMMON_KEEPER_EN;
	else
		val |= COMMON_KEEPER_EN;
	intel_de_write(dev_priv, ICL_PORT_PCS_DW1_GRP(phy), val);

	/* 2. Program loadgen select */
	/*
	 * Program PORT_TX_DW4 depending on Bit rate and used lanes
	 * <= 6 GHz and 4 lanes (LN0=0, LN1=1, LN2=1, LN3=1)
	 * <= 6 GHz and 1,2 lanes (LN0=0, LN1=1, LN2=1, LN3=0)
	 * > 6 GHz (LN0=0, LN1=0, LN2=0, LN3=0)
	 */
	for (ln = 0; ln < 4; ln++) {
		intel_de_rmw(dev_priv, ICL_PORT_TX_DW4_LN(ln, phy),
			     LOADGEN_SELECT,
			     icl_combo_phy_loadgen_select(crtc_state, ln));
	}

	/* 3. Set PORT_CL_DW5 SUS Clock Config to 11b */
	intel_de_rmw(dev_priv, ICL_PORT_CL_DW5(phy),
		     0, SUS_CLOCK_CONFIG);

	/* 4. Clear training enable to change swing values */
	val = intel_de_read(dev_priv, ICL_PORT_TX_DW5_LN(0, phy));
	val &= ~TX_TRAINING_EN;
	intel_de_write(dev_priv, ICL_PORT_TX_DW5_GRP(phy), val);

	/* 5. Program swing and de-emphasis */
	icl_ddi_combo_vswing_program(encoder, crtc_state);

	/* 6. Set training enable to trigger update */
	val = intel_de_read(dev_priv, ICL_PORT_TX_DW5_LN(0, phy));
	val |= TX_TRAINING_EN;
	intel_de_write(dev_priv, ICL_PORT_TX_DW5_GRP(phy), val);
}

static int translate_signal_level(struct intel_dp *intel_dp,
				  u8 signal_levels)
{
	struct drm_i915_private *i915 = dp_to_i915(intel_dp);
	int i;

	for (i = 0; i < ARRAY_SIZE(index_to_dp_signal_levels); i++) {
		if (index_to_dp_signal_levels[i] == signal_levels)
			return i;
	}

	drm_WARN(&i915->drm, 1,
		 "Unsupported voltage swing/pre-emphasis level: 0x%x\n",
		 signal_levels);

	return 0;
}

static void intel_ddi_power_up_lanes(struct intel_encoder *encoder,
				     const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	struct intel_digital_port *dig_port = enc_to_dig_port(encoder);
	enum phy phy = intel_port_to_phy(i915, encoder->port);

	if (intel_phy_is_combo(i915, phy)) {
		bool lane_reversal =
			dig_port->saved_port_bits & DDI_BUF_PORT_REVERSAL;

		intel_combo_phy_power_up_lanes(i915, phy, false,
					       crtc_state->lane_count,
					       lane_reversal);
	}
}

static void intel_ddi_mso_configure(const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_i915_private *i915 = to_i915(crtc->base.dev);
	enum pipe pipe = crtc->pipe;
	u32 dss1 = 0;

	if (!HAS_MSO(i915))
		return;

	if (crtc_state->splitter.enable) {
		dss1 |= SPLITTER_ENABLE;
		dss1 |= OVERLAP_PIXELS(crtc_state->splitter.pixel_overlap);
		if (crtc_state->splitter.link_count == 2)
			dss1 |= SPLITTER_CONFIGURATION_2_SEGMENT;
		else
			dss1 |= SPLITTER_CONFIGURATION_4_SEGMENT;
	}

	intel_de_rmw(i915, ICL_PIPE_DSS_CTL1(pipe),
		     SPLITTER_ENABLE | SPLITTER_CONFIGURATION_MASK |
		     OVERLAP_PIXELS_MASK, dss1);
}

static enum transcoder
tgl_dp_tp_transcoder(const struct intel_crtc_state *crtc_state)
{
	if (intel_crtc_has_type(crtc_state, INTEL_OUTPUT_DP_MST))
		return crtc_state->mst_master_transcoder;
	else
		return crtc_state->cpu_transcoder;
}

i915_reg_t dp_tp_ctl_reg(struct intel_encoder *encoder,
			 const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);

	if (DISPLAY_VER(dev_priv) >= 12)
		return TGL_DP_TP_CTL(tgl_dp_tp_transcoder(crtc_state));
	else
		return DP_TP_CTL(encoder->port);
}

i915_reg_t dp_tp_status_reg(struct intel_encoder *encoder,
			    const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);

	if (DISPLAY_VER(dev_priv) >= 12)
		return TGL_DP_TP_STATUS(tgl_dp_tp_transcoder(crtc_state));
	else
		return DP_TP_STATUS(encoder->port);
}

void intel_wait_ddi_buf_idle(struct drm_i915_private *dev_priv,
			     enum port port)
{
	if (IS_BROXTON(dev_priv)) {
		udelay(16);
		return;
	}

	if (wait_for_us((intel_de_read(dev_priv, DDI_BUF_CTL(port)) &
			 DDI_BUF_IS_IDLE), 8))
		drm_err(&dev_priv->drm, "Timeout waiting for DDI BUF %c to get idle\n",
			port_name(port));
}

static void intel_wait_ddi_buf_active(struct drm_i915_private *dev_priv,
				      enum port port)
{
	enum phy phy = intel_port_to_phy(dev_priv, port);
	int timeout_us;
	int ret;

	/* Wait > 518 usecs for DDI_BUF_CTL to be non idle */
	if (DISPLAY_VER(dev_priv) < 10) {
		usleep_range(518, 1000);
		return;
	}

	if (DISPLAY_VER(dev_priv) >= 14) {
		timeout_us = 10000;
	} else if (IS_DG2(dev_priv)) {
		timeout_us = 1200;
	} else if (DISPLAY_VER(dev_priv) >= 12) {
		if (intel_phy_is_tc(dev_priv, phy))
			timeout_us = 3000;
		else
			timeout_us = 1000;
	} else {
		timeout_us = 500;
	}

	if (DISPLAY_VER(dev_priv) >= 14)
		ret = _wait_for(!(intel_de_read(dev_priv, XELPDP_PORT_BUF_CTL1(port)) & XELPDP_PORT_BUF_PHY_IDLE),
				timeout_us, 10, 10);
	else
		ret = _wait_for(!(intel_de_read(dev_priv, DDI_BUF_CTL(port)) & DDI_BUF_IS_IDLE),
				timeout_us, 10, 10);

	if (ret)
		drm_err(&dev_priv->drm, "Timeout waiting for DDI BUF %c to get active\n",
			port_name(port));
}

static void intel_ddi_prepare_link_retrain(struct intel_dp *intel_dp,
					   const struct intel_crtc_state *crtc_state)
{
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);
	struct intel_encoder *encoder = &dig_port->base;
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	enum port port = encoder->port;
	u32 dp_tp_ctl, ddi_buf_ctl;
	bool wait = false;

	dp_tp_ctl = intel_de_read(dev_priv, dp_tp_ctl_reg(encoder, crtc_state));

	if (dp_tp_ctl & DP_TP_CTL_ENABLE) {
		ddi_buf_ctl = intel_de_read(dev_priv, DDI_BUF_CTL(port));
		if (ddi_buf_ctl & DDI_BUF_CTL_ENABLE) {
			intel_de_write(dev_priv, DDI_BUF_CTL(port),
				       ddi_buf_ctl & ~DDI_BUF_CTL_ENABLE);
			wait = true;
		}

		dp_tp_ctl &= ~DP_TP_CTL_ENABLE;
		intel_de_write(dev_priv, dp_tp_ctl_reg(encoder, crtc_state), dp_tp_ctl);
		intel_de_posting_read(dev_priv, dp_tp_ctl_reg(encoder, crtc_state));

		if (wait)
			intel_wait_ddi_buf_idle(dev_priv, port);
	}

	dp_tp_ctl = DP_TP_CTL_ENABLE | DP_TP_CTL_LINK_TRAIN_PAT1;
	if (intel_crtc_has_type(crtc_state, INTEL_OUTPUT_DP_MST)) {
		dp_tp_ctl |= DP_TP_CTL_MODE_MST;
	} else {
		dp_tp_ctl |= DP_TP_CTL_MODE_SST;
		if (crtc_state->enhanced_framing)
			dp_tp_ctl |= DP_TP_CTL_ENHANCED_FRAME_ENABLE;
	}
	intel_de_write(dev_priv, dp_tp_ctl_reg(encoder, crtc_state), dp_tp_ctl);
	intel_de_posting_read(dev_priv, dp_tp_ctl_reg(encoder, crtc_state));

	if (IS_ALDERLAKE_P(dev_priv) &&
	    (intel_tc_port_in_dp_alt_mode(dig_port) || intel_tc_port_in_legacy_mode(dig_port)))
		adlp_tbt_to_dp_alt_switch_wa(encoder);

	intel_dp->DP |= DDI_BUF_CTL_ENABLE;
	intel_de_write(dev_priv, DDI_BUF_CTL(port), intel_dp->DP);
	intel_de_posting_read(dev_priv, DDI_BUF_CTL(port));

	intel_wait_ddi_buf_active(dev_priv, port);
}

static void intel_ddi_set_link_train(struct intel_dp *intel_dp,
				     const struct intel_crtc_state *crtc_state,
				     u8 dp_train_pat)
{
	struct intel_encoder *encoder = &dp_to_dig_port(intel_dp)->base;
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	u32 temp;

	temp = intel_de_read(dev_priv, dp_tp_ctl_reg(encoder, crtc_state));

	temp &= ~DP_TP_CTL_LINK_TRAIN_MASK;
	switch (intel_dp_training_pattern_symbol(dp_train_pat)) {
	case DP_TRAINING_PATTERN_DISABLE:
		temp |= DP_TP_CTL_LINK_TRAIN_NORMAL;
		break;
	case DP_TRAINING_PATTERN_1:
		temp |= DP_TP_CTL_LINK_TRAIN_PAT1;
		break;
	case DP_TRAINING_PATTERN_2:
		temp |= DP_TP_CTL_LINK_TRAIN_PAT2;
		break;
	case DP_TRAINING_PATTERN_3:
		temp |= DP_TP_CTL_LINK_TRAIN_PAT3;
		break;
	case DP_TRAINING_PATTERN_4:
		temp |= DP_TP_CTL_LINK_TRAIN_PAT4;
		break;
	}

	intel_de_write(dev_priv, dp_tp_ctl_reg(encoder, crtc_state), temp);
}

static void intel_ddi_set_idle_link_train(struct intel_dp *intel_dp,
					  const struct intel_crtc_state *crtc_state)
{
	struct intel_encoder *encoder = &dp_to_dig_port(intel_dp)->base;
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	enum port port = encoder->port;

	intel_de_rmw(dev_priv, dp_tp_ctl_reg(encoder, crtc_state),
		     DP_TP_CTL_LINK_TRAIN_MASK, DP_TP_CTL_LINK_TRAIN_IDLE);

	/*
	 * Until TGL on PORT_A we can have only eDP in SST mode. There the only
	 * reason we need to set idle transmission mode is to work around a HW
	 * issue where we enable the pipe while not in idle link-training mode.
	 * In this case there is requirement to wait for a minimum number of
	 * idle patterns to be sent.
	 */
	if (port == PORT_A && DISPLAY_VER(dev_priv) < 12)
		return;

	if (intel_de_wait_for_set(dev_priv,
				  dp_tp_status_reg(encoder, crtc_state),
				  DP_TP_STATUS_IDLE_DONE, 1))
		drm_err(&dev_priv->drm,
			"Timed out waiting for DP idle patterns\n");
}

static void intel_ddi_disable_fec(struct intel_encoder *encoder,
				  const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);

	if (!crtc_state->fec_enable)
		return;

	intel_de_rmw(dev_priv, dp_tp_ctl_reg(encoder, crtc_state),
		     DP_TP_CTL_FEC_ENABLE, 0);
	intel_de_posting_read(dev_priv, dp_tp_ctl_reg(encoder, crtc_state));
}

static void disable_ddi_buf(struct intel_encoder *encoder,
			    const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	enum port port = encoder->port;
	bool wait = false;
	u32 val;

	val = intel_de_read(dev_priv, DDI_BUF_CTL(port));
	if (val & DDI_BUF_CTL_ENABLE) {
		val &= ~DDI_BUF_CTL_ENABLE;
		intel_de_write(dev_priv, DDI_BUF_CTL(port), val);
		wait = true;
	}

	if (intel_crtc_has_dp_encoder(crtc_state))
		intel_de_rmw(dev_priv, dp_tp_ctl_reg(encoder, crtc_state),
			     DP_TP_CTL_ENABLE, 0);

	intel_ddi_disable_fec(encoder, crtc_state);

	if (wait)
		intel_wait_ddi_buf_idle(dev_priv, port);
}

static void intel_disable_ddi_buf(struct intel_encoder *encoder,
				  const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);

	if (DISPLAY_VER(dev_priv) >= 14) {
		mtl_disable_ddi_buf(encoder, crtc_state);

		/* 3.f Disable DP_TP_CTL FEC Enable if it is needed */
		intel_ddi_disable_fec(encoder, crtc_state);
	} else {
		disable_ddi_buf(encoder, crtc_state);
	}

	intel_ddi_wait_for_fec_status(encoder, crtc_state, false);
}

void intel_ddi_disable_transcoder_func(const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	enum transcoder cpu_transcoder = crtc_state->cpu_transcoder;
	u32 ctl;

	if (DISPLAY_VER(dev_priv) >= 11)
		intel_de_write(dev_priv,
			       TRANS_DDI_FUNC_CTL2(cpu_transcoder), 0);

	ctl = intel_de_read(dev_priv, TRANS_DDI_FUNC_CTL(cpu_transcoder));

	drm_WARN_ON(crtc->base.dev, ctl & TRANS_DDI_HDCP_SIGNALLING);

	ctl &= ~TRANS_DDI_FUNC_ENABLE;

	if (IS_DISPLAY_VER(dev_priv, 8, 10))
		ctl &= ~(TRANS_DDI_PORT_SYNC_ENABLE |
			 TRANS_DDI_PORT_SYNC_MASTER_SELECT_MASK);

	if (DISPLAY_VER(dev_priv) >= 12) {
		if (!intel_dp_mst_is_master_trans(crtc_state)) {
			ctl &= ~(TGL_TRANS_DDI_PORT_MASK |
				 TRANS_DDI_MODE_SELECT_MASK);
		}
	} else {
		ctl &= ~(TRANS_DDI_PORT_MASK | TRANS_DDI_MODE_SELECT_MASK);
	}

	intel_de_write(dev_priv, TRANS_DDI_FUNC_CTL(cpu_transcoder), ctl);

	if (intel_has_quirk(dev_priv, QUIRK_INCREASE_DDI_DISABLED_TIME) &&
	    intel_crtc_has_type(crtc_state, INTEL_OUTPUT_HDMI)) {
		drm_dbg_kms(&dev_priv->drm,
			    "Quirk Increase DDI disabled time\n");
		/* Quirk time at 100ms for reliable operation */
		msleep(100);
	}
}

static void intel_dp_sink_set_msa_timing_par_ignore_state(struct intel_dp *intel_dp,
							  const struct intel_crtc_state *crtc_state,
							  bool enable)
{
	struct drm_i915_private *i915 = dp_to_i915(intel_dp);

	if (!crtc_state->vrr.enable)
		return;

	if (drm_dp_dpcd_writeb(&intel_dp->aux, DP_DOWNSPREAD_CTRL,
			       enable ? DP_MSA_TIMING_PAR_IGNORE_EN : 0) <= 0)
		drm_dbg_kms(&i915->drm,
			    "Failed to %s MSA_TIMING_PAR_IGNORE in the sink\n",
			    str_enable_disable(enable));
}

static void intel_disable_ddi_dp(struct intel_atomic_state *state,
				 struct intel_encoder *encoder,
				 const struct intel_crtc_state *old_crtc_state,
				 const struct drm_connector_state *old_conn_state)
{
	struct intel_dp *intel_dp = enc_to_intel_dp(encoder);
	struct intel_connector *connector =
		to_intel_connector(old_conn_state->connector);

	intel_dp->link_trained = false;

	intel_psr_disable(intel_dp, old_crtc_state);
	intel_edp_backlight_off(old_conn_state);
	/* Disable the decompression in DP Sink */
	intel_dp_sink_disable_decompression(state,
					    connector, old_crtc_state);
	/* Disable Ignore_MSA bit in DP Sink */
	intel_dp_sink_set_msa_timing_par_ignore_state(intel_dp, old_crtc_state,
						      false);
}

static void intel_disable_ddi(struct intel_atomic_state *state,
			      struct intel_encoder *encoder,
			      const struct intel_crtc_state *old_crtc_state,
			      const struct drm_connector_state *old_conn_state)
{
	intel_tc_port_link_cancel_reset_work(enc_to_dig_port(encoder));

	intel_hdcp_disable(to_intel_connector(old_conn_state->connector));

	if (intel_crtc_has_type(old_crtc_state, INTEL_OUTPUT_HDMI))
		intel_disable_ddi_hdmi(state, encoder, old_crtc_state,
				       old_conn_state);
	else
		intel_disable_ddi_dp(state, encoder, old_crtc_state,
				     old_conn_state);
}

static void intel_ddi_post_disable_dp(struct intel_atomic_state *state,
				      struct intel_encoder *encoder,
				      const struct intel_crtc_state *old_crtc_state,
				      const struct drm_connector_state *old_conn_state)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_digital_port *dig_port = enc_to_dig_port(encoder);
	struct intel_dp *intel_dp = &dig_port->dp;
	intel_wakeref_t wakeref;
	bool is_mst = intel_crtc_has_type(old_crtc_state,
					  INTEL_OUTPUT_DP_MST);

	if (!is_mst)
		intel_dp_set_infoframes(encoder, false,
					old_crtc_state, old_conn_state);

	/*
	 * Power down sink before disabling the port, otherwise we end
	 * up getting interrupts from the sink on detecting link loss.
	 */
	intel_dp_set_power(intel_dp, DP_SET_POWER_D3);

	if (DISPLAY_VER(dev_priv) >= 12) {
		if (is_mst) {
			enum transcoder cpu_transcoder = old_crtc_state->cpu_transcoder;

			intel_de_rmw(dev_priv, TRANS_DDI_FUNC_CTL(cpu_transcoder),
				     TGL_TRANS_DDI_PORT_MASK | TRANS_DDI_MODE_SELECT_MASK,
				     0);
		}
	} else {
		if (!is_mst)
			intel_ddi_disable_transcoder_clock(old_crtc_state);
	}

	intel_disable_ddi_buf(encoder, old_crtc_state);

	intel_dp_sink_set_fec_ready(intel_dp, old_crtc_state, false);

	/*
	 * From TGL spec: "If single stream or multi-stream master transcoder:
	 * Configure Transcoder Clock select to direct no clock to the
	 * transcoder"
	 */
	if (DISPLAY_VER(dev_priv) >= 12)
		intel_ddi_disable_transcoder_clock(old_crtc_state);

	intel_pps_vdd_on(intel_dp);
	intel_pps_off(intel_dp);

	wakeref = fetch_and_zero(&dig_port->ddi_io_wakeref);

	if (wakeref)
		intel_display_power_put(dev_priv,
					dig_port->ddi_io_power_domain,
					wakeref);

	intel_ddi_disable_clock(encoder);

	/* De-select Thunderbolt */
	if (DISPLAY_VER(dev_priv) >= 14)
		intel_de_rmw(dev_priv, XELPDP_PORT_BUF_CTL1(encoder->port),
			     XELPDP_PORT_BUF_IO_SELECT_TBT, 0);
}

static void intel_ddi_post_disable(struct intel_atomic_state *state,
				   struct intel_encoder *encoder,
				   const struct intel_crtc_state *old_crtc_state,
				   const struct drm_connector_state *old_conn_state)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_crtc *slave_crtc;

	if (!intel_crtc_has_type(old_crtc_state, INTEL_OUTPUT_DP_MST)) {
		intel_crtc_vblank_off(old_crtc_state);

		intel_disable_transcoder(old_crtc_state);

		intel_ddi_disable_transcoder_func(old_crtc_state);

		intel_dsc_disable(old_crtc_state);

		if (DISPLAY_VER(dev_priv) >= 9)
			skl_scaler_disable(old_crtc_state);
		else
			ilk_pfit_disable(old_crtc_state);
	}

	for_each_intel_crtc_in_pipe_mask(&dev_priv->drm, slave_crtc,
					 intel_crtc_bigjoiner_slave_pipes(old_crtc_state)) {
		const struct intel_crtc_state *old_slave_crtc_state =
			intel_atomic_get_old_crtc_state(state, slave_crtc);

		intel_crtc_vblank_off(old_slave_crtc_state);

		intel_dsc_disable(old_slave_crtc_state);
		skl_scaler_disable(old_slave_crtc_state);
	}

	/*
	 * When called from DP MST code:
	 * - old_conn_state will be NULL
	 * - encoder will be the main encoder (ie. mst->primary)
	 * - the main connector associated with this port
	 *   won't be active or linked to a crtc
	 * - old_crtc_state will be the state of the last stream to
	 *   be deactivated on this port, and it may not be the same
	 *   stream that was activated last, but each stream
	 *   should have a state that is identical when it comes to
	 *   the DP link parameteres
	 */

	if (intel_crtc_has_type(old_crtc_state, INTEL_OUTPUT_HDMI))
		intel_ddi_post_disable_hdmi(state, encoder, old_crtc_state,
					    old_conn_state);
	else
		intel_ddi_post_disable_dp(state, encoder, old_crtc_state,
					  old_conn_state);
}

static void intel_ddi_post_pll_disable(struct intel_atomic_state *state,
				       struct intel_encoder *encoder,
				       const struct intel_crtc_state *old_crtc_state,
				       const struct drm_connector_state *old_conn_state)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	struct intel_digital_port *dig_port = enc_to_dig_port(encoder);
	enum phy phy = intel_port_to_phy(i915, encoder->port);
	bool is_tc_port = intel_phy_is_tc(i915, phy);

	main_link_aux_power_domain_put(dig_port, old_crtc_state);

	if (is_tc_port)
		intel_tc_port_put_link(dig_port);
}

static int icl_ddi_min_voltage_level(const struct intel_crtc_state *crtc_state)
{
	if (crtc_state->port_clock > 594000)
		return 1;
	else
		return 0;
}

static int jsl_ddi_min_voltage_level(const struct intel_crtc_state *crtc_state)
{
	if (crtc_state->port_clock > 594000)
		return 3;
	else
		return 0;
}

static int tgl_ddi_min_voltage_level(const struct intel_crtc_state *crtc_state)
{
	if (crtc_state->port_clock > 594000)
		return 2;
	else
		return 0;
}

void intel_ddi_compute_min_voltage_level(struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *dev_priv = to_i915(crtc_state->uapi.crtc->dev);

	if (DISPLAY_VER(dev_priv) >= 14)
		crtc_state->min_voltage_level = icl_ddi_min_voltage_level(crtc_state);
	else if (DISPLAY_VER(dev_priv) >= 12)
		crtc_state->min_voltage_level = tgl_ddi_min_voltage_level(crtc_state);
	else if (IS_JASPERLAKE(dev_priv) || IS_ELKHARTLAKE(dev_priv))
		crtc_state->min_voltage_level = jsl_ddi_min_voltage_level(crtc_state);
	else if (DISPLAY_VER(dev_priv) >= 11)
		crtc_state->min_voltage_level = icl_ddi_min_voltage_level(crtc_state);
}

static int intel_ddi_hdmi_level(struct intel_encoder *encoder,
				const struct intel_ddi_buf_trans *trans)
{
	int level;

	level = intel_bios_hdmi_level_shift(encoder->devdata);
	if (level < 0)
		level = trans->hdmi_default_entry;

	return level;
}

static void intel_ddi_pre_enable_hdmi(struct intel_atomic_state *state,
				      struct intel_encoder *encoder,
				      const struct intel_crtc_state *crtc_state,
				      const struct drm_connector_state *conn_state)
{
	struct intel_digital_port *dig_port = enc_to_dig_port(encoder);
	struct intel_hdmi *intel_hdmi = &dig_port->hdmi;
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);

	intel_dp_dual_mode_set_tmds_output(intel_hdmi, true);
	intel_ddi_enable_clock(encoder, crtc_state);

	drm_WARN_ON(&dev_priv->drm, dig_port->ddi_io_wakeref);
	dig_port->ddi_io_wakeref = intel_display_power_get(dev_priv,
							   dig_port->ddi_io_power_domain);

	icl_program_mg_dp_mode(dig_port, crtc_state);

	intel_ddi_enable_transcoder_clock(encoder, crtc_state);

	dig_port->set_infoframes(encoder,
				 crtc_state->has_infoframe,
				 crtc_state, conn_state);
}

static void intel_enable_ddi_hdmi(struct intel_atomic_state *state,
				  struct intel_encoder *encoder,
				  const struct intel_crtc_state *crtc_state,
				  const struct drm_connector_state *conn_state)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_digital_port *dig_port = enc_to_dig_port(encoder);
	struct drm_connector *connector = conn_state->connector;
	enum port port = encoder->port;
	enum phy phy = intel_port_to_phy(dev_priv, port);
	u32 buf_ctl;

	if (!intel_hdmi_handle_sink_scrambling(encoder, connector,
					       crtc_state->hdmi_high_tmds_clock_ratio,
					       crtc_state->hdmi_scrambling))
		drm_dbg_kms(&dev_priv->drm,
			    "[CONNECTOR:%d:%s] Failed to configure sink scrambling/TMDS bit clock ratio\n",
			    connector->base.id, connector->name);

	if (has_buf_trans_select(dev_priv))
		hsw_prepare_hdmi_ddi_buffers(encoder, crtc_state);

	/* e. Enable D2D Link for C10/C20 Phy */
	if (DISPLAY_VER(dev_priv) >= 14)
		mtl_ddi_enable_d2d(encoder);

	encoder->set_signal_levels(encoder, crtc_state);

	/* Display WA #1143: skl,kbl,cfl */
	if (DISPLAY_VER(dev_priv) == 9 && !IS_BROXTON(dev_priv)) {
		/*
		 * For some reason these chicken bits have been
		 * stuffed into a transcoder register, event though
		 * the bits affect a specific DDI port rather than
		 * a specific transcoder.
		 */
		i915_reg_t reg = gen9_chicken_trans_reg_by_port(dev_priv, port);
		u32 val;

		val = intel_de_read(dev_priv, reg);

		if (port == PORT_E)
			val |= DDIE_TRAINING_OVERRIDE_ENABLE |
				DDIE_TRAINING_OVERRIDE_VALUE;
		else
			val |= DDI_TRAINING_OVERRIDE_ENABLE |
				DDI_TRAINING_OVERRIDE_VALUE;

		intel_de_write(dev_priv, reg, val);
		intel_de_posting_read(dev_priv, reg);

		udelay(1);

		if (port == PORT_E)
			val &= ~(DDIE_TRAINING_OVERRIDE_ENABLE |
				 DDIE_TRAINING_OVERRIDE_VALUE);
		else
			val &= ~(DDI_TRAINING_OVERRIDE_ENABLE |
				 DDI_TRAINING_OVERRIDE_VALUE);

		intel_de_write(dev_priv, reg, val);
	}

	intel_ddi_power_up_lanes(encoder, crtc_state);

	/* In HDMI/DVI mode, the port width, and swing/emphasis values
	 * are ignored so nothing special needs to be done besides
	 * enabling the port.
	 *
	 * On ADL_P the PHY link rate and lane count must be programmed but
	 * these are both 0 for HDMI.
	 *
	 * But MTL onwards HDMI2.1 is supported and in TMDS mode this
	 * is filled with lane count, already set in the crtc_state.
	 * The same is required to be filled in PORT_BUF_CTL for C10/20 Phy.
	 */
	buf_ctl = dig_port->saved_port_bits | DDI_BUF_CTL_ENABLE;
	if (DISPLAY_VER(dev_priv) >= 14) {
		u8  lane_count = mtl_get_port_width(crtc_state->lane_count);
		u32 port_buf = 0;

		port_buf |= XELPDP_PORT_WIDTH(lane_count);

		if (dig_port->saved_port_bits & DDI_BUF_PORT_REVERSAL)
			port_buf |= XELPDP_PORT_REVERSAL;

		intel_de_rmw(dev_priv, XELPDP_PORT_BUF_CTL1(port),
			     XELPDP_PORT_WIDTH_MASK | XELPDP_PORT_REVERSAL, port_buf);

		buf_ctl |= DDI_PORT_WIDTH(crtc_state->lane_count);

		if (DISPLAY_VER(dev_priv) >= 20)
			buf_ctl |= XE2LPD_DDI_BUF_D2D_LINK_ENABLE;
	} else if (IS_ALDERLAKE_P(dev_priv) && intel_phy_is_tc(dev_priv, phy)) {
		drm_WARN_ON(&dev_priv->drm, !intel_tc_port_in_legacy_mode(dig_port));
		buf_ctl |= DDI_BUF_CTL_TC_PHY_OWNERSHIP;
	}

	intel_de_write(dev_priv, DDI_BUF_CTL(port), buf_ctl);

	intel_wait_ddi_buf_active(dev_priv, port);
}

static void intel_disable_ddi_hdmi(struct intel_atomic_state *state,
				   struct intel_encoder *encoder,
				   const struct intel_crtc_state *old_crtc_state,
				   const struct drm_connector_state *old_conn_state)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	struct drm_connector *connector = old_conn_state->connector;

	if (!intel_hdmi_handle_sink_scrambling(encoder, connector,
					       false, false))
		drm_dbg_kms(&i915->drm,
			    "[CONNECTOR:%d:%s] Failed to reset sink scrambling/TMDS bit clock ratio\n",
			    connector->base.id, connector->name);
}

static void intel_ddi_post_disable_hdmi(struct intel_atomic_state *state,
					struct intel_encoder *encoder,
					const struct intel_crtc_state *old_crtc_state,
					const struct drm_connector_state *old_conn_state)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_digital_port *dig_port = enc_to_dig_port(encoder);
	struct intel_hdmi *intel_hdmi = &dig_port->hdmi;
	intel_wakeref_t wakeref;

	dig_port->set_infoframes(encoder, false,
				 old_crtc_state, old_conn_state);

	if (DISPLAY_VER(dev_priv) < 12)
		intel_ddi_disable_transcoder_clock(old_crtc_state);

	intel_disable_ddi_buf(encoder, old_crtc_state);

	if (DISPLAY_VER(dev_priv) >= 12)
		intel_ddi_disable_transcoder_clock(old_crtc_state);

	wakeref = fetch_and_zero(&dig_port->ddi_io_wakeref);
	if (wakeref)
		intel_display_power_put(dev_priv,
					dig_port->ddi_io_power_domain,
					wakeref);

	intel_ddi_disable_clock(encoder);

	intel_dp_dual_mode_set_tmds_output(intel_hdmi, false);
}

static void intel_ddi_get_encoder_pipes(struct intel_encoder *encoder,
					u8 *pipe_mask, bool *is_dp_mst)
{
	struct drm_device *dev = encoder->base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	enum port port = encoder->port;
	intel_wakeref_t wakeref;
	enum pipe p;
	u32 tmp;
	u8 mst_pipe_mask;

	*pipe_mask = 0;
	*is_dp_mst = false;

	wakeref = intel_display_power_get_if_enabled(dev_priv,
						     encoder->power_domain);
	if (!wakeref)
		return;

	tmp = intel_de_read(dev_priv, DDI_BUF_CTL(port));
	if (!(tmp & DDI_BUF_CTL_ENABLE))
		goto out;

	if (HAS_TRANSCODER(dev_priv, TRANSCODER_EDP) && port == PORT_A) {
		tmp = intel_de_read(dev_priv,
				    TRANS_DDI_FUNC_CTL(TRANSCODER_EDP));

		switch (tmp & TRANS_DDI_EDP_INPUT_MASK) {
		default:
			MISSING_CASE(tmp & TRANS_DDI_EDP_INPUT_MASK);
			fallthrough;
		case TRANS_DDI_EDP_INPUT_A_ON:
		case TRANS_DDI_EDP_INPUT_A_ONOFF:
			*pipe_mask = BIT(PIPE_A);
			break;
		case TRANS_DDI_EDP_INPUT_B_ONOFF:
			*pipe_mask = BIT(PIPE_B);
			break;
		case TRANS_DDI_EDP_INPUT_C_ONOFF:
			*pipe_mask = BIT(PIPE_C);
			break;
		}

		goto out;
	}

	mst_pipe_mask = 0;
	for_each_pipe(dev_priv, p) {
		enum transcoder cpu_transcoder = (enum transcoder)p;
		unsigned int port_mask, ddi_select;
		intel_wakeref_t trans_wakeref;

		trans_wakeref = intel_display_power_get_if_enabled(dev_priv,
								   POWER_DOMAIN_TRANSCODER(cpu_transcoder));
		if (!trans_wakeref)
			continue;

		if (DISPLAY_VER(dev_priv) >= 12) {
			port_mask = TGL_TRANS_DDI_PORT_MASK;
			ddi_select = TGL_TRANS_DDI_SELECT_PORT(port);
		} else {
			port_mask = TRANS_DDI_PORT_MASK;
			ddi_select = TRANS_DDI_SELECT_PORT(port);
		}

		tmp = intel_de_read(dev_priv,
				    TRANS_DDI_FUNC_CTL(cpu_transcoder));
		intel_display_power_put(dev_priv, POWER_DOMAIN_TRANSCODER(cpu_transcoder),
					trans_wakeref);

		if ((tmp & port_mask) != ddi_select)
			continue;

		if ((tmp & TRANS_DDI_MODE_SELECT_MASK) == TRANS_DDI_MODE_SELECT_DP_MST ||
		    (HAS_DP20(dev_priv) &&
		     (tmp & TRANS_DDI_MODE_SELECT_MASK) == TRANS_DDI_MODE_SELECT_FDI_OR_128B132B))
			mst_pipe_mask |= BIT(p);

		*pipe_mask |= BIT(p);
	}

	if (!*pipe_mask)
		drm_dbg_kms(&dev_priv->drm,
			    "No pipe for [ENCODER:%d:%s] found\n",
			    encoder->base.base.id, encoder->base.name);

	if (!mst_pipe_mask && hweight8(*pipe_mask) > 1) {
		drm_dbg_kms(&dev_priv->drm,
			    "Multiple pipes for [ENCODER:%d:%s] (pipe_mask %02x)\n",
			    encoder->base.base.id, encoder->base.name,
			    *pipe_mask);
		*pipe_mask = BIT(ffs(*pipe_mask) - 1);
	}

	if (mst_pipe_mask && mst_pipe_mask != *pipe_mask)
		drm_dbg_kms(&dev_priv->drm,
			    "Conflicting MST and non-MST state for [ENCODER:%d:%s] (pipe_mask %02x mst_pipe_mask %02x)\n",
			    encoder->base.base.id, encoder->base.name,
			    *pipe_mask, mst_pipe_mask);
	else
		*is_dp_mst = mst_pipe_mask;

out:
	if (*pipe_mask && (IS_GEMINILAKE(dev_priv) || IS_BROXTON(dev_priv))) {
		tmp = intel_de_read(dev_priv, BXT_PHY_CTL(port));
		if ((tmp & (BXT_PHY_CMNLANE_POWERDOWN_ACK |
			    BXT_PHY_LANE_POWERDOWN_ACK |
			    BXT_PHY_LANE_ENABLED)) != BXT_PHY_LANE_ENABLED)
			drm_err(&dev_priv->drm,
				"[ENCODER:%d:%s] enabled but PHY powered down? (PHY_CTL %08x)\n",
				encoder->base.base.id, encoder->base.name, tmp);
	}

	intel_display_power_put(dev_priv, encoder->power_domain, wakeref);
}

bool intel_ddi_get_hw_state(struct intel_encoder *encoder,
			    enum pipe *pipe)
{
	u8 pipe_mask;
	bool is_mst;

	intel_ddi_get_encoder_pipes(encoder, &pipe_mask, &is_mst);

	if (is_mst || !pipe_mask)
		return false;

	*pipe = ffs(pipe_mask) - 1;

	return true;
}

static void intel_ddi_read_func_ctl(struct intel_encoder *encoder,
				    struct intel_crtc_state *pipe_config)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_crtc *crtc = to_intel_crtc(pipe_config->uapi.crtc);
	enum transcoder cpu_transcoder = pipe_config->cpu_transcoder;
	struct intel_digital_port *dig_port = enc_to_dig_port(encoder);
	u32 temp, flags = 0;

	temp = intel_de_read(dev_priv, TRANS_DDI_FUNC_CTL(cpu_transcoder));
	if (temp & TRANS_DDI_PHSYNC)
		flags |= DRM_MODE_FLAG_PHSYNC;
	else
		flags |= DRM_MODE_FLAG_NHSYNC;
	if (temp & TRANS_DDI_PVSYNC)
		flags |= DRM_MODE_FLAG_PVSYNC;
	else
		flags |= DRM_MODE_FLAG_NVSYNC;

	pipe_config->hw.adjusted_mode.flags |= flags;

	switch (temp & TRANS_DDI_BPC_MASK) {
	case TRANS_DDI_BPC_6:
		pipe_config->pipe_bpp = 18;
		break;
	case TRANS_DDI_BPC_8:
		pipe_config->pipe_bpp = 24;
		break;
	case TRANS_DDI_BPC_10:
		pipe_config->pipe_bpp = 30;
		break;
	case TRANS_DDI_BPC_12:
		pipe_config->pipe_bpp = 36;
		break;
	default:
		break;
	}

	switch (temp & TRANS_DDI_MODE_SELECT_MASK) {
	case TRANS_DDI_MODE_SELECT_HDMI:
		pipe_config->has_hdmi_sink = true;

		pipe_config->infoframes.enable |=
			intel_hdmi_infoframes_enabled(encoder, pipe_config);

		if (pipe_config->infoframes.enable)
			pipe_config->has_infoframe = true;

		if (temp & TRANS_DDI_HDMI_SCRAMBLING)
			pipe_config->hdmi_scrambling = true;
		if (temp & TRANS_DDI_HIGH_TMDS_CHAR_RATE)
			pipe_config->hdmi_high_tmds_clock_ratio = true;
		fallthrough;
	case TRANS_DDI_MODE_SELECT_DVI:
		pipe_config->output_types |= BIT(INTEL_OUTPUT_HDMI);
		if (DISPLAY_VER(dev_priv) >= 14)
			pipe_config->lane_count =
				((temp & DDI_PORT_WIDTH_MASK) >> DDI_PORT_WIDTH_SHIFT) + 1;
		else
			pipe_config->lane_count = 4;
		break;
	case TRANS_DDI_MODE_SELECT_DP_SST:
		if (encoder->type == INTEL_OUTPUT_EDP)
			pipe_config->output_types |= BIT(INTEL_OUTPUT_EDP);
		else
			pipe_config->output_types |= BIT(INTEL_OUTPUT_DP);
		pipe_config->lane_count =
			((temp & DDI_PORT_WIDTH_MASK) >> DDI_PORT_WIDTH_SHIFT) + 1;

		intel_cpu_transcoder_get_m1_n1(crtc, cpu_transcoder,
					       &pipe_config->dp_m_n);
		intel_cpu_transcoder_get_m2_n2(crtc, cpu_transcoder,
					       &pipe_config->dp_m2_n2);

		pipe_config->enhanced_framing =
			intel_de_read(dev_priv, dp_tp_ctl_reg(encoder, pipe_config)) &
			DP_TP_CTL_ENHANCED_FRAME_ENABLE;

		if (DISPLAY_VER(dev_priv) >= 11)
			pipe_config->fec_enable =
				intel_de_read(dev_priv,
					      dp_tp_ctl_reg(encoder, pipe_config)) & DP_TP_CTL_FEC_ENABLE;

		if (dig_port->lspcon.active && intel_dp_has_hdmi_sink(&dig_port->dp))
			pipe_config->infoframes.enable |=
				intel_lspcon_infoframes_enabled(encoder, pipe_config);
		else
			pipe_config->infoframes.enable |=
				intel_hdmi_infoframes_enabled(encoder, pipe_config);
		break;
	case TRANS_DDI_MODE_SELECT_FDI_OR_128B132B:
		if (!HAS_DP20(dev_priv)) {
			/* FDI */
			pipe_config->output_types |= BIT(INTEL_OUTPUT_ANALOG);
			pipe_config->enhanced_framing =
				intel_de_read(dev_priv, dp_tp_ctl_reg(encoder, pipe_config)) &
				DP_TP_CTL_ENHANCED_FRAME_ENABLE;
			break;
		}
		fallthrough; /* 128b/132b */
	case TRANS_DDI_MODE_SELECT_DP_MST:
		pipe_config->output_types |= BIT(INTEL_OUTPUT_DP_MST);
		pipe_config->lane_count =
			((temp & DDI_PORT_WIDTH_MASK) >> DDI_PORT_WIDTH_SHIFT) + 1;

		if (DISPLAY_VER(dev_priv) >= 12)
			pipe_config->mst_master_transcoder =
					REG_FIELD_GET(TRANS_DDI_MST_TRANSPORT_SELECT_MASK, temp);

		intel_cpu_transcoder_get_m1_n1(crtc, cpu_transcoder,
					       &pipe_config->dp_m_n);

		if (DISPLAY_VER(dev_priv) >= 11)
			pipe_config->fec_enable =
				intel_de_read(dev_priv,
					      dp_tp_ctl_reg(encoder, pipe_config)) & DP_TP_CTL_FEC_ENABLE;

		pipe_config->infoframes.enable |=
			intel_hdmi_infoframes_enabled(encoder, pipe_config);
		break;
	default:
		break;
	}
}

static void ddi_dotclock_get(struct intel_crtc_state *pipe_config)
{
	/* CRT dotclock is determined via other means */
	if (pipe_config->has_pch_encoder)
		return;

	pipe_config->hw.adjusted_mode.crtc_clock =
		intel_crtc_dotclock(pipe_config);
}

static void intel_ddi_get_config(struct intel_encoder *encoder,
				 struct intel_crtc_state *pipe_config)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	enum transcoder cpu_transcoder = pipe_config->cpu_transcoder;

	/* XXX: DSI transcoder paranoia */
	if (drm_WARN_ON(&dev_priv->drm, transcoder_is_dsi(cpu_transcoder)))
		return;

	intel_ddi_read_func_ctl(encoder, pipe_config);

	intel_ddi_mso_get_config(encoder, pipe_config);

	pipe_config->has_audio =
		intel_ddi_is_audio_enabled(dev_priv, cpu_transcoder);

	if (encoder->type == INTEL_OUTPUT_EDP)
		intel_edp_fixup_vbt_bpp(encoder, pipe_config->pipe_bpp);

	ddi_dotclock_get(pipe_config);

	if (IS_GEMINILAKE(dev_priv) || IS_BROXTON(dev_priv))
		pipe_config->lane_lat_optim_mask =
			bxt_ddi_phy_get_lane_lat_optim_mask(encoder);

	intel_ddi_compute_min_voltage_level(pipe_config);

	intel_hdmi_read_gcp_infoframe(encoder, pipe_config);

	intel_read_infoframe(encoder, pipe_config,
			     HDMI_INFOFRAME_TYPE_AVI,
			     &pipe_config->infoframes.avi);
	intel_read_infoframe(encoder, pipe_config,
			     HDMI_INFOFRAME_TYPE_SPD,
			     &pipe_config->infoframes.spd);
	intel_read_infoframe(encoder, pipe_config,
			     HDMI_INFOFRAME_TYPE_VENDOR,
			     &pipe_config->infoframes.hdmi);
	intel_read_infoframe(encoder, pipe_config,
			     HDMI_INFOFRAME_TYPE_DRM,
			     &pipe_config->infoframes.drm);

	if (DISPLAY_VER(dev_priv) >= 8)
		bdw_get_trans_port_sync_config(pipe_config);

	intel_read_dp_sdp(encoder, pipe_config, HDMI_PACKET_TYPE_GAMUT_METADATA);
	intel_read_dp_sdp(encoder, pipe_config, DP_SDP_VSC);

	intel_psr_get_config(encoder, pipe_config);

	intel_audio_codec_get_config(encoder, pipe_config);
}

void intel_ddi_get_clock(struct intel_encoder *encoder,
			 struct intel_crtc_state *crtc_state,
			 struct intel_shared_dpll *pll)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	enum icl_port_dpll_id port_dpll_id = ICL_PORT_DPLL_DEFAULT;
	struct icl_port_dpll *port_dpll = &crtc_state->icl_port_dplls[port_dpll_id];
	bool pll_active;

	if (drm_WARN_ON(&i915->drm, !pll))
		return;

	port_dpll->pll = pll;
	pll_active = intel_dpll_get_hw_state(i915, pll, &port_dpll->hw_state);
	drm_WARN_ON(&i915->drm, !pll_active);

	icl_set_active_port_dpll(crtc_state, port_dpll_id);

	crtc_state->port_clock = intel_dpll_get_freq(i915, crtc_state->shared_dpll,
						     &crtc_state->dpll_hw_state);
}

static struct intel_shared_dpll *
_icl_ddi_get_pll(struct drm_i915_private *i915, i915_reg_t reg,
		 u32 clk_sel_mask, u32 clk_sel_shift)
{
	enum intel_dpll_id id;

	id = (intel_de_read(i915, reg) & clk_sel_mask) >> clk_sel_shift;

	return intel_get_shared_dpll_by_id(i915, id);
}

struct intel_shared_dpll *icl_ddi_combo_get_pll(struct intel_encoder *encoder)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	enum phy phy = intel_port_to_phy(i915, encoder->port);

	return _icl_ddi_get_pll(i915, ICL_DPCLKA_CFGCR0,
				ICL_DPCLKA_CFGCR0_DDI_CLK_SEL_MASK(phy),
				ICL_DPCLKA_CFGCR0_DDI_CLK_SEL_SHIFT(phy));
}

static void icl_ddi_combo_get_config(struct intel_encoder *encoder,
				     struct intel_crtc_state *crtc_state)
{
	intel_ddi_get_clock(encoder, crtc_state, icl_ddi_combo_get_pll(encoder));
	intel_ddi_get_config(encoder, crtc_state);
}

static void intel_ddi_sync_state(struct intel_encoder *encoder,
				 const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	enum phy phy = intel_port_to_phy(i915, encoder->port);

	if (intel_phy_is_tc(i915, phy))
		intel_tc_port_sanitize_mode(enc_to_dig_port(encoder),
					    crtc_state);

	if (crtc_state && intel_crtc_has_dp_encoder(crtc_state))
		intel_dp_sync_state(encoder, crtc_state);
}

static void intel_ddi_get_power_domains(struct intel_encoder *encoder,
					struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct intel_digital_port *dig_port;

	/*
	 * TODO: Add support for MST encoders. Atm, the following should never
	 * happen since fake-MST encoders don't set their get_power_domains()
	 * hook.
	 */
	if (drm_WARN_ON(&dev_priv->drm,
			intel_crtc_has_type(crtc_state, INTEL_OUTPUT_DP_MST)))
		return;

	dig_port = enc_to_dig_port(encoder);

	if (!intel_tc_port_in_tbt_alt_mode(dig_port)) {
		drm_WARN_ON(&dev_priv->drm, dig_port->ddi_io_wakeref);
		dig_port->ddi_io_wakeref = intel_display_power_get(dev_priv,
								   dig_port->ddi_io_power_domain);
	}

	main_link_aux_power_domain_get(dig_port, crtc_state);
}

static bool _icl_ddi_is_clock_enabled(struct drm_i915_private *i915, i915_reg_t reg,
				      u32 clk_off)
{
	return !(intel_de_read(i915, reg) & clk_off);
}

static bool icl_ddi_combo_is_clock_enabled(struct intel_encoder *encoder)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	enum phy phy = intel_port_to_phy(i915, encoder->port);

	return _icl_ddi_is_clock_enabled(i915, ICL_DPCLKA_CFGCR0,
					 ICL_DPCLKA_CFGCR0_DDI_CLK_OFF(phy));
}

bool intel_ddi_connector_get_hw_state(struct intel_connector *intel_connector)
{
	struct drm_device *dev = intel_connector->base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	struct intel_encoder *encoder = intel_attached_encoder(intel_connector);
	int type = intel_connector->base.connector_type;
	enum port port = encoder->port;
	enum transcoder cpu_transcoder;
	intel_wakeref_t wakeref;
	enum pipe pipe = 0;
	u32 tmp;
	bool ret;

	wakeref = intel_display_power_get_if_enabled(dev_priv,
						     encoder->power_domain);
	if (!wakeref)
		return false;

	if (!encoder->get_hw_state(encoder, &pipe)) {
		ret = false;
		goto out;
	}

	if (HAS_TRANSCODER(dev_priv, TRANSCODER_EDP) && port == PORT_A)
		cpu_transcoder = TRANSCODER_EDP;
	else
		cpu_transcoder = (enum transcoder) pipe;

	tmp = intel_de_read(dev_priv, TRANS_DDI_FUNC_CTL(cpu_transcoder));

	switch (tmp & TRANS_DDI_MODE_SELECT_MASK) {
	case TRANS_DDI_MODE_SELECT_HDMI:
	case TRANS_DDI_MODE_SELECT_DVI:
		ret = type == DRM_MODE_CONNECTOR_HDMIA;
		break;

	case TRANS_DDI_MODE_SELECT_DP_SST:
		ret = type == DRM_MODE_CONNECTOR_eDP ||
		      type == DRM_MODE_CONNECTOR_DisplayPort;
		break;

	case TRANS_DDI_MODE_SELECT_DP_MST:
		/* if the transcoder is in MST state then
		 * connector isn't connected */
		ret = false;
		break;

	case TRANS_DDI_MODE_SELECT_FDI_OR_128B132B:
		if (HAS_DP20(dev_priv))
			/* 128b/132b */
			ret = false;
		else
			/* FDI */
			ret = type == DRM_MODE_CONNECTOR_VGA;
		break;

	default:
		ret = false;
		break;
	}

out:
	intel_display_power_put(dev_priv, encoder->power_domain, wakeref);

	return ret;
}

void intel_ddi_sanitize_encoder_pll_mapping(struct intel_encoder *encoder)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	u32 port_mask;
	bool ddi_clk_needed;

	/*
	 * In case of DP MST, we sanitize the primary encoder only, not the
	 * virtual ones.
	 */
	if (encoder->type == INTEL_OUTPUT_DP_MST)
		return;

	if (!encoder->base.crtc && intel_encoder_is_dp(encoder)) {
		u8 pipe_mask;
		bool is_mst;

		intel_ddi_get_encoder_pipes(encoder, &pipe_mask, &is_mst);
		/*
		 * In the unlikely case that BIOS enables DP in MST mode, just
		 * warn since our MST HW readout is incomplete.
		 */
		if (drm_WARN_ON(&i915->drm, is_mst))
			return;
	}

	port_mask = BIT(encoder->port);
	ddi_clk_needed = encoder->base.crtc;

	if (encoder->type == INTEL_OUTPUT_DSI) {
		struct intel_encoder *other_encoder;

		port_mask = intel_dsi_encoder_ports(encoder);
		/*
		 * Sanity check that we haven't incorrectly registered another
		 * encoder using any of the ports of this DSI encoder.
		 */
		for_each_intel_encoder(&i915->drm, other_encoder) {
			if (other_encoder == encoder)
				continue;

			if (drm_WARN_ON(&i915->drm,
					port_mask & BIT(other_encoder->port)))
				return;
		}
		/*
		 * For DSI we keep the ddi clocks gated
		 * except during enable/disable sequence.
		 */
		ddi_clk_needed = false;
	}

	if (ddi_clk_needed || !encoder->is_clock_enabled ||
	    !encoder->is_clock_enabled(encoder))
		return;

	drm_notice(&i915->drm,
		   "[ENCODER:%d:%s] is disabled/in DSI mode with an ungated DDI clock, gate it\n",
		   encoder->base.base.id, encoder->base.name);

	encoder->disable_clock(encoder);
}


#include "parity_ddi_emit_glue.inc"	/* zedBSD: builds the encoder / crtc state and records the words */
