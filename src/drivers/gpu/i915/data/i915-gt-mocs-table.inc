/* SPDX-License-Identifier: MIT */
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
 *
 * Modified for zedBSD: excerpted by hand from the Linux kernel 6.8.12
 * drivers/gpu/drm/i915/gt/intel_mocs.c (gen12_mocs_table, which expands
 * GEN11_MOCS_ENTRIES) and reformatted; values are unchanged.
 * Rewrites: MOCS_ENTRY(index, control, l3cc) becomes one row
 * { index, control, l3cc } of an array initializer.
 *
 * The rows are included inside an array of { index, control, l3cc }; the
 * value macros come from i915-gt-workarounds.inc.  Entries 0 and 1 are
 * "defined per-platform" and Gen12 does not define them.
 *
 * XXX: GEN11_MOCS_ENTRIES in Linux also defines entry 63 (LE_3_WB |
 * LE_TC_1_LLC | LE_LRUM(3), L3_1_UC).  This table has no row 63, so entry 63
 * takes the unused-entry values (L3_3_WB instead of L3_1_UC in the upper half
 * of LNCFCMOCS31).  The row is left out exactly as the hardware-checked table
 * had it.
 */
	/* Base - L3 + LLC */
	{  2, LE_3_WB | LE_TC_1_LLC | LE_LRUM(3), L3_3_WB },
	/* Base - Uncached */
	{  3, LE_1_UC | LE_TC_1_LLC, L3_1_UC },
	/* Base - L3 */
	{  4, LE_1_UC | LE_TC_1_LLC, L3_3_WB },
	/* Base - LLC */
	{  5, LE_3_WB | LE_TC_1_LLC | LE_LRUM(3), L3_1_UC },
	/* Age 0 - LLC */
	{  6, LE_3_WB | LE_TC_1_LLC | LE_LRUM(1), L3_1_UC },
	/* Age 0 - L3 + LLC */
	{  7, LE_3_WB | LE_TC_1_LLC | LE_LRUM(1), L3_3_WB },
	/* Age: Don't Chg. - LLC */
	{  8, LE_3_WB | LE_TC_1_LLC | LE_LRUM(2), L3_1_UC },
	/* Age: Don't Chg. - L3 + LLC */
	{  9, LE_3_WB | LE_TC_1_LLC | LE_LRUM(2), L3_3_WB },
	/* No AOM - LLC */
	{ 10, LE_3_WB | LE_TC_1_LLC | LE_LRUM(3) | LE_AOM(1), L3_1_UC },
	/* No AOM - L3 + LLC */
	{ 11, LE_3_WB | LE_TC_1_LLC | LE_LRUM(3) | LE_AOM(1), L3_3_WB },
	/* No AOM; Age 0 - LLC */
	{ 12, LE_3_WB | LE_TC_1_LLC | LE_LRUM(1) | LE_AOM(1), L3_1_UC },
	/* No AOM; Age 0 - L3 + LLC */
	{ 13, LE_3_WB | LE_TC_1_LLC | LE_LRUM(1) | LE_AOM(1), L3_3_WB },
	/* No AOM; Age:DC - LLC */
	{ 14, LE_3_WB | LE_TC_1_LLC | LE_LRUM(2) | LE_AOM(1), L3_1_UC },
	/* No AOM; Age:DC - L3 + LLC */
	{ 15, LE_3_WB | LE_TC_1_LLC | LE_LRUM(2) | LE_AOM(1), L3_3_WB },
	/* Bypass LLC - Uncached (EHL+) */
	{ 16, LE_1_UC | LE_TC_1_LLC | LE_SCF(1), L3_1_UC },
	/* Bypass LLC - L3 (Read-Only) (EHL+) */
	{ 17, LE_1_UC | LE_TC_1_LLC | LE_SCF(1), L3_3_WB },
	/* Self-Snoop - L3 + LLC */
	{ 18, LE_3_WB | LE_TC_1_LLC | LE_LRUM(3) | LE_SSE(3), L3_3_WB },
	/* Skip Caching - L3 + LLC(12.5%) */
	{ 19, LE_3_WB | LE_TC_1_LLC | LE_LRUM(3) | LE_SCC(7), L3_3_WB },
	/* Skip Caching - L3 + LLC(25%) */
	{ 20, LE_3_WB | LE_TC_1_LLC | LE_LRUM(3) | LE_SCC(3), L3_3_WB },
	/* Skip Caching - L3 + LLC(50%) */
	{ 21, LE_3_WB | LE_TC_1_LLC | LE_LRUM(3) | LE_SCC(1), L3_3_WB },
	/* Skip Caching - L3 + LLC(75%) */
	{ 22, LE_3_WB | LE_TC_1_LLC | LE_LRUM(3) | LE_RSC(1) | LE_SCC(3), L3_3_WB },
	/* Skip Caching - L3 + LLC(87.5%) */
	{ 23, LE_3_WB | LE_TC_1_LLC | LE_LRUM(3) | LE_RSC(1) | LE_SCC(7), L3_3_WB },
	/* HW Reserved - SW program but never use */
	{ 62, LE_3_WB | LE_TC_1_LLC | LE_LRUM(3), L3_1_UC },
	/* Implicitly enable L1 - HDC:L1 + L3 + LLC */
	{ 48, LE_3_WB | LE_TC_1_LLC | LE_LRUM(3), L3_3_WB },
	/* Implicitly enable L1 - HDC:L1 + L3 */
	{ 49, LE_1_UC | LE_TC_1_LLC, L3_3_WB },
	/* Implicitly enable L1 - HDC:L1 + LLC */
	{ 50, LE_3_WB | LE_TC_1_LLC | LE_LRUM(3), L3_1_UC },
	/* Implicitly enable L1 - HDC:L1 */
	{ 51, LE_1_UC | LE_TC_1_LLC, L3_1_UC },
	/* HW Special Case (CCS) */
	{ 60, LE_3_WB | LE_TC_1_LLC | LE_LRUM(3), L3_1_UC },
	/* HW Special Case (Displayable) */
	{ 61, LE_1_UC | LE_TC_1_LLC, L3_3_WB },
