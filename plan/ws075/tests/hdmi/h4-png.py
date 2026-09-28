#!/usr/bin/env python3
# ws075-p013 (H4): turns the shots of h4-ctl.py into PNG next to them: NAME-vga.ppm and splash-*.ppm (the standard
# VGA), and NAME-{A,B}.raw (the resident buffers, XRGB8888 rows of the pitch in NAME.json).  Files that already have
# their PNG are skipped.
#
#   h4-png.py DIR
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
import glob
import json
import os
import sys

from PIL import Image


def main():
    folder = sys.argv[1]
    made = 0
    for ppm in sorted(glob.glob(os.path.join(folder, '*.ppm'))):
        png = ppm[:-4] + '.png'
        if os.path.exists(png):
            continue
        Image.open(ppm).save(png)
        made += 1
    for raw in sorted(glob.glob(os.path.join(folder, '*-[AB].raw'))):
        png = raw[:-4] + '.png'
        if os.path.exists(png):
            continue
        with open(raw[:-6] + '.json') as j:
            info = json.load(j)
        with open(raw, 'rb') as f:
            data = f.read()
        if len(data) < info['pitch'] * info['height']:
            print(f'h4-png: {raw} is short')
            continue
        Image.frombuffer('RGB', (info['width'], info['height']), data, 'raw', 'BGRX', info['pitch'], 1).save(png)
        made += 1
    print(f'h4-png: {made} PNG made in {folder}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
