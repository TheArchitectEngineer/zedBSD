#!/usr/bin/env python3
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
"""Verify sharp text shadows, inheritance, and paint order."""

import argparse
from pathlib import Path
import re
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--program", default="build/ws074-host/plain/browser")
    args = parser.parse_args()
    fonts = "userland/desktop/fonts/"
    page = Path("plan/ws074/tests/sharp-text-shadow.html").resolve()
    command = [args.program, "--dump=paint", "--width=800", "--height=600",
               "--font=" + fonts + "Mahora-Regular.ttf",
               "--mono-font=" + fonts + "JetBrainsMono-Regular.ttf",
               "--fallback-font=" + fonts + "DroidSansFallbackFull.ttf", str(page)]
    result = subprocess.run(command, capture_output=True, text=True, timeout=30)
    if result.returncode:
        raise RuntimeError(result.stderr[-1200:])
    entries = []
    for line in result.stdout.splitlines():
        match = re.match(r'text ([\d.]+) ([\d.]+) .*?(#[0-9a-f]+) "([ABC])"$', line)
        if match:
            entries.append((float(match[1]), float(match[2]), match[3], match[4]))
    expected = ["A", "A", "B", "B", "C"]
    if [entry[3] for entry in entries] != expected or len(entries) != 5:
        print("FAIL sharp text shadow sequence: %r" % (entries,))
        return 1
    for shadow, original in ((entries[0], entries[1]), (entries[2], entries[3])):
        if (round(shadow[0] - original[0], 2) != 3.0 or
                round(shadow[1] - original[1], 2) != 3.0 or
                shadow[2] != "#ffc0c0c0" or original[2] != "#ff000000"):
            print("FAIL sharp text shadow pair: %r %r" % (shadow, original))
            return 1
    if entries[4][2] != "#ff000000":
        print("FAIL sharp text shadow none: %r" % (entries[4],))
        return 1
    print("sharp text shadow: pass")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
