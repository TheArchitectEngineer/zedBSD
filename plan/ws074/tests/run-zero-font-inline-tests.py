#!/usr/bin/env python3
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
"""Check that a zero-font-size strut contributes no phantom half pixel."""

import argparse
from pathlib import Path
import re
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--program", default="build/ws074-host/plain/browser")
    args = parser.parse_args()
    fonts = "userland/desktop/fonts/"
    page = Path("plan/ws074/tests/zero-font-inline.html").resolve()
    command = [args.program, "--dump=layout", "--width=800", "--height=600",
               "--font=" + fonts + "Mahora-Regular.ttf",
               "--mono-font=" + fonts + "JetBrainsMono-Regular.ttf",
               "--fallback-font=" + fonts + "DroidSansFallbackFull.ttf", str(page)]
    result = subprocess.run(command, capture_output=True, text=True, timeout=30)
    if result.returncode:
        raise RuntimeError(result.stderr[-1200:])
    line = re.search(r"line 0\.00 0\.00 height ([0-9.]+) baseline ([0-9.]+)", result.stdout)
    next_block = re.search(r"block <p> 0\.00 ([0-9.]+)", result.stdout)
    if line is None or next_block is None:
        print("FAIL zero-font inline: missing line or next block")
        return 1
    values = (line.group(1), line.group(2), next_block.group(1))
    if values != ("162.00", "162.00", "162.00"):
        print("FAIL zero-font inline: height/baseline/next=%r" % (values,))
        return 1
    print("zero-font inline: pass")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
