/*
 * zedBSD WS031: enum plane_id extracted textually from the Linux v6.8.12 i915 reference
 * (display/intel_display_limits.h: SPDX MIT, Copyright Intel Corporation -- full notice in skl_plane_port.c)
 * by tools/port_lcd_calc.py.  Do not edit by hand.
 */
#ifndef PARITY_LCD_PLANE_TYPES_H
#define PARITY_LCD_PLANE_TYPES_H

enum plane_id {
	PLANE_PRIMARY,
	PLANE_SPRITE0,
	PLANE_SPRITE1,
	PLANE_SPRITE2,
	PLANE_SPRITE3,
	PLANE_SPRITE4,
	PLANE_SPRITE5,
	PLANE_CURSOR,

	I915_MAX_PLANES,
};


#endif /* PARITY_LCD_PLANE_TYPES_H */
