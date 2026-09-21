// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022-2023 Intel Corporation
 */

/*
 * zedBSD WS031: generated from Linux v6.8.12 drivers/gpu/drm/i915/display/intel_vblank.c
 * (sha256 e1ee4e3595d64d267773c042bec68486eb6751d6ba7db4308713c1982889ef6e) by plan/ws031/handover/tools/port_lcd_calc.py.
 * The function bodies are the reference text.  Kept / changed:
 *  - kept: pipe_scanline_is_moving, wait_for_pipe_scanline_moving, intel_wait_for_pipe_scanline_stopped, intel_wait_for_pipe_scanline_moving,
 *    g4x_get_vblank_counter, __intel_get_crtc_scanline, intel_get_crtc_scanline, intel_crtc_scanline_offset,
 *    intel_crtc_update_active_timings;
 *  - the includes are replaced by: lcd_compat.h, lcd_modeset_compat.h, lcd_flip_compat.h;
 */

#include "lcd_compat.h"
#include "lcd_modeset_compat.h"
#include "lcd_flip_compat.h"

/* zedBSD: forward declarations of the static functions of this file (generated; the keep-list is not in call order) */
static bool pipe_scanline_is_moving(struct drm_i915_private *dev_priv,
				    enum pipe pipe);
static void wait_for_pipe_scanline_moving(struct intel_crtc *crtc, bool state);
static int __intel_get_crtc_scanline(struct intel_crtc *crtc);
static int intel_crtc_scanline_offset(const struct intel_crtc_state *crtc_state);

static bool pipe_scanline_is_moving(struct drm_i915_private *dev_priv,
				    enum pipe pipe)
{
	i915_reg_t reg = PIPEDSL(pipe);
	u32 line1, line2;

	line1 = intel_de_read(dev_priv, reg) & PIPEDSL_LINE_MASK;
	msleep(5);
	line2 = intel_de_read(dev_priv, reg) & PIPEDSL_LINE_MASK;

	return line1 != line2;
}

static void wait_for_pipe_scanline_moving(struct intel_crtc *crtc, bool state)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	enum pipe pipe = crtc->pipe;

	/* Wait for the display line to settle/start moving */
	if (wait_for(pipe_scanline_is_moving(dev_priv, pipe) == state, 100))
		drm_err(&dev_priv->drm,
			"pipe %c scanline %s wait timed out\n",
			pipe_name(pipe), str_on_off(state));
}

void intel_wait_for_pipe_scanline_stopped(struct intel_crtc *crtc)
{
	wait_for_pipe_scanline_moving(crtc, false);
}

void intel_wait_for_pipe_scanline_moving(struct intel_crtc *crtc)
{
	wait_for_pipe_scanline_moving(crtc, true);
}

u32 g4x_get_vblank_counter(struct drm_crtc *crtc)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->dev);
	struct drm_vblank_crtc *vblank = &dev_priv->drm.vblank[drm_crtc_index(crtc)];
	enum pipe pipe = to_intel_crtc(crtc)->pipe;

	if (!vblank->max_vblank_count)
		return 0;

	return intel_de_read(dev_priv, PIPE_FRMCOUNT_G4X(pipe));
}

/*
 * intel_de_read_fw(), only for fast reads of display block, no need for
 * forcewake etc.
 */
static int __intel_get_crtc_scanline(struct intel_crtc *crtc)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	const struct drm_display_mode *mode;
	struct drm_vblank_crtc *vblank;
	enum pipe pipe = crtc->pipe;
	int position, vtotal;

	if (!crtc->active)
		return 0;

	vblank = &crtc->base.dev->vblank[drm_crtc_index(&crtc->base)];
	mode = &vblank->hwmode;

	if (crtc->mode_flags & I915_MODE_FLAG_GET_SCANLINE_FROM_TIMESTAMP)
		return __intel_get_crtc_scanline_from_timestamp(crtc);

	vtotal = mode->crtc_vtotal;
	if (mode->flags & DRM_MODE_FLAG_INTERLACE)
		vtotal /= 2;

	position = intel_de_read_fw(dev_priv, PIPEDSL(pipe)) & PIPEDSL_LINE_MASK;

	/*
	 * On HSW, the DSL reg (0x70000) appears to return 0 if we
	 * read it just before the start of vblank.  So try it again
	 * so we don't accidentally end up spanning a vblank frame
	 * increment, causing the pipe_update_end() code to squak at us.
	 *
	 * The nature of this problem means we can't simply check the ISR
	 * bit and return the vblank start value; nor can we use the scanline
	 * debug register in the transcoder as it appears to have the same
	 * problem.  We may need to extend this to include other platforms,
	 * but so far testing only shows the problem on HSW.
	 */
	if (HAS_DDI(dev_priv) && !position) {
		int i, temp;

		for (i = 0; i < 100; i++) {
			udelay(1);
			temp = intel_de_read_fw(dev_priv, PIPEDSL(pipe)) & PIPEDSL_LINE_MASK;
			if (temp != position) {
				position = temp;
				break;
			}
		}
	}

	/*
	 * See update_scanline_offset() for the details on the
	 * scanline_offset adjustment.
	 */
	return (position + crtc->scanline_offset) % vtotal;
}

int intel_get_crtc_scanline(struct intel_crtc *crtc)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	unsigned long irqflags;
	int position;

	local_irq_save(irqflags);
	intel_vblank_section_enter(dev_priv);

	position = __intel_get_crtc_scanline(crtc);

	intel_vblank_section_exit(dev_priv);
	local_irq_restore(irqflags);

	return position;
}

static int intel_crtc_scanline_offset(const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *i915 = to_i915(crtc_state->uapi.crtc->dev);
	const struct drm_display_mode *adjusted_mode = &crtc_state->hw.adjusted_mode;

	/*
	 * The scanline counter increments at the leading edge of hsync.
	 *
	 * On most platforms it starts counting from vtotal-1 on the
	 * first active line. That means the scanline counter value is
	 * always one less than what we would expect. Ie. just after
	 * start of vblank, which also occurs at start of hsync (on the
	 * last active line), the scanline counter will read vblank_start-1.
	 *
	 * On gen2 the scanline counter starts counting from 1 instead
	 * of vtotal-1, so we have to subtract one (or rather add vtotal-1
	 * to keep the value positive), instead of adding one.
	 *
	 * On HSW+ the behaviour of the scanline counter depends on the output
	 * type. For DP ports it behaves like most other platforms, but on HDMI
	 * there's an extra 1 line difference. So we need to add two instead of
	 * one to the value.
	 *
	 * On VLV/CHV DSI the scanline counter would appear to increment
	 * approx. 1/3 of a scanline before start of vblank. Unfortunately
	 * that means we can't tell whether we're in vblank or not while
	 * we're on that particular line. We must still set scanline_offset
	 * to 1 so that the vblank timestamps come out correct when we query
	 * the scanline counter from within the vblank interrupt handler.
	 * However if queried just before the start of vblank we'll get an
	 * answer that's slightly in the future.
	 */
	if (DISPLAY_VER(i915) == 2) {
		int vtotal;

		vtotal = adjusted_mode->crtc_vtotal;
		if (adjusted_mode->flags & DRM_MODE_FLAG_INTERLACE)
			vtotal /= 2;

		return vtotal - 1;
	} else if (HAS_DDI(i915) && intel_crtc_has_type(crtc_state, INTEL_OUTPUT_HDMI)) {
		return 2;
	} else {
		return 1;
	}
}

void intel_crtc_update_active_timings(const struct intel_crtc_state *crtc_state,
				      bool vrr_enable)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_i915_private *i915 = to_i915(crtc->base.dev);
	u8 mode_flags = crtc_state->mode_flags;
	struct drm_display_mode adjusted_mode;
	int vmax_vblank_start = 0;
	unsigned long irqflags;

	drm_mode_init(&adjusted_mode, &crtc_state->hw.adjusted_mode);

	if (vrr_enable) {
		drm_WARN_ON(&i915->drm, (mode_flags & I915_MODE_FLAG_VRR) == 0);

		adjusted_mode.crtc_vtotal = crtc_state->vrr.vmax;
		adjusted_mode.crtc_vblank_end = crtc_state->vrr.vmax;
		adjusted_mode.crtc_vblank_start = intel_vrr_vmin_vblank_start(crtc_state);
		vmax_vblank_start = intel_vrr_vmax_vblank_start(crtc_state);
	} else {
		mode_flags &= ~I915_MODE_FLAG_VRR;
	}

	/*
	 * Belts and suspenders locking to guarantee everyone sees 100%
	 * consistent state during fastset seamless refresh rate changes.
	 *
	 * vblank_time_lock takes care of all drm_vblank.c stuff, and
	 * uncore.lock takes care of __intel_get_crtc_scanline() which
	 * may get called elsewhere as well.
	 *
	 * TODO maybe just protect everything (including
	 * __intel_get_crtc_scanline()) with vblank_time_lock?
	 * Need to audit everything to make sure it's safe.
	 */
	spin_lock_irqsave(&i915->drm.vblank_time_lock, irqflags);
	intel_vblank_section_enter(i915);

	drm_calc_timestamping_constants(&crtc->base, &adjusted_mode);

	crtc->vmax_vblank_start = vmax_vblank_start;

	crtc->mode_flags = mode_flags;

	crtc->scanline_offset = intel_crtc_scanline_offset(crtc_state);
	intel_vblank_section_exit(i915);
	spin_unlock_irqrestore(&i915->drm.vblank_time_lock, irqflags);
}

