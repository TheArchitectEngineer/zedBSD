#!/usr/bin/env python3
"""WS031 E-123 round 91: with two screens, "the current screen" must be rebound at every entry point.
The generated text reaches its objects through file-scope pointers that prepare() bound (the encoder of the DDI
callers, the device of the display / watermark code).  With one screen that was enough; with two, the screen that
prepared last owned them -- so the first screen's disable ran the encoder hooks against the other screen's digital
port, and its DDI IO / AUX power references were never given back (seen on the hardware: pipe and PLL off, wakerefs
still held).  parity_lcd_modeset_select() and every entry point now bind the selected object first.
Idempotent.  usage: round91.py <repo root>"""
import sys
root = sys.argv[1].rstrip("/") + "/"
L = root + "src/drivers/gpu/i915/parity/lcd/"
NL = chr(10)
TAB = chr(9)

p = L + "parity_lcd_modeset.c"
s = open(p).read()
if "static void bind_current(void)" not in s:
    anchor = ("/* the screen the following calls work on; every screen keeps its own state meanwhile */" + NL +
              "int parity_lcd_modeset_select(unsigned screen)")
    assert s.count(anchor) == 1
    bind = ("/*" + NL +
            " * The generated files reach their objects through file-scope pointers (the DDI callers' encoder, the" + NL +
            " * display's device, the watermark state).  They belong to the SELECTED screen: bind them whenever the" + NL +
            " * selection may have moved, which is at every entry point." + NL +
            " */" + NL +
            "static void bind_current(void)" + NL +
            "{" + NL +
            TAB + "if (!ms.prepared)" + NL +
            TAB * 2 + "return;" + NL +
            TAB + "parity_lcd_cur_i915 = &ms.i915;" + NL +
            TAB + "parity_lcd_wm = &ms.wm;" + NL +
            TAB + "parity_lcd_ms_bind_encoder(&ms);" + NL +
            TAB + "parity_lcd_error_bind(on_error, 0);" + NL +
            "}" + NL + NL)
    s = s.replace(anchor, bind + anchor)
    s = s.replace("	ms_sel = screen;" + NL + "	return 0;" + NL + "}",
                  "	ms_sel = screen;" + NL + "	bind_current();" + NL + "	return 0;" + NL + "}", 1)
    # every entry point binds the selected screen first
    for fn in ["int parity_lcd_modeset_enable(void)" + NL + "{" + NL + "	struct parity_lcd_modeset_status st;" + NL + NL,
               "int parity_lcd_modeset_plane_update(void)" + NL + "{" + NL,
               "int parity_lcd_modeset_plane_disable(void)" + NL + "{" + NL,
               "int parity_lcd_modeset_disable(void)" + NL + "{" + NL + "	unsigned before = ms_errors;" + NL + NL,
               "int parity_lcd_modeset_commit_enable(void)" + NL + "{" + NL +
               "	struct intel_power_domain_mask put_domains;" + NL + "	int rc, prc = PARITY_LCD_MS_OK;" + NL + NL,
               "int parity_lcd_modeset_commit_disable(void)" + NL + "{" + NL +
               "	static struct intel_crtc_state off_state;       /* the NEW state of this commit: the crtc inactive */" + NL +
               "	struct intel_power_domain_mask put_domains;" + NL + "	unsigned before;" + NL + "	int rc, prc;" + NL + NL]:
        assert s.count(fn) == 1, fn[:60]
        s = s.replace(fn, fn + TAB + "bind_current();" + NL)
    open(p, "w").write(s)
print("done")
