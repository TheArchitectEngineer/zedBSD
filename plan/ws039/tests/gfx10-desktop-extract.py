#!/usr/bin/env python3
"""Writes the desktop's SPIR-V modules (userland/desktop/**/shaders.h, as the components ship them) to a directory,
named <component>-<array>.<vert|frag|comp>.spv by the execution model of the entry point (ws039-p007).

Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib

usage: gfx10-desktop-extract.py REPO DIR
"""
import pathlib
import re
import struct
import sys

ARRAY = re.compile(r"static const uint32_t (\w+)\[\] = \{(.*?)\};", re.S)
WORD = re.compile(r"0x([0-9a-fA-F]{8})U")
MODELS = {0: "vert", 4: "frag", 5: "comp"}


def stage(words):
    """The execution model of the first OpEntryPoint, by its file suffix."""
    at = 5
    while at < len(words):
        count = words[at] >> 16
        if (words[at] & 0xffff) == 15:
            return MODELS.get(words[at + 1], "other")
        if count == 0:
            break
        at += count
    return "other"


def main():
    repo = pathlib.Path(sys.argv[1])
    out = pathlib.Path(sys.argv[2])
    count = 0
    for header in sorted((repo / "userland" / "desktop").glob("**/shaders.h")):
        component = header.parent.relative_to(repo / "userland" / "desktop").as_posix().replace("/", "-")
        for name, body in ARRAY.findall(header.read_text()):
            words = [int(word, 16) for word in WORD.findall(body)]
            if not words or words[0] != 0x07230203:
                continue
            path = out / ("%s-%s.%s.spv" % (component, name, stage(words)))
            path.write_bytes(struct.pack("<%dI" % len(words), *words))
            count += 1
    print("gfx10-desktop-extract: %d modules" % count)


main()
