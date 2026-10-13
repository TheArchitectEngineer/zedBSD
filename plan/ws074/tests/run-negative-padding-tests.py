#!/usr/bin/env python3
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
"""Check that invalid negative padding leaves each prior declaration intact."""

import argparse
from pathlib import Path
import re
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--program", default="build/ws074-host/plain/browser")
    args = parser.parse_args()
    fonts = "userland/desktop/fonts/"
    page = Path("plan/ws074/tests/negative-padding.html").resolve()
    command = [args.program, "--dump=style", "--width=800", "--height=600",
               "--font=" + fonts + "Mahora-Regular.ttf",
               "--mono-font=" + fonts + "JetBrainsMono-Regular.ttf",
               "--fallback-font=" + fonts + "DroidSansFallbackFull.ttf", str(page)]
    result = subprocess.run(command, capture_output=True, text=True, timeout=30)
    if result.returncode:
        raise RuntimeError(result.stderr[-1200:])
    lines = re.findall(r'^\|\s+div display=.*$', result.stdout, re.MULTILINE)
    values = [(re.search(r'margin=([^ ]+)', line).group(1),
               re.search(r'padding=([^ ]+)', line).group(1)) for line in lines]
    expected = [("0.00,0.00,0.00,0.00", "0.00,0.00,0.00,0.00"),
                ("0.00,0.00,0.00,0.00", "1.00,2.00,3.00,4.00"),
                ("0.00,0.00,0.00,0.00", "0.00,2.00,0.00,4.00"),
                ("0.00,0.00,0.00,0.00", "3.00,0.00,5.00,0.00"),
                ("0.00,-10.00,0.00,0.00", "0.00,7.00,0.00,0.00"),
                ("0.00,0.00,0.00,0.00", "0.00,6.00,10.00%,0.00")]
    if values != expected:
        print("FAIL negative padding: %r" % (values,))
        return 1
    print("negative padding: pass")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
