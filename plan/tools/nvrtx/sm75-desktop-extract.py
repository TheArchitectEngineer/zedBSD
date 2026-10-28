#!/usr/bin/env python3
"""Writes the desktop's SPIR-V modules, as the components ship them, to a directory (ws037-p008).

Every userland/desktop/**/shaders.h holds the validated SPIR-V words of one component's shaders
(written by that component's shaders/regenerate.py).  Each array becomes DIR/<component>-<name>.spv,
so the nvrtx compiler is tried on exactly the words the desktop hands the GPU.
Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib

usage: sm75-desktop-extract.py REPO DIR
"""
import pathlib
import re
import struct
import sys

ARRAY = re.compile(r"static const uint32_t (\w+)\[\] = \{(.*?)\};", re.S)
WORD = re.compile(r"0x([0-9a-fA-F]{8})U")


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
            (out / ("%s-%s.spv" % (component, name))).write_bytes(struct.pack("<%dI" % len(words), *words))
            count += 1
    print("sm75-desktop-extract: %d modules" % count)


main()
