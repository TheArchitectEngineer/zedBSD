/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2014-2018 Intel Corporation
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
 * drivers/gpu/drm/i915/gt/intel_workarounds.c (mcr_ranges_gen12) and
 * reformatted; values are unchanged.  The terminating empty entry is dropped.
 *
 * The rows are included inside an array of { start, end }, both inclusive.
 * They are the Gen12 (below 12.50) multicast register ranges: a command
 * streamer read in one of them reaches an unsteered instance.
 */
	{ 0x8150U, 0x815fU },
	{ 0x9520U, 0x955fU },
	{ 0xb100U, 0xb3ffU },
	{ 0xde80U, 0xe8ffU },
	{ 0x24a00U, 0x24a7fU },
