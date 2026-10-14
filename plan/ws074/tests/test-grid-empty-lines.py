#!/usr/bin/env python3
# zedBSD
# Copyright (C) 2026 Awe Morris
# SPDX-License-Identifier: Zlib
"""Check empty Grid line-name CSSOM serialization on both axes."""

from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parents[3]
PROGRAM = ROOT / "build/ws074-host/plain/browser"
FIXTURE = ROOT / "plan/ws074/phase221/evidence/20261014-q1128/empty-line-names.html"
FONTS = ROOT / "userland/desktop/fonts"


def main():
    command = [str(PROGRAM), "--run", "--settle-ms=100",
               "--font=" + str(FONTS / "Mahora-Regular.ttf"),
               "--mono-font=" + str(FONTS / "JetBrainsMono-Regular.ttf"),
               "--fallback-font=" + str(FONTS / "DroidSansFallbackFull.ttf"),
               str(FIXTURE)]
    observed = subprocess.check_output(command, text=True).splitlines()
    expected = [name + "=repeat(1, 10px)|150px 1fr|[one] 10px [two]|"
                for name in ("grid-template-columns", "grid-template-rows")]
    if observed != expected:
        raise AssertionError("Grid empty line-name serialization differs: " + repr(observed))
    print("Grid empty line names: PASS")


if __name__ == "__main__":
    main()
