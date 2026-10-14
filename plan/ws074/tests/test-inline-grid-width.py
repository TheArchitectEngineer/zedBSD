#!/usr/bin/env python3
# zedBSD
# Copyright (C) 2026 Awe Morris
# SPDX-License-Identifier: Zlib
"""Check inline-grid intrinsic width and a right-start absolute child."""

from pathlib import Path
import re
import subprocess


ROOT = Path(__file__).resolve().parents[3]
PROGRAM = ROOT / "build/ws074-host/plain/browser"
FIXTURE = ROOT / "plan/ws074/phase217/evidence/20261014-q1120/inline-grid-width.html"
FONTS = ROOT / "userland/desktop/fonts"
BOX = re.compile(r"^(    |      |        )block <div> ([-\d.]+) ([-\d.]+) ([-\d.]+) ([-\d.]+)$")


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
            boxes.append((len(match.group(1)),) +
                         tuple(float(value) for value in match.groups()[1:]))
    expected = [(6, 8.0, 8.0, 250.0, 150.0),
                (8, 8.0, 8.0, 60.0, 10.0),
                (6, 8.0, 161.0, 250.0, 150.0),
                (8, 198.0, 161.0, 60.0, 10.0),
                (6, 8.0, 314.0, 400.0, 150.0),
                (8, 8.0, 314.0, 60.0, 10.0),
                (4, 8.0, 467.0, 1008.0, 150.0)]
    if boxes != expected:
        raise AssertionError("inline-grid geometry differs: " + repr(boxes))
    display_command = [str(PROGRAM), "--run", "--settle-ms=100",
                       *command[2:]]
    display_output = subprocess.check_output(display_command, text=True)
    if display_output.splitlines() != ["inline-grid-display=inline-grid",
                                       "block-grid-display=grid"]:
        raise AssertionError("inline-grid computed display differs: " + repr(display_output))
    print("inline-grid-width: PASS")


if __name__ == "__main__":
    main()
