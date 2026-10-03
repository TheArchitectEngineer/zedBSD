/*
 * WS031 Linux-parity -- zedBSD glue at the end of intel_crtc_port.c: the vblank bookkeeping the update helpers read,
 * and the reference's update of a running crtc reduced to its one primary plane (intel_update_crtc():
 * intel_crtc_planes_update_noarm -> intel_pipe_update_start -> commit_pipe_pre_planes (nothing for a plane-only
 * update) -> intel_crtc_planes_update_arm -> commit_pipe_post_planes (nothing) -> intel_pipe_update_end).
 */
#include "lcd_plane_compat.h"
#include "parity_lcd_modeset_int.h"

int parity_lcd_flip_pipe;

/* i915's crtc funcs on display version 5+: the hardware frame counter (g4x_get_vblank_counter) */
static const struct drm_crtc_funcs parity_lcd_crtc_funcs = { g4x_get_vblank_counter };

/* the same table for a crtc that is not a modeset object (the N1 readout registry) */
const struct drm_crtc_funcs *parity_lcd_ms_crtc_funcs(void)
{
	return &parity_lcd_crtc_funcs;
}

/* intel_enable_crtc(): intel_crtc_update_active_timings(new_crtc_state, false) before the crtc_enable hook;
 * drm_crtc_set_max_vblank_count(intel_crtc_max_vblank_count()) = the full 32-bit counter (not DSI) */
void parity_lcd_ms_active_timings(struct parity_lcd_modeset *ms)
{
	parity_lcd_cur_i915 = &ms->i915;
	ms->i915.drm.vblank = ms->vblank;
	ms->vblank[ms->crtc.pipe].max_vblank_count = 0xffffffffu;
	ms->crtc.base.funcs = &parity_lcd_crtc_funcs;
	intel_crtc_update_active_timings(&ms->crtc_state, false);
}

/* local_irq_* over ops->irq_off / irq_on, with the state the kernel primitives keep (save / restore nest) */
static int parity_lcd_irqs_off;

void parity_lcd_irq_disable(void)
{
	if (!parity_lcd_irqs_off)
		FLIP_OPS(parity_lcd_cur_i915)->irq_off(FLIP_OPS(parity_lcd_cur_i915)->ctx);
	parity_lcd_irqs_off = 1;
}

void parity_lcd_irq_enable(void)
{
	if (parity_lcd_irqs_off)
		FLIP_OPS(parity_lcd_cur_i915)->irq_on(FLIP_OPS(parity_lcd_cur_i915)->ctx);
	parity_lcd_irqs_off = 0;
}

unsigned long parity_lcd_irq_save(void)
{
	unsigned long was_off = (unsigned long)parity_lcd_irqs_off;

	parity_lcd_irq_disable();
	return was_off;
}

void parity_lcd_irq_restore(unsigned long was_off)
{
	if (!was_off)
		parity_lcd_irq_enable();
}

/* intel_crtc_vblank_evade_scanlines() of the running mode: the window intel_pipe_update_start() sleeps out of */
int parity_lcd_ms_evade_window(struct parity_lcd_modeset *ms, int *min, int *max, int *vblank_start)
{
	parity_lcd_cur_i915 = &ms->i915;
	ms->state.base.dev = &ms->i915.drm;
	ms->state.crtc_state = &ms->crtc_state;
	ms->state.old_crtc_state = &ms->crtc_state;
	intel_crtc_vblank_evade_scanlines(&ms->state, &ms->crtc, min, max, vblank_start);
	return *min > 0 && *max > 0 ? 0 : -1;
}

/* E-124 diagnostics: the size of a crtc state AS THIS UNIT SEES IT (it memsets one) */
unsigned long parity_crtc_state_size_crtc_unit(void)
{
	return (unsigned long)sizeof(struct intel_crtc_state);
}
