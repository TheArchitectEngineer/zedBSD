/*
 * WS031 Linux-parity — zedBSD glue at the end of intel_color_port.c: the colour hooks of display version 12+
 * (the reference's tgl_color_funcs: icl_color_commit_noarm / icl_color_commit_arm / icl_load_luts) and the
 * part of its .color_check (icl_color_check) that computes the two mode words.  No LUT and no CTM exist in
 * this path, so check_luts / intel_assign_luts / icl_assign_csc have nothing to work on.
 */
#include "lcd_dp_compat.h"
#include "lcd_plane_compat.h"
#include "parity_lcd_modeset_int.h"

static const struct intel_color_funcs parity_tgl_color_funcs = {
	.color_commit_noarm = icl_color_commit_noarm,
	.color_commit_arm = icl_color_commit_arm,
	.load_luts = icl_load_luts,
};

/* the same table for a device that is not a modeset object (the N1 readout: the sanitize of an ACTIVE
 * crtc commits the colour registers through it) */
const struct intel_color_funcs *parity_lcd_ms_color_funcs(void)
{
	return &parity_tgl_color_funcs;
}

void parity_lcd_ms_color_check(struct parity_lcd_modeset *ms)
{
	ms->i915.display.funcs.color = &parity_tgl_color_funcs;
	ms->crtc_state.gamma_mode = icl_gamma_mode(&ms->crtc_state);
	ms->crtc_state.csc_mode = icl_csc_mode(&ms->crtc_state);
}
