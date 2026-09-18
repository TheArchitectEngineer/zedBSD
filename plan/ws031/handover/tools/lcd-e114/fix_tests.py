#!/usr/bin/env python3
"""WS031 E-114: the E-113 recorder-only enable-sequence checks leave lcd-host-test.c and edp_ktest.c (the order is
now checked on the integrated run); repair one printf of the modeset test.  usage: fix_tests.py <repo root>"""
import sys, re
NL, TAB = chr(10), chr(9)
root = sys.argv[1].rstrip("/") + "/"
def load(p): return open(root + p).read()
def save(p, s):
    open(root + p, "w").write(s); print("patched", p)

p = "plan/ws031/tests/lcd-modeset-host-test.c"
s = load(p)
a = s.index(TAB + 'printf("  B2: training-pattern writes=')
b = s.index(TAB + "CHECK(lcd.training_pattern_writes >= 2u")
s = s[:a] + TAB + 'printf("  B2: training-pattern writes=%u train_set=%02x %02x' + chr(92) + 'n", lcd.training_pattern_writes, s.train_set[0], s.train_set[1]);' + NL + s[b:]
save(p, s)

p = "plan/ws031/tests/lcd-host-test.c"
s = load(p)
a = s.index(TAB + "/* ---- the modeset enable sequence, driven by the reference's own callers ---- */")
b = s.index(TAB + 'printf("lcd_host_test: %u checks, %u failures')
s = s[:a] + TAB + "/* the modeset enable / disable sequence is exercised end to end by lcd-modeset-host-test.c */" + NL + NL + s[b:]
save(p, s)

p = "src/drivers/gpu/i915/parity/dp/edp_ktest.c"
s = load(p)
a = s.index(TAB * 4 + "static struct parity_lcd_words sq;")
a = s.rfind(TAB * 3 + "{" + NL, 0, a)
b = s.index("LCD-A-ENABLE-SEQ", a)
b = s.index(TAB * 3 + "}" + NL, b) + len(TAB * 3 + "}" + NL)
s = s[:a] + s[b:]
save(p, s)
