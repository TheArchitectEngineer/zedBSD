#!/usr/bin/env python3
"""WS031 E-120 round 60: -DPARITY_N0_FORCE_STOP=1 (VM test builds only) forces the N0 decision to STOP, so the early
teardown taken on the laptop (before P3.6) is exercised in the VM first.  usage: round60.py <repo root>"""
import sys
root = sys.argv[1].rstrip("/") + "/"
P = root + "src/drivers/gpu/i915/parity/"
NL = chr(10)
b = open(P + "bios.h").read()
if "PARITY_N0_FORCE_STOP" not in b:
    old = "#ifndef PARITY_LCDD_TEST"
    assert b.count(old) == 1
    b = b.replace(old, "#ifndef PARITY_N0_FORCE_STOP" + NL +
                  "#define PARITY_N0_FORCE_STOP 0        /* VM test of the N0 STOP path (the early teardown); never in a native image */" + NL +
                  "#endif" + NL + old)
    open(P + "bios.h", "w").write(b)
p = open(P + "probe.c").read()
if "PARITY_N0_FORCE_STOP" not in p:
    old = "		(void)parity_native_precheck(&nd, &n0);" + NL
    assert p.count(old) == 1
    p = p.replace(old, old + "		if (PARITY_N0_FORCE_STOP) {" + NL +
                  "			n0.proceed = 0;" + NL +
                  "			n0.reason = \"forced by the test build (PARITY_N0_FORCE_STOP): the early teardown is exercised\";" + NL +
                  "		}" + NL)
    open(P + "probe.c", "w").write(p)
print("done")
