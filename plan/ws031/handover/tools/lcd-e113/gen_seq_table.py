#!/usr/bin/env python3
"""WS031 E-113: turn the enable-sequence list printed by the LCD host test into a table.
For every named step: where the reference defines the callee, and whether a function of that name already exists
under src/drivers/gpu/i915/parity (generated or hand-ported).  No judgement is made here about whether a step is
needed on the target -- that column is left to be filled from the reference text, one step at a time.
usage: gen_seq_table.py <host test output> <reference dir (ubu-i915-src)> <parity src dir> > table.md"""
import os, re, subprocess, sys
out, ref, par = sys.argv[1], sys.argv[2], sys.argv[3]
NL = chr(10)
rows = []
for l in open(out, errors="replace"):
    m = re.match(r"\s+seq\[\s*(\d+)\] (STEP|write|rmw)\s+(.*)$", l)
    if m:
        rows.append((int(m.group(1)), m.group(2), m.group(3).strip()))

def grep_def(name, root):
    pat = r"^[A-Za-z_][A-Za-z0-9_ \*]*\b" + re.escape(name) + r"\(|^" + re.escape(name) + r"\("
    r = subprocess.run(["grep", "-rlE", "--include=*.c", pat, root], capture_output=True, text=True)
    files = sorted(os.path.relpath(f, root) for f in r.stdout.split())
    return files

print("| # | 種別 | 内容 | 正本での定義 | parity 側に同名関数 |")
print("|---|---|---|---|---|")
for i, kind, text in rows:
    if kind != "STEP":
        print("| %d | %s | `%s` | （移植済み writer の出力） | — |" % (i, kind, text))
        continue
    name = text.lstrip("> ").split(" ")[0].split("(")[0]
    if name.startswith("encoder->"):
        name = "icl_combo_phy_set_signal_levels"
    d = grep_def(name, ref)
    q = grep_def(name, par)
    print("| %d | step | `%s` | %s | %s |" % (i, text, ", ".join("`" + x + "`" for x in d) or "（macro／inline）",
                                           ", ".join("`" + x + "`" for x in q) or "なし"))
