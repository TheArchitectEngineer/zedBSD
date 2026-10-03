// SPDX-License-Identifier: MIT
/*
 * Copyright © 2020 Intel Corporation
 */

/*
 * zedBSD WS031: generated from Linux v6.8.12 drivers/gpu/drm/i915/display/intel_crtc.c
 * (sha256 ff4d1d58a92d4e8a1f8890f20fc7d382e7833778f19bd9c22d2a7a57aeef1d82) by plan/ws031/handover/tools/port_lcd_calc.py.
 * The function bodies are the reference text.  Kept / changed:
 *  - kept: intel_crtc_state_reset, intel_usecs_to_scanlines, intel_crtc_get_vblank_counter, intel_crtc_needs_vblank_work,
 *    intel_mode_vblank_start, intel_crtc_vblank_evade_scanlines, intel_pipe_update_start, intel_pipe_update_end,
 *    intel_crtc_wait_for_next_vblank;
 *  - the includes are replaced by: lcd_compat.h, lcd_seq_compat.h, lcd_modeset_compat.h, lcd_flip_compat.h;
 *  - parity_flip_glue.inc (zedBSD code) is included at the end of the file.
 */

#include "lcd_compat.h"
#include "lcd_seq_compat.h"
#include "lcd_modeset_compat.h"
#include "lcd_flip_compat.h"

/* zedBSD: forward declarations of the static functions of this file (generated; the keep-list is not in call order) */
static bool intel_crtc_needs_vblank_work(const struct intel_crtc_state *crtc_state);
static int intel_mode_vblank_start(const struct drm_display_mode *mode);
static void intel_crtc_vblank_evade_scanlines(struct intel_atomic_state *state,
					      struct intel_crtc *crtc,
					      int *min, int *max, int *vblank_start);

void intel_crtc_state_reset(struct intel_crtc_state *crtc_state,
			    struct intel_crtc *crtc)
{
	memset(crtc_state, 0, sizeof(*crtc_state));

	__drm_atomic_helper_crtc_state_reset(&crtc_state->uapi, &crtc->base);

	crtc_state->cpu_transcoder = INVALID_TRANSCODER;
	crtc_state->master_transcoder = INVALID_TRANSCODER;
	crtc_state->hsw_workaround_pipe = INVALID_PIPE;
	crtc_state->scaler_state.scaler_id = -1;
	crtc_state->mst_master_transcoder = INVALID_TRANSCODER;
	crtc_state->max_link_bpp_x16 = INT_MAX;
}

int intel_usecs_to_scanlines(const struct drm_display_mode *adjusted_mode,
			     int usecs)
{
	/* paranoia */
	if (!adjusted_mode->crtc_htotal)
		return 1;

	return DIV_ROUND_UP(usecs * adjusted_mode->crtc_clock,
			    1000 * adjusted_mode->crtc_htotal);
}

u32 intel_crtc_get_vblank_counter(struct intel_crtc *crtc)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_vblank_crtc *vblank = &dev->vblank[drm_crtc_index(&crtc->base)];

	if (!crtc->active)
		return 0;

	if (!vblank->max_vblank_count)
		return (u32)drm_crtc_accurate_vblank_count(&crtc->base);

	return crtc->base.funcs->get_vblank_counter(&crtc->base);
}

static bool intel_crtc_needs_vblank_work(const struct intel_crtc_state *crtc_state)
{
	return crtc_state->hw.active &&
		!intel_crtc_needs_modeset(crtc_state) &&
		!crtc_state->preload_luts &&
		intel_crtc_needs_color_update(crtc_state) &&
		!intel_color_uses_dsb(crtc_state);
}

static int intel_mode_vblank_start(const struct drm_display_mode *mode)
{
	int vblank_start = mode->crtc_vblank_start;

	if (mode->flags & DRM_MODE_FLAG_INTERLACE)
		vblank_start = DIV_ROUND_UP(vblank_start, 2);

	return vblank_start;
}

static void intel_crtc_vblank_evade_scanlines(struct intel_atomic_state *state,
					      struct intel_crtc *crtc,
					      int *min, int *max, int *vblank_start)
{
	const struct intel_crtc_state *old_crtc_state =
		intel_atomic_get_old_crtc_state(state, crtc);
	const struct intel_crtc_state *new_crtc_state =
		intel_atomic_get_new_crtc_state(state, crtc);
	const struct intel_crtc_state *crtc_state;
	const struct drm_display_mode *adjusted_mode;

	/*
	 * During fastsets/etc. the transcoder is still
	 * running with the old timings at this point.
	 *
	 * TODO: maybe just use the active timings here?
	 */
	if (intel_crtc_needs_modeset(new_crtc_state))
		crtc_state = new_crtc_state;
	else
		crtc_state = old_crtc_state;

	adjusted_mode = &crtc_state->hw.adjusted_mode;

	if (crtc->mode_flags & I915_MODE_FLAG_VRR) {
		/* timing changes should happen with VRR disabled */
		drm_WARN_ON(state->base.dev, intel_crtc_needs_modeset(new_crtc_state) ||
			    new_crtc_state->update_m_n || new_crtc_state->update_lrr);

		if (intel_vrr_is_push_sent(crtc_state))
			*vblank_start = intel_vrr_vmin_vblank_start(crtc_state);
		else
			*vblank_start = intel_vrr_vmax_vblank_start(crtc_state);
	} else {
		*vblank_start = intel_mode_vblank_start(adjusted_mode);
	}

	/* FIXME needs to be calibrated sensibly */
	*min = *vblank_start - intel_usecs_to_scanlines(adjusted_mode,
							VBLANK_EVASION_TIME_US);
	*max = *vblank_start - 1;

	/*
	 * M/N and TRANS_VTOTAL are double buffered on the transcoder's
	 * undelayed vblank, so with seamless M/N and LRR we must evade
	 * both vblanks.
	 *
	 * DSB execution waits for the transcoder's undelayed vblank,
	 * hence we must kick off the commit before that.
	 */
	if (new_crtc_state->dsb || new_crtc_state->update_m_n || new_crtc_state->update_lrr)
		*min -= adjusted_mode->crtc_vblank_start - adjusted_mode->crtc_vdisplay;
}

/**
 * intel_pipe_update_start() - start update of a set of display registers
 * @state: the atomic state
 * @crtc: the crtc
 *
 * Mark the start of an update to pipe registers that should be updated
 * atomically regarding vblank. If the next vblank will happens within
 * the next 100 us, this function waits until the vblank passes.
 *
 * After a successful call to this function, interrupts will be disabled
 * until a subsequent call to intel_pipe_update_end(). That is done to
 * avoid random delays.
 */
void intel_pipe_update_start(struct intel_atomic_state *state,
			     struct intel_crtc *crtc)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	struct intel_crtc_state *new_crtc_state =
		intel_atomic_get_new_crtc_state(state, crtc);
	long timeout = msecs_to_jiffies_timeout(1);
	int scanline, min, max, vblank_start;
	wait_queue_head_t *wq = drm_crtc_vblank_waitqueue(&crtc->base);
	bool need_vlv_dsi_wa = (IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv)) &&
		intel_crtc_has_type(new_crtc_state, INTEL_OUTPUT_DSI);
	DEFINE_WAIT(wait);

	intel_psr_lock(new_crtc_state);

	if (new_crtc_state->do_async_flip) {
		spin_lock_irq(&crtc->base.dev->event_lock);
		/* arm the event for the flip done irq handler */
		crtc->flip_done_event = new_crtc_state->uapi.event;
		spin_unlock_irq(&crtc->base.dev->event_lock);

		new_crtc_state->uapi.event = NULL;
		return;
	}

	if (intel_crtc_needs_vblank_work(new_crtc_state))
		intel_crtc_vblank_work_init(new_crtc_state);

	intel_crtc_vblank_evade_scanlines(state, crtc, &min, &max, &vblank_start);
	if (min <= 0 || max <= 0)
		goto irq_disable;

	if (drm_WARN_ON(&dev_priv->drm, drm_crtc_vblank_get(&crtc->base)))
		goto irq_disable;

	/*
	 * Wait for psr to idle out after enabling the VBL interrupts
	 * VBL interrupts will start the PSR exit and prevent a PSR
	 * re-entry as well.
	 */
	intel_psr_wait_for_idle_locked(new_crtc_state);

	local_irq_disable();

	crtc->debug.min_vbl = min;
	crtc->debug.max_vbl = max;
	trace_intel_pipe_update_start(crtc);

	for (;;) {
		/*
		 * prepare_to_wait() has a memory barrier, which guarantees
		 * other CPUs can see the task state update by the time we
		 * read the scanline.
		 */
		prepare_to_wait(wq, &wait, TASK_UNINTERRUPTIBLE);

		scanline = intel_get_crtc_scanline(crtc);
		if (scanline < min || scanline > max)
			break;

		if (!timeout) {
			drm_err(&dev_priv->drm,
				"Potential atomic update failure on pipe %c\n",
				pipe_name(crtc->pipe));
			break;
		}

		local_irq_enable();

		timeout = schedule_timeout(timeout);

		local_irq_disable();
	}

	finish_wait(wq, &wait);

	drm_crtc_vblank_put(&crtc->base);

	/*
	 * On VLV/CHV DSI the scanline counter would appear to
	 * increment approx. 1/3 of a scanline before start of vblank.
	 * The registers still get latched at start of vblank however.
	 * This means we must not write any registers on the first
	 * line of vblank (since not the whole line is actually in
	 * vblank). And unfortunately we can't use the interrupt to
	 * wait here since it will fire too soon. We could use the
	 * frame start interrupt instead since it will fire after the
	 * critical scanline, but that would require more changes
	 * in the interrupt code. So for now we'll just do the nasty
	 * thing and poll for the bad scanline to pass us by.
	 *
	 * FIXME figure out if BXT+ DSI suffers from this as well
	 */
	while (need_vlv_dsi_wa && scanline == vblank_start)
		scanline = intel_get_crtc_scanline(crtc);

	crtc->debug.scanline_start = scanline;
	crtc->debug.start_vbl_time = ktime_get();
	crtc->debug.start_vbl_count = intel_crtc_get_vblank_counter(crtc);

	trace_intel_pipe_update_vblank_evaded(crtc);
	return;

irq_disable:
	local_irq_disable();
}

/**
 * intel_pipe_update_end() - end update of a set of display registers
 * @state: the atomic state
 * @crtc: the crtc
 *
 * Mark the end of an update started with intel_pipe_update_start(). This
 * re-enables interrupts and verifies the update was actually completed
 * before a vblank.
 */
void intel_pipe_update_end(struct intel_atomic_state *state,
			   struct intel_crtc *crtc)
{
	struct intel_crtc_state *new_crtc_state =
		intel_atomic_get_new_crtc_state(state, crtc);
	enum pipe pipe = crtc->pipe;
	int scanline_end = intel_get_crtc_scanline(crtc);
	u32 end_vbl_count = intel_crtc_get_vblank_counter(crtc);
	ktime_t end_vbl_time = ktime_get();
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);

	if (new_crtc_state->do_async_flip)
		goto out;

	trace_intel_pipe_update_end(crtc, end_vbl_count, scanline_end);

	/*
	 * Incase of mipi dsi command mode, we need to set frame update
	 * request for every commit.
	 */
	if (DISPLAY_VER(dev_priv) >= 11 &&
	    intel_crtc_has_type(new_crtc_state, INTEL_OUTPUT_DSI))
		icl_dsi_frame_update(new_crtc_state);

	/* We're still in the vblank-evade critical section, this can't race.
	 * Would be slightly nice to just grab the vblank count and arm the
	 * event outside of the critical section - the spinlock might spin for a
	 * while ... */
	if (intel_crtc_needs_vblank_work(new_crtc_state)) {
		drm_vblank_work_schedule(&new_crtc_state->vblank_work,
					 drm_crtc_accurate_vblank_count(&crtc->base) + 1,
					 false);
	} else if (new_crtc_state->uapi.event) {
		drm_WARN_ON(&dev_priv->drm,
			    drm_crtc_vblank_get(&crtc->base) != 0);

		spin_lock(&crtc->base.dev->event_lock);
		drm_crtc_arm_vblank_event(&crtc->base,
					  new_crtc_state->uapi.event);
		spin_unlock(&crtc->base.dev->event_lock);

		new_crtc_state->uapi.event = NULL;
	}

	/*
	 * Send VRR Push to terminate Vblank. If we are already in vblank
	 * this has to be done _after_ sampling the frame counter, as
	 * otherwise the push would immediately terminate the vblank and
	 * the sampled frame counter would correspond to the next frame
	 * instead of the current frame.
	 *
	 * There is a tiny race here (iff vblank evasion failed us) where
	 * we might sample the frame counter just before vmax vblank start
	 * but the push would be sent just after it. That would cause the
	 * push to affect the next frame instead of the current frame,
	 * which would cause the next frame to terminate already at vmin
	 * vblank start instead of vmax vblank start.
	 */
	intel_vrr_send_push(new_crtc_state);

	local_irq_enable();

	if (intel_vgpu_active(dev_priv))
		goto out;

	if (crtc->debug.start_vbl_count &&
	    crtc->debug.start_vbl_count != end_vbl_count) {
		drm_err(&dev_priv->drm,
			"Atomic update failure on pipe %c (start=%u end=%u) time %lld us, min %d, max %d, scanline start %d, end %d\n",
			pipe_name(pipe), crtc->debug.start_vbl_count,
			end_vbl_count,
			ktime_us_delta(end_vbl_time,
				       crtc->debug.start_vbl_time),
			crtc->debug.min_vbl, crtc->debug.max_vbl,
			crtc->debug.scanline_start, scanline_end);
	}

	dbg_vblank_evade(crtc, end_vbl_time);

out:
	intel_psr_unlock(new_crtc_state);
}

void intel_crtc_wait_for_next_vblank(struct intel_crtc *crtc)
{
	drm_crtc_wait_one_vblank(&crtc->base);
}


#include "parity_flip_glue.inc"
