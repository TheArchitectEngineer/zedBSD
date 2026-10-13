#!/usr/bin/env python3
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
"""Check physical flex left/right under row reversal, RTL and column axes."""

import argparse
from pathlib import Path
import re
import subprocess
import tempfile


def layout(program, declarations, child, body="", display="flex"):
    source = ('<!doctype html><body style="%s"><div style="display:%s;width:30px;'
              '%s"><div style="height:10px;%s"></div></div></body>' %
              (body, display, declarations, child))
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
    for direction in ("", "direction:rtl;"):
        for side, y in (("left", 0.0), ("right", 20.0)):
            cases.append(("writing-mode:vertical-lr;flex-direction:row-reverse;"
                          "height:30px;%sjustify-content:%s;" % (direction, side),
                          "flex:0 0 10px;width:10px;", (0.0, y, 10.0)))
    for side in ("left", "right"):
        cases.append(("writing-mode:vertical-lr;flex-direction:column-reverse;"
                      "height:30px;justify-content:%s;" % side,
                      "flex:0 0 10px;width:10px;", (0.0, 0.0, 10.0)))
    inherited = ("writing-mode:inherit;flex-direction:row-reverse;"
                 "height:30px;justify-content:right;",
                 "flex:0 0 10px;width:10px;", (0.0, 20.0, 10.0))
    failures = 0
    for declarations, child, expected in cases:
        observed = layout(args.program, declarations, child)
        if observed != expected:
            print("FAIL %s: expected %s, got %s" %
                  (declarations, expected, observed))
            failures += 1
    observed = layout(args.program, inherited[0], inherited[1],
                      "writing-mode:vertical-lr;")
    if observed != inherited[2]:
        print("FAIL inherited vertical-lr: expected %s, got %s" %
              (inherited[2], observed))
        failures += 1
    for side, x in (("left", 0.0), ("right", 20.0)):
        observed = layout(args.program,
                          "grid-template-columns:10px;justify-content:%s;" % side,
                          "width:10px;", display="grid")
        expected = (x, 0.0, 10.0)
        if observed != expected:
            print("FAIL grid %s: expected %s, got %s" %
                  (side, expected, observed))
            failures += 1
    if failures:
        return 1
    print("physical justify-content/align grammar: %d/%d passed" %
          (len(cases) + 3, len(cases) + 3))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
