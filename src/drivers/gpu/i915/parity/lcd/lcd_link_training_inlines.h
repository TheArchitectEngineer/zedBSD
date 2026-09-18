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
#ifndef PARITY_LCD_LINK_TRAINING_INLINES_H
#define PARITY_LCD_LINK_TRAINING_INLINES_H

static inline u8 intel_dp_training_pattern_symbol(u8 pattern)
{
	return pattern & ~DP_LINK_SCRAMBLING_DISABLE;
}

#endif /* PARITY_LCD_LINK_TRAINING_INLINES_H */
