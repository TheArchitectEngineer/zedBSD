#!/usr/bin/env python3
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
"""Check physical flex left/right under row reversal, RTL and column axes."""

import argparse
from pathlib import Path
import re
import subprocess
import tempfile


def layout(program, declarations, child):
    source = ('<!doctype html><div style="display:flex;width:30px;'
              '%s"><div style="height:10px;%s"></div></div>' %
              (declarations, child))
    fonts = Path("userland/desktop/fonts")
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory) / "physical-justify.html"
        path.write_text(source, encoding="utf-8")
        result = subprocess.run(
            [program, "--dump=layout", "--font=" + str(fonts / "Mahora-Regular.ttf"),
             "--mono-font=" + str(fonts / "JetBrainsMono-Regular.ttf"),
             "--fallback-font=" + str(fonts / "DroidSansFallbackFull.ttf"), str(path)],
            capture_output=True, text=True, timeout=10,
        )
    if result.returncode:
        raise RuntimeError(result.stderr[-1000:])
    boxes = re.findall(r"^ +block <div> (-?\d+\.\d+) (-?\d+\.\d+) "
                       r"(-?\d+\.\d+) (-?\d+\.\d+)$", result.stdout, re.M)
    if len(boxes) != 2:
        raise RuntimeError("expected two boxes: " + result.stdout[-1000:])
    parent, item = (tuple(map(float, box)) for box in boxes)
    return item[0] - parent[0], item[1] - parent[1], item[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--program", default="build/ws074-host/plain/browser")
    args = parser.parse_args()
    cases = []
    for direction in ("", "flex-direction:row-reverse;", "direction:rtl;"):
        cases.append((direction + "justify-content:left;", "flex:0 0 10px;", (0.0, 0.0, 10.0)))
        cases.append((direction + "justify-content:right;", "flex:0 0 10px;", (20.0, 0.0, 10.0)))
    for direction in ("flex-direction:column;", "flex-direction:column-reverse;"):
        cases.append((direction + "height:30px;justify-content:left;", "", (0.0, 0.0, 30.0)))
        cases.append((direction + "height:30px;justify-content:right;", "", (0.0, 0.0, 30.0)))
    for side in ("left", "right"):
        cases.append(("flex-direction:column;height:30px;align-items:%s;" % side,
                      "", (0.0, 0.0, 30.0)))
    failures = 0
    for declarations, child, expected in cases:
        observed = layout(args.program, declarations, child)
        if observed != expected:
            print("FAIL %s: expected %s, got %s" %
                  (declarations, expected, observed))
            failures += 1
    if failures:
        return 1
    print("physical justify-content/align grammar: %d/%d passed" %
          (len(cases), len(cases)))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
