#!/usr/bin/env python3
"""Reports multi-line comments whose delimiters share a line with prose.

usage: comment-check.py FILE...
"""
import re
import sys

count = 0
for path in sys.argv[1:]:
    with open(path, encoding='utf-8', errors='replace') as handle:
        lines = handle.read().split('\n')
    inside = False
    for number, line in enumerate(lines, 1):
        text = line
        if not inside:
            position = text.find('/*')
            if position < 0:
                continue
            rest = text[position + 2:]
            if '*/' in rest:
                continue
            inside = True
            if rest.strip() != '' and rest.strip() != '*':
                print('%s:%d: opening line carries prose' % (path, number))
                count += 1
            continue
        if '*/' in text:
            inside = False
            before = text[:text.find('*/')].strip()
            if before not in ('', '*'):
                print('%s:%d: closing line carries prose' % (path, number))
                count += 1
print('findings=%d' % count)
