#!/usr/bin/env python3
"""WS031 E-124 round 100: the timing helpers the readout calls, and the bigjoiner ones it never needs here."""
import sys, json
root = sys.argv[1].rstrip("/") + "/"
L = root + "src/drivers/gpu/i915/parity/lcd/"
NL = chr(10)
spec = root + "plan/ws031/handover/tools/port_lcd_modeset.json"
j = json.load(open(spec))
have = j["extra"]["intel_display.c"]
for n in ["intel_mode_from_crtc_timings", "intel_splitter_adjust_timings"]:
    if n not in have:
        have.append(n)
j["extra"]["intel_display.c"] = have
open(spec, "w").write(json.dumps(j, indent=1) + NL)
p = L + "n1_compat.h"
s = open(p).read()
if "intel_bigjoiner_num_pipes" not in s:
    s = s.replace("#endif /* PARITY_N1_COMPAT_H */",
        "/* bigjoiner: one pipe per stream on this display */" + NL +
        "#define intel_bigjoiner_num_pipes(cs) (1)" + NL +
        "#define intel_bigjoiner_adjust_timings(cs, mode) ((void)0)" + NL +
        "#define drm_mode_copy(dst, src) (*(dst) = *(src))" + NL + NL +
        "#endif /* PARITY_N1_COMPAT_H */")
    open(p, "w").write(s)
print("done")
