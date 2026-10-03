#!/usr/bin/env python3
"""WS031 E-123 round 85: HDMI-B reads the sink's EDID again while the port is enabled.
The DDC of this HDMI port does not answer while the port is off (the same happens with Linux on this machine); a
retimer / level shifter that only powers up with the TMDS output would explain it.  The picture is up for the whole
window, so the window hook asks the hotplug path to detect the connector again and logs what the EDID read did.
Idempotent.  usage: round85.py <repo root>"""
import sys
root = sys.argv[1].rstrip("/") + "/"
L = root + "src/drivers/gpu/i915/parity/lcd/"
NL = chr(10)
TAB = chr(9)

p = L + "parity_lcd_kernel.c"
s = open(p).read()
if "hdmib_window" not in s:
    anchor = "int parity_lcd_kernel_hdmib_run(const struct parity_lcd_kernel_deps *d)" + NL + "{" + NL
    assert s.count(anchor) == 1
    hook = ("/*" + NL +
            " * While the picture is up: read the sink's EDID once more.  With the port off this DDC answers nothing (the" + NL +
            " * reference behaves the same on this machine), so the question is whether driving the TMDS output brings the" + NL +
            " * sink's DDC up.  Reading it does not change the run's verdict: the value is logged either way." + NL +
            " */" + NL +
            "static int hdmib_window(void *ctx, struct parity_lcd_observer *o)" + NL +
            "{" + NL +
            TAB + "struct parity_hpd_summary hs;" + NL +
            TAB + "struct parity_hpd_edid_info ei;" + NL +
            TAB + "int st;" + NL + NL +
            TAB + "(void)ctx; (void)o;" + NL +
            TAB + "parity_hpd_summary(&hs);" + NL +
            TAB + "if (!hs.started || hs.hdmi_connector < 0) {" + NL +
            TAB * 2 + 'kern_logf("i915: parity HDMI-B EDID while lit: the hotplug path is not running\\n");' + NL +
            TAB * 2 + "return 0;" + NL +
            TAB + "}" + NL +
            TAB + "st = parity_hpd_probe_connector((unsigned)hs.hdmi_connector);" + NL +
            TAB + "parity_hpd_edid_info(&ei);" + NL +
            TAB + 'kern_logf("i915: parity HDMI-B EDID while lit: status %d (1 connected, 2 disconnected) | reads %u fails %u '
            'rc %d | %s product 0x%04x EDID %u.%u %s | DTD1 %ux%u %u kHz\\n", st, ei.reads, ei.fails, ei.rc,' + NL +
            TAB * 2 + "ei.mfg, ei.product, ei.version, ei.revision, ei.digital ? \"digital\" : \"analog\", ei.hactive," + NL +
            TAB * 2 + "ei.vactive, ei.pixel_clock_khz);" + NL +
            TAB + "return 0;" + NL +
            "}" + NL + NL)
    s = s.replace(anchor, hook + anchor)
    old = TAB + "p.window_ms = PARITY_HDMIB_WINDOW_MS;"
    assert s.count(old) == 1
    s = s.replace(old, old + NL + TAB + "p.in_window = hdmib_window;")
    if '#include "parity_hotplug.h"' not in s:
        inc = '#include "parity_lcd_kernel.h"' + NL
        assert s.count(inc) == 1
        s = s.replace(inc, inc + '#include "parity_hotplug.h"' + NL)
    open(p, "w").write(s)
print("done")
