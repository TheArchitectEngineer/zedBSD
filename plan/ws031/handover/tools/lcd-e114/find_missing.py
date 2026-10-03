#!/usr/bin/env python3
"""WS031 E-114 helper: compile the generated LCD files (syntax only), collect the identifiers the compiler does not
know, add those that are MACROS of a listed reference header to the roots in port_lcd_modeset.json, print the rest.
usage: find_missing.py <repo root>   (run the generator again afterwards; repeat until nothing is added)"""
import json, os, re, subprocess, sys
root = sys.argv[1].rstrip("/")
L = root + "/src/drivers/gpu/i915/parity/lcd"
T = root + "/plan/ws031/handover/tools/port_lcd_modeset.json"
R = root + "/plan/ws031/linux-parity/linux-reference"
spec = json.load(open(T))
files = [f for f in sorted(os.listdir(L)) if f.endswith("_port.c")]
missing = {}
for f in files:
    r = subprocess.run(["cc", "-std=gnu11", "-fsyntax-only", "-Wno-unused", "-ferror-limit=0" if False else "-fmax-errors=0", "-I" + L, L + "/" + f],
                       capture_output=True, text=True)
    for m in re.finditer(r"error: .(\w+). undeclared|implicit declaration of function .(\w+).|error: unknown type name .(\w+).", r.stderr):
        n = m.group(1) or m.group(2) or m.group(3)
        missing.setdefault(n, set()).add(f)
    other = [l for l in r.stderr.split("\n") if "error:" in l and "undeclared" not in l and "implicit declaration" not in l and "unknown type name" not in l]
    for l in other[:6]:
        print("OTHER", l[len(L) + 1:][:230])
added = 0
for h in spec["macro_headers"]:
    base = R + ("/ubu-i915-src/" if h["dir"] == "i915" else "/drm-v6.8.12/")
    text = open(base + h["source"]).read()
    names = set(re.findall(r"(?m)^#[ \t]*define[ \t]+([A-Za-z_]\w*)", text))
    for n in sorted(missing):
        if n in names and n not in h["roots"] and n not in h.get("exclude", []):
            h["roots"].append(n); added += 1
            missing[n] = None
    h["roots"].sort()
json.dump(spec, open(T, "w"), indent=1, sort_keys=True)
print("roots added:", added)
import glob
srcs = {}
for f in glob.glob(R + "/ubu-i915-src/display/*.[ch]") + glob.glob(R + "/ubu-i915-src/*.h") + glob.glob(R + "/drm-v6.8.12/*.[ch]"):
    srcs[f] = open(f, errors="replace").read()
for n in sorted(k for k, v in missing.items() if v):
    homes = []
    for f, t in srcs.items():
        for m in re.finditer(r"(?m)^(?:[A-Za-z_][^;(){}]*?[ *])?" + re.escape(n) + r"\(", t):
            b, sc = t.find("{", m.end()), t.find(";", m.end())
            if b != -1 and (sc == -1 or b < sc):
                homes.append(os.path.basename(f)); break
        if re.search(r"(?m)^#[ 	]*define[ 	]+" + re.escape(n) + r"", t):
            homes.append("macro:" + os.path.basename(f))
        if re.search(r"(?m)^\s+" + re.escape(n) + r"[^(]*[,=]", t) and "enum" in t:
            pass
    print("MISSING %-42s %-28s <- %s" % (n, ",".join(sorted(missing[n]))[:28], ",".join(sorted(set(homes)))[:60]))
