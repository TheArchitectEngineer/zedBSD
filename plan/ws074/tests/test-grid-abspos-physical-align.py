#!/usr/bin/env python3
# zedBSD
# Copyright (C) 2026 Awe Morris
# SPDX-License-Identifier: Zlib
"""Check physical Grid edges and the child's self-start direction."""

from pathlib import Path
import re
import subprocess


ROOT = Path(__file__).resolve().parents[3]
PROGRAM = ROOT / "build/ws074-host/plain/browser"
FIXTURE = ROOT / "plan/ws074/phase217/evidence/20261014-q1112/grid-abspos-physical-align.html"
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
    expected = [(8.0, 8.0, 20.0, 10.0),
                (88.0, 8.0, 20.0, 10.0),
                (88.0, 8.0, 20.0, 10.0),
                (15.0, 8.0, 20.0, 10.0),
                (8.0, 8.0, 20.0, 10.0),
                (8.0, 108.0, 20.0, 10.0),
                (88.0, 108.0, 20.0, 10.0),
                (8.0, 108.0, 20.0, 10.0)]
    if boxes != expected:
        raise AssertionError("grid absolute physical alignment differs: " + repr(boxes))
    print("grid-abspos-physical-align: PASS")


if __name__ == "__main__":
    main()
