#!/usr/bin/env python3
"""WS031 E-115 round 9: backlight compat.  usage: round9.py <repo root>"""
import sys, json
NL, TAB = chr(10), chr(9)
root = sys.argv[1].rstrip("/") + "/"
L = "src/drivers/gpu/i915/parity/lcd/"
J = "plan/ws031/handover/tools/port_lcd_modeset.json"
def load(p): return open(root + p).read()
def save(p, s):
    open(root + p, "w").write(s); print("patched", p)
def rep(s, old, new):
    assert s.count(old) == 1, old[:90]
    return s.replace(old, new)

c = load(L + "lcd_compat.h")
c = rep(c, "		void *device;               /* no backlight class device here: always NULL */",
        "		struct { struct { int brightness, max_brightness, power; } props; } *device;   /* no backlight class device here: always NULL */")
c = rep(c, "		struct { u32 rawclk_freq; } runtime;", "		struct { u32 rawclk_freq; } runtime;" + NL +
        "		struct { int invert_brightness; } params;   /* module parameter: 0 (default) */")
save(L + "lcd_compat.h", c)

m = load(L + "lcd_modeset_compat.h")
m = rep(m, "/* intel_quirks.c: QUIRK_INCREASE_DDI_DISABLED_TIME is set for PCI devices 0x3184 / 0x3185 only (not 0x46a8) */",
        "/* intel_quirks.c: no entry of intel_quirks[] names PCI device 0x46a8 (they are 0x0046 .. 0x3185), and the DMI\n * quirks name other machines -- so no quirk is set on the target */")
m = rep(m, "#define KHz(x) (1000 * (x))", """#define KHz(x) (1000 * (x))
#include "lcd_pch_enum.h"               /* reference, extracted: enum intel_pch */
#define INTEL_PCH_TYPE(i915) PCH_ADP    /* [fixed] the PCH the probe identified on the target (Alder Lake PCH) */
#undef ENODEV
#define ENODEV 19                       /* Linux numbering, returned negative */
#ifndef WARN_ON
#define WARN_ON(cond) ({ int _w = !!(cond); if (_w) parity_lcd_error("WARN_ON(" #cond ")\\n"); _w; })
#endif
#define clamp(val, lo, hi) ({ __typeof__(val) _v = (val); __typeof__(val) _l = (lo); __typeof__(val) _h = (hi); _v < _l ? _l : (_v > _h ? _h : _v); })
#define clamp_t(type, val, lo, hi) ({ type _v = (type)(val); type _l = (type)(lo); type _h = (type)(hi); _v < _l ? _l : (_v > _h ? _h : _v); })
#define DIV_ROUND_CLOSEST_ULL(x, d) ((u64)(((u64)(x) + ((u64)(d) / 2u)) / (u64)(d)))""")
save(L + "lcd_modeset_compat.h", m)
d = load(L + "lcd_dp_compat.h")
d = rep(d, "#define WARN_ON(cond) ({ int _w = !!(cond); if (_w) parity_lcd_error(\"WARN_ON(\" #cond \")\\n\"); _w; })" + NL, "")
save(L + "lcd_dp_compat.h", d)

j = json.load(open(root + J))
for f in j["new_files"]:
    if f["out"] == "intel_backlight_port.c":
        f["functions"].insert(f["functions"].index("cnp_backlight_controller_is_valid"), "cnp_num_backlight_controllers")
j["range_headers"].append({"out": "lcd_pch_enum.h", "dir": "i915", "source": "soc/intel_pch.h", "path": "drivers/gpu/drm/i915/soc/intel_pch.h",
                           "ranges": [["enum intel_pch {", NL + "};" + NL, "enum intel_pch"]]})
json.dump(j, open(root + J, "w"), indent=1, sort_keys=True)
print("spec updated")
