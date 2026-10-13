/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 */

/*
 * Copyright © 2013 Intel Corporation
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
 * The rows of the Gen9 register to forcewake domain map; mmio.c includes
 * them inside the initializer of i915_gen9_ranges[].
 *
 * Modified for zedBSD: excerpted and reformatted by hand from the Linux
 * 6.8.12 reference.  Values are unchanged.  Source (SHA-256 of the file):
 *   drivers/gpu/drm/i915/intel_uncore.c, __gen9_fw_ranges
 *     015f123f42dff0393c11315af79bcb49bc7cd4f9c92a109dcdb3533a6509768e
 * The reference names the copy engine's domain BLITTER and lists it as GT;
 * its single media domain is I915_FORCEWAKE_MEDIA_GEN9 here.  The two ranges
 * the reference gives to RENDER and MEDIA together are listed under RENDER:
 * the running device holds every domain at once, so the map only decides
 * which hold the checked access looks for.  The always-on entries are
 * omitted because no match already means always on.  The notice above is the
 * source file's own.
 */

	{ 0x0u, 0xaffu, I915_FORCEWAKE_GT },
	/* 0xb00 .. 0x1fff always-on */
	{ 0x2000u, 0x26ffu, I915_FORCEWAKE_RENDER },
	{ 0x2700u, 0x2fffu, I915_FORCEWAKE_GT },
	{ 0x3000u, 0x3fffu, I915_FORCEWAKE_RENDER },
	{ 0x4000u, 0x51ffu, I915_FORCEWAKE_GT },
	{ 0x5200u, 0x7fffu, I915_FORCEWAKE_RENDER },
	{ 0x8000u, 0x812fu, I915_FORCEWAKE_GT },
	{ 0x8130u, 0x813fu, I915_FORCEWAKE_MEDIA_GEN9 },
	{ 0x8140u, 0x815fu, I915_FORCEWAKE_RENDER },
	{ 0x8160u, 0x82ffu, I915_FORCEWAKE_GT },
	{ 0x8300u, 0x84ffu, I915_FORCEWAKE_RENDER },
	{ 0x8500u, 0x87ffu, I915_FORCEWAKE_GT },
	{ 0x8800u, 0x89ffu, I915_FORCEWAKE_MEDIA_GEN9 },
	{ 0x8a00u, 0x8bffu, I915_FORCEWAKE_GT },
	{ 0x8c00u, 0x8cffu, I915_FORCEWAKE_RENDER },
	{ 0x8d00u, 0x93ffu, I915_FORCEWAKE_GT },
	/* 0x9400 .. 0x97ff is RENDER and MEDIA in the reference */
	{ 0x9400u, 0x97ffu, I915_FORCEWAKE_RENDER },
	{ 0x9800u, 0xafffu, I915_FORCEWAKE_GT },
	{ 0xb000u, 0xb47fu, I915_FORCEWAKE_RENDER },
	{ 0xb480u, 0xcfffu, I915_FORCEWAKE_GT },
	{ 0xd000u, 0xd7ffu, I915_FORCEWAKE_MEDIA_GEN9 },
	{ 0xd800u, 0xdfffu, I915_FORCEWAKE_GT },
	{ 0xe000u, 0xe8ffu, I915_FORCEWAKE_RENDER },
	{ 0xe900u, 0x11fffu, I915_FORCEWAKE_GT },
	{ 0x12000u, 0x13fffu, I915_FORCEWAKE_MEDIA_GEN9 },
	{ 0x14000u, 0x19fffu, I915_FORCEWAKE_GT },
	{ 0x1a000u, 0x1e9ffu, I915_FORCEWAKE_MEDIA_GEN9 },
	{ 0x1ea00u, 0x243ffu, I915_FORCEWAKE_GT },
	{ 0x24400u, 0x247ffu, I915_FORCEWAKE_RENDER },
	{ 0x24800u, 0x2ffffu, I915_FORCEWAKE_GT },
	{ 0x30000u, 0x3ffffu, I915_FORCEWAKE_MEDIA_GEN9 },
