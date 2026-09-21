/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

/*
 * zedBSD WS031: definitions extracted textually from the Linux v6.8.12 reference drivers/gpu/drm/i915/display/intel_dp_link_training.h
 * (sha256 0a4d31fd19d9db3d19fb5f44a21112457d6bd7495a43124fac5a1cb290a9a0cb) by tools/port_lcd_calc.py:
 * intel_dp_training_pattern_symbol.
 * The notice above is the source file's own.  Do not edit by hand.
 */
/*
 * zedBSD display rebuild: moved from src/drivers/gpu/i915/parity/lcd/lcd_link_training_inlines.h of the
 * pre-rebuild tree (commit 7e7ff337) without change to any definition;
 * the include guard PARITY_LCD_LINK_TRAINING_INLINES_H was renamed.  The provenance above still applies and names the generator.
 */

#ifndef DRIVERS_GPU_I915_DATA_DISPLAY_LINK_TRAINING_INLINES_INC
#define DRIVERS_GPU_I915_DATA_DISPLAY_LINK_TRAINING_INLINES_INC

static inline u8 intel_dp_training_pattern_symbol(u8 pattern)
{
	return pattern & ~DP_LINK_SCRAMBLING_DISABLE;
}

#endif /* DRIVERS_GPU_I915_DATA_DISPLAY_LINK_TRAINING_INLINES_INC */
