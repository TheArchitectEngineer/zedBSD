#!/usr/bin/env python3
# ws099-p001, C7: the contrast ratio (WCAG 2) of the text in a region of a screenshot against the glass under it.
# The glass is the region's median luminance, the text its darkest PERCENT of pixels (the cores of the glyphs; the
# region is chosen to hold only text on glass).
#
#   c7-contrast.py IMAGE NAME X0 Y0 X1 Y1 [NAME X0 Y0 X1 Y1 ...]
# Prints "C7 NAME contrast=R text=L glass=L" per region.
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


def main():
    image = Image.open(sys.argv[1]).convert('RGB')
    arguments = sys.argv[2:]
    while arguments:
        name, x0, y0, x1, y1 = arguments[0], *map(int, arguments[1:5])
        arguments = arguments[5:]
        values = sorted(luminance(p) for p in image.crop((x0, y0, x1, y1)).getdata())
        glass = values[len(values) // 2]
        text = values[max(0, int(len(values) * PERCENT / 100.0) - 1)]
        light, dark = max(glass, text), min(glass, text)
        ratio = (light + 0.05) / (dark + 0.05)
        print(f'C7 {name} contrast={ratio:.2f} text={text:.4f} glass={glass:.4f}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
