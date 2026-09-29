#!/usr/bin/env python3
# ws035-p134, p136: tells whether a rectangle's four corners in a screenshot (a window's body, or its floating
# title bar) are square or rounded.  Each corner pixel (one pixel in from the corner) is compared with a pixel
# inside the rectangle on the same row, past the rounding (INSET pixels in from the side), and with one outside
# it on the same row (two pixels beyond the side).  A square corner is like the inside one; a rounded corner
# shows what is behind the rectangle, like the outside one.  Where the outside pixel is off the screen (a docked
# window's side at the output's edge), the corner is square when it is within LIMIT of the inside pixel.
#
#   p134-corners.py IMAGE X Y WIDTH HEIGHT square|round [NAME]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
import sys

from PIL import Image

INSET = 20
LIMIT = 24


def difference(a, b):
    return max(abs(a[0] - b[0]), abs(a[1] - b[1]), abs(a[2] - b[2]))


def main():
    path, x, y, width, height, want = sys.argv[1], *map(int, sys.argv[2:6]), sys.argv[6]
    name = sys.argv[7] if len(sys.argv) > 7 else 'rectangle'
    image = Image.open(path).convert('RGB')
    top, bottom = y + 1, y + height - 2
    corners = {
        'top-left': (x + 1, top, x + INSET, x - 2),
        'top-right': (x + width - 2, top, x + width - 1 - INSET, x + width + 1),
        'bottom-left': (x + 1, bottom, x + INSET, x - 2),
        'bottom-right': (x + width - 2, bottom, x + width - 1 - INSET, x + width + 1),
    }
    square = 0
    for corner, (cx, cy, ix, ox) in corners.items():
        pixel = image.getpixel((cx, cy))
        inside = difference(pixel, image.getpixel((ix, cy)))
        if 0 <= ox < image.width:
            outside = difference(pixel, image.getpixel((ox, cy)))
            kind = 'square' if inside < outside else 'round'
        else:
            outside = None
            kind = 'square' if inside <= LIMIT else 'round'
        square += kind == 'square'
        print(f'{name} {corner} at {cx},{cy}: to inside {inside}, to outside {outside} -> {kind}')
    ok = square == 4 if want == 'square' else square == 0
    print(f'{name} corners: {square} of 4 square, want {want}: {"PASS" if ok else "FAIL"}')
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
