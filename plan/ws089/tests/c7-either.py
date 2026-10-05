#!/usr/bin/env python3
# ws089-p017: the contrast ratio (WCAG 2) of the text in a region of a screenshot against what is under it, for light
# text on a dark ground as well as dark text on a light one (plan/ws099/tests/c7-contrast.py measures dark text only).
# The ground is the region's median luminance; the text is its darkest or its brightest PERCENT of pixels (the cores
# of the glyphs), whichever stands farther from the ground (the region is chosen to hold only text on its ground).
#
#   c7-either.py IMAGE NAME X0 Y0 X1 Y1 [NAME X0 Y0 X1 Y1 ...]
# Prints "C7 NAME contrast=R text=L ground=L ink=dark|light" per region.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
import sys

from PIL import Image

PERCENT = 1.0


def channel(value):
    value /= 255.0
    return value / 12.92 if value <= 0.04045 else ((value + 0.055) / 1.055) ** 2.4


def luminance(pixel):
    r, g, b = pixel[:3]
    return 0.2126 * channel(r) + 0.7152 * channel(g) + 0.0722 * channel(b)


def ratio(a, b):
    light, dark = max(a, b), min(a, b)
    return (light + 0.05) / (dark + 0.05)


def main():
    image = Image.open(sys.argv[1]).convert('RGB')
    arguments = sys.argv[2:]
    while arguments:
        name, x0, y0, x1, y1 = arguments[0], *map(int, arguments[1:5])
        arguments = arguments[5:]
        values = sorted(luminance(p) for p in image.crop((x0, y0, x1, y1)).getdata())
        ground = values[len(values) // 2]
        count = max(1, int(len(values) * PERCENT / 100.0))
        darkest = values[count - 1]
        brightest = values[len(values) - count]
        text, ink = darkest, 'dark'
        if ratio(brightest, ground) > ratio(darkest, ground):
            text, ink = brightest, 'light'
        print(f'C7 {name} contrast={ratio(text, ground):.2f} text={text:.4f} ground={ground:.4f} ink={ink}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
