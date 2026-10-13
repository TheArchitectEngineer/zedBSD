#!/usr/bin/env python3
# zedBSD
# Copyright (C) 2026 Awe Morris
# SPDX-License-Identifier: Zlib
"""Check LTR Grid item placement from justify-items and justify-self."""

import argparse
from pathlib import Path
import re
import subprocess
import tempfile


def position(program, parent_style, child_style, auto_width=False):
    width = "auto" if auto_width else "20px"
    content = '<span style="display:inline-block;width:20px;height:10px"></span>' if auto_width else ""
    source = ('<!doctype html><body><div style="display:grid;width:100px;'
              'grid-template-columns:100px;%s"><div style="width:%s;'
              'height:10px;%s">%s</div></div></body>' %
              (parent_style, width, child_style, content))
    fonts = Path("userland/desktop/fonts")
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory) / "grid-justify.html"
        path.write_text(source, encoding="utf-8")
        result = subprocess.run(
            [program, "--dump=layout",
             "--font=" + str(fonts / "Mahora-Regular.ttf"),
             "--mono-font=" + str(fonts / "JetBrainsMono-Regular.ttf"),
             "--fallback-font=" + str(fonts / "DroidSansFallbackFull.ttf"),
             str(path)], capture_output=True, text=True, timeout=10,
        )
    if result.returncode:
        raise RuntimeError(result.stderr[-1000:])
    boxes = re.findall(r"^ +block <div> (-?\d+\.\d+) (-?\d+\.\d+) "
                       r"(-?\d+\.\d+) (-?\d+\.\d+)$", result.stdout, re.M)
    if len(boxes) != 2:
        raise RuntimeError("expected parent and item: " + result.stdout[-1000:])
    parent, child = (tuple(map(float, box)) for box in boxes)
    return child[0] - parent[0], child[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--program", default="build/ws074-host/plain/browser")
    args = parser.parse_args()
    cases = [
        ("", "", (0.0, 20.0)),
        ("justify-items:start;", "", (0.0, 20.0)),
        ("justify-items:end;", "", (80.0, 20.0)),
        ("justify-items:center;", "", (40.0, 20.0)),
        ("justify-items:stretch;", "", (0.0, 20.0)),
        ("justify-items:end;", "justify-self:start;", (0.0, 20.0)),
        ("justify-items:start;", "justify-self:end;", (80.0, 20.0)),
        ("justify-items:start;", "justify-self:center;", (40.0, 20.0)),
        ("", "justify-self:right;", (80.0, 20.0)),
        ("justify-items:self-start;", "direction:rtl;", (80.0, 20.0)),
        ("justify-items:self-end;", "direction:rtl;", (0.0, 20.0)),
        ("", "direction:rtl;justify-self:self-start;", (80.0, 20.0)),
        ("", "direction:rtl;justify-self:self-end;", (0.0, 20.0)),
        ("direction:rtl;", "", (80.0, 20.0)),
        ("direction:rtl;justify-items:start;", "", (80.0, 20.0)),
        ("direction:rtl;justify-items:stretch;", "", (80.0, 20.0)),
        ("direction:rtl;justify-items:end;", "", (0.0, 20.0)),
        ("direction:rtl;justify-items:center;", "", (40.0, 20.0)),
        ("direction:rtl;justify-items:left;", "", (0.0, 20.0)),
        ("direction:rtl;justify-items:right;", "", (80.0, 20.0)),
        ("direction:rtl;justify-items:end;", "justify-self:start;", (80.0, 20.0)),
        ("direction:rtl;justify-items:start;", "justify-self:end;", (0.0, 20.0)),
        ("direction:rtl;", "direction:ltr;justify-self:self-start;", (0.0, 20.0)),
        ("direction:rtl;", "direction:ltr;justify-self:self-end;", (80.0, 20.0)),
        ("grid-template-columns:30px 70px;", "grid-column:1;", (0.0, 20.0)),
        ("grid-template-columns:30px 70px;", "grid-column:2;", (30.0, 20.0)),
        ("direction:rtl;grid-template-columns:30px 70px;", "grid-column:1;", (80.0, 20.0)),
        ("direction:rtl;grid-template-columns:30px 70px;", "grid-column:2;", (50.0, 20.0)),
        ("direction:rtl;grid-template-columns:30px;justify-content:start;", "", (80.0, 20.0)),
        ("direction:rtl;grid-template-columns:30px;justify-content:end;", "", (10.0, 20.0)),
        ("direction:rtl;grid-template-columns:30px;justify-content:left;", "", (10.0, 20.0)),
        ("direction:rtl;grid-template-columns:30px;justify-content:right;", "", (80.0, 20.0)),
        ("direction:rtl;grid-template-columns:30px;justify-content:center;", "", (45.0, 20.0)),
    ]
    failures = 0
    for parent_style, child_style, expected in cases:
        observed = position(args.program, parent_style, child_style)
        if observed != expected:
            print("FAIL %s %s: expected %s, got %s" %
                  (parent_style, child_style, expected, observed))
            failures += 1
    auto_cases = [
        ("", "", (0.0, 100.0)),
        ("justify-items:start;", "", (0.0, 20.0)),
        ("justify-items:end;", "", (80.0, 20.0)),
        ("justify-items:center;", "", (40.0, 20.0)),
        ("justify-items:start;", "justify-self:stretch;", (0.0, 100.0)),
        ("direction:rtl;justify-items:start;", "", (80.0, 20.0)),
        ("direction:rtl;justify-items:end;", "", (0.0, 20.0)),
        ("", "margin-left:auto;", (80.0, 20.0)),
        ("", "margin-right:auto;", (0.0, 20.0)),
        ("", "margin-left:auto;margin-right:auto;", (40.0, 20.0)),
        ("justify-items:end;", "margin-left:auto;margin-right:auto;", (40.0, 20.0)),
    ]
    for parent_style, child_style, expected in auto_cases:
        observed = position(args.program, parent_style, child_style, True)
        if observed != expected:
            print("FAIL auto %s %s: expected %s, got %s" %
                  (parent_style, child_style, expected, observed))
            failures += 1
    if failures:
        return 1
    total = len(cases) + len(auto_cases)
    print("grid inline-axis placement: %d/%d passed" % (total, total))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
