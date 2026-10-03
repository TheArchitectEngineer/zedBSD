/*
 * Copyright 2006 Dave Airlie <airlied@linux.ie>
 * Copyright © 2006-2009 Intel Corporation
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *	Eric Anholt <eric@anholt.net>
 *	Jesse Barnes <jesse.barnes@intel.com>
 */

/*
 * zedBSD WS031: generated from Linux v6.8.12 drivers/gpu/drm/i915/display/intel_hdmi.c
 * (sha256 200e4d8f5842ccc549196c503605560bda5ee5fddfb11633cd93f474c1ac824d) by plan/ws031/handover/tools/port_lcd_calc.py.
 * The function bodies are the reference text.  Kept / changed:
 *  - kept: assert_hdmi_transcoder_func_disabled, hsw_set_infoframes, intel_dp_dual_mode_set_tmds_output, intel_hdmi_handle_sink_scrambling;
 *  - the includes are replaced by: lcd_compat.h, lcd_ddi_regs.h, lcd_mreg_hdmi_dip.h, lcd_seq_compat.h, lcd_modeset_compat.h;
 *  - parity_hdmi_mode_glue.inc (zedBSD code) is included at the end of the file.
 */

#include "lcd_compat.h"
#include "lcd_ddi_regs.h"
#include "lcd_mreg_hdmi_dip.h"
#include "lcd_seq_compat.h"
#include "lcd_modeset_compat.h"

/* zedBSD: forward declarations of the static functions of this file (generated; the keep-list is not in call order) */
static void
assert_hdmi_transcoder_func_disabled(struct drm_i915_private *dev_priv,
				     enum transcoder cpu_transcoder);
static void hsw_set_infoframes(struct intel_encoder *encoder,
			       bool enable,
			       const struct intel_crtc_state *crtc_state,
			       const struct drm_connector_state *conn_state);

static void
assert_hdmi_transcoder_func_disabled(struct drm_i915_private *dev_priv,
				     enum transcoder cpu_transcoder)
{
	drm_WARN(&dev_priv->drm,
		 intel_de_read(dev_priv, TRANS_DDI_FUNC_CTL(cpu_transcoder)) &
		 TRANS_DDI_FUNC_ENABLE,
		 "HDMI transcoder function enabled, expecting disabled\n");
}

static void hsw_set_infoframes(struct intel_encoder *encoder,
			       bool enable,
			       const struct intel_crtc_state *crtc_state,
			       const struct drm_connector_state *conn_state)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	i915_reg_t reg = HSW_TVIDEO_DIP_CTL(crtc_state->cpu_transcoder);
	u32 val = intel_de_read(dev_priv, reg);

	assert_hdmi_transcoder_func_disabled(dev_priv,
					     crtc_state->cpu_transcoder);

	val &= ~(VIDEO_DIP_ENABLE_VSC_HSW | VIDEO_DIP_ENABLE_AVI_HSW |
		 VIDEO_DIP_ENABLE_GCP_HSW | VIDEO_DIP_ENABLE_VS_HSW |
		 VIDEO_DIP_ENABLE_GMP_HSW | VIDEO_DIP_ENABLE_SPD_HSW |
		 VIDEO_DIP_ENABLE_DRM_GLK);

	if (!enable) {
		intel_de_write(dev_priv, reg, val);
		intel_de_posting_read(dev_priv, reg);
		return;
	}

	if (intel_hdmi_set_gcp_infoframe(encoder, crtc_state, conn_state))
		val |= VIDEO_DIP_ENABLE_GCP_HSW;

	intel_de_write(dev_priv, reg, val);
	intel_de_posting_read(dev_priv, reg);

	intel_write_infoframe(encoder, crtc_state,
			      HDMI_INFOFRAME_TYPE_AVI,
			      &crtc_state->infoframes.avi);
	intel_write_infoframe(encoder, crtc_state,
			      HDMI_INFOFRAME_TYPE_SPD,
			      &crtc_state->infoframes.spd);
	intel_write_infoframe(encoder, crtc_state,
			      HDMI_INFOFRAME_TYPE_VENDOR,
			      &crtc_state->infoframes.hdmi);
	intel_write_infoframe(encoder, crtc_state,
			      HDMI_INFOFRAME_TYPE_DRM,
			      &crtc_state->infoframes.drm);
}

void intel_dp_dual_mode_set_tmds_output(struct intel_hdmi *hdmi, bool enable)
{
	struct drm_i915_private *dev_priv = intel_hdmi_to_i915(hdmi);
	struct i2c_adapter *ddc = hdmi->attached_connector->base.ddc;

	if (hdmi->dp_dual_mode.type < DRM_DP_DUAL_MODE_TYPE2_DVI)
		return;

	drm_dbg_kms(&dev_priv->drm, "%s DP dual mode adaptor TMDS output\n",
		    enable ? "Enabling" : "Disabling");

	drm_dp_dual_mode_set_tmds_output(&dev_priv->drm,
					 hdmi->dp_dual_mode.type, ddc, enable);
}

/*
 * intel_hdmi_handle_sink_scrambling: handle sink scrambling/clock ratio setup
 * @encoder: intel_encoder
 * @connector: drm_connector
 * @high_tmds_clock_ratio = bool to indicate if the function needs to set
 *  or reset the high tmds clock ratio for scrambling
 * @scrambling: bool to Indicate if the function needs to set or reset
 *  sink scrambling
 *
 * This function handles scrambling on HDMI 2.0 capable sinks.
 * If required clock rate is > 340 Mhz && scrambling is supported by sink
 * it enables scrambling. This should be called before enabling the HDMI
 * 2.0 port, as the sink can choose to disable the scrambling if it doesn't
 * detect a scrambled clock within 100 ms.
 *
 * Returns:
 * True on success, false on failure.
 */
bool intel_hdmi_handle_sink_scrambling(struct intel_encoder *encoder,
				       struct drm_connector *connector,
				       bool high_tmds_clock_ratio,
				       bool scrambling)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	struct drm_scrambling *sink_scrambling =
		&connector->display_info.hdmi.scdc.scrambling;

	if (!sink_scrambling->supported)
		return true;

	drm_dbg_kms(&dev_priv->drm,
		    "[CONNECTOR:%d:%s] scrambling=%s, TMDS bit clock ratio=1/%d\n",
		    connector->base.id, connector->name,
		    str_yes_no(scrambling), high_tmds_clock_ratio ? 40 : 10);

	/* Set TMDS bit clock ratio to 1/40 or 1/10, and enable/disable scrambling */
	return drm_scdc_set_high_tmds_clock_ratio(connector, high_tmds_clock_ratio) &&
		drm_scdc_set_scrambling(connector, scrambling);
}


#include "parity_hdmi_mode_glue.inc"
