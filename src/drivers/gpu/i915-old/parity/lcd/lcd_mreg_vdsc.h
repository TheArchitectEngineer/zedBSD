/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

/*
 * zedBSD WS031: macro definitions extracted textually from the Linux v6.8.12 reference drivers/gpu/drm/i915/display/intel_vdsc_regs.h
 * (sha256 a104c2392247301738c6a340bf67127c3bfd2f23d6227e760607a11b025f231d) by tools/port_lcd_calc.py:
 * the root macros listed in tools/port_lcd_modeset.json plus every macro of that header they use.
 * The notice above is the source file's own.  Do not edit by hand.
 */
#ifndef PARITY_LCD_MREG_VDSC_H
#define PARITY_LCD_MREG_VDSC_H

#define  SPLITTER_ENABLE			(1 << 31)
#define  OVERLAP_PIXELS_MASK			(0xf << 16)
#define  OVERLAP_PIXELS(pixels)			((pixels) << 16)
#define _ICL_PIPE_DSS_CTL1_PB			0x78200
#define _ICL_PIPE_DSS_CTL1_PC			0x78400
#define ICL_PIPE_DSS_CTL1(pipe)			_MMIO_PIPE((pipe) - PIPE_B, \
							   _ICL_PIPE_DSS_CTL1_PB, \
							   _ICL_PIPE_DSS_CTL1_PC)
#define  SPLITTER_CONFIGURATION_MASK		REG_GENMASK(26, 25)
#define  SPLITTER_CONFIGURATION_2_SEGMENT	REG_FIELD_PREP(SPLITTER_CONFIGURATION_MASK, 0)
#define  SPLITTER_CONFIGURATION_4_SEGMENT	REG_FIELD_PREP(SPLITTER_CONFIGURATION_MASK, 1)

#endif /* PARITY_LCD_MREG_VDSC_H */
