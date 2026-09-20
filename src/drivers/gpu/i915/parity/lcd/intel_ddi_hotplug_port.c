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
 *  - kept: intel_ddi_hotplug, lpt_digital_port_connected;
 *  - the includes are replaced by: hpd_compat.h;
 *  - parity_ddi_hotplug_glue.inc (zedBSD code) is included at the end of the file.
 */

#include "hpd_compat.h"

/* zedBSD: forward declarations of the static functions of this file (generated; the keep-list is not in call order) */
static enum intel_hotplug_state
intel_ddi_hotplug(struct intel_encoder *encoder,
		  struct intel_connector *connector);
static bool lpt_digital_port_connected(struct intel_encoder *encoder);

static enum intel_hotplug_state
intel_ddi_hotplug(struct intel_encoder *encoder,
		  struct intel_connector *connector)
{
	struct drm_i915_private *i915 = to_i915(encoder->base.dev);
	struct intel_digital_port *dig_port = enc_to_dig_port(encoder);
	struct intel_dp *intel_dp = &dig_port->dp;
	enum phy phy = intel_port_to_phy(i915, encoder->port);
	bool is_tc = intel_phy_is_tc(i915, phy);
	struct drm_modeset_acquire_ctx ctx;
	enum intel_hotplug_state state;
	int ret;

	if (intel_dp->compliance.test_active &&
	    intel_dp->compliance.test_type == DP_TEST_LINK_PHY_TEST_PATTERN) {
		intel_dp_phy_test(encoder);
		/* just do the PHY test and nothing else */
		return INTEL_HOTPLUG_UNCHANGED;
	}

	state = intel_encoder_hotplug(encoder, connector);

	if (!intel_tc_port_link_reset(dig_port)) {
		intel_modeset_lock_ctx_retry(&ctx, NULL, 0, ret) {
			if (connector->base.connector_type == DRM_MODE_CONNECTOR_HDMIA)
				ret = intel_hdmi_reset_link(encoder, &ctx);
			else
				ret = intel_dp_retrain_link(encoder, &ctx);
		}

		drm_WARN_ON(encoder->base.dev, ret);
	}

	/*
	 * Unpowered type-c dongles can take some time to boot and be
	 * responsible, so here giving some time to those dongles to power up
	 * and then retrying the probe.
	 *
	 * On many platforms the HDMI live state signal is known to be
	 * unreliable, so we can't use it to detect if a sink is connected or
	 * not. Instead we detect if it's connected based on whether we can
	 * read the EDID or not. That in turn has a problem during disconnect,
	 * since the HPD interrupt may be raised before the DDC lines get
	 * disconnected (due to how the required length of DDC vs. HPD
	 * connector pins are specified) and so we'll still be able to get a
	 * valid EDID. To solve this schedule another detection cycle if this
	 * time around we didn't detect any change in the sink's connection
	 * status.
	 *
	 * Type-c connectors which get their HPD signal deasserted then
	 * reasserted, without unplugging/replugging the sink from the
	 * connector, introduce a delay until the AUX channel communication
	 * becomes functional. Retry the detection for 5 seconds on type-c
	 * connectors to account for this delay.
	 */
	if (state == INTEL_HOTPLUG_UNCHANGED &&
	    connector->hotplug_retries < (is_tc ? 5 : 1) &&
	    !dig_port->dp.is_mst)
		state = INTEL_HOTPLUG_RETRY;

	return state;
}

static bool lpt_digital_port_connected(struct intel_encoder *encoder)
{
	struct drm_i915_private *dev_priv = to_i915(encoder->base.dev);
	u32 bit = dev_priv->display.hotplug.pch_hpd[encoder->hpd_pin];

	return intel_de_read(dev_priv, SDEISR) & bit;
}


#include "parity_ddi_hotplug_glue.inc"
