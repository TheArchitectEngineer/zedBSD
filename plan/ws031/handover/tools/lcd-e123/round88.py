#!/usr/bin/env python3
"""WS031 E-123 round 88: two screens need two modeset objects.
The file kept ONE object (`ms`) and its per-run bookkeeping; both become a small pool that a selector points at, so
the eDP panel and the HDMI sink each have their own crtc / encoder / plane / flip state while the device-wide things
(the DPLL pool, the locks the backend serialises, the power domains) stay shared.  Every entry point works on the
selected object; with one screen nothing changes (index 0 is the default).
Idempotent.  usage: round88.py <repo root>"""
import sys
root = sys.argv[1].rstrip("/") + "/"
L = root + "src/drivers/gpu/i915/parity/lcd/"
NL = chr(10)
TAB = chr(9)

p = L + "parity_lcd_modeset.c"
s = open(p).read()
if "parity_lcd_modeset_select" not in s:
    old = ("static struct parity_lcd_modeset ms;" + NL +
           "static struct parity_lcd_emit *ms_ops;" + NL +
           "static const char *ms_first_error;" + NL +
           "static unsigned ms_errors;" + NL +
           "static int ms_retained;                 /* outlives prepare's memset: only _discard_model() clears it */" + NL +
           "static const struct parity_lcd_emit *ms_retained_ops;")
    assert s.count(old) == 1
    new = ("/*" + NL +
           " * The screens this driver can drive at once: one object per screen (crtc, encoder, plane, flip state and" + NL +
           " * the run's bookkeeping), selected by parity_lcd_modeset_select().  What belongs to the DEVICE is not here:" + NL +
           " * the shared DPLLs are one pool (parity_dpll_glue.inc), the DBUF / MBUS state is the device's, and the" + NL +
           " * backend's locks and power domains are the same objects for every screen." + NL +
           " */" + NL +
           "#define PARITY_LCD_MS_SCREENS 2" + NL +
           "static struct parity_lcd_modeset ms_pool[PARITY_LCD_MS_SCREENS];" + NL +
           "static struct parity_lcd_emit *ms_ops_pool[PARITY_LCD_MS_SCREENS];" + NL +
           "static const char *ms_first_error_pool[PARITY_LCD_MS_SCREENS];" + NL +
           "static unsigned ms_errors_pool[PARITY_LCD_MS_SCREENS];" + NL +
           "static int ms_retained_pool[PARITY_LCD_MS_SCREENS];   /* outlives prepare's memset: only _discard_model() clears it */" + NL +
           "static const struct parity_lcd_emit *ms_retained_ops_pool[PARITY_LCD_MS_SCREENS];" + NL +
           "static unsigned ms_sel;                 /* the selected screen */" + NL + NL +
           "#define ms (ms_pool[ms_sel])" + NL +
           "#define ms_ops (ms_ops_pool[ms_sel])" + NL +
           "#define ms_first_error (ms_first_error_pool[ms_sel])" + NL +
           "#define ms_errors (ms_errors_pool[ms_sel])" + NL +
           "#define ms_retained (ms_retained_pool[ms_sel])" + NL +
           "#define ms_retained_ops (ms_retained_ops_pool[ms_sel])" + NL + NL +
           "/* the screen the following calls work on; every screen keeps its own state meanwhile */" + NL +
           "int parity_lcd_modeset_select(unsigned screen)" + NL +
           "{" + NL +
           TAB + "if (screen >= PARITY_LCD_MS_SCREENS)" + NL +
           TAB * 2 + "return -EINVAL;" + NL +
           TAB + "ms_sel = screen;" + NL +
           TAB + "return 0;" + NL +
           "}" + NL + NL +
           "unsigned parity_lcd_modeset_selected(void)" + NL +
           "{" + NL +
           TAB + "return ms_sel;" + NL +
           "}")
    s = s.replace(old, new)
    # the error hook and the glue entry points need the selected object; on_error already uses the macros
    open(p, "w").write(s)

h = open(L + "parity_lcd_modeset.h").read()
if "parity_lcd_modeset_select" not in h:
    anchor = "int parity_lcd_modeset_prepare("
    assert h.count(anchor) == 1
    h = h.replace(anchor,
        "/*" + NL +
        " * The screen the calls below work on (0 = the first).  Each screen has its own crtc, encoder, plane and flip" + NL +
        " * state; the device's shared parts (DPLLs, DBUF / MBUS, power domains, locks) are common to all of them." + NL +
        " */" + NL +
        "int parity_lcd_modeset_select(unsigned screen);" + NL +
        "unsigned parity_lcd_modeset_selected(void);" + NL + NL + anchor)
    open(L + "parity_lcd_modeset.h", "w").write(h)
print("done")
