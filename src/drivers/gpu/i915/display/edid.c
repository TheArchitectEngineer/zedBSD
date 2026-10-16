/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Derived from the Linux kernel v6.8.12 (drivers/gpu/drm/drm_modes.c),
 * which carries the following notice.
 *
 * Copyright © 1997-2003 by The XFree86 Project, Inc.
 * Copyright © 2007 Dave Airlie
 * Copyright © 2007-2008 Intel Corporation
 *   Jesse Barnes <jesse.barnes@intel.com>
 * Copyright 2005-2006 Luc Verhaegen
 * Copyright (c) 2001, Andy Ritger  aritger@nvidia.com
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Except as contained in this notice, the name of the copyright holder(s)
 * and author(s) shall not be used in advertising or otherwise to promote
 * the sale, use or other dealings in this Software without prior written
 * authorization from the copyright holder(s) and author(s).
 */

/*
 * The display mode helpers of the one-screen modeset path (see edid.h).
 *
 * The helpers the pipe, vblank and watermark text use follow the Linux
 * 6.8.12 drm_modes.c text: drm_mode_copy(), drm_mode_init(),
 * drm_mode_get_hv_timing() and drm_mode_set_crtcinfo().  The panel's mode
 * is decoded by xd-edid.c.
 */

#include <uapi/errno.h>
#include <kern/kcrt.h>

#include "internal.h"
#include "modeset-internal.h"
#include "edid.h"

/*
 * Copies a display mode, keeping the list head of the destination.
 *
 * The Linux drm_mode_copy(): the destination stays linked wherever it was
 * linked before the copy.
 */
void
drv_i915_drm_mode_copy(
	struct drm_display_mode *dst,
	const struct drm_display_mode *src)
{
	struct list_head head;

	/* Saves the list link of the destination. */
	head = dst->head;

	/* Copies the mode and puts the saved link back. */
	*dst = *src;
	dst->head = head;
}

/*
 * Initializes a display mode from another one.
 *
 * The Linux drm_mode_init(): the destination is cleared first, so its list
 * head is zero instead of whatever an on-stack mode held.
 */
void
drv_i915_drm_mode_init(
	struct drm_display_mode *dst,
	const struct drm_display_mode *src)
{
	/* Clears the destination, including its list head. */
	kern_memset(dst, 0, sizeof(*dst));

	/* Copies the mode over the cleared destination. */
	drv_i915_drm_mode_copy(dst, src);
}

/*
 * Reports the visible width and height of a display mode.
 *
 * The Linux drm_mode_get_hv_timing(): the height is doubled for a stereo
 * mode whose layout carries both eyes in one frame.
 */
void
drv_i915_drm_mode_get_hv_timing(
	const struct drm_display_mode *mode,
	int *hdisplay,
	int *vdisplay)
{
	struct drm_display_mode adjusted;

	/* Works on a copy, so the caller's mode keeps its crtc timings. */
	drv_i915_drm_mode_init(&adjusted, mode);

	/* Computes the crtc timings with only the stereo adjustment applied. */
	drv_i915_drm_mode_set_crtcinfo(&adjusted, CRTC_STEREO_DOUBLE_ONLY);

	/* Reports the visible size the stereo adjustment gave. */
	*hdisplay = adjusted.crtc_hdisplay;
	*vdisplay = adjusted.crtc_vdisplay;
}

/*
 * Sets the crtc timings of a display mode, adjusting them as asked.
 *
 * The Linux drm_mode_set_crtcinfo().  CRTC_INTERLACE_HALVE_V halves the
 * vertical timings of an interlaced mode.  CRTC_STEREO_DOUBLE computes the
 * timings of a buffer that carries both eyes (only for the frame-packing
 * layout).  CRTC_NO_DBLSCAN and CRTC_NO_VSCAN ask that a doublescan mode
 * and a mode with vscan > 1 are not adjusted.
 */
void
drv_i915_drm_mode_set_crtcinfo(
	struct drm_display_mode *p,
	int adjust_flags)
{
	unsigned int layout;

	/* A missing mode has no timings to set. */
	if (!p)
		return;

	/* Starts from the mode's own timings. */
	p->crtc_clock = p->clock;
	p->crtc_hdisplay = p->hdisplay;
	p->crtc_hsync_start = p->hsync_start;
	p->crtc_hsync_end = p->hsync_end;
	p->crtc_htotal = p->htotal;
	p->crtc_hskew = p->hskew;
	p->crtc_vdisplay = p->vdisplay;
	p->crtc_vsync_start = p->vsync_start;
	p->crtc_vsync_end = p->vsync_end;
	p->crtc_vtotal = p->vtotal;

	/* An interlaced mode is programmed per field when the caller asks for it. */
	if (p->flags & DRM_MODE_FLAG_INTERLACE) {
		if (adjust_flags & CRTC_INTERLACE_HALVE_V) {
			p->crtc_vdisplay /= 2;
			p->crtc_vsync_start /= 2;
			p->crtc_vsync_end /= 2;
			p->crtc_vtotal /= 2;
		}
	}

	/* A doublescan mode sends every line twice, unless the caller refuses the adjustment. */
	if (!(adjust_flags & CRTC_NO_DBLSCAN)) {
		if (p->flags & DRM_MODE_FLAG_DBLSCAN) {
			p->crtc_vdisplay *= 2;
			p->crtc_vsync_start *= 2;
			p->crtc_vsync_end *= 2;
			p->crtc_vtotal *= 2;
		}
	}

	/* A mode with vscan > 1 sends every line vscan times, unless the caller refuses the adjustment. */
	if (!(adjust_flags & CRTC_NO_VSCAN)) {
		if (p->vscan > 1) {
			p->crtc_vdisplay *= p->vscan;
			p->crtc_vsync_start *= p->vscan;
			p->crtc_vsync_end *= p->vscan;
			p->crtc_vtotal *= p->vscan;
		}
	}

	/* A stereo buffer that carries both eyes doubles the frame of the frame-packing layout. */
	if (adjust_flags & CRTC_STEREO_DOUBLE) {
		layout = p->flags & DRM_MODE_FLAG_3D_MASK;

		/* Only the frame-packing layout stacks the two eyes in one frame. */
		switch (layout) {
		case DRM_MODE_FLAG_3D_FRAME_PACKING:
			p->crtc_clock *= 2;
			p->crtc_vdisplay += p->crtc_vtotal;
			p->crtc_vsync_start += p->crtc_vtotal;
			p->crtc_vsync_end += p->crtc_vtotal;
			p->crtc_vtotal += p->crtc_vtotal;
			break;
		}
	}

	/* The blanking spans from the end of the visible area or the sync to the end of the total. */
	p->crtc_vblank_start = min(p->crtc_vsync_start, p->crtc_vdisplay);
	p->crtc_vblank_end = max(p->crtc_vsync_end, p->crtc_vtotal);
	p->crtc_hblank_start = min(p->crtc_hsync_start, p->crtc_hdisplay);
	p->crtc_hblank_end = max(p->crtc_hsync_end, p->crtc_htotal);
}

/*
 * Names a display mode.
 *
 * The name is a label for logs, and the callers print the numbers
 * themselves, so no name is written.
 */
void
drv_i915_lcd_drm_mode_set_name(
	struct drm_display_mode *mode)
{
	UNUSED_PARAMETER(mode);
}

