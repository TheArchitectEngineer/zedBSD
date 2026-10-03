#!/usr/bin/env python3
"""WS031 E-114: generator fixes -- macro_closure takes an exclude list; forward declarations go right before the first
function (after the types / tables the keep-list put first).  usage: gen_fix2.py <port_lcd_calc.py>"""
import sys
p = sys.argv[1]; s = open(p).read()
def rep(old, new):
    global s
    assert s.count(old) == 1, old[:80]
    s = s.replace(old, new)
rep("def macro_closure(text, roots, src, label):", "def macro_closure(text, roots, src, label, exclude=()):")
rep("            if tok in defs and tok not in want:", "            if tok in defs and tok not in want and tok not in exclude:")
rep('macro_closure(stext, spec["roots"], spec["source"], spec["out"])', 'macro_closure(stext, spec["roots"], spec["source"], spec["out"], spec.get("exclude", []))')
rep('''    return ("/* zedBSD: forward declarations of the static functions below (generated; the keep-list is not in call order) */" + NL +
            NL.join(out) + NL + NL) if out else ""''', '''    if not out:
        return body
    first = re.search(r"(?m)^(?:/\*(?:[^*]|\*(?!/))*\*/\n)?(?:static |const |struct |enum |bool |int |void |u8 |u32 |i915_reg_t )[^;{}=]*?\([^;{}]*?\)\s*\n\{", body)
    at = first.start() if first else 0
    return (body[:at] + "/* zedBSD: forward declarations of the static functions of this file (generated; the keep-list is not in call order) */" + NL +
            NL.join(out) + NL + NL + body[at:])''')
s = s.replace("body = protos(body) + body", "body = protos(body)")
open(p, "w").write(s)
print("generator fixed")
