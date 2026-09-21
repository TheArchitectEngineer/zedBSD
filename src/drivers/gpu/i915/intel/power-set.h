/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 */

/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2019 Intel Corporation
 */

/*
 * The power domain mask and set of Linux intel_display_power.h.  A header
 * of its own because display/modeset-internal.h includes it after its
 * bitmap macros (DECLARE_BITMAP()).
 *
 * zedBSD WS031: definitions extracted textually from the Linux v6.8.12 reference drivers/gpu/drm/i915/display/intel_display_power.h
 * (sha256 9dd042ad4c10c5efd3f4320145f9e08bb2ff76539db70ce538c99367bd80c04d) by tools/port_lcd_calc.py:
 * struct intel_power_domain_mask, struct intel_display_power_domain_set, for_each_power_domain().
 * The notice above is the source file's own.
 */

#ifndef DRIVERS_GPU_I915_INTEL_POWER_SET_H
#define DRIVERS_GPU_I915_INTEL_POWER_SET_H

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

#endif /* DRIVERS_GPU_I915_INTEL_POWER_SET_H */
