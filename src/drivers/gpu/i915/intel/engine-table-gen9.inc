/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 */

/*
 * Copyright © 2016 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/*
 * The rows of the Gen9 engine table: the engines a Skylake, Kaby Lake,
 * Coffee Lake or Comet Lake GT can have, from Linux intel_engine_cs.c
 * intel_engines[].  device-info.c includes them inside the table's
 * initializer.
 *
 * Modified for zedBSD: excerpted and reformatted by hand from the Linux
 * 6.8.12 reference.  Values are unchanged.  Source (SHA-256 of the file):
 *   drivers/gpu/drm/i915/gt/intel_engine_cs.c, intel_engines[]
 *     da0063e3ff12448e17585c301a442bf2fb90ee9ca5f787af30b640747ade369e
 * Each row keeps the register base the reference picks for graphics
 * version 9; the base macros (i915_reg.h) are written as their values, with
 * the Linux name beside each one.  The second video decode engine of the
 * GT3 and GT4 parts (VCS1, GEN8_BSD2_RING_BASE 0x1c000) is left out: the
 * driver offers no video work on Gen9.  The rows are
 * { id, class, instance, register base, name }.
 */

	/* I915_GT_RENDER_RING_BASE */
	{ I915_RCS0, I915_RENDER_CLASS, 0, 0x02000U, "rcs0" },

	/* I915_GT_BLT_RING_BASE */
	{ I915_BCS0, I915_COPY_ENGINE_CLASS, 0, 0x22000U, "bcs0" },

	/* GEN6_BSD_RING_BASE */
	{ I915_VCS0, I915_VIDEO_DECODE_CLASS, 0, 0x12000U, "vcs0" },

	/* VEBOX_RING_BASE */
	{ I915_VECS0, I915_VIDEO_ENHANCEMENT_CLASS, 0, 0x1a000U, "vecs0" },
