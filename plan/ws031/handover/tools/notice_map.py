#!/usr/bin/env python3
"""WS031: which Linux i915 files does each parity/ file say it follows, and what notice do those
upstream files carry in the fixed reference tree?  Facts only; the input for restoring notices."""
import os, re, collections
root = os.path.expanduser("~/zedBSD")
par = os.path.join(root, "src/drivers/gpu/i915/parity")
ref = os.path.join(root, "plan/ws031/linux-parity/linux-reference/ubu-i915-src")

# index of upstream files by basename
up = collections.defaultdict(list)
for d, _, fs in os.walk(ref):
    for f in fs:
        if f.endswith((".c", ".h")):
            up[f].append(os.path.relpath(os.path.join(d, f), ref))

def upstream_notice(rel):
    head = open(os.path.join(ref, rel), errors="replace").read(2500)
    spdx = re.search(r"SPDX-License-Identifier:\s*([^\s*]+(?:\s+(?:OR|AND|WITH)\s+[^\s*]+)*)", head)
    cps = [c.strip(" */\t") for c in re.findall(r"Copyright[^\n]*", head)]
    mit_text = "Permission is hereby granted, free of charge" in head
    lic = spdx.group(1) if spdx else ("MIT (permission notice text, no SPDX tag)" if mit_text else "?")
    return lic, cps

rows = []
for d, _, fs in os.walk(par):
    for f in sorted(fs):
        if not f.endswith((".c", ".h", ".inc")):
            continue
        p = os.path.join(d, f)
        rel = os.path.relpath(p, root)
        s = open(p, errors="replace").read()
        names = collections.Counter(re.findall(r"\b((?:intel|i915|gen[0-9]+|skl|icl|vlv|hsw)_[a-z0-9_]+\.[ch])\b", s))
        # a header next to a .c usually holds the "reference mapping"
        found = [(n, c) for n, c in names.most_common() if n in up]
        strong = bool(re.search(r"(?i)\b(port(s|ed)? (of|from)|faithful port|direct port|transcri|re-derived from|generated from|verbatim)\b", s[:4000]))
        rows.append((rel, s.count("\n") + 1, found, strong))

print("# WS031 parity/ の出典対応表（自動抽出、事実のみ）\n")
print("各 parity ファイルが本文中で名指ししている Linux i915 のファイル（固定参照 tree に実在するもののみ）と、その上流ファイルが持つ表示。`port 文言` は冒頭 4000 字に port/transcribe/generated 等の語があるか。**名指し＝複製の証明ではない**（API 契約の参照だけの場合もある）。区分の確定は人が行う。\n")
print("| parity file | lines | port 文言 | 名指ししている上流ファイル（回数） |")
print("|---|---|---|---|")
allup = collections.Counter()
for rel, n, found, strong in sorted(rows):
    lst = ", ".join("`%s`×%d" % (up[a][0], c) for a, c in found[:6]) or "—"
    for a, c in found:
        allup[up[a][0]] += 1
    print("| `%s` | %d | %s | %s |" % (rel.replace("src/drivers/gpu/i915/", ""), n, "yes" if strong else "-", lst))
print("\n## 上流ファイルの表示（名指しされたもの）\n")
print("| 上流ファイル | 名指しする parity ファイル数 | license | copyright 行 |")
print("|---|---|---|---|")
for u, c in allup.most_common():
    lic, cps = upstream_notice(u)
    print("| `%s` | %d | %s | %s |" % (u, c, lic, "; ".join(cps[:3]) or "—"))
n_named = sum(1 for r in rows if r[2])
print("\n%d parity files; %d name at least one upstream file; %d carry port wording; %d distinct upstream files named." % (
    len(rows), n_named, sum(1 for r in rows if r[3]), len(allup)))
