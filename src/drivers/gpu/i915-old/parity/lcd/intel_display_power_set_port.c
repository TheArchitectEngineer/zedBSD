/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

/*
 * zedBSD WS031: generated from Linux v6.8.12 drivers/gpu/drm/i915/display/intel_display_power.c
 * (sha256 acbe0526744d4f90f191a694823c51a458d169c20423f098694f083e0f912708) by plan/ws031/handover/tools/port_lcd_calc.py.
 * The function bodies are the reference text.  Kept / changed:
 *  - kept: intel_display_power_get_in_set, intel_display_power_put_mask_in_set;
 *  - the includes are replaced by: lcd_compat.h, lcd_seq_compat.h, lcd_modeset_compat.h;
 */

#include "lcd_compat.h"
#include "lcd_seq_compat.h"
#include "lcd_modeset_compat.h"

void
intel_display_power_get_in_set(struct drm_i915_private *i915,
			       struct intel_display_power_domain_set *power_domain_set,
			       enum intel_display_power_domain domain)
{
	intel_wakeref_t __maybe_unused wf;

	drm_WARN_ON(&i915->drm, test_bit(domain, power_domain_set->mask.bits));

	wf = intel_display_power_get(i915, domain);
#if IS_ENABLED(CONFIG_DRM_I915_DEBUG_RUNTIME_PM)
	power_domain_set->wakerefs[domain] = wf;
#endif
	set_bit(domain, power_domain_set->mask.bits);
}

void
intel_display_power_put_mask_in_set(struct drm_i915_private *i915,
				    struct intel_display_power_domain_set *power_domain_set,
				    struct intel_power_domain_mask *mask)
{
	enum intel_display_power_domain domain;

	drm_WARN_ON(&i915->drm,
		    !bitmap_subset(mask->bits, power_domain_set->mask.bits, POWER_DOMAIN_NUM));

	for_each_power_domain(domain, mask) {
		intel_wakeref_t __maybe_unused wf = -1;

#if IS_ENABLED(CONFIG_DRM_I915_DEBUG_RUNTIME_PM)
		wf = fetch_and_zero(&power_domain_set->wakerefs[domain]);
#endif
		intel_display_power_put(i915, domain, wf);
		clear_bit(domain, power_domain_set->mask.bits);
	}
}

