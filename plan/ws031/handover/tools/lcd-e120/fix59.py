#!/usr/bin/env python3
"""repair of round 59: the relog string and the runner hook (backslash written as chr(92))."""
import sys
root = sys.argv[1].rstrip("/") + "/"
P = root + "src/drivers/gpu/i915/parity/"
BS = chr(92)
NL = chr(10)
n = open(P + "native_precheck.c").read()
bad = "(repeated at the end of the run) ----" + NL + '");'
good = "(repeated at the end of the run) ----" + BS + 'n");'
if bad in n:
    n = n.replace(bad, good)
    open(P + "native_precheck.c", "w").write(n)
r = open(P + "runner.c").read()
if "parity_native_log_again" not in r:
    old = '#include "runner.h"' + NL
    assert r.count(old) == 1
    r = r.replace(old, old + '#include "native_precheck.h"' + NL)
    old = '\tkern_logf("i915: parity runner thread end' + BS + 'n");' + NL + '}'
    assert r.count(old) == 1, "runner end"
    r = r.replace(old, '\tparity_native_log_again();' + NL + old)
    open(P + "runner.c", "w").write(r)
print("repaired")
