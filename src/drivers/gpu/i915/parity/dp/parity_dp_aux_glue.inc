/*
 * WS031 Linux-parity — zedBSD glue at the end of intel_dp_aux_port.c.
 *
 * intel_dp_aux_init() for this platform: the reference picks, for DISPLAY_VER
 * 12..13, the TGL register selectors, and for DISPLAY_VER >= 9 the SKL clock
 * divider and send-control word; then drm_dp_aux_init() and the transfer hook.
 * (The reference function itself is removed because its other branches name the
 * removed per-platform functions.)
 */
void parity_drm_dp_aux_init(struct drm_dp_aux *aux);

void parity_intel_dp_aux_init(struct intel_dp *intel_dp)
{
	intel_dp->aux_ch_ctl_reg = tgl_aux_ctl_reg;
	intel_dp->aux_ch_data_reg = tgl_aux_data_reg;
	intel_dp->get_aux_clock_divider = skl_get_aux_clock_divider;
	intel_dp->get_aux_send_ctl = skl_get_aux_send_ctl;

	parity_drm_dp_aux_init(&intel_dp->aux);
	intel_dp->aux.transfer = intel_dp_aux_transfer;
}
