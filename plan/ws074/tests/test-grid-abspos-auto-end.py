#!/usr/bin/env python3
# zedBSD
# Copyright (C) 2026 Awe Morris
# SPDX-License-Identifier: Zlib
"""Check an absolute Grid child's 2/auto inline boundary with centered tracks."""

from pathlib import Path
import re
import subprocess


ROOT = Path(__file__).resolve().parents[3]
PROGRAM = ROOT / "build/ws074-host/plain/browser"
FIXTURE = ROOT / "plan/ws074/phase217/evidence/20261014-q1115/grid-abspos-auto-end.html"
FONTS = ROOT / "userland/desktop/fonts"
BOX = re.compile(r"^      block <div> ([-\d.]+) ([-\d.]+) ([-\d.]+) ([-\d.]+)$")


def main():
    command = [str(PROGRAM), "--dump=layout",
               "--font=" + str(FONTS / "Mahora-Regular.ttf"),
               "--mono-font=" + str(FONTS / "JetBrainsMono-Regular.ttf"),
               "--fallback-font=" + str(FONTS / "DroidSansFallbackFull.ttf"),
               str(FIXTURE)]
    output = subprocess.check_output(command, text=True)
    boxes = []
    for line in output.splitlines():
        match = BOX.match(line)
        if match is not None:
            boxes.append(tuple(float(value) for value in match.groups()))
    expected = [(208.0, 8.0, 60.0, 10.0),
                (398.0, 8.0, 60.0, 10.0),
                (303.0, 8.0, 60.0, 10.0),
                (298.0, 8.0, 60.0, 10.0),
                (198.0, 108.0, 60.0, 10.0),
                (8.0, 108.0, 60.0, 10.0),
                (103.0, 108.0, 60.0, 10.0),
                (108.0, 108.0, 60.0, 10.0)]
    if boxes != expected:
        raise AssertionError("grid auto end geometry differs: " + repr(boxes))
    print("grid-abspos-auto-end: PASS")


if __name__ == "__main__":
    main()
