#!/usr/bin/env python3
"""E-123 round 92 fixup: the teardown's log string was split across lines, and probe.c needs the declaration."""
import sys
root = sys.argv[1].rstrip("/") + "/"
p = root + "src/drivers/gpu/i915/parity/probe.c"
NL = chr(10)
TAB = chr(9)
BS = chr(92)
s = open(p).read()
bad = (TAB * 3 + 'kern_logf("i915: parity teardown: LAST-RESORT stopped %u display element(s) the driver had not' + NL +
       TAB * 4 + '" released' + BS + 'n", forced);')
good = (TAB * 3 + 'kern_logf("i915: parity teardown: LAST-RESORT stopped %u display element(s) the driver had not "' + NL +
        TAB * 4 + '"released' + BS + 'n", forced);')
if bad in s:
    s = s.replace(bad, good)
inc = '#include "lcd/parity_lcd_kernel.h"' + NL
assert s.count(inc) == 1
if 'lcd/parity_lcd_show.h' not in s:
    s = s.replace(inc, inc + '#include "lcd/parity_lcd_show.h"' + NL)
open(p, "w").write(s)
print("fixed")
