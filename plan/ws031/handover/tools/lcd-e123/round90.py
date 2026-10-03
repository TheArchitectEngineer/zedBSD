#!/usr/bin/env python3
"""WS031 E-123 round 90: DUAL (-DPARITY_DUAL_TEST=1) -- the panel and the external display at once, each with its own
picture, then the external one stops while the panel keeps running, then the panel stops.

The two screens are two modeset objects (parity_lcd_modeset_select); the device's parts are shared: the DPLL pool
picks a PLL per screen by the reference's rule, and the DBUF / MBUS state is computed for BOTH pipes (each screen's
configuration names the other's pipe in also_active_pipes), which is the state the reference's atomic check would
produce for this configuration.  ADAPTATION, recorded: the two crtcs are committed one after the other instead of in
one atomic commit, and the pictures are the CPU patterns of the LCD tests.
Idempotent.  usage: round90.py <repo root>"""
import sys
root = sys.argv[1].rstrip("/") + "/"
P = root + "src/drivers/gpu/i915/parity/"
L = P + "lcd/"
NL = chr(10)
TAB = chr(9)
BS = chr(92)

body = open(root + "plan/ws031/handover/tools/lcd-e123/dual_run.c").read()

p = L + "parity_lcd_kernel.c"
s = open(p).read()
if "parity_lcd_kernel_dual_run" not in s:
    anchor = "/* ---------------- LCD reuse (-DPARITY_LCDR_TEST=1) ---------------- */"
    assert s.count(anchor) == 1
    s = s.replace(anchor, body + NL + anchor)
    open(p, "w").write(s)

h = open(L + "parity_lcd_kernel.h").read()
if "parity_lcd_kernel_dual_run" not in h:
    anchor = "int parity_lcd_kernel_hdmib_run(const struct parity_lcd_kernel_deps *d);"
    assert h.count(anchor) == 1
    h = h.replace(anchor, anchor + NL +
        "/* DUAL (-DPARITY_DUAL_TEST=1): the panel and the external HDMI display at once, each with its own picture */" + NL +
        "#ifndef PARITY_DUAL_WINDOW_MS" + NL + "#define PARITY_DUAL_WINDOW_MS   20000u" + NL + "#endif" + NL +
        "int parity_lcd_kernel_dual_run(const struct parity_lcd_kernel_deps *d);")
    open(L + "parity_lcd_kernel.h", "w").write(h)

b = open(P + "bios.h").read()
if "PARITY_DUAL_TEST 0" not in b:
    b = b.replace("#ifndef PARITY_HDMI_B_TEST" + NL,
        "#ifndef PARITY_DUAL_TEST" + NL +
        "#define PARITY_DUAL_TEST 0            /* E-123: the panel and the external HDMI display at once */" + NL +
        "#endif" + NL + "#ifndef PARITY_HDMI_B_TEST" + NL, 1)
    b = b.replace("#define PARITY_VBT_EXPLICIT (PARITY_HDMI_B_TEST || ",
                  "#define PARITY_VBT_EXPLICIT (PARITY_DUAL_TEST || PARITY_HDMI_B_TEST || ", 1)
    open(P + "bios.h", "w").write(b)

c = open(P + "probe.c").read()
if "parity_lcd_kernel_dual_run" not in c:
    old = "PARITY_LCDO_TEST || PARITY_HDMI_B_TEST) {"
    assert c.count(old) == 1
    c = c.replace(old, "PARITY_LCDO_TEST || PARITY_HDMI_B_TEST || PARITY_DUAL_TEST) {")
    old2 = TAB * 3 + "} else if (PARITY_HDMI_B_TEST) {"
    assert c.count(old2) == 1
    c = c.replace(old2, TAB * 3 + "} else if (PARITY_DUAL_TEST) {" + NL +
                  TAB * 4 + "(void)parity_lcd_kernel_dual_run(&lcdb);" + NL + old2)
    open(P + "probe.c", "w").write(c)
print("done")
