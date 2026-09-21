/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

/*
 * zedBSD WS031: definitions extracted textually from the Linux v6.8.12 reference drivers/gpu/drm/i915/display/intel_display_power.h
 * (sha256 9dd042ad4c10c5efd3f4320145f9e08bb2ff76539db70ce538c99367bd80c04d) by tools/port_lcd_calc.py: 
 * enum dbuf_slice.
 * The notice above is the source file's own.  Do not edit by hand.
 */
#ifndef PARITY_LCD_DBUF_SLICE_ENUM_H
#define PARITY_LCD_DBUF_SLICE_ENUM_H

enum dbuf_slice {
	DBUF_S1,
	DBUF_S2,
	DBUF_S3,
	DBUF_S4,
	I915_MAX_DBUF_SLICES
};

#endif /* PARITY_LCD_DBUF_SLICE_ENUM_H */
