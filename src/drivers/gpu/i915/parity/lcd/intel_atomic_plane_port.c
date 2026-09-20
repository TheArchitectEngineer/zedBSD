/*
 * Copyright © 2014 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

/*
 * zedBSD WS031: generated from Linux v6.8.12 drivers/gpu/drm/i915/display/intel_atomic_plane.c
 * (sha256 e0a4cb70c1b830d76e4d014181ed313bd3cc0202e2055a08d78696a76e8d48da) by plan/ws031/handover/tools/port_lcd_calc.py.
 * The function bodies are the reference text.  Kept / changed:
 *  - kept: intel_adjusted_rate, intel_plane_pixel_rate, use_min_ddb, intel_plane_relative_data_rate,
 *    intel_plane_data_rate;
 *  - the includes are replaced by: lcd_compat.h, lcd_seq_compat.h, lcd_modeset_compat.h, lcd_plane_compat.h, lcd_wm_compat.h;
 *  - parity_atomic_plane_glue.inc (zedBSD code) is included at the end of the file.
 */

#include "lcd_compat.h"
#include "lcd_seq_compat.h"
#include "lcd_modeset_compat.h"
#include "lcd_plane_compat.h"
#include "lcd_wm_compat.h"

/* zedBSD: forward declarations of the static functions of this file (generated; the keep-list is not in call order) */
static bool
use_min_ddb(const struct intel_crtc_state *crtc_state,
	    struct intel_plane *plane);
static unsigned int
intel_plane_relative_data_rate(const struct intel_crtc_state *crtc_state,
			       const struct intel_plane_state *plane_state,
			       int color_plane);

unsigned int intel_adjusted_rate(const struct drm_rect *src,
				 const struct drm_rect *dst,
				 unsigned int rate)
{
	unsigned int src_w, src_h, dst_w, dst_h;

	src_w = drm_rect_width(src) >> 16;
	src_h = drm_rect_height(src) >> 16;
	dst_w = drm_rect_width(dst);
	dst_h = drm_rect_height(dst);

	/* Downscaling limits the maximum pixel rate */
	dst_w = min(src_w, dst_w);
	dst_h = min(src_h, dst_h);

	return DIV_ROUND_UP_ULL(mul_u32_u32(rate, src_w * src_h),
				dst_w * dst_h);
}

unsigned int intel_plane_pixel_rate(const struct intel_crtc_state *crtc_state,
				    const struct intel_plane_state *plane_state)
{
	/*
	 * Note we don't check for plane visibility here as
	 * we want to use this when calculating the cursor
	 * watermarks even if the cursor is fully offscreen.
	 * That depends on the src/dst rectangles being
	 * correctly populated whenever the watermark code
	 * considers the cursor to be visible, whether or not
	 * it is actually visible.
	 *
	 * See: intel_wm_plane_visible() and intel_check_cursor()
	 */

	return intel_adjusted_rate(&plane_state->uapi.src,
				   &plane_state->uapi.dst,
				   crtc_state->pixel_rate);
}

static bool
use_min_ddb(const struct intel_crtc_state *crtc_state,
	    struct intel_plane *plane)
{
	struct drm_i915_private *i915 = to_i915(plane->base.dev);

	return DISPLAY_VER(i915) >= 13 &&
	       crtc_state->uapi.async_flip &&
	       plane->async_flip;
}

static unsigned int
intel_plane_relative_data_rate(const struct intel_crtc_state *crtc_state,
			       const struct intel_plane_state *plane_state,
			       int color_plane)
{
	struct intel_plane *plane = to_intel_plane(plane_state->uapi.plane);
	const struct drm_framebuffer *fb = plane_state->hw.fb;
	int width, height;
	unsigned int rel_data_rate;

	if (!plane_state->uapi.visible)
		return 0;

	/*
	 * We calculate extra ddb based on ratio plane rate/total data rate
	 * in case, in some cases we should not allocate extra ddb for the plane,
	 * so do not count its data rate, if this is the case.
	 */
	if (use_min_ddb(crtc_state, plane))
		return 0;

	/*
	 * Src coordinates are already rotated by 270 degrees for
	 * the 90/270 degree plane rotation cases (to match the
	 * GTT mapping), hence no need to account for rotation here.
	 */
	width = drm_rect_width(&plane_state->uapi.src) >> 16;
	height = drm_rect_height(&plane_state->uapi.src) >> 16;

	/* UV plane does 1/2 pixel sub-sampling */
	if (color_plane == 1) {
		width /= 2;
		height /= 2;
	}

	rel_data_rate = width * height * fb->format->cpp[color_plane];

	if (plane->id == PLANE_CURSOR)
		return rel_data_rate;

	return intel_adjusted_rate(&plane_state->uapi.src,
				   &plane_state->uapi.dst,
				   rel_data_rate);
}

unsigned int intel_plane_data_rate(const struct intel_crtc_state *crtc_state,
				   const struct intel_plane_state *plane_state,
				   int color_plane)
{
	const struct drm_framebuffer *fb = plane_state->hw.fb;

	if (!plane_state->uapi.visible)
		return 0;

	return intel_plane_pixel_rate(crtc_state, plane_state) *
		fb->format->cpp[color_plane];
}


#include "parity_atomic_plane_glue.inc"
