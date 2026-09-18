#!/usr/bin/env python3
"""WS031 E-114 round 5.  usage: round5.py <repo root>"""
import sys, json
NL, TAB = chr(10), chr(9)
root = sys.argv[1].rstrip("/") + "/"
L = "src/drivers/gpu/i915/parity/lcd/"
G = "plan/ws031/handover/tools/port_lcd_calc.py"
def load(p): return open(root + p).read()
def save(p, s):
    open(root + p, "w").write(s); print("patched", p)
def rep(s, old, new):
    assert s.count(old) == 1, old[:90]
    return s.replace(old, new)

c = load(L + "lcd_compat.h")
c = rep(c, "	u8 (*preemph_max)(struct intel_dp *intel_dp, u8 voltage_swing);", "	u8 (*preemph_max)(struct intel_dp *intel_dp);")
c = rep(c, "	struct { bool active; } lspcon;", "	struct intel_lspcon { bool active; } lspcon;")
c = rep(c, "struct drm_connector_state { enum drm_colorspace colorspace; void *connector; };",
        "struct drm_connector_state { enum drm_colorspace colorspace; void *connector; void *best_encoder; };")
c = rep(c, "		struct { bool override_afc_startup; u8 override_afc_startup_val; } vbt;",
        "		struct { bool override_afc_startup; u8 override_afc_startup_val; } vbt;" + NL +
        "		struct { bool ignore_long_hpd; } hotplug;" + NL +
        "		struct { struct { int which; } lock; } backlight;")
save(L + "lcd_compat.h", c)

q = load(L + "lcd_seq_compat.h")
q = rep(q, "#endif /* PARITY_LCD_SEQ_COMPAT_H */",
        "/* the PWM side of the backlight (intel_backlight.c) is not ported yet; the PPS side runs for real */" + NL +
        "#define intel_backlight_enable(cs, conn) PARITY_LCD_STEP(parity_lcd_cur_i915, \"intel_backlight_enable\")" + NL +
        "#define intel_backlight_disable(conn) PARITY_LCD_STEP(parity_lcd_cur_i915, \"intel_backlight_disable\")" + NL + NL +
        "#endif /* PARITY_LCD_SEQ_COMPAT_H */")
save(L + "lcd_seq_compat.h", q)

g = load(G)
g = rep(g, """'#include "lcd_dp_msa.h"' + NL + '#include "lcd_seq_compat.h"' + NL + '#include "lcd_modeset_compat.h"' + NL + NL + body""",
        """'#include "lcd_dp_msa.h"' + NL + '#include "lcd_seq_compat.h"' + NL + '#include "lcd_modeset_compat.h"' + NL + '#include "lcd_dp_compat.h"' + NL + NL + body""")
save(G, g)
print("done")
