#!/usr/bin/env python3
# zedBSD
# Copyright (C) 2026 Awe Morris
# SPDX-License-Identifier: Zlib
"""Check CSSOM grid-template-rows against the existing columns path."""

from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parents[3]
PROGRAM = ROOT / "build/ws074-host/plain/browser"
FIXTURE = ROOT / "plan/ws074/phase221/evidence/20261014-q1124/grid-template-rows.html"
FONTS = ROOT / "userland/desktop/fonts"


def main():
    command = [str(PROGRAM), "--run", "--settle-ms=100",
               "--font=" + str(FONTS / "Mahora-Regular.ttf"),
               "--mono-font=" + str(FONTS / "JetBrainsMono-Regular.ttf"),
               "--fallback-font=" + str(FONTS / "DroidSansFallbackFull.ttf"),
               str(FIXTURE)]
    observed = subprocess.check_output(command, text=True).splitlines()
    expected = ["tracks=15px 2fr auto/15px 2fr auto",
                "columns=10px 20px", "none=none/none",
                "nongrid=25px/25px", "changed=5px 7px/5px 7px"]
    if observed != expected:
        raise AssertionError("grid-template-rows CSSOM differs: " + repr(observed))
    print("grid-template-rows CSSOM: PASS")


if __name__ == "__main__":
    main()
