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
 *  - kept: intel_hdmi_unset_edid, intel_hdmi_set_edid, intel_hdmi_detect;
 *  - the includes are replaced by: hpd_compat.h;
 *  - parity_hdmi_detect_glue.inc (zedBSD code) is included at the end of the file.
 */

#include "hpd_compat.h"

/* zedBSD: forward declarations of the static functions of this file (generated; the keep-list is not in call order) */
static void
intel_hdmi_unset_edid(struct drm_connector *connector);
static bool
intel_hdmi_set_edid(struct drm_connector *connector);
static enum drm_connector_status
intel_hdmi_detect(struct drm_connector *connector, bool force);

static void
intel_hdmi_unset_edid(struct drm_connector *connector)
{
	struct intel_hdmi *intel_hdmi = intel_attached_hdmi(to_intel_connector(connector));

	intel_hdmi->dp_dual_mode.type = DRM_DP_DUAL_MODE_NONE;
	intel_hdmi->dp_dual_mode.max_tmds_clock = 0;

	drm_edid_free(to_intel_connector(connector)->detect_edid);
	to_intel_connector(connector)->detect_edid = NULL;
}

static bool
intel_hdmi_set_edid(struct drm_connector *connector)
{
	struct drm_i915_private *dev_priv = to_i915(connector->dev);
	struct intel_hdmi *intel_hdmi = intel_attached_hdmi(to_intel_connector(connector));
	struct i2c_adapter *ddc = connector->ddc;
	intel_wakeref_t wakeref;
	const struct drm_edid *drm_edid;
	bool connected = false;

	wakeref = intel_display_power_get(dev_priv, POWER_DOMAIN_GMBUS);

	drm_edid = drm_edid_read_ddc(connector, ddc);

	if (!drm_edid && !intel_gmbus_is_forced_bit(ddc)) {
		drm_dbg_kms(&dev_priv->drm,
			    "HDMI GMBUS EDID read failed, retry using GPIO bit-banging\n");
		intel_gmbus_force_bit(ddc, true);
		drm_edid = drm_edid_read_ddc(connector, ddc);
		intel_gmbus_force_bit(ddc, false);
	}

	/* Below we depend on display info having been updated */
	drm_edid_connector_update(connector, drm_edid);

	to_intel_connector(connector)->detect_edid = drm_edid;

	if (drm_edid_is_digital(drm_edid)) {
		intel_hdmi_dp_dual_mode_detect(connector);

		connected = true;
	}

	intel_display_power_put(dev_priv, POWER_DOMAIN_GMBUS, wakeref);

	cec_notifier_set_phys_addr(intel_hdmi->cec_notifier,
				   connector->display_info.source_physical_address);

	return connected;
}

static enum drm_connector_status
intel_hdmi_detect(struct drm_connector *connector, bool force)
{
	enum drm_connector_status status = connector_status_disconnected;
	struct drm_i915_private *dev_priv = to_i915(connector->dev);
	struct intel_hdmi *intel_hdmi = intel_attached_hdmi(to_intel_connector(connector));
	struct intel_encoder *encoder = &hdmi_to_dig_port(intel_hdmi)->base;
	intel_wakeref_t wakeref;

	drm_dbg_kms(&dev_priv->drm, "[CONNECTOR:%d:%s]\n",
		    connector->base.id, connector->name);

	if (!intel_display_device_enabled(dev_priv))
		return connector_status_disconnected;

	wakeref = intel_display_power_get(dev_priv, POWER_DOMAIN_GMBUS);

	if (DISPLAY_VER(dev_priv) >= 11 &&
	    !intel_digital_port_connected(encoder))
		goto out;

	intel_hdmi_unset_edid(connector);

	if (intel_hdmi_set_edid(connector))
		status = connector_status_connected;

out:
	intel_display_power_put(dev_priv, POWER_DOMAIN_GMBUS, wakeref);

	if (status != connector_status_connected)
		cec_notifier_phys_addr_invalidate(intel_hdmi->cec_notifier);

	return status;
}


#include "parity_hdmi_detect_glue.inc"
