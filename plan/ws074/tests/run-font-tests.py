#!/usr/bin/env python3
# zedBSD
# Copyright (C) 2026 Awe Morris
# SPDX-License-Identifier: Zlib
"""Checks browser's web fonts (ws074-p070) on the font test page make-test-images.py writes.

  run-font-tests.py [--program PATH]

Makes build/ws074-images (the page, mono.woff, mono-stored.woff, mono.ttf), draws fonts.html's display list with
--dump=paint, and checks the face each line's text is drawn in: the sans face (0) for the page's own font and for a
family whose font is not there, the first web face (3) for the WOFF font and for a family found second in a list,
the next faces (4, 5, 6) for the bold WOFF face with stored tables, the TrueType font and the ranged italic face, and
the WOFF face for a line with Japanese characters too (the display list keeps the font's face; each glyph the face
lacks is drawn from the fallback face, which the pictures show).  Prints one line a check and a total; exits 1
when any fails.
"""

import argparse
import os
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../../.."))

# ws074-p080: localStorage goes under XDG_DATA_HOME; the browsers this tool starts keep theirs under build/,
# away from the user's ~/.local/share.
os.environ["XDG_DATA_HOME"] = os.path.join(ROOT, "build/ws074-host-data")

# The first word of each line, and the faces its words are drawn in.
EXPECTED = [
    ("The", {0}),
    ("A", {3}),
    ("Its", {4}),
    ("A", {5}),
    ("A", {0}),
    ("The", {3}),
    ("A", {6}),
    ("Fallback", {3}),
]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--program", default=os.path.join(ROOT, "build/ws074-host/plain/browser"))
    arguments = parser.parse_args()
    subprocess.run([sys.executable, os.path.join(ROOT, "plan/ws074/tests/make-test-images.py")], check=True,
                   stdout=subprocess.DEVNULL)
    fonts = os.path.join(ROOT, "userland/desktop/fonts")
    page = os.path.join(ROOT, "build/ws074-images/fonts.html")
    dump = subprocess.run([arguments.program, "--dump=paint", "--width=800", "--height=400",
                           "--font=" + os.path.join(fonts, "Mahora-Regular.ttf"),
                           "--mono-font=" + os.path.join(fonts, "JetBrainsMono-Regular.ttf"),
                           "--fallback-font=" + os.path.join(fonts, "DroidSansFallbackFull.ttf"), page],
                          check=True, capture_output=True, text=True, timeout=120).stdout
    lines = {}
    order = []
    for row in dump.splitlines():
        words = row.split()
        if not words or words[0] != "text":
            continue
        baseline = words[2]
        if baseline not in lines:
            lines[baseline] = []
            order.append(baseline)
        lines[baseline].append((int(words[5]), row.split('"')[1]))
    failed = 0
    for index, (first, faces) in enumerate(EXPECTED):
        if index >= len(order):
            print("FAIL line %d: missing" % (index + 1))
            failed += 1
            continue
        pieces = [piece for piece in lines[order[index]] if piece[1].strip()]
        used = {face for face, _ in pieces}
        ok = pieces and pieces[0][1] == first and used == faces
        print("%s line %d (%s): faces %s" % ("pass" if ok else "FAIL", index + 1, first, sorted(used)))
        if not ok:
            failed += 1
    print("font-tests %d/%d" % (len(EXPECTED) - failed, len(EXPECTED)))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
