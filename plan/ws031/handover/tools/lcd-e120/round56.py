#!/usr/bin/env python3
"""WS031 E-120 round 56: the N0 decision as a pure function (native_decide.c) so every rule is host-tested.
usage: round56.py <repo root>"""
import sys
root = sys.argv[1].rstrip("/") + "/"
P = root + "src/drivers/gpu/i915/parity/"
c = open(P + "native_precheck.c").read()
a = c.index("	/* the driver writes GGTT pages [driver_ggtt_first, ggtt_pages): no firmware scanout may live there */")
b = c.index("	return r->proceed;", a)
body = c[a:b]
c = c[:a] + "	parity_native_decide(r);\n" + c[b:]
open(P + "native_precheck.c", "w").write(c)
body = body.replace("d->driver_ggtt_first", "r->driver_ggtt_first")
open(P + "native_decide.c", "w").write("""/*
 * WS031 Linux-parity -- the N0 decision (native_precheck.h), pure: it reads only the report.  zedBSD project code.
 */
#include "native_precheck.h"

void
parity_native_decide(struct parity_native_report *r)
{
	unsigned p;

	r->overlap = 0;
""" + body + "}\n")
h = open(P + "native_precheck.h").read()
h = h.replace("void parity_native_log(const struct parity_native_report *r);",
              "void parity_native_log(const struct parity_native_report *r);\n/* the decision alone, from the recorded facts (pure; host-tested) */\nvoid parity_native_decide(struct parity_native_report *r);", 1)
open(P + "native_precheck.h", "w").write(h)
mk = open(root + "platform/amd64/vmunix.mk").read()
if "native_decide.c" not in mk:
    mk = mk.replace("src/drivers/gpu/i915/parity/native_precheck.c", "src/drivers/gpu/i915/parity/native_precheck.c src/drivers/gpu/i915/parity/native_decide.c", 1)
    open(root + "platform/amd64/vmunix.mk", "w").write(mk)
print("done")
