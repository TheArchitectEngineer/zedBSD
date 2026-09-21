/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

/*
 * zedBSD WS031: definitions extracted textually from the Linux v6.8.12 reference drivers/gpu/drm/i915/display/intel_display_power.h
 * (sha256 9dd042ad4c10c5efd3f4320145f9e08bb2ff76539db70ce538c99367bd80c04d) by tools/port_lcd_calc.py:
 * struct intel_power_domain_mask, struct intel_display_power_domain_set, for_each_power_domain().
 * The notice above is the source file's own.  Do not edit by hand.
 */
/*
 * zedBSD display rebuild: moved from src/drivers/gpu/i915/parity/lcd/lcd_power_domain_set_types.h of the
 * pre-rebuild tree (commit 7e7ff337) without change to any definition;
 * the include guard PARITY_LCD_POWER_DOMAIN_SET_TYPES_H was renamed.  The provenance above still applies and names the generator.
 */

#ifndef DRIVERS_GPU_I915_DATA_DISPLAY_POWER_DOMAIN_SET_TYPES_INC
#define DRIVERS_GPU_I915_DATA_DISPLAY_POWER_DOMAIN_SET_TYPES_INC

struct intel_power_domain_mask {
	DECLARE_BITMAP(bits, POWER_DOMAIN_NUM);
};

struct intel_display_power_domain_set {
	struct intel_power_domain_mask mask;
#ifdef CONFIG_DRM_I915_DEBUG_RUNTIME_PM
	intel_wakeref_t wakerefs[POWER_DOMAIN_NUM];
#endif
};

#define for_each_power_domain(__domain, __mask)				\
	for ((__domain) = 0; (__domain) < POWER_DOMAIN_NUM; (__domain)++)	\
		for_each_if(test_bit((__domain), (__mask)->bits))

#endif /* DRIVERS_GPU_I915_DATA_DISPLAY_POWER_DOMAIN_SET_TYPES_INC */
