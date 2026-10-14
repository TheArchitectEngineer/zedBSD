#!/usr/bin/env python3
# zedBSD
# Copyright (C) 2026 Awe Morris
# SPDX-License-Identifier: Zlib
"""Check fixed implicit Grid track sizes and row dense backfill."""

from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parents[3]
PROGRAM = ROOT / "build/ws074-host/plain/browser"
FIXTURE = ROOT / "plan/ws074/phase222/evidence/20261014-q1132/implicit-tracks.html"
FONTS = ROOT / "userland/desktop/fonts"


def main():
    command = [str(PROGRAM), "--run", "--settle-ms=100",
               "--font=" + str(FONTS / "Mahora-Regular.ttf"),
               "--mono-font=" + str(FONTS / "JetBrainsMono-Regular.ttf"),
               "--fallback-font=" + str(FONTS / "DroidSansFallbackFull.ttf"),
               str(FIXTURE)]
    observed = subprocess.check_output(command, text=True).splitlines()
    expected = ["sparse=0,0,100,50|0,50,200,100|0,150,100,50|100,150,100,50",
                "dense=0,0,100,50|0,50,200,100|100,0,100,50|0,150,100,50"]
    if observed != expected:
        raise AssertionError("Grid implicit track geometry differs: " + repr(observed))
    print("Grid implicit track geometry: PASS")


if __name__ == "__main__":
    main()
