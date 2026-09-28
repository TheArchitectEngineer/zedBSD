#!/usr/bin/env python3
# ws079-p014: checks pictures of an updated PDF against the original's, at points given in page points as shown.
#   update-pixels.py DPI ORIGINAL UPDATED X,Y:changed|same[:RRGGBB] ...
# changed: the pixel must differ from the original's (and be near RRGGBB when given); same: it must not differ.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
import sys

from PIL import Image


def main():
    dpi = float(sys.argv[1])
    original = Image.open(sys.argv[2]).convert('RGB')
    updated = Image.open(sys.argv[3]).convert('RGB')
    scale = dpi / 72.0
    status = 0
    for spec in sys.argv[4:]:
        parts = spec.split(':')
        x, y = (float(value) for value in parts[0].split(','))
        want = parts[1]
        colour = parts[2] if len(parts) > 2 else None
        px = int(x * scale)
        py = int(y * scale)
        before = original.getpixel((px, py))
        after = updated.getpixel((px, py))
        difference = max(abs(a - b) for a, b in zip(before, after))
        ok = difference > 40 if want == 'changed' else difference <= 8
        if ok and colour is not None:
            target = tuple(int(colour[i:i + 2], 16) for i in (0, 2, 4))
            ok = max(abs(a - b) for a, b in zip(after, target)) <= 60
        print('%s (%d,%d px): before %s after %s -> %s' % (spec, px, py, before, after, 'ok' if ok else 'FAILED'))
        if not ok:
            status = 1
    sys.exit(status)


main()
