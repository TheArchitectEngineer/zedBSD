/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

/*
 * zedBSD WS031: definitions extracted textually from the Linux v6.8.12 reference drivers/gpu/drm/i915/display/skl_watermark.h
 * (sha256 f5d091e86d64b9c75ddc37d7600a0364cde9fb954391893a2d805a6c20166a7d) by tools/port_lcd_calc.py:
 * struct intel_dbuf_state.
 * The notice above is the source file's own.  Do not edit by hand.
 */
/*
 * zedBSD display rebuild: moved from src/drivers/gpu/i915/parity/lcd/lcd_dbuf_types.h of the
 * pre-rebuild tree (commit 7e7ff337) without change to any definition;
 * the include guard PARITY_LCD_DBUF_TYPES_H was renamed.  The provenance above still applies and names the generator.
 */

#ifndef DRIVERS_GPU_I915_DATA_DISPLAY_DBUF_TYPES_INC
#define DRIVERS_GPU_I915_DATA_DISPLAY_DBUF_TYPES_INC

struct intel_dbuf_state {
	struct intel_global_state base;

	struct skl_ddb_entry ddb[I915_MAX_PIPES];
	unsigned int weight[I915_MAX_PIPES];
	u8 slices[I915_MAX_PIPES];
	u8 enabled_slices;
	u8 active_pipes;
	bool joined_mbus;
};

#endif /* DRIVERS_GPU_I915_DATA_DISPLAY_DBUF_TYPES_INC */
