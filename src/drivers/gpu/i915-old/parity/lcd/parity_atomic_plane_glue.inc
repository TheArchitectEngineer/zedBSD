/*
 * WS031 Linux-parity — zedBSD glue at the end of intel_atomic_plane_port.c: the two assignments of
 * intel_plane_atomic_check_with_state() that give a visible plane its data rates (the DDB allocation weighs
 * planes by rel_data_rate).
 */
#include "lcd_dp_compat.h"
#include "parity_lcd_modeset_int.h"

void parity_lcd_ms_plane_data_rates(struct parity_lcd_modeset *ms)
{
	struct intel_crtc_state *cs = &ms->crtc_state;

	cs->data_rate[ms->plane.id] = intel_plane_data_rate(cs, &ms->plane_state, 0);
	cs->rel_data_rate[ms->plane.id] = intel_plane_relative_data_rate(cs, &ms->plane_state, 0);
}
