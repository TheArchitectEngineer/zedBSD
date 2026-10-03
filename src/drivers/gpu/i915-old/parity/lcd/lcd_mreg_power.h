/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

/*
 * zedBSD WS031: macro definitions extracted textually from the Linux v6.8.12 reference drivers/gpu/drm/i915/display/intel_display_power.h
 * (sha256 9dd042ad4c10c5efd3f4320145f9e08bb2ff76539db70ce538c99367bd80c04d) by tools/port_lcd_calc.py:
 * the root macros listed in tools/port_lcd_modeset.json plus every macro of that header they use.
 * The notice above is the source file's own.  Do not edit by hand.
 */
#ifndef PARITY_LCD_MREG_POWER_H
#define PARITY_LCD_MREG_POWER_H

#define POWER_DOMAIN_PIPE(pipe) ((pipe) + POWER_DOMAIN_PIPE_A)
#define POWER_DOMAIN_PIPE_PANEL_FITTER(pipe) \
		((pipe) + POWER_DOMAIN_PIPE_PANEL_FITTER_A)
#define POWER_DOMAIN_TRANSCODER(tran) \
	((tran) == TRANSCODER_EDP ? POWER_DOMAIN_TRANSCODER_EDP : \
	 (tran) + POWER_DOMAIN_TRANSCODER_A)

#endif /* PARITY_LCD_MREG_POWER_H */
