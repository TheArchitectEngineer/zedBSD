#!/usr/bin/env python3
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
"""Verify parent-child margin collapse across a float formatting context."""

import argparse
from pathlib import Path
import re
import subprocess
import tempfile


def layout(program, parent_style, child_style):
    source = ('<!doctype html><div style="width:20px;%s">'
              '<div style="height:10px;%s"></div></div>' %
              (parent_style, child_style))
    fonts = Path("userland/desktop/fonts")
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory) / "margin.html"
        path.write_text(source, encoding="utf-8")
        result = subprocess.run(
            [program, "--dump=layout", "--font=" + str(fonts / "Mahora-Regular.ttf"),
             "--mono-font=" + str(fonts / "JetBrainsMono-Regular.ttf"),
             "--fallback-font=" + str(fonts / "DroidSansFallbackFull.ttf"), str(path)],
            capture_output=True, text=True, timeout=10,
        )
    if result.returncode:
        raise RuntimeError(result.stderr[-1000:])
    parent = re.search(r"^    block <div> (-?\d+\.\d+) (-?\d+\.\d+) "
                       r"(-?\d+\.\d+) (-?\d+\.\d+)$", result.stdout, re.M)
    child = re.search(r"^      block <div> (-?\d+\.\d+) (-?\d+\.\d+) "
                      r"(-?\d+\.\d+) (-?\d+\.\d+)$", result.stdout, re.M)
    if parent is None or child is None:
        raise RuntimeError("missing parent or child box: " + result.stdout[-1000:])
    return tuple(map(float, parent.groups())), tuple(map(float, child.groups()))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--program", default="build/ws074-host/plain/browser")
    args = parser.parse_args()
    cases = (
        ("float top", "float:left", "margin-top:30px", 40.0, 30.0),
        ("flow top", "", "margin-top:30px", 10.0, 0.0),
        ("float bottom", "float:left", "margin-bottom:30px", 40.0, 0.0),
        ("flow bottom", "", "margin-bottom:30px", 10.0, 0.0),
    )
    failures = 0
    for name, parent_style, child_style, height, offset in cases:
        parent, child = layout(args.program, parent_style, child_style)
        observed = (parent[3], child[1] - parent[1])
        expected = (height, offset)
        if observed != expected:
            print("FAIL %s: expected %s, got %s" % (name, expected, observed))
            failures += 1
    if failures:
        return 1
    print("BFC parent-child margins: %d/%d passed" % (len(cases), len(cases)))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
