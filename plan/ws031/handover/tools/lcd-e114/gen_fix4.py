#!/usr/bin/env python3
"""WS031 E-114: replace protos() in port_lcd_calc.py (the earlier patch was damaged in transit).  usage: <port_lcd_calc.py>"""
import sys
p = sys.argv[1]; s = open(p).read()
a = s.index("def protos(body):")
b = s.index("def macro_closure(")
BS = chr(92)
new = '''def protos(body):
    """`body` with forward declarations of its static functions inserted before the first function definition
    (generated text, not reference text; types and tables the keep-list put first stay in front)"""
    sig = r"(?m)^static [^;{}=]*?BS([^;{}]*?BS)BSs*BSnBS{".replace("BS", chr(92))
    out = []
    for m in re.finditer(sig, body):
        head = body[m.start():m.end()].rsplit("{", 1)[0].rstrip()
        if " inline " not in head.split("(")[0] + " ":
            out.append(head + ";")
    if not out:
        return body
    anyfn = r"(?m)^(?:static |const |struct |enum |bool |int |void |u8 |u32 |i915_reg_t )[^;{}=]*?BS([^;{}]*?BS)BSs*BSnBS{".replace("BS", chr(92))
    first = re.search(anyfn, body)
    at = first.start() if first else 0
    pre = body[:at].rstrip(NL)
    if pre.endswith("*/"):                      # keep a directly preceding comment with its function
        c = pre.rfind("/*")
        if (NL + NL) not in body[c:at].rstrip(NL):
            at = c
    return (body[:at] + "/* zedBSD: forward declarations of the static functions of this file (generated; the keep-list is not in call order) */" + NL +
            NL.join(out) + NL + NL + body[at:])

'''
s = s[:a] + new + s[b:]
open(p, "w").write(s)
print("protos replaced")
