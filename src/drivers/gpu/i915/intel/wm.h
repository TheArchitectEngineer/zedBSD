/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 */

/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2019 Intel Corporation
 * Copyright © 2021 Intel Corporation
 */

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
 * The watermark types of the Linux display headers, for the modeset
 * environment: enum dbuf_slice (intel_display_power.h), the watermark level
 * and the plane and pipe watermarks (intel_display_types.h), and the DDB
 * entry (intel_wm_types.h).
 *
 * The DBUF slices (Linux intel_display_power.h).
 * zedBSD WS031: definitions extracted textually from the Linux v6.8.12 reference drivers/gpu/drm/i915/display/intel_display_power.h
 * (sha256 9dd042ad4c10c5efd3f4320145f9e08bb2ff76539db70ce538c99367bd80c04d) by tools/port_lcd_calc.py:
 * enum dbuf_slice.
 * The notice above is the source file's own.
 *
 * The watermark levels (Linux intel_display_types.h).
 * zedBSD WS031: definitions extracted textually from the Linux v6.8.12 reference drivers/gpu/drm/i915/display/intel_display_types.h
 * (sha256 647773fcee0c549791a50b9c6a4cde175c11005497689e9d6d4bbd2db8d38db4) by tools/port_lcd_calc.py:
 * struct skl_wm_level, struct skl_plane_wm, struct skl_pipe_wm.
 * The notice above is the source file's own.
 *
 * The DDB entry (Linux intel_wm_types.h).
 * zedBSD WS031: definitions extracted textually from the Linux v6.8.12 reference drivers/gpu/drm/i915/display/intel_wm_types.h
 * (sha256 275b607f847b0134300fdf4ce6bd4a7f5cef9cd83dcc1c83ba48dc634f8c9e2f) by tools/port_lcd_calc.py:
 * struct skl_ddb_entry, skl_ddb_entry_size, skl_ddb_entry_equal.
 * The notice above is the source file's own.
 */

#ifndef DRIVERS_GPU_I915_INTEL_WM_H
#define DRIVERS_GPU_I915_INTEL_WM_H

/* The DBUF slices (Linux intel_display_power.h). */

enum dbuf_slice {
	DBUF_S1,
	DBUF_S2,
	DBUF_S3,
	DBUF_S4,
	I915_MAX_DBUF_SLICES
};

/* The watermark levels (Linux intel_display_types.h). */

struct skl_wm_level {
	u16 min_ddb_alloc;
	u16 blocks;
	u8 lines;
	bool enable;
	bool ignore_lines;
	bool can_sagv;
};

struct skl_plane_wm {
	struct skl_wm_level wm[8];
	struct skl_wm_level uv_wm[8];
	struct skl_wm_level trans_wm;
	struct {
		struct skl_wm_level wm0;
		struct skl_wm_level trans_wm;
	} sagv;
	bool is_planar;
};

struct skl_pipe_wm {
	struct skl_plane_wm planes[I915_MAX_PLANES];
	bool use_sagv_wm;
};

/* The DDB entry (Linux intel_wm_types.h). */

struct skl_ddb_entry {
	u16 start, end;	/* in number of blocks, 'end' is exclusive */
};

static inline u16 skl_ddb_entry_size(const struct skl_ddb_entry *entry)
{
	return entry->end - entry->start;
}

static inline bool skl_ddb_entry_equal(const struct skl_ddb_entry *e1,
				       const struct skl_ddb_entry *e2)
{
	if (e1->start == e2->start && e1->end == e2->end)
		return true;

	return false;
}

#endif /* DRIVERS_GPU_I915_INTEL_WM_H */
