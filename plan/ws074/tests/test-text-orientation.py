#!/usr/bin/env python3
# zedBSD
# Copyright (C) 2026 Awe Morris
# SPDX-License-Identifier: Zlib
"""Check CSS text-orientation parsing, inheritance and CSSOM values."""

from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parents[3]
PROGRAM = ROOT / "build/ws074-host/plain/browser"
FIXTURE = ROOT / "plan/ws074/phase220/evidence/20261014-q1122/text-orientation.html"
FONTS = ROOT / "userland/desktop/fonts"


def main():
    command = [str(PROGRAM), "--run", "--settle-ms=100",
               "--font=" + str(FONTS / "Mahora-Regular.ttf"),
               "--mono-font=" + str(FONTS / "JetBrainsMono-Regular.ttf"),
               "--fallback-font=" + str(FONTS / "DroidSansFallbackFull.ttf"),
               str(FIXTURE)]
    observed = subprocess.check_output(command, text=True).splitlines()
    expected = ["default=mixed/mixed/horizontal-tb",
                "parent=sideways/sideways/vertical-lr",
                "inherited=sideways/sideways/vertical-lr",
                "override=upright/upright/vertical-lr",
                "initial=mixed/mixed/vertical-lr",
                "unset=sideways/sideways/vertical-lr",
                "inherit=sideways/sideways/vertical-lr",
                "invalid=sideways/sideways/vertical-lr",
                "reverse=upright/upright/vertical-rl",
                "changed=mixed/mixed/vertical-lr",
                "rejected=mixed/mixed/vertical-lr"]
    if observed != expected:
        raise AssertionError("text-orientation values differ: " + repr(observed))
    print("text-orientation: PASS")


if __name__ == "__main__":
    main()
