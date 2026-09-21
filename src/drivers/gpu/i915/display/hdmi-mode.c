/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Derived from the Linux kernel v6.8.12 (drivers/gpu/drm/i915/display/intel_hdmi.c),
 * which carries the following notice.
 *
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
 * 	Eric Anholt <eric@anholt.net>
 * 	Jesse Barnes <jesse.barnes@intel.com>
 */

/*
 * The HDMI mode half of the one-screen modeset path (see hdmi-mode.h).
 *
 * The functions follow the Linux 6.8.12 intel_hdmi.c text: the infoframe
 * hook of an HDMI port on a DDI platform (hsw_set_infoframes), the DP
 * dual-mode adaptor's TMDS output switch and the sink's scrambling and TMDS
 * clock ratio.  The sink-side halves that need the sink's DDC (SCDC, the
 * dual-mode adaptor, the GCP and the infoframe packing) are not ported:
 * they are reported as named steps where the Linux text calls them (see
 * modeset-internal.h).
 */

#include "internal.h"
#include "modeset-internal.h"
#include "../data/display-ddi-regs.inc"
#include "../data/display-mreg-hdmi-dip.inc"
#include "hdmi-mode.h"

static struct drm_i915_private *i915_hdmi_cur_i915(const struct intel_hdmi *hdmi);
static void i915_assert_hdmi_transcoder_func_disabled(struct drm_i915_private *dev_priv, enum transcoder cpu_transcoder);
static void i915_hsw_set_infoframes(struct intel_encoder *encoder, bool enable, const struct intel_crtc_state *crtc_state, const struct drm_connector_state *conn_state);

/*
 * Switches the TMDS output of a DP dual-mode adaptor on or off.
 *
 * The Linux intel_dp_dual_mode_set_tmds_output(): only a type 2 adaptor has
 * a switchable output.  The switch itself is not ported and is reported as
 * a step.
 */
void
drv_i915_dp_dual_mode_set_tmds_output(
	struct intel_hdmi *hdmi,
	bool enable)
{
	struct drm_i915_private *cur_i915;
	struct drm_i915_private *dev_priv;
	/* Named for the unported switch, which does not read it. */
	struct i2c_adapter *ddc __maybe_unused;

	/* Finds the device of the port and the DDC bus of its sink. */
	cur_i915 = i915_hdmi_cur_i915(hdmi);
	dev_priv = i915_lcd_intel_hdmi_to_i915(cur_i915, hdmi);
	ddc = hdmi->attached_connector->base.ddc;

	/* Only a type 2 adaptor can switch its TMDS output. */
	if (hdmi->dp_dual_mode.type < DRM_DP_DUAL_MODE_TYPE2_DVI)
		return;

	/* Notes the switch. */
	I915_LCD_DRM_DBG_KMS(&dev_priv->drm,
			     "%s DP dual mode adaptor TMDS output\n",
			     enable ? "Enabling" : "Disabling");

	/* Asks the adaptor to switch its output (dev_priv is the modeset's device here). */
	I915_LCD_DRM_DP_DUAL_MODE_SET_TMDS_OUTPUT(dev_priv,
						  &dev_priv->drm,
						  hdmi->dp_dual_mode.type,
						  ddc,
						  enable);
}

/*
 * Sets up the sink's scrambling and TMDS bit clock ratio.
 *
 * The Linux intel_hdmi_handle_sink_scrambling(): on an HDMI 2.0 sink that
 * supports scrambling, a TMDS clock above 340 MHz needs scrambling and the
 * 1/40 clock ratio.  It is called before the HDMI 2.0 port is enabled, as
 * the sink may turn scrambling off again if it sees no scrambled clock
 * within 100 ms.  Returns true on success, false on failure; a sink
 * without scrambling support needs nothing and succeeds.
 */
bool
drv_i915_hdmi_handle_sink_scrambling(
	struct intel_encoder *encoder,
	struct drm_connector *connector,
	bool high_tmds_clock_ratio,
	bool scrambling)
{
	/* Named for the debug line, which does not evaluate its device. */
	struct drm_i915_private *dev_priv __maybe_unused;
	struct drm_i915_private *cur_i915;
	struct drm_scrambling *sink_scrambling;
	bool ratio_set;
	bool scrambling_set;

	/* Finds the device of the encoder, the device of the modeset and the sink's capabilities. */
	dev_priv = i915_lcd_to_i915(encoder->base.dev);
	cur_i915 = i915_hdmi_cur_i915(&i915_lcd_enc_to_dig_port(encoder)->hdmi);
	sink_scrambling = &connector->display_info.hdmi.scdc.scrambling;

	/* A sink without scrambling support needs nothing. */
	if (!sink_scrambling->supported)
		return true;

	/* Notes what the sink is asked for. */
	I915_LCD_DRM_DBG_KMS(&dev_priv->drm,
			     "[CONNECTOR:%d:%s] scrambling=%s, TMDS bit clock ratio=1/%d\n",
			     connector->base.id,
			     connector->name,
			     str_yes_no(scrambling),
			     high_tmds_clock_ratio ? 40 : 10);

	/* Set TMDS bit clock ratio to 1/40 or 1/10 */
	ratio_set = I915_LCD_DRM_SCDC_SET_HIGH_TMDS_CLOCK_RATIO(cur_i915,
							       connector,
							       high_tmds_clock_ratio);
	if (!ratio_set)
		return false;

	/* ... and enable/disable scrambling, only once the ratio is set. */
	scrambling_set = I915_LCD_DRM_SCDC_SET_SCRAMBLING(cur_i915, connector, scrambling);
	if (!scrambling_set)
		return false;

	/* Succeeded: the sink has the ratio and the scrambling asked for. */
	return true;
}

/*
 * Returns the infoframe writer of an HDMI port.
 *
 * The digital port carries it as dig_port->set_infoframes; it is the Linux
 * hsw_set_infoframes().
 */
void
(*drv_i915_lcd_hdmi_set_infoframes(void))(
	struct intel_encoder *encoder,
	bool enable,
	const struct intel_crtc_state *crtc_state,
	const struct drm_connector_state *conn_state)
{
	/* Succeeded: the writer of display version 9 and later. */
	return i915_hsw_set_infoframes;
}

/*
 * Switches the TMDS output of the port's DP dual-mode adaptor.
 *
 * The entry the modeset object binds; see
 * drv_i915_dp_dual_mode_set_tmds_output().
 */
void
drv_i915_lcd_hdmi_tmds_output(
	struct intel_hdmi *hdmi,
	bool enable)
{
	/* Switches the adaptor's output. */
	drv_i915_dp_dual_mode_set_tmds_output(hdmi, enable);
}

/* The device the Linux text reached through the file-scope pointer: the one recorded in the port's world. */
static struct drm_i915_private *
i915_hdmi_cur_i915(
	const struct intel_hdmi *hdmi)
{
	const struct i915_lcd_modeset *ms;

	/* The HDMI half belongs to the digital port of a modeset object. */
	ms = container_of(hdmi, struct i915_lcd_modeset, dig_port.hdmi);

	/* Reports the device the entry point recorded. */
	return ms->world->i915_lcd_cur_i915;
}

/* Warns when the transcoder's DDI function is still enabled while its infoframes change. */
static void
i915_assert_hdmi_transcoder_func_disabled(
	struct drm_i915_private *dev_priv,
	enum transcoder cpu_transcoder)
{
	/* The infoframe setup expects the transcoder's DDI function off. */
	I915_LCD_DRM_WARN(&dev_priv->drm,
			  i915_lcd_intel_de_read(dev_priv, TRANS_DDI_FUNC_CTL(cpu_transcoder)) & TRANS_DDI_FUNC_ENABLE,
			  "HDMI transcoder function enabled, expecting disabled\n");
}

/* Sets up or disables the infoframes of an HDMI port (the Linux hsw_set_infoframes()). */
static void
i915_hsw_set_infoframes(
	struct intel_encoder *encoder,
	bool enable,
	const struct intel_crtc_state *crtc_state,
	const struct drm_connector_state *conn_state)
{
	struct drm_i915_private *dev_priv;
	struct drm_i915_private *cur_i915;
	i915_reg_t reg;
	u32 val;
	bool gcp;

	UNUSED_PARAMETER(conn_state);

	/* Finds the device of the encoder and the device of the modeset. */
	dev_priv = i915_lcd_to_i915(encoder->base.dev);
	cur_i915 = i915_hdmi_cur_i915(&i915_lcd_enc_to_dig_port(encoder)->hdmi);

	/* Reads the transcoder's DIP control. */
	reg = HSW_TVIDEO_DIP_CTL(crtc_state->cpu_transcoder);
	val = i915_lcd_intel_de_read(dev_priv, reg);

	/* Checks that the transcoder's DDI function is off. */
	i915_assert_hdmi_transcoder_func_disabled(dev_priv, crtc_state->cpu_transcoder);

	/* Stops every infoframe the port may send. */
	val &= ~(VIDEO_DIP_ENABLE_VSC_HSW | VIDEO_DIP_ENABLE_AVI_HSW |
		 VIDEO_DIP_ENABLE_GCP_HSW | VIDEO_DIP_ENABLE_VS_HSW |
		 VIDEO_DIP_ENABLE_GMP_HSW | VIDEO_DIP_ENABLE_SPD_HSW |
		 VIDEO_DIP_ENABLE_DRM_GLK);

	/* A disable only writes the stopped infoframes back. */
	if (!enable) {
		i915_lcd_intel_de_write(dev_priv, reg, val);
		i915_lcd_intel_de_posting_read(dev_priv, reg);
		return;
	}

	/* Writes the general control packet and sends it if the state asks for one. */
	gcp = I915_LCD_INTEL_HDMI_SET_GCP_INFOFRAME(cur_i915, encoder, crtc_state, conn_state);
	if (gcp)
		val |= VIDEO_DIP_ENABLE_GCP_HSW;

	/* Writes the DIP control. */
	i915_lcd_intel_de_write(dev_priv, reg, val);
	i915_lcd_intel_de_posting_read(dev_priv, reg);

	/* Writes the AVI, SPD, vendor and DRM infoframes of the state. */
	I915_LCD_INTEL_WRITE_INFOFRAME(cur_i915,
				       encoder,
				       crtc_state,
				       HDMI_INFOFRAME_TYPE_AVI,
				       &crtc_state->infoframes.avi);
	I915_LCD_INTEL_WRITE_INFOFRAME(cur_i915,
				       encoder,
				       crtc_state,
				       HDMI_INFOFRAME_TYPE_SPD,
				       &crtc_state->infoframes.spd);
	I915_LCD_INTEL_WRITE_INFOFRAME(cur_i915,
				       encoder,
				       crtc_state,
				       HDMI_INFOFRAME_TYPE_VENDOR,
				       &crtc_state->infoframes.hdmi);
	I915_LCD_INTEL_WRITE_INFOFRAME(cur_i915,
				       encoder,
				       crtc_state,
				       HDMI_INFOFRAME_TYPE_DRM,
				       &crtc_state->infoframes.drm);
}
