/* SPDX-License-Identifier: MIT */
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
 *
 * Modified for zedBSD: excerpted and reformatted by hand from the Linux
 * 6.8.12 reference (plan/ws031/linux-parity/linux-reference/i915-src).
 * Values are unchanged.  Source (SHA-256 of the reference file):
 *   drivers/gpu/drm/i915/gt/intel_engine_cs.c, intel_engines[]
 *     da0063e3ff12448e17585c301a442bf2fb90ee9ca5f787af30b640747ade369e
 * Only the engines Alder Lake-P can have remain, each with the Gen11+
 * register base; the base macros are written as their values, with the
 * Linux name beside each one.  The rows are
 * { id, class, instance, register base, name }.
 */

	/* RENDER_RING_BASE */
	{ I915_RCS0, I915_RENDER_CLASS, 0, 0x02000U, "rcs0" },

	/* BLT_RING_BASE */
	{ I915_BCS0, I915_COPY_ENGINE_CLASS, 0, 0x22000U, "bcs0" },

	/* GEN11_BSD_RING_BASE */
	{ I915_VCS0, I915_VIDEO_DECODE_CLASS, 0, 0x1c0000U, "vcs0" },

	/* GEN11_BSD3_RING_BASE (GEN11_BSD2_RING_BASE, 0x1c4000, is VCS1) */
	{ I915_VCS2, I915_VIDEO_DECODE_CLASS, 2, 0x1d0000U, "vcs2" },

	/* GEN11_VEBOX_RING_BASE */
	{ I915_VECS0, I915_VIDEO_ENHANCEMENT_CLASS, 0, 0x1c8000U, "vecs0" },
