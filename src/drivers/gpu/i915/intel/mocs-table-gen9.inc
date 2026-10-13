/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 */

/*
 * Copyright © 2015 Intel Corporation
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
 * IN NO EVENT SHALL TUNGSTEN GRAPHICS AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/*
 * The rows of the Gen9 MOCS table of Linux intel_mocs.c; workarounds.c
 * includes them inside the initializer of i915_gen9_mocs_table[].
 *
 * Modified for zedBSD: excerpted by hand from the Linux kernel 6.8.12
 * drivers/gpu/drm/i915/gt/intel_mocs.c (skl_mocs_table, which expands
 * GEN9_MOCS_ENTRIES) and reformatted; values are unchanged.  Source
 * (SHA-256): 49f2b6a2a47bcf26bf57eef51d5a125458378298602462b8ff9986a54a15ffe7.
 * Rewrites: MOCS_ENTRY(index, control, l3cc) becomes one row
 * { index, control, l3cc } of an array initializer; I915_MOCS_UNCACHED,
 * I915_MOCS_PTE and I915_MOCS_CACHED are written as 0, 1 and 2.  Every
 * other index takes the values of index 1 (I915_MOCS_PTE).
 */

	/* I915_MOCS_UNCACHED */
	{  0, LE_1_UC | LE_TC_2_LLC_ELLC, L3_1_UC },
	/* I915_MOCS_PTE */
	{  1, LE_0_PAGETABLE | LE_TC_0_PAGETABLE | LE_LRUM(3), L3_3_WB },
	/* I915_MOCS_CACHED */
	{  2, LE_3_WB | LE_TC_2_LLC_ELLC | LE_LRUM(3), L3_3_WB },
	/* used by the L3 for all of its evictions, and to force L3 uncacheable cycles */
	{ 63, LE_3_WB | LE_TC_1_LLC | LE_LRUM(3), L3_1_UC },
