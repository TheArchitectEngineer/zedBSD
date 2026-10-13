#!/usr/bin/env python3
# zedBSD
# Copyright (C) 2026 Awe Morris
# SPDX-License-Identifier: Zlib
"""Check a replaced Grid item's natural, stretched and aligned dimensions."""

import argparse
from pathlib import Path
import re
import shutil
import subprocess
import tempfile


def image_box(program, image, parent_style):
    source = ('<!doctype html><div style="display:grid;width:200px;'
              'grid-template-columns:200px;grid-template-rows:150px;%s">'
              '<img src="image.png"></div>' % parent_style)
    fonts = Path("userland/desktop/fonts")
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory)
        shutil.copyfile(image, path / "image.png")
        (path / "test.html").write_text(source, encoding="utf-8")
        result = subprocess.run(
            [program, "--dump=layout",
             "--font=" + str(fonts / "Mahora-Regular.ttf"),
             "--mono-font=" + str(fonts / "JetBrainsMono-Regular.ttf"),
             "--fallback-font=" + str(fonts / "DroidSansFallbackFull.ttf"),
             str(path / "test.html")], capture_output=True, text=True,
            timeout=10,
        )
    if result.returncode:
        raise RuntimeError(result.stderr[-1000:])
    parent = re.search(r"^ +block <div> (-?\d+\.\d+) (-?\d+\.\d+) "
                       r"(-?\d+\.\d+) (-?\d+\.\d+)$", result.stdout,
                       re.M)
    child = re.search(r"^ +block <img> (-?\d+\.\d+) (-?\d+\.\d+) "
                      r"(-?\d+\.\d+) (-?\d+\.\d+) image 100x100$",
                      result.stdout, re.M)
    if parent is None or child is None:
        raise RuntimeError("expected Grid and image: " + result.stdout[-1000:])
    x = float(child.group(1)) - float(parent.group(1))
    return x, float(child.group(3)), float(child.group(4))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--program", default="build/ws074-host/plain/browser")
    parser.add_argument(
        "--image", default="/tmp/zedbsd-interop2025-wpt/css/css-grid/"
        "alignment/support/100x100-green.png")
    args = parser.parse_args()
    cases = [
        ("", (0.0, 100.0, 100.0)),
        ("justify-items:normal;", (0.0, 100.0, 100.0)),
        ("justify-items:stretch;", (0.0, 200.0, 200.0)),
        ("justify-items:start;", (0.0, 100.0, 100.0)),
        ("justify-items:end;", (100.0, 100.0, 100.0)),
        ("justify-items:center;", (50.0, 100.0, 100.0)),
        ("align-items:stretch;", (0.0, 100.0, 150.0)),
    ]
    failures = 0
    for style, expected in cases:
        observed = image_box(args.program, Path(args.image), style)
        if observed != expected:
            print("FAIL %s: expected %s, got %s" % (style, expected, observed))
            failures += 1
    if failures:
        return 1
    print("grid replaced alignment: %d/%d passed" % (len(cases), len(cases)))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
