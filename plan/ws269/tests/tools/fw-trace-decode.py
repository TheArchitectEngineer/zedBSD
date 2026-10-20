#!/usr/bin/env python3
"""Decodes the PowerVR firmware trace words a zedBSD dmesg dump prints.

The dump's lines are "powervr: dump: trace N: w w w w w w w w" (and a
"... left out" line where the dump skipped words).  Each trace entry is
its id, a 64-bit timestamp, then its arguments; the id is the format's
number | group << 12 | argument count << 16 | 0x70000000, as Linux's
drivers/gpu/drm/imagination/pvr_rogue_fwif_sf.h (ROGUE_FW_LOG_CREATESFID
and the ROGUE_FW_GROUP enumeration) defines them.

Use: python3 -I fw-trace-decode.py <pvr_rogue_fwif_sf.h> <dmesg file>
Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
"""
import re
import sys


def main():
    header = open(sys.argv[1]).read()
    groups = {}
    for index, name in enumerate(re.findall(r'ROGUE_FW_GROUP_(\w+),', header.split('enum')[1].split('}')[0])):
        groups[name] = index
    formats = {}
    for match in re.finditer(r'ROGUE_FW_LOG_CREATESFID\((\d+),\s*ROGUE_FW_GROUP_(\w+),\s*(\d+)\),\s*"((?:[^"\\]|\\.)*)"', header):
        number, group, count, text = match.groups()
        formats[int(number) | groups[group] << 12 | int(count) << 16 | 0x70000000] = (group, text, int(count))
    words = []
    for line in open(sys.argv[2]):
        match = re.search(r'dump: trace\s+\d+: (.*)', line)
        if match:
            words += [int(word, 16) for word in match.group(1).split()]
        elif 'left out' in line:
            words.append(None)
    index = 0
    while index < len(words):
        word = words[index]
        if word in formats:
            group, text, count = formats[word]
            arguments = words[index + 3:index + 3 + count]
            if None not in arguments:
                try:
                    shown = re.sub(r'%[-0-9.]*l*[dux]', lambda _: '{}', text).format(*[hex(value) for value in arguments])
                except (IndexError, ValueError):
                    shown = text + str(arguments)
                print(group, shown)
                index += 3 + count
                continue
        if word is None:
            print('---gap---')
        index += 1


if __name__ == '__main__':
    main()
