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
/*
 * zedBSD display rebuild: moved from src/drivers/gpu/i915/parity/lcd/lcd_mreg_display_device.h of the
 * pre-rebuild tree (commit 7e7ff337) without change to any definition;
 * the include guard PARITY_LCD_MREG_DISPLAY_DEVICE_H was renamed.  The provenance above still applies and names the generator.
 */

#ifndef DRIVERS_GPU_I915_DATA_DISPLAY_MREG_DISPLAY_DEVICE_INC
#define DRIVERS_GPU_I915_DATA_DISPLAY_MREG_DISPLAY_DEVICE_INC

#define HAS_DDI(i915)			(DISPLAY_INFO(i915)->has_ddi)
#define HAS_HW_SAGV_WM(i915)		(DISPLAY_VER(i915) >= 13 && !IS_DGFX(i915))
#define HAS_MBUS_JOINING(i915)		(IS_ALDERLAKE_P(i915) || DISPLAY_VER(i915) >= 14)
#define HAS_MSO(i915)			(DISPLAY_VER(i915) >= 12)

#endif /* DRIVERS_GPU_I915_DATA_DISPLAY_MREG_DISPLAY_DEVICE_INC */
