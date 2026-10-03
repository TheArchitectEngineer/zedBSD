/*
 * WS031 E-123 -- zedBSD glue of intel_ddi_hotplug_port.c (included at its end).  zedBSD project code.
 * The static callbacks of the unit for the object setup, and what intel_ddi_hotplug() calls around the detection.
 */

enum intel_hotplug_state (*parity_hpd_ddi_hotplug_fn(void))(struct intel_encoder *, struct intel_connector *)
{
	return intel_ddi_hotplug;
}

bool (*parity_hpd_lpt_connected_fn(void))(struct intel_encoder *)
{
	return lpt_digital_port_connected;
}

/* intel_port_to_phy() / intel_phy_is_tc(), ADL-P: the TC ports D.. map onto PHY_F.., PHY_F..PHY_I are Type-C */
enum phy intel_port_to_phy(struct drm_i915_private *i915, enum port port)
{
	if (port >= PORT_TC1)
		return (enum phy)(PHY_F + port - PORT_TC1);
	return (enum phy)(PHY_A + port - PORT_A);
}

bool intel_phy_is_tc(struct drm_i915_private *i915, enum phy phy)
{
	return phy >= PHY_F && phy <= PHY_I;
}

/* intel_tc_port_link_reset(): false unless the port is Type-C; the TC link reset itself is not ported (step) */
bool intel_tc_port_link_reset(struct intel_digital_port *dig_port)
{
	if (!intel_phy_is_tc(NULL, intel_port_to_phy(NULL, dig_port->base.port)))
		return false;
	kern_logf("i915: parity hpd step intel_tc_port_link_reset (unported): %s -> false\n", dig_port->base.base.name);
	return false;
}

void intel_dp_phy_test(struct intel_encoder *encoder)
{
	kern_logf("i915: parity hpd step intel_dp_phy_test (unported): %s\n", encoder->base.name);
}

int intel_dp_retrain_link(struct intel_encoder *encoder, struct drm_modeset_acquire_ctx *ctx)
{
	kern_logf("i915: parity hpd step intel_dp_retrain_link (unported): %s -> 0\n", encoder->base.name);
	return 0;
}

/*
 * intel_hdmi_reset_link(): the reference's own early returns (the sink is not connected; no crtc drives the
 * connector) are kept; past them the SCDC TMDS-config comparison and the modeset are a recorded step (they need an
 * active HDMI crtc, which comes with the external-display modeset).
 */
static int intel_hdmi_reset_link(struct intel_encoder *encoder, struct drm_modeset_acquire_ctx *ctx)
{
	struct intel_hdmi *hdmi = &enc_to_dig_port(encoder)->hdmi;
	struct intel_connector *connector = hdmi->attached_connector;

	if (connector->base.status != connector_status_connected)
		return 0;
	if (connector->base.state == NULL || connector->base.state->crtc == NULL)
		return 0;
	kern_logf("i915: parity hpd step intel_hdmi_reset_link SCDC check (unported): %s\n", connector->base.name);
	return 0;
}
