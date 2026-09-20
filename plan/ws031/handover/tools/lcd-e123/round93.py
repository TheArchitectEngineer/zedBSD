#!/usr/bin/env python3
"""WS031 E-123 round 93: one buffer, two screens.
A scanout buffer had ONE user: begin() marked it in use, end() gave it back.  Two pipes can read the same buffer, so
the buffer counts its users: it stays in use while any display reads it, and only the last end() lets it be unpinned
or destroyed.  The DUAL-SHARED test shows the same buffer on both screens -- the panel shows all of it, the external
display the top-left 1280x720 of the same rows (same address, same pitch): a PARTIAL view of a shared buffer, not a
scaled mirror (a scaler would be needed for that, and is not used here).
Idempotent.  usage: round93.py <repo root>"""
import sys
root = sys.argv[1].rstrip("/") + "/"
P = root + "src/drivers/gpu/i915/parity/"
L = P + "lcd/"
NL = chr(10)
TAB = chr(9)

def edit(path, pairs, marker):
    s = open(path).read()
    if marker in s:
        return
    for old, new in pairs:
        assert s.count(old) == 1, (path, old[:90])
        s = s.replace(old, new)
    open(path, "w").write(s)

edit(L + "scanout.h", [(
    TAB + "unsigned publishes;",
    TAB + "unsigned users;                 /* how many displays read it now (begin / end); IN_USE while > 0 */" + NL +
    TAB + "unsigned publishes;")], "unsigned users;")

edit(L + "scanout.c", [(
    "	if (so == 0 || so->state != PARITY_SCANOUT_PINNED)" + NL +
    "		return -EINVAL;" + NL +
    "	so->state = PARITY_SCANOUT_IN_USE;" + NL +
    "	return 0;",
    "	/* a second display may read the same buffer: the users are counted, the state is their union */" + NL +
    "	if (so == 0 || (so->state != PARITY_SCANOUT_PINNED && so->state != PARITY_SCANOUT_IN_USE))" + NL +
    "		return -EINVAL;" + NL +
    "	so->users++;" + NL +
    "	so->state = PARITY_SCANOUT_IN_USE;" + NL +
    "	return 0;"), (
    "	if (so != 0 && so->state == PARITY_SCANOUT_IN_USE)" + NL +
    "		so->state = PARITY_SCANOUT_PINNED;",
    "	/* the buffer is given back when the LAST display has let go of it */" + NL +
    "	if (so != 0 && so->state == PARITY_SCANOUT_IN_USE && so->users != 0u && --so->users == 0u)" + NL +
    "		so->state = PARITY_SCANOUT_PINNED;")], "so->users++;")

d = open(root + "plan/ws031/handover/tools/lcd-e123/dual_share.c").read()
p = L + "parity_lcd_kernel.c"
s = open(p).read()
if "parity_lcd_kernel_dual_share_run" not in s:
    anchor = "/* ---------------- LCD reuse (-DPARITY_LCDR_TEST=1) ---------------- */"
    assert s.count(anchor) == 1
    s = s.replace(anchor, d + NL + anchor)
    open(p, "w").write(s)

edit(L + "parity_lcd_kernel.h", [(
    "int parity_lcd_kernel_dual_run(const struct parity_lcd_kernel_deps *d);",
    "int parity_lcd_kernel_dual_run(const struct parity_lcd_kernel_deps *d);" + NL +
    "/* DUAL-SHARED (-DPARITY_DUAL_SHARE_TEST=1): ONE buffer on both screens (the external one sees part of it) */" + NL +
    "int parity_lcd_kernel_dual_share_run(const struct parity_lcd_kernel_deps *d);")], "parity_lcd_kernel_dual_share_run")

b = open(P + "bios.h").read()
if "PARITY_DUAL_SHARE_TEST 0" not in b:
    b = b.replace("#ifndef PARITY_DUAL_TEST" + NL,
        "#ifndef PARITY_DUAL_SHARE_TEST" + NL +
        "#define PARITY_DUAL_SHARE_TEST 0      /* E-123: one buffer, both screens (the external one sees part of it) */" + NL +
        "#endif" + NL + "#ifndef PARITY_DUAL_TEST" + NL, 1)
    b = b.replace("#define PARITY_VBT_EXPLICIT (PARITY_DUAL_TEST || ",
                  "#define PARITY_VBT_EXPLICIT (PARITY_DUAL_SHARE_TEST || PARITY_DUAL_TEST || ", 1)
    open(P + "bios.h", "w").write(b)

c = open(P + "probe.c").read()
if "parity_lcd_kernel_dual_share_run" not in c:
    old = "PARITY_HDMI_B_TEST || PARITY_DUAL_TEST) {"
    assert c.count(old) == 1
    c = c.replace(old, "PARITY_HDMI_B_TEST || PARITY_DUAL_TEST || PARITY_DUAL_SHARE_TEST) {")
    old2 = TAB * 3 + "} else if (PARITY_DUAL_TEST) {"
    assert c.count(old2) == 1
    c = c.replace(old2, TAB * 3 + "} else if (PARITY_DUAL_SHARE_TEST) {" + NL +
                  TAB * 4 + "(void)parity_lcd_kernel_dual_share_run(&lcdb);" + NL + old2)
    open(P + "probe.c", "w").write(c)
print("done")
