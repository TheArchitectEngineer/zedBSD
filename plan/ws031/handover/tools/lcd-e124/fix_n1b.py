#!/usr/bin/env python3
"""E-124 fixup: the power put of this path is a statement macro (it cannot sit in a for-header), and the Broxton
predicate must be a value here."""
import sys
root = sys.argv[1].rstrip("/") + "/"
p = root + "src/drivers/gpu/i915/parity/lcd/n1_compat.h"
NL = chr(10)
TAB = chr(9)
BS = chr(92)
s = open(p).read()
old = ("#define with_intel_display_power_if_enabled(i915, domain, wf) " + BS + NL +
       TAB + "for ((wf) = parity_n1_power_get_if_enabled(domain); (wf); " + BS + NL +
       TAB + "     intel_display_power_put((i915), (domain), (wf)), (wf) = 0)")
new = ("/* the put of this path is a statement macro: wrap it so it can sit in the loop's header */" + NL +
       "static inline void parity_n1_power_put(struct drm_i915_private *i915," + NL +
       TAB + "enum intel_display_power_domain domain, intel_wakeref_t wf)" + NL +
       "{" + NL + TAB + "intel_display_power_put(i915, domain, wf);" + NL + "}" + NL +
       "#define with_intel_display_power_if_enabled(i915, domain, wf) " + BS + NL +
       TAB + "for ((wf) = parity_n1_power_get_if_enabled(domain); (wf) != 0; " + BS + NL +
       TAB + "     parity_n1_power_put((i915), (domain), (wf)), (wf) = 0)")
if old in s:
    s = s.replace(old, new)
if "#undef IS_BROXTON" not in s:
    s = s.replace("#define BXT_PHY_LANE_ENABLED 0u",
                  "#undef IS_BROXTON" + NL + "#define IS_BROXTON(i915) (0)" + NL +
                  "#undef IS_GEMINILAKE" + NL + "#define IS_GEMINILAKE(i915) (0)" + NL +
                  "#define BXT_PHY_LANE_ENABLED 0u")
open(p, "w").write(s)
print("fixed")
