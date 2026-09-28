#!/usr/bin/env python3
# Compares libtruetype's truetype_glyph_outline() with fontTools, glyph by glyph.
#
#   truetype-outline-check.py DUMP-PROGRAM FONT...
#
# For every glyph of each font: advance and left side bearing (hmtx), the box
# glyf records, the contour ends, and every point (coordinates within 0.01,
# on-curve flag exact), composites flattened by fontTools' getCoordinates.
# Also names a few glyphs it expects and whether they are composite, so a
# reader sees that accents were really exercised.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
import subprocess
import sys

from fontTools.ttLib import TTFont


def expected(font):
    glyf = font["glyf"]
    hmtx = font["hmtx"]
    rows = []
    for index, name in enumerate(font.getGlyphOrder()):
        glyph = glyf[name]
        coordinates, ends, flags = glyph.getCoordinates(glyf)
        advance, lsb = hmtx[name]
        box = (0, 0, 0, 0)
        if glyph.numberOfContours != 0:
            box = (glyph.xMin, glyph.yMin, glyph.xMax, glyph.yMax)
        points = [(float(x), float(y), flags[i] & 1) for i, (x, y) in enumerate(coordinates)]
        rows.append((index, advance, lsb, box, list(ends), points))
    return rows


def parse(line):
    head, ends, points = line.split("|")
    fields = [int(v) for v in head.split()]
    parsed = []
    for item in points.split():
        x, y, on = item.split(",")
        parsed.append((float(x), float(y), int(on)))
    return (fields[0], fields[1], fields[2], tuple(fields[3:7]),
            [int(v) for v in ends.split()], parsed)


def check_font(program, path):
    font = TTFont(path)
    want = expected(font)
    output = subprocess.run([program, path, str(len(want))], check=True,
                            capture_output=True, text=True).stdout.splitlines()
    got = [parse(line) for line in output]
    failures = 0
    if len(got) != len(want):
        print(f"FAILED   {path}: {len(got)} glyphs, expected {len(want)}")
        return 1
    composites = 0
    transformed = 0
    for g, w in zip(got, want):
        name = font.getGlyphOrder()[w[0]]
        glyph = font["glyf"][name]
        if glyph.isComposite():
            composites += 1
            if any(hasattr(c, "transform") for c in glyph.components):
                transformed += 1
        problem = None
        if g[:3] != w[:3]:
            problem = f"metrics {g[:3]} != {w[:3]}"
        elif g[3] != w[3]:
            problem = f"box {g[3]} != {w[3]}"
        elif g[4] != w[4]:
            problem = f"ends {g[4]} != {w[4]}"
        elif len(g[5]) != len(w[5]):
            problem = f"{len(g[5])} points != {len(w[5])}"
        else:
            for i, (a, b) in enumerate(zip(g[5], w[5])):
                if abs(a[0] - b[0]) > 0.01 or abs(a[1] - b[1]) > 0.01 or a[2] != b[2]:
                    problem = f"point {i} {a} != {b}"
                    break
        if problem:
            failures += 1
            if failures <= 10:
                print(f"FAILED   {path} glyph {w[0]} {name}: {problem}")
    print(f"{'ok' if failures == 0 else 'FAILED':8} {path}: {len(want)} glyphs, "
          f"{composites} composite ({transformed} with a scale or matrix), {failures} differ")

    # Names a few glyphs so the log shows what kinds were covered.
    cmap = font.getBestCmap()
    for char in "Ao\u00e9\u00c5\u01fa":
        name = cmap.get(ord(char))
        if name is None:
            continue
        index = font.getGlyphID(name)
        row = got[index]
        kind = "composite" if font["glyf"][name].isComposite() else "simple"
        print(f"         U+{ord(char):04X} {name}: {kind}, {len(row[4])} contours, "
              f"{len(row[5])} points, advance {row[1]}, lsb {row[2]}, box {row[3]}")
    return failures


def main():
    program = sys.argv[1]
    failures = 0
    for path in sys.argv[2:]:
        failures += check_font(program, path)
    sys.exit(1 if failures else 0)


main()
