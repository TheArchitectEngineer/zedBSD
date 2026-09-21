/*
 * Copyright (c) 2006 Dave Airlie <airlied@linux.ie>
 * Copyright (c) 2007-2008 Intel Corporation
 *   Jesse Barnes <jesse.barnes@intel.com>
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
 */

/*
 * zedBSD WS031: macro definitions extracted textually from the Linux v6.8.12 reference drivers/gpu/drm/i915/display/intel_display_types.h
 * (sha256 647773fcee0c549791a50b9c6a4cde175c11005497689e9d6d4bbd2db8d38db4) by tools/port_lcd_calc.py:
 * the root macros listed in tools/port_lcd_modeset.json plus every macro of that header they use.
 * The notice above is the source file's own.  Do not edit by hand.
 */
/*
 * zedBSD display rebuild: moved from src/drivers/gpu/i915/parity/lcd/lcd_mreg_display_types.h of the
 * pre-rebuild tree (commit 7e7ff337) without change to any definition;
 * the include guard PARITY_LCD_MREG_DISPLAY_TYPES_H was renamed.  The provenance above still applies and names the generator.
 */

#ifndef DRIVERS_GPU_I915_DATA_DISPLAY_MREG_DISPLAY_TYPES_INC
#define DRIVERS_GPU_I915_DATA_DISPLAY_MREG_DISPLAY_TYPES_INC

#define I915_MODE_FLAG_GET_SCANLINE_FROM_TIMESTAMP (1<<1)
#define I915_MODE_FLAG_VRR (1<<6)

#endif /* DRIVERS_GPU_I915_DATA_DISPLAY_MREG_DISPLAY_TYPES_INC */
