#!/usr/bin/env python3
# zedBSD
# Copyright (C) 2026 Awe Morris
# SPDX-License-Identifier: Zlib
"""Check row dense backfills a hole while sparse placement advances."""

from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parents[3]
PROGRAM = ROOT / "build/ws074-host/plain/browser"
FIXTURE = ROOT / "plan/ws074/phase222/evidence/20261014-q1131/row-dense.html"
FONTS = ROOT / "userland/desktop/fonts"


def main():
    command = [str(PROGRAM), "--run", "--settle-ms=100",
               "--font=" + str(FONTS / "Mahora-Regular.ttf"),
               "--mono-font=" + str(FONTS / "JetBrainsMono-Regular.ttf"),
               "--fallback-font=" + str(FONTS / "DroidSansFallbackFull.ttf"),
               str(FIXTURE)]
    observed = subprocess.check_output(command, text=True).splitlines()
    expected = ["sparse=0,0|0,20|0,40",
                "dense=0,0|0,20|40,0"]
    if observed != expected:
        raise AssertionError("Grid row dense placement differs: " + repr(observed))
    print("Grid row dense placement: PASS")


if __name__ == "__main__":
    main()
