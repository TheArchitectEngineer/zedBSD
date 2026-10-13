#!/usr/bin/env python3
# zedBSD
# Copyright (C) 2026 Awe Morris
# SPDX-License-Identifier: Zlib
"""Check explicit grid shorthand track parsing and atomic invalid rejection."""

from pathlib import Path
import re
import subprocess


ROOT = Path(__file__).resolve().parents[3]
PROGRAM = ROOT / "build/ws074-host/plain/browser"
FIXTURE = ROOT / "plan/ws074/phase218/evidence/20261014-q1095/explicit-cases.html"
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
    expected = [(8.0, 8.0, 50.0, 10.0), (63.0, 8.0, 50.0, 10.0),
                (8.0, 98.0, 30.0, 10.0), (38.0, 98.0, 70.0, 10.0),
                (8.0, 188.0, 30.0, 10.0), (38.0, 188.0, 70.0, 10.0),
                (8.0, 238.0, 50.0, 10.0), (58.0, 238.0, 50.0, 10.0),
                (8.0, 258.0, 50.0, 10.0), (58.0, 258.0, 50.0, 10.0),
                (8.0, 348.0, 30.0, 10.0), (38.0, 348.0, 70.0, 10.0)]
    if boxes != expected:
        raise AssertionError("explicit grid shorthand geometry differs: " + repr(boxes))
    print("grid-explicit-shorthand: PASS")


if __name__ == "__main__":
    main()
