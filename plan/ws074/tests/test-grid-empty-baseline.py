#!/usr/bin/env python3
# zedBSD
# Copyright (C) 2026 Awe Morris
# SPDX-License-Identifier: Zlib
"""Check synthesized Grid baseline groups in fixed rows of empty items."""

from pathlib import Path
import re
import subprocess


ROOT = Path(__file__).resolve().parents[3]
PROGRAM = ROOT / "build/ws074-host/plain/browser"
FIXTURE = ROOT / "plan/ws074/phase219/evidence/20261014-q1096/empty-baseline-cases.html"
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
    expected = [(8.0, 38.0, 50.0, 20.0), (58.0, 8.0, 50.0, 50.0),
                (108.0, 28.0, 50.0, 30.0), (8.0, 138.0, 50.0, 10.0),
                (58.0, 108.0, 50.0, 40.0), (108.0, 168.0, 50.0, 20.0)]
    if boxes != expected:
        raise AssertionError("grid empty baseline geometry differs: " + repr(boxes))
    print("grid-empty-baseline: PASS")


if __name__ == "__main__":
    main()
