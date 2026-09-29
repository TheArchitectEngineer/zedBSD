#!/usr/bin/env python3
# ws035-p134: tells whether a window's four body corners in a screenshot are square or rounded.
# Each corner pixel (one pixel in from the corner) is compared with the middle of the same horizontal edge,
# one pixel in: a square corner shows the window there too (a small difference), a rounded one shows what is
# behind the window (the wallpaper or the shadow).
#
#   p134-corners.py IMAGE X Y WIDTH HEIGHT square|round
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
import sys

from PIL import Image

LIMIT = 24


def difference(a, b):
    return max(abs(a[0] - b[0]), abs(a[1] - b[1]), abs(a[2] - b[2]))


def main():
    path, x, y, width, height, want = sys.argv[1], *map(int, sys.argv[2:6]), sys.argv[6]
    image = Image.open(path).convert('RGB')
    top, bottom = y + 1, y + height - 2
    left, right = x + 1, x + width - 2
    corners = {
        'top-left': ((left, top), (x + width // 2, top)),
        'top-right': ((right, top), (x + width // 2, top)),
        'bottom-left': ((left, bottom), (x + width // 2, bottom)),
        'bottom-right': ((right, bottom), (x + width // 2, bottom)),
    }
    square = 0
    for name, (corner, edge) in corners.items():
        d = difference(image.getpixel(corner), image.getpixel(edge))
        kind = 'square' if d <= LIMIT else 'round'
        square += kind == 'square'
        print(f'corner {name} at {corner[0]},{corner[1]}: {image.getpixel(corner)} edge {image.getpixel(edge)} '
              f'difference {d} -> {kind}')
    ok = square == 4 if want == 'square' else square == 0
    print(f'corners: {square} of 4 square, want {want}: {"PASS" if ok else "FAIL"}')
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
