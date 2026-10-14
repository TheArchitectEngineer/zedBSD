#!/usr/bin/env python3
# zedBSD
# Copyright (C) 2026 Awe Morris
# SPDX-License-Identifier: Zlib
"""Check grid-auto-flow parsing, CSSOM and CSS-wide computed values."""

from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parents[3]
PROGRAM = ROOT / "build/ws074-host/plain/browser"
FIXTURE = ROOT / "plan/ws074/phase221/evidence/20261014-q1130/auto-flow.html"
FONTS = ROOT / "userland/desktop/fonts"


def main():
    command = [str(PROGRAM), "--run", "--settle-ms=100",
               "--font=" + str(FONTS / "Mahora-Regular.ttf"),
               "--mono-font=" + str(FONTS / "JetBrainsMono-Regular.ttf"),
               "--fallback-font=" + str(FONTS / "DroidSansFallbackFull.ttf"),
               str(FIXTURE)]
    observed = subprocess.check_output(command, text=True).splitlines()
    expected = ["auto-flow=7/7 accepted,3/3 rejected,true,true,true"]
    if observed != expected:
        raise AssertionError("grid-auto-flow CSS state differs: " + repr(observed))
    print("grid-auto-flow CSS state: PASS")


if __name__ == "__main__":
    main()
