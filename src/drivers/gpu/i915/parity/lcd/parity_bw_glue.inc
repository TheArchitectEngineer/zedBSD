/*
 * WS031 Linux-parity -- zedBSD glue at the end of intel_bw_port.c: the one crtc's share of the reference's
 * intel_bw_calc_min_cdclk() and of intel_bw_data_rate() (kB/s -> MB/s as intel_bw_check_qgv_points() rounds it).
 */
#include "lcd_dp_compat.h"
#include "parity_lcd_modeset_int.h"

int parity_lcd_ms_bw_min_cdclk(struct parity_lcd_modeset *ms)
{
	return intel_bw_crtc_min_cdclk(&ms->crtc_state);
}

unsigned int parity_lcd_ms_bw_data_rate(struct parity_lcd_modeset *ms)
{
	return DIV_ROUND_UP(intel_bw_crtc_data_rate(&ms->crtc_state), 1000);
}
