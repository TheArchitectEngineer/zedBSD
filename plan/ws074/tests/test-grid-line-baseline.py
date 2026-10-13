#!/usr/bin/env python3
# zedBSD
# Copyright (C) 2026 Awe Morris
# SPDX-License-Identifier: Zlib
"""Check first-line and synthesized Grid baselines in fixed rows."""

from pathlib import Path
import re
import subprocess


ROOT = Path(__file__).resolve().parents[3]
PROGRAM = ROOT / "build/ws074-host/plain/browser"
FIXTURE = ROOT / "plan/ws074/phase219/evidence/20261014-q1098/line-baseline-cases.html"
FONTS = ROOT / "userland/desktop/fonts"
BOX = re.compile(r"^      block <div> ([-\d.]+) ([-\d.]+) ([-\d.]+) ([-\d.]+)$")
LINE = re.compile(r"^        line [-\d.]+ [-\d.]+ height [-\d.]+ baseline ([-\d.]+)$")


def main():
    command = [str(PROGRAM), "--dump=layout",
               "--font=" + str(FONTS / "Mahora-Regular.ttf"),
               "--mono-font=" + str(FONTS / "JetBrainsMono-Regular.ttf"),
               "--fallback-font=" + str(FONTS / "DroidSansFallbackFull.ttf"),
               str(FIXTURE)]
    output = subprocess.check_output(command, text=True)
    boxes = []
    baselines = []
    for line in output.splitlines():
        match = BOX.match(line)
        if match is not None:
            boxes.append(tuple(float(value) for value in match.groups()))
        match = LINE.match(line)
        if match is not None:
            baselines.append(float(match.group(1)))
    if baselines[:3] != [42.5, 42.5, 42.5]:
        raise AssertionError("first row line baselines differ: " + repr(baselines))
    if baselines[3:] != [148.0]:
        raise AssertionError("line baseline differs from the empty sibling: " + repr(baselines))
    if boxes[3:] != [(8.0, 108.0, 50.0, 40.0),
                     (58.0, 131.0, 50.0, 20.0),
                     (108.0, 168.0, 50.0, 20.0)]:
        raise AssertionError("mixed baseline/end row differs: " + repr(boxes))
    print("grid-line-baseline: PASS")


if __name__ == "__main__":
    main()
