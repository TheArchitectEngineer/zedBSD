#!/usr/bin/env python3
# zedBSD
# Copyright (C) 2026 Awe Morris
# SPDX-License-Identifier: Zlib
"""Check definite Grid block-axis absolute alignment after child sizing."""

from pathlib import Path
import re
import subprocess


ROOT = Path(__file__).resolve().parents[3]
PROGRAM = ROOT / "build/ws074-host/plain/browser"
FIXTURE = ROOT / "plan/ws074/phase217/evidence/20261014-q1109/grid-abspos-block-align.html"
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
    expected = [(8.0, 148.0, 20.0, 10.0),
                (8.0, 228.0, 20.0, 10.0),
                (8.0, 15.0, 20.0, 10.0),
                (8.0, 8.0, 20.0, 10.0),
                (88.0, 598.0, 20.0, 10.0)]
    if boxes != expected:
        raise AssertionError("grid absolute block alignment differs: " + repr(boxes))
    print("grid-abspos-block-align: PASS")


if __name__ == "__main__":
    main()
