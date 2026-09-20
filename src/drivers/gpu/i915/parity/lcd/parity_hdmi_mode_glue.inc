/*
 * WS031 E-123 -- zedBSD glue of intel_hdmi_mode_port.c (included at its end).  zedBSD project code.
 * The infoframe hook the digital port carries (dig_port->set_infoframes) and the entry the modeset object binds.
 */
void (*parity_lcd_hdmi_set_infoframes(void))(struct intel_encoder *, bool, const struct intel_crtc_state *,
	const struct drm_connector_state *)
{
	return hsw_set_infoframes;
}

void parity_lcd_hdmi_tmds_output(struct intel_hdmi *hdmi, bool enable)
{
	intel_dp_dual_mode_set_tmds_output(hdmi, enable);
}
