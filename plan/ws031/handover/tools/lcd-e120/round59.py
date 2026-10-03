#!/usr/bin/env python3
"""WS031 E-120 round 59 (native test image): the N0 record is printed once more as the runner's last output, so it is
still on the screen (and at the end of the ring) after teardown and the self-tests.  usage: round59.py <repo root>"""
import sys
root = sys.argv[1].rstrip("/") + "/"
P = root + "src/drivers/gpu/i915/parity/"
def rep(s, old, new):
    assert s.count(old) == 1, old[:80]
    return s.replace(old, new)
n = open(P + "native_precheck.c").read()
if "parity_native_log_again" not in n:
    n = rep(n, "void\nparity_native_log(const struct parity_native_report *r)\n{",
            "static const struct parity_native_report *n0_last;\n\n"
            "/* the last record again (the runner's final output), or nothing when N0 did not run */\n"
            "void\nparity_native_log_again(void)\n{\n\tif (n0_last == 0)\n\t\treturn;\n"
            "\tkern_logf(\"i915: parity N0 ---- summary (repeated at the end of the run) ----\n\");\n"
            "\tparity_native_log(n0_last);\n}\n\n"
            "void\nparity_native_log(const struct parity_native_report *r)\n{")
    n = rep(n, "\tfor (i = 0u; i < 8u; i++) {\n\t\tstatic const char hx[]", "\tn0_last = r;\n\tfor (i = 0u; i < 8u; i++) {\n\t\tstatic const char hx[]")
    open(P + "native_precheck.c", "w").write(n)
h = open(P + "native_precheck.h").read()
if "parity_native_log_again" not in h:
    h = rep(h, "void parity_native_log(const struct parity_native_report *r);",
            "void parity_native_log(const struct parity_native_report *r);\n/* the last logged record once more (end of the run: it stays on the screen) */\nvoid parity_native_log_again(void);")
    open(P + "native_precheck.h", "w").write(h)
r = open(P + "runner.c").read()
if "parity_native_log_again" not in r:
    r = rep(r, '#include "runner.h"\n', '#include "runner.h"\n#include "native_precheck.h"\n')
    r = rep(r, '\tkern_logf("i915: parity runner thread end\n");\n}', '\tparity_native_log_again();\n\tkern_logf("i915: parity runner thread end\n");\n}')
    open(P + "runner.c", "w").write(r)
print("done")
