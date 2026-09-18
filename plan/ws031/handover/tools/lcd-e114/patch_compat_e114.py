#!/usr/bin/env python3
"""WS031 E-114: lcd_compat.h takes the ops from parity_lcd_ops.h; errors go to the error hook; the generated
files include lcd_modeset_compat.h; the steps that now have bodies leave lcd_seq_compat.h.
usage: patch_compat_e114.py <repo root>"""
import sys, re
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
a = c.index("/* register writes: a word list in the tests / the state calculation, the hardware later */")
b = c.index("typedef struct { u32 reg; } i915_reg_t;")
c = c[:a] + '#include "parity_lcd_ops.h"      /* zedBSD: registers, waits, time, DPCD, panel power, display power, locks, errors */' + NL + c[b:]
c = rep(c, "#define drm_dbg_kms(dev, fmt, ...) do { if (0) (void)parity_lcd_fmtcheck(fmt, ##__VA_ARGS__); parity_lcd_note(fmt); } while (0)",
        "#define drm_dbg_kms(dev, fmt, ...) do { if (0) (void)parity_lcd_fmtcheck(fmt, ##__VA_ARGS__); parity_lcd_note(fmt); } while (0)" + NL +
        "/* drm_err / WARN: counted, and handed to the device's error hook (the first one explains a failed run) */" + NL +
        "void parity_lcd_error(const char *what);" + NL +
        "#define drm_err(dev, fmt, ...) do { if (0) (void)parity_lcd_fmtcheck(fmt, ##__VA_ARGS__); parity_lcd_error(fmt); } while (0)" + NL +
        "#define drm_WARN(dev, cond, fmt, ...) ({ int _w = !!(cond); if (_w) parity_lcd_error(fmt); _w; })" + NL +
        "#define drm_WARN_ON_ONCE(dev, cond) drm_WARN_ON(dev, cond)" + NL)
c = rep(c, '#define drm_WARN_ON(dev, cond) ({ int _w = !!(cond); if (_w) parity_lcd_note("WARN_ON(" #cond ")\\n"); _w; })',
        '#define drm_WARN_ON(dev, cond) ({ int _w = !!(cond); if (_w) parity_lcd_error("WARN_ON(" #cond ")\\n"); _w; })')
c = rep(c, "#define intel_de_rmw(i915, r, clear, set) (i915)->emit->rmw32((i915)->emit->ctx, (r).reg, (clear), (set))",
        "#define intel_de_rmw(i915, r, clear, set) ((i915)->emit->rmw32((i915)->emit->ctx, (r).reg, (clear), (set)))")
save(L + "lcd_compat.h", c)

k = load(L + "parity_lcd_calc.c")
k = rep(k, "static void record_rmw(void *ctx, u32 reg, u32 clear, u32 set)" + NL + "{", "static u32 record_rmw(void *ctx, u32 reg, u32 clear, u32 set)" + NL + "{")
# the two returns inside record_rmw
m = re.search(r"static u32 record_rmw\(.*?\n}\n", k, re.S)
body = m.group(0).replace("\t\treturn;\n", "\t\treturn 0u;\n").replace("\tout->n++;\n}\n", "\tout->n++;\n\treturn 0u;\n}\n")
k = k[:m.start()] + body + k[m.end():]
k = rep(k, "void parity_lcd_note(const char *fmt)", "static unsigned lcd_errors;" + NL + "static void (*lcd_error_hook)(void *ctx, const char *what);" + NL +
        "static void *lcd_error_ctx;" + NL + NL +
        "void parity_lcd_error(const char *what)" + NL + "{" + NL + TAB + "lcd_errors++;" + NL +
        TAB + "if (lcd_error_hook != 0)" + NL + TAB + TAB + "lcd_error_hook(lcd_error_ctx, what);" + NL + "}" + NL + NL +
        "unsigned parity_lcd_errors(void)" + NL + "{" + NL + TAB + "return lcd_errors;" + NL + "}" + NL + NL +
        "void parity_lcd_error_bind(void (*hook)(void *ctx, const char *what), void *ctx)" + NL + "{" + NL +
        TAB + "lcd_error_hook = hook;" + NL + TAB + "lcd_error_ctx = ctx;" + NL + TAB + "lcd_errors = 0u;" + NL + "}" + NL + NL +
        "void parity_lcd_note(const char *fmt)")
save(L + "parity_lcd_calc.c", k)
h = load(L + "parity_lcd_calc.h")
h = rep(h, "/* how many plain writes to `reg` the list holds; the last one's value in *value */",
        "/* drm_err / WARN lines of the reference text since the last bind; the hook sees each one as it happens */" + NL +
        "unsigned parity_lcd_errors(void);" + NL +
        "void parity_lcd_error_bind(void (*hook)(void *ctx, const char *what), void *ctx);" + NL + NL +
        "/* how many plain writes to `reg` the list holds; the last one's value in *value */")
save(L + "parity_lcd_calc.h", h)

g = load(G)
g = g.replace("""'#include "lcd_seq_compat.h"' + NL + NL + body""", """'#include "lcd_seq_compat.h"' + NL + '#include "lcd_modeset_compat.h"' + NL + NL + body""")
assert g.count('lcd_modeset_compat.h') == 2
g = rep(g, """   '#include "lcd_compat.h"' + TAB + "/* zedBSD: replaces the linux/ and i915 includes */" + NL + NL + body + NL +
   '#include "parity_dpll_glue.inc"'""", """   '#include "lcd_compat.h"' + TAB + "/* zedBSD: replaces the linux/ and i915 includes */" + NL +
   '#include "lcd_modeset_compat.h"' + NL + NL + body + NL +
   '#include "parity_dpll_glue.inc"'""")
save(G, g)

q = load(L + "lcd_seq_compat.h")
gone = ["intel_enable_shared_dpll", "main_link_aux_power_domain_get", "intel_ddi_enable_clock", "intel_display_power_get",
        "intel_ddi_enable_transcoder_clock", "intel_ddi_power_up_lanes", "intel_ddi_mso_configure", "bdw_set_pipe_misc",
        "icl_set_pipe_chicken", "hsw_set_linetime_wm", "intel_enable_transcoder"]
for n in gone:
    lines = [l for l in q.split(NL) if l.startswith("#define " + n + "(")]
    assert len(lines) == 1, n
    q = q.replace(lines[0] + NL, "")
save(L + "lcd_seq_compat.h", q)
print("done")
