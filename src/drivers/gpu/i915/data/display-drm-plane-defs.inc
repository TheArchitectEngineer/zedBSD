/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2006-2019 Intel Corporation
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 */

/*
 * zedBSD: the notice above is the one the extract below referred to (kept in the old
 * parity/lcd/lcd_mreg_display.h); it is repeated here because that file is not carried over.
 */
/*
 * zedBSD WS031: plane-related DRM definitions extracted textually from Linux v6.8.12 by tools/port_lcd_calc.py:
 *   include/drm/drm_blend.h        (sha256 46a47b37fcb9dbb1da3f98d58db0075d21b1a0cde21290c5d627d4ace05f044f): DRM_MODE_BLEND_*, drm_rotation_90_or_270
 *   include/uapi/drm/drm_mode.h    (sha256 6f1e99012854f40c59e62ba9ab031aa6e0f7354f41f25d0a9d23e6dfc6bd370b): DRM_MODE_ROTATE_* / REFLECT_*
 *   include/drm/drm_color_mgmt.h   (sha256 6332adb2c833e3a6ac6a7b1083446eb1e2964c3e2b7f40014309a067f0b8ee5e): enum drm_color_encoding / drm_color_range
 * Each source file carries its own copyright / permission notice (kept unmodified in
 * plan/ws031/linux-parity/linux-reference/drm-v6.8.12/); three sources in one extract is recorded in the
 * provenance ledger as not yet audited.  Do not edit by hand.
 */
/*
 * zedBSD display rebuild: moved from src/drivers/gpu/i915/parity/lcd/lcd_drm_plane_defs.h of the
 * pre-rebuild tree (commit 7e7ff337) without change to any definition;
 * the include guard PARITY_LCD_DRM_PLANE_DEFS_H was renamed.  The provenance above still applies and names the generator.
 */

#ifndef DRIVERS_GPU_I915_DATA_DISPLAY_DRM_PLANE_DEFS_INC
#define DRIVERS_GPU_I915_DATA_DISPLAY_DRM_PLANE_DEFS_INC

#define DRM_MODE_BLEND_PREMULTI		0
#define DRM_MODE_BLEND_COVERAGE		1
#define DRM_MODE_BLEND_PIXEL_NONE	2

#define DRM_MODE_ROTATE_0       (1<<0)
#define DRM_MODE_ROTATE_90      (1<<1)
#define DRM_MODE_ROTATE_180     (1<<2)
#define DRM_MODE_ROTATE_270     (1<<3)
#define DRM_MODE_REFLECT_X      (1<<4)
#define DRM_MODE_REFLECT_Y      (1<<5)
#define DRM_MODE_ROTATE_MASK (\
		DRM_MODE_ROTATE_0  | \
		DRM_MODE_ROTATE_90  | \
		DRM_MODE_ROTATE_180 | \
		DRM_MODE_ROTATE_270)
#define DRM_MODE_REFLECT_MASK (\
		DRM_MODE_REFLECT_X | \
		DRM_MODE_REFLECT_Y)

static inline bool drm_rotation_90_or_270(unsigned int rotation)
{
	return rotation & (DRM_MODE_ROTATE_90 | DRM_MODE_ROTATE_270);
}

enum drm_color_encoding {
	DRM_COLOR_YCBCR_BT601,
	DRM_COLOR_YCBCR_BT709,
	DRM_COLOR_YCBCR_BT2020,
	DRM_COLOR_ENCODING_MAX,
};

enum drm_color_range {
	DRM_COLOR_YCBCR_LIMITED_RANGE,
	DRM_COLOR_YCBCR_FULL_RANGE,
	DRM_COLOR_RANGE_MAX,
};

#endif /* DRIVERS_GPU_I915_DATA_DISPLAY_DRM_PLANE_DEFS_INC */
