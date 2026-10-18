#!/usr/bin/env python3
"""Puts the delimiters of every multi-line comment on lines of their own.

usage: comment-fix.py FILE...

Only comments of the form

    /* first line
     * more lines ... */

are rewritten; every other comment is left alone.
"""
import re
import sys

for path in sys.argv[1:]:
    with open(path, encoding='utf-8') as handle:
        lines = handle.read().split('\n')
    output = []
    index = 0
    changed = 0
    while index < len(lines):
        line = lines[index]
        match = re.match(r'^(\s*)/\*\s*(\S.*)$', line)
        if match is None or '*/' in line:
            output.append(line)
            index += 1
            continue
        indent = match.group(1)
        body = [match.group(2).rstrip()]
        end = index + 1
        while end < len(lines) and '*/' not in lines[end]:
            end += 1
        if end >= len(lines):
            output.append(line)
            index += 1
            continue
        for middle in lines[index + 1:end + 1]:
            text = middle.strip()
            if text.endswith('*/'):
                text = text[:-2].rstrip()
            if text.startswith('*'):
                text = text[1:].strip()
            if text != '':
                body.append(text)
        output.append(indent + '/*')
        for text in body:
            output.append(indent + ' * ' + text)
        output.append(indent + ' */')
        changed += 1
        index = end + 1
    with open(path, 'w', encoding='utf-8') as handle:
        handle.write('\n'.join(output))
    print('%s: %d comments' % (path, changed))
