#!/usr/bin/env python3
"""WS031 E-112: first_comment_end() -- a source that opens with a one-line SPDX comment keeps its copyright
comment too (intel_psr_regs.h lost its Copyright line in the extract).  usage: <path to port_lcd_calc.py>"""
import sys
p = sys.argv[1]
s = open(p).read()
old = 'def first_comment_end(text):\n    return text.index("*/") + 2\n'
assert s.count(old) == 1
s = s.replace(old, '''def first_comment_end(text):
    """end of the source's leading notice: the first comment, plus the next one when the first is only the SPDX line"""
    e = text.index("*/") + 2
    if text[:e].lstrip().startswith("/* SPDX-License-Identifier") and text[e:].lstrip().startswith("/*"):
        e = text.index("*/", e) + 2
    return e
''')
open(p, "w").write(s)
print("first_comment_end patched")
