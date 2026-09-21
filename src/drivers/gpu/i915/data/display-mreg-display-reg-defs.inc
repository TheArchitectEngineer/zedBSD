/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

/*
 * zedBSD WS031: macro definitions extracted textually from the Linux v6.8.12 reference drivers/gpu/drm/i915/display/intel_display_reg_defs.h
 * (sha256 a33d6ec765231b345dff39754227988263740254e06ad3e158f52876d81745d6) by tools/port_lcd_calc.py:
 * the root macros listed in tools/port_lcd_modeset.json plus every macro of that header they use.
 * The notice above is the source file's own.  Do not edit by hand.
 */
/*
 * zedBSD display rebuild: moved from src/drivers/gpu/i915/parity/lcd/lcd_mreg_display_reg_defs.h of the
 * pre-rebuild tree (commit 7e7ff337) without change to any definition;
 * the include guard PARITY_LCD_MREG_DISPLAY_REG_DEFS_H was renamed.  The provenance above still applies and names the generator.
 */

#ifndef DRIVERS_GPU_I915_DATA_DISPLAY_MREG_DISPLAY_REG_DEFS_INC
#define DRIVERS_GPU_I915_DATA_DISPLAY_MREG_DISPLAY_REG_DEFS_INC

#define _PIPE(pipe, a, b)		_PICK_EVEN(pipe, a, b)
#define _PLL(pll, a, b)			_PICK_EVEN(pll, a, b)
#define _MMIO_PIPE(pipe, a, b)		_MMIO(_PIPE(pipe, a, b))
#define _MMIO_PLL(pll, a, b)		_MMIO(_PLL(pll, a, b))

#endif /* DRIVERS_GPU_I915_DATA_DISPLAY_MREG_DISPLAY_REG_DEFS_INC */
