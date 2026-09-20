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
#ifndef PARITY_LCD_DBUF_TYPES_H
#define PARITY_LCD_DBUF_TYPES_H

struct intel_dbuf_state {
	struct intel_global_state base;

	struct skl_ddb_entry ddb[I915_MAX_PIPES];
	unsigned int weight[I915_MAX_PIPES];
	u8 slices[I915_MAX_PIPES];
	u8 enabled_slices;
	u8 active_pipes;
	bool joined_mbus;
};

#endif /* PARITY_LCD_DBUF_TYPES_H */
