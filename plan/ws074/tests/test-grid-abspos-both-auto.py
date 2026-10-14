#!/usr/bin/env python3
# zedBSD
# Copyright (C) 2026 Awe Morris
# SPDX-License-Identifier: Zlib
"""Check a full-width absolute Grid auto/auto area and explicit row origin."""

from pathlib import Path
import re
import subprocess


ROOT = Path(__file__).resolve().parents[3]
PROGRAM = ROOT / "build/ws074-host/plain/browser"
FIXTURE = ROOT / "plan/ws074/phase217/evidence/20261014-q1116/grid-abspos-both-auto.html"
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
    expected = [(8.0, 108.0, 60.0, 10.0),
                (398.0, 108.0, 60.0, 10.0),
                (203.0, 108.0, 60.0, 10.0),
                (108.0, 108.0, 60.0, 10.0),
                (398.0, 308.0, 60.0, 10.0),
                (8.0, 308.0, 60.0, 10.0),
                (203.0, 308.0, 60.0, 10.0),
                (298.0, 308.0, 60.0, 10.0)]
    if boxes != expected:
        raise AssertionError("grid both-auto geometry differs: " + repr(boxes))
    print("grid-abspos-both-auto: PASS")


if __name__ == "__main__":
    main()
