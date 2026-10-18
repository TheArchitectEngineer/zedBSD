#!/usr/bin/env python3
# ws245: the source audit of the display 11 to 13 files (design.md section 5.4, guardrail "MIT・GPL の出典のデータの引用").
# For every non-empty file of src/drivers/gpu/i915/display/ (not the display 9 files gen9-*) and of the xd headers of
# src/drivers/gpu/i915/intel/:
#   - the file's description (the comment after the copyright header) names no source: no Linux, Mesa, Intel
#     copyright, MIT notice, "Derived" or "referenced:";
#   - no "Derived from the Linux kernel" and no copied license notice anywhere in the file;
#   - every "Value(s) referenced:" and "Procedure referenced:" stands in a multi-line comment, never in a one-line
#     /* ... */;
#   - a "referenced:" names its source (Linux v6.8.12 or v6.19 and a path, an Intel or VESA, CTA or NIST document, this
#     tree's headers).
#   python3 -I plan/ws245/tests/check-sources.py [TREE]   (TREE defaults to src/drivers/gpu/i915)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
import os
import re
import sys

HEAD_FORBIDDEN = re.compile(r'Linux|Mesa|Derived|derived from|referenced:|Copyright ©|'
                            r'Intel Corporation|SPDX-License-Identifier: MIT|Permission is hereby granted')
BODY_FORBIDDEN = re.compile(r'Derived from the Linux|Permission is hereby granted|Copyright ©|'
                            r'SPDX-License-Identifier: MIT|SPDX-License-Identifier: GPL')
ONE_LINE_REFERENCE = re.compile(r'/\*.*referenced:.*\*/')
REFERENCE = re.compile(r'(Values?|Procedure) referenced:\s*(.*)')
REFERENCE_SOURCE = re.compile(r'^(Linux v6\.8\.12|Linux v6\.19|Intel |the PRM|this tree|Mesa |VESA |CTA-861|NIST |the offsets are|$)')


def files(tree):
    display = os.path.join(tree, 'display')
    for name in sorted(os.listdir(display)):
        if name.startswith('gen9-') or not name.endswith(('.c', '.h')):
            continue
        yield os.path.join(display, name)
    intel = os.path.join(tree, 'intel')
    for name in sorted(os.listdir(intel)):
        if name.startswith(('display-regs-xd', 'display-dpcd-xd', 'display-tables-xd', 'xd-')):
            yield os.path.join(intel, name)


def description(text):
    """Returns the second comment of a file: the one after the copyright header."""
    comments = re.findall(r'/\*.*?\*/', text, re.S)
    if len(comments) < 2:
        return ''
    return comments[1]


def check(path):
    problems = []
    text = open(path, encoding='utf-8', errors='replace').read()
    if not text.strip():
        return problems
    head = description(text)
    found = HEAD_FORBIDDEN.search(head)
    if found:
        problems.append('the file description names a source (%s)' % found.group(0))
    for number, line in enumerate(text.splitlines(), 1):
        if BODY_FORBIDDEN.search(line):
            problems.append('%d: a copied notice or derivation line' % number)
        if ONE_LINE_REFERENCE.search(line):
            problems.append('%d: a referenced: block on one line' % number)
        found = REFERENCE.search(line)
        if found and not REFERENCE_SOURCE.match(found.group(2)):
            problems.append('%d: a referenced: block without its version and path' % number)
    return problems


def main():
    tree = sys.argv[1] if len(sys.argv) > 1 else 'src/drivers/gpu/i915'
    failed = 0
    count = 0
    for path in files(tree):
        count += 1
        for problem in check(path):
            print('check-sources: %s: %s' % (path, problem))
            failed = 1
    if failed:
        return 1
    print('check-sources: %d files, no source named at a file head, no copied notice' % count)
    return 0


if __name__ == '__main__':
    sys.exit(main())
