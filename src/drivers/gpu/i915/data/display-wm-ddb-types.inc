/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2021 Intel Corporation
 */

/*
 * zedBSD WS031: definitions extracted textually from the Linux v6.8.12 reference drivers/gpu/drm/i915/display/intel_wm_types.h
 * (sha256 275b607f847b0134300fdf4ce6bd4a7f5cef9cd83dcc1c83ba48dc634f8c9e2f) by tools/port_lcd_calc.py:
 * struct skl_ddb_entry, skl_ddb_entry_size, skl_ddb_entry_equal.
 * The notice above is the source file's own.  Do not edit by hand.
 */
/*
 * zedBSD display rebuild: moved from src/drivers/gpu/i915/parity/lcd/lcd_wm_ddb_types.h of the
 * pre-rebuild tree (commit 7e7ff337) without change to any definition;
 * the include guard PARITY_LCD_WM_DDB_TYPES_H was renamed.  The provenance above still applies and names the generator.
 */

#ifndef DRIVERS_GPU_I915_DATA_DISPLAY_WM_DDB_TYPES_INC
#define DRIVERS_GPU_I915_DATA_DISPLAY_WM_DDB_TYPES_INC

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

#endif /* DRIVERS_GPU_I915_DATA_DISPLAY_WM_DDB_TYPES_INC */
