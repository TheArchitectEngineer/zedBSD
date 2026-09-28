#!/usr/bin/env python3
# ws075-p012: turns the scanout buffers shot.py saved (DIR/shots.json and DIR/shot-*.raw, XRGB8888 rows of `pitch`
# bytes) into PNG files next to them, and a contact sheet DIR/sheet.png of all of them at a quarter of their size.
#
#   raw2png.py DIR
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
import json
import os
import sys

from PIL import Image


def main():
    folder = sys.argv[1]
    with open(os.path.join(folder, 'shots.json')) as j:
        info = json.load(j)
    width, height, pitch = info['width'], info['height'], info['pitch']
    images = []
    for shot in info['shots']:
        path = os.path.join(folder, shot['file'])
        if not os.path.exists(path):
            continue
        with open(path, 'rb') as raw:
            data = raw.read()
        if len(data) < pitch * height:
            print(f'raw2png: {path} is short ({len(data)} bytes)')
            continue
        image = Image.frombuffer('RGB', (width, height), data, 'raw', 'BGRX', pitch, 1)
        png = path[:-4] + '.png'
        image.save(png)
        images.append((shot, image))
        print(f'raw2png: {png}')
    if not images:
        return 1
    scale = 4
    tile_w, tile_h = width // scale, height // scale
    sheet = Image.new('RGB', (tile_w * 2, tile_h * ((len(images) + 1) // 2)), (40, 40, 40))
    for index, (shot, image) in enumerate(images):
        sheet.paste(image.resize((tile_w, tile_h)), ((index % 2) * tile_w, (index // 2) * tile_h))
    sheet.save(os.path.join(folder, 'sheet.png'))
    print(f'raw2png: {os.path.join(folder, "sheet.png")}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
