#!/usr/bin/env python3
"""WS031 E-124 round 98: the last structural gaps of the readout.
 - the crtc keeps ONE set of enabled power domains (my extra field duplicated it);
 - `intel_power_domain_mask` is the mask inside a domain set: the readout's `*_in_set` helpers take the SET;
 - the crtc state gains `sink_format` and the device the DBUF object the watermark readout reads;
 - to_i915 is defined once (lcd_compat.h) for this path.
usage: round98.py <repo root>"""
import sys
root = sys.argv[1].rstrip("/") + "/"
L = root + "src/drivers/gpu/i915/parity/lcd/"
NL = chr(10)
TAB = chr(9)

p = L + "lcd_compat.h"
s = open(p).read()
s = s.replace(TAB + "struct intel_power_domain_mask enabled_power_domains; struct drm_crtc base;",
              TAB + "struct drm_crtc base;", 1)
if "sink_format" not in s:
    s = s.replace(TAB + "bool has_hdmi_sink, hdmi_scrambling, hdmi_high_tmds_clock_ratio;",
                  TAB + "bool has_hdmi_sink, hdmi_scrambling, hdmi_high_tmds_clock_ratio;" + NL +
                  TAB + "int sink_format;                /* enum intel_output_format the readout found */", 1)
if "} dbuf;" in s and "struct intel_dbuf_state *obj" not in s:
    s = s.replace(TAB * 2 + "struct { u32 fw_mask; } dmc;",
                  TAB * 2 + "struct { u32 fw_mask; } dmc;" + NL +
                  TAB * 2 + "/* the device's DBUF object the watermark readout fills (skl_wm_get_hw_state) */" + NL +
                  TAB * 2 + "struct { struct intel_dbuf_state *obj; u8 enabled_slices; } dbuf;", 1)
open(p, "w").write(s)

p = L + "n1_compat.h"
s = open(p).read()
s = s.replace("void parity_n1_power_get_in_set_if_enabled(struct intel_display_power_domain_set *set,"
              + NL + TAB + "enum intel_display_power_domain domain);",
              "void parity_n1_power_get_in_set_if_enabled(struct intel_display_power_domain_set *set," + NL +
              TAB + "enum intel_display_power_domain domain);" + NL +
              "/* the readout's own mask lives inside the set (intel_display_power.h) */" + NL +
              "#define parity_n1_set_of(m) ((struct intel_display_power_domain_set *)(void *)(m))")
if "intel_display_power_put_mask_in_set(i915, &crtc->enabled_power_domains" not in s:
    s = s.replace("#endif /* PARITY_N1_COMPAT_H */",
                  "/* the readout hands the crtc's own set; the helpers of lcd_modeset_compat.h take that set */" + NL +
                  "#define intel_display_power_put_all_in_set_mask(i915, set, mask) " +
                  "intel_display_power_put_mask_in_set((i915), (set), (mask))" + NL + NL +
                  "#endif /* PARITY_N1_COMPAT_H */")
open(p, "w").write(s)
print("done")
