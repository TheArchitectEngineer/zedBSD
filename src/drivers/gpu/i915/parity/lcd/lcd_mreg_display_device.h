/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

/*
 * zedBSD WS031: macro definitions extracted textually from the Linux v6.8.12 reference drivers/gpu/drm/i915/display/intel_display_device.h
 * (sha256 cb7d717bb48eacf17756cb666dad7cf0e006611dcbb6358a0f33f2fed23e1e9f) by tools/port_lcd_calc.py:
 * the root macros listed in tools/port_lcd_modeset.json plus every macro of that header they use.
 * The notice above is the source file's own.  Do not edit by hand.
 */
#ifndef PARITY_LCD_MREG_DISPLAY_DEVICE_H
#define PARITY_LCD_MREG_DISPLAY_DEVICE_H

#define HAS_MSO(i915)			(DISPLAY_VER(i915) >= 12)

#endif /* PARITY_LCD_MREG_DISPLAY_DEVICE_H */
