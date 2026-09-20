/*
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
 * zedBSD WS031: generated from Linux v6.8.12 drivers/gpu/drm/drm_modes.c
 * (sha256 e3a32332f5eb4f8a40c7df278bcfc9fd3b21468f2c94aa0470cb1176c94680ef) by plan/ws031/handover/tools/port_lcd_calc.py.
 * The function bodies are the reference text.  Kept / changed:
 *  - kept: drm_mode_copy, drm_mode_init, drm_mode_get_hv_timing;
 *  - the includes are replaced by: lcd_compat.h;
 */

#include "lcd_compat.h"

/**
 * drm_mode_copy - copy the mode
 * @dst: mode to overwrite
 * @src: mode to copy
 *
 * Copy an existing mode into another mode, preserving the
 * list head of the destination mode.
 */
void drm_mode_copy(struct drm_display_mode *dst, const struct drm_display_mode *src)
{
	struct list_head head = dst->head;

	*dst = *src;
	dst->head = head;
}

/**
 * drm_mode_init - initialize the mode from another mode
 * @dst: mode to overwrite
 * @src: mode to copy
 *
 * Copy an existing mode into another mode, zeroing the
 * list head of the destination mode. Typically used
 * to guarantee the list head is not left with stack
 * garbage in on-stack modes.
 */
void drm_mode_init(struct drm_display_mode *dst, const struct drm_display_mode *src)
{
	memset(dst, 0, sizeof(*dst));
	drm_mode_copy(dst, src);
}

/**
 * drm_mode_get_hv_timing - Fetches hdisplay/vdisplay for given mode
 * @mode: mode to query
 * @hdisplay: hdisplay value to fill in
 * @vdisplay: vdisplay value to fill in
 *
 * The vdisplay value will be doubled if the specified mode is a stereo mode of
 * the appropriate layout.
 */
void drm_mode_get_hv_timing(const struct drm_display_mode *mode,
			    int *hdisplay, int *vdisplay)
{
	struct drm_display_mode adjusted;

	drm_mode_init(&adjusted, mode);

	drm_mode_set_crtcinfo(&adjusted, CRTC_STEREO_DOUBLE_ONLY);
	*hdisplay = adjusted.crtc_hdisplay;
	*vdisplay = adjusted.crtc_vdisplay;
}

