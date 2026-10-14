#!/usr/bin/env python3
# zedBSD
# Copyright (C) 2026 Awe Morris
# SPDX-License-Identifier: Zlib
"""Check Grid template line-name syntax on both axes."""

from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parents[3]
PROGRAM = ROOT / "build/ws074-host/plain/browser"
FIXTURE = ROOT / "plan/ws074/phase221/evidence/20261014-q1126/line-names.html"
FONTS = ROOT / "userland/desktop/fonts"


def main():
    command = [str(PROGRAM), "--run", "--settle-ms=100",
               "--font=" + str(FONTS / "Mahora-Regular.ttf"),
               "--mono-font=" + str(FONTS / "JetBrainsMono-Regular.ttf"),
               "--fallback-font=" + str(FONTS / "DroidSansFallbackFull.ttf"),
               str(FIXTURE)]
    observed = subprocess.check_output(command, text=True).splitlines()
    expected = ["grid-template-columns=6/6 rejected,5/5 accepted",
                "grid-template-rows=6/6 rejected,5/5 accepted"]
    if observed != expected:
        raise AssertionError("Grid line-name parsing differs: " + repr(observed))
    print("Grid line names: PASS")


if __name__ == "__main__":
    main()
