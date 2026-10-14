#!/usr/bin/env python3
# zedBSD
# Copyright (C) 2026 Awe Morris
# SPDX-License-Identifier: Zlib
"""Check absolute Grid auto row boundaries with centered fixed tracks."""

from pathlib import Path
import re
import subprocess


ROOT = Path(__file__).resolve().parents[3]
PROGRAM = ROOT / "build/ws074-host/plain/browser"
FIXTURE = ROOT / "plan/ws074/phase217/evidence/20261014-q1118/grid-auto-row-boundaries.html"
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
    expected = [(8.0, 98.0, 20.0, 10.0),
                (8.0, 53.0, 20.0, 10.0),
                (8.0, 448.0, 20.0, 10.0),
                (8.0, 353.0, 20.0, 10.0),
                (8.0, 448.0, 20.0, 10.0),
                (8.0, 228.0, 20.0, 10.0),
                (8.0, 248.0, 20.0, 10.0),
                (238.0, 548.0, 20.0, 10.0),
                (238.0, 898.0, 20.0, 10.0),
                (238.0, 898.0, 20.0, 10.0)]
    if boxes != expected:
        raise AssertionError("grid auto row geometry differs: " + repr(boxes))
    print("grid-auto-row-boundaries: PASS")


if __name__ == "__main__":
    main()
