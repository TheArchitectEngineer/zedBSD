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
 * zedBSD WS031: enum plane_id extracted textually from the Linux v6.8.12 i915 reference
 * (display/intel_display_limits.h: SPDX MIT, Copyright Intel Corporation -- full notice in skl_plane_port.c)
 * by tools/port_lcd_calc.py.  Do not edit by hand.
 */
/*
 * zedBSD display rebuild: moved from src/drivers/gpu/i915/parity/lcd/lcd_plane_types.h of the
 * pre-rebuild tree (commit 7e7ff337) without change to any definition;
 * the include guard PARITY_LCD_PLANE_TYPES_H was renamed.  The provenance above still applies and names the generator.
 */

#ifndef DRIVERS_GPU_I915_DATA_DISPLAY_PLANE_TYPES_INC
#define DRIVERS_GPU_I915_DATA_DISPLAY_PLANE_TYPES_INC

enum plane_id {
	PLANE_PRIMARY,
	PLANE_SPRITE0,
	PLANE_SPRITE1,
	PLANE_SPRITE2,
	PLANE_SPRITE3,
	PLANE_SPRITE4,
	PLANE_SPRITE5,
	PLANE_CURSOR,

	I915_MAX_PLANES,
};


#endif /* DRIVERS_GPU_I915_DATA_DISPLAY_PLANE_TYPES_INC */
