#!/usr/bin/env python3
"""WS031 E-123 round 84: an HDMI run is not judged by a DP link.  The show report carries the output kind; the
"the sink lost the link while the picture was up" check and the pass criterion's clock-recovery / equalisation
flags apply to DP only (HDMI has no link training -- its evidence is the frame counter and the stop).
Idempotent.  usage: round84.py <repo root>"""
import sys
root = sys.argv[1].rstrip("/") + "/"
L = root + "src/drivers/gpu/i915/parity/lcd/"
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

edit(L + "parity_lcd_show.h", [(
    "struct parity_lcd_show_report {" + NL + TAB + "int stage;",
    "struct parity_lcd_show_report {" + NL +
    TAB + "int output_hdmi;                        /* the run drove an HDMI sink: no DP link to judge it by */" + NL +
    TAB + "int stage;")], "int output_hdmi;")

edit(L + "parity_lcd_show.c", [
    (TAB * 2 + "parity_lcd_modeset_link_status(&r->at_window_end);" + NL +
     TAB * 2 + "if (r->steady_rc == 0 && (!r->at_window_end.cr_ok || !r->at_window_end.eq_ok))" + NL +
     TAB * 3 + 'anomaly(r, "the sink lost the link while the picture was up", -EIO);',
     TAB * 2 + "parity_lcd_modeset_link_status(&r->at_window_end);" + NL +
     TAB * 2 + "if (!r->output_hdmi && r->steady_rc == 0 && (!r->at_window_end.cr_ok || !r->at_window_end.eq_ok))" + NL +
     TAB * 3 + 'anomaly(r, "the sink lost the link while the picture was up", -EIO);'),
    (TAB + "return r->first_anomaly == 0 && r->window_hook_rc == 0 && released && r->enable_rc == PARITY_LCD_MS_OK && r->steady_rc == 0 &&" + NL +
     TAB * 2 + "r->at_enable.cr_ok && r->at_enable.eq_ok && r->obs.seen_steady == 0u && r->obs.vblank_unmasked_seen == 0 &&",
     TAB + "return r->first_anomaly == 0 && r->window_hook_rc == 0 && released && r->enable_rc == PARITY_LCD_MS_OK && r->steady_rc == 0 &&" + NL +
     TAB * 2 + "(r->output_hdmi || (r->at_enable.cr_ok && r->at_enable.eq_ok)) && r->obs.seen_steady == 0u &&" + NL +
     TAB * 2 + "r->obs.vblank_unmasked_seen == 0 &&"),
], "r->output_hdmi")

s = open(L + "parity_lcd_show.c").read()
if "r->output_hdmi = env->cfg.output_hdmi;" not in s:
    anchor = "static int show_begin(struct parity_lcd_show_env *env, struct parity_lcd_show_report *r)" + NL + "{" + NL
    assert s.count(anchor) == 1
    a2 = TAB + "memset(r, 0, sizeof(*r));" + NL
    assert s.count(a2) == 1
    s = s.replace(a2, a2 + TAB + "r->output_hdmi = env->cfg.output_hdmi;   /* after the report is cleared */" + NL)
    open(L + "parity_lcd_show.c", "w").write(s)
print("done")
