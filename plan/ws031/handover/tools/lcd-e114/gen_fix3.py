#!/usr/bin/env python3
"""WS031 E-114: extra() also takes textual ranges (tables) listed under "extra_ranges".  usage: gen_fix3.py <port_lcd_calc.py>"""
import sys
p = sys.argv[1]; s = open(p).read()
old = '''    return "".join(func(text, n, src) + NL for n in SPEC["extra"].get(srcname, []))'''
assert s.count(old) == 1
s = s.replace(old, '''    ranges = "".join(between(text, r[0], r[1], src, r[2]) + NL for r in SPEC.get("extra_ranges", {}).get(srcname, []))
    return ranges + "".join(func(text, n, src) + NL for n in SPEC["extra"].get(srcname, []))''')
open(p, "w").write(s)
print("ok")
