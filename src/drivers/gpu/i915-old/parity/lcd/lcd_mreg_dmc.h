/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

/*
 * zedBSD WS031: macro definitions extracted textually from the Linux v6.8.12 reference drivers/gpu/drm/i915/display/intel_dmc_regs.h
 * (sha256 5b4518fce358d21fbb78543da94c5dc440913470b4359e5c411cf63dcaed1184) by tools/port_lcd_calc.py:
 * the root macros listed in tools/port_lcd_modeset.json plus every macro of that header they use.
 * The notice above is the source file's own.  Do not edit by hand.
 */
#ifndef PARITY_LCD_MREG_DMC_H
#define PARITY_LCD_MREG_DMC_H

#define _PIPEDMC_CONTROL_A		0x45250
#define _PIPEDMC_CONTROL_B		0x45254
#define PIPEDMC_CONTROL(pipe)		_MMIO_PIPE(pipe, \
						   _PIPEDMC_CONTROL_A, \
						   _PIPEDMC_CONTROL_B)
#define  PIPEDMC_ENABLE			REG_BIT(0)
#define MTL_PIPEDMC_CONTROL		_MMIO(0x45250)
#define  PIPEDMC_ENABLE_MTL(pipe)	REG_BIT(((pipe) - PIPE_A) * 4)

#endif /* PARITY_LCD_MREG_DMC_H */
