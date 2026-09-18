// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022-2023 Intel Corporation
 */

/*
 * zedBSD WS031: generated from Linux v6.8.12 drivers/gpu/drm/i915/display/intel_vblank.c
 * (sha256 e1ee4e3595d64d267773c042bec68486eb6751d6ba7db4308713c1982889ef6e) by plan/ws031/handover/tools/port_lcd_calc.py.
 * The function bodies are the reference text.  Kept / changed:
 *  - kept: pipe_scanline_is_moving, wait_for_pipe_scanline_moving, intel_wait_for_pipe_scanline_stopped, intel_wait_for_pipe_scanline_moving;
 *  - the includes are replaced by: lcd_compat.h, lcd_modeset_compat.h;
 */

#include "lcd_compat.h"
#include "lcd_modeset_compat.h"

/* zedBSD: forward declarations of the static functions of this file (generated; the keep-list is not in call order) */
static bool pipe_scanline_is_moving(struct drm_i915_private *dev_priv,
				    enum pipe pipe);
static void wait_for_pipe_scanline_moving(struct intel_crtc *crtc, bool state);

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

