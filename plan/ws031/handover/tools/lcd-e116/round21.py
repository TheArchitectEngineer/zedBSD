#!/usr/bin/env python3
"""WS031 E-116 round 21: last compat gaps of the commit's outer part.
usage: round21.py <repo root>"""
import sys
NL, TAB, BS = chr(10), chr(9), chr(92)
root = sys.argv[1].rstrip("/") + "/"
L = "src/drivers/gpu/i915/parity/lcd/"
def load(p): return open(root + p).read()
def save(p, s):
    open(root + p, "w").write(s); print("patched", p)
def rep(s, old, new):
    assert s.count(old) == 1, old[:90]
    return s.replace(old, new)

c = load(L + "lcd_compat.h")
c = rep(c, "		struct { struct { u32 size; u8 slice_mask; } dbuf; } device_info;",
        "		struct { struct { u32 size; u8 slice_mask; } dbuf; bool has_ddi; } device_info;")
c = rep(c, "struct drm_i915_private {", "#define DISPLAY_INFO(i915) (&(i915)->display.device_info)" + NL + "struct drm_i915_private {")
c = rep(c, "const struct drm_property_blob *degamma_lut, *gamma_lut, *ctm; bool active; } hw;",
        "const struct drm_property_blob *degamma_lut, *gamma_lut, *ctm; bool active, enable; } hw;")
c = rep(c, "struct intel_encoder {", "#define to_intel_encoder(e) ((struct intel_encoder *)(e))      /* base is the first member */" + NL + "struct intel_encoder {")
save(L + "lcd_compat.h", c)

w = load(L + "lcd_wm_compat.h")
w = rep(w, "#define DISPLAY_INFO(i915) (&(i915)->display.device_info)" + NL, "")
save(L + "lcd_wm_compat.h", w)
d = load(L + "lcd_dp_compat.h")
d = rep(d, "#define to_intel_encoder(e) ((struct intel_encoder *)(e))" + NL, "")
save(L + "lcd_dp_compat.h", d)

m = load(L + "lcd_modeset_compat.h")
m = rep(m, "typedef int intel_wakeref_t;", "typedef int intel_wakeref_t;" + NL +
        "void intel_display_power_get_in_set(struct drm_i915_private *i915, struct intel_display_power_domain_set *power_domain_set," + NL +
        "	enum intel_display_power_domain domain);" + NL +
        "void intel_display_power_put_mask_in_set(struct drm_i915_private *i915, struct intel_display_power_domain_set *power_domain_set," + NL +
        "	struct intel_power_domain_mask *mask);")
save(L + "lcd_modeset_compat.h", m)
print("done")
