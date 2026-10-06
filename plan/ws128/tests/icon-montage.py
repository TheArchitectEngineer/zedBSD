#!/usr/bin/env python3
# ws128-p012: the montage of the applications' icons as the compositor draws them: each picture's coverage from
# icon-dump (userland/desktop/wayland/icons.c's own rasterizer), white on a circle of the application's colour (the
# picture's 24-unit grid PICTURE of the diameter, as shell.c's kwl_glass_draw_app_mark; the 2026-10-06 user decision:
# a circle, one colour, a white knocked-out symbol), on a
# light and a dark ground, at the sizes of the bar (26), Wiseview's labels (32) and App Home (64), and one large.
#   icon-montage.py DUMP_DIR OUT.png
# DUMP_DIR holds NN.pgm from "icon-dump 448 DUMP_DIR".
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
import os
import sys
from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.normpath(os.path.join(os.path.dirname(__file__), '..', '..', '..'))
FONT = os.path.join(ROOT, 'userland/desktop/fonts/Inter.ttf')

# The standard applications of App Home (userland/desktop/wayland/apps.conf): name, picture (icons.h's order from
# GLASS_ICON_FIRST_APP, the names of icon_app_names), colour.
APPS = [
    ('Files', 'files', 0xe8b53e), ('Notes', 'notes', 0xff8a3d), ('Settings', 'settings', 0x6b7a8f),
    ('Terminal', 'terminal', 0x2e3440), ('PDF Viewer', 'pdf', 0xe5483f), ('Image Viewer', 'image', 0xec5f9a),
    ('Video Player', 'video', 0x7a4fd0), ('Phone', 'phone', 0x34c759), ('Calendar', 'calendar', 0x2f7cf6),
    ('Mail', 'mail', 0x19a1e6), ('Text Editor', 'text', 0x5c6bc0), ('System Monitor', 'monitor', 0x13a89e),
    ('Browser', 'browser', 0x0e7490), ('X terminal', 'xterm', 0x4a4a78), ('Model Viewer', 'model', 0xe07a5a),
    ('Gears', 'gears', 0xa0522d), ('Lock Screen', 'lock', 0x5b6475), ('Log Out', 'logout', 0x5b6475),
]

# The picture's grid as a part of the circle's diameter (shell.c's APP_MARK_PICTURE).
PICTURE = 0.68


def names():
    """The pictures' names in icons.c's order (icon_app_names), read from the source."""
    text = open(os.path.join(ROOT, 'userland/desktop/wayland/icons.c')).read()
    start = text.index('icon_app_names[GLASS_ICON_APPS] = {')
    end = text.index('};', start)
    return [part.strip().strip('"') for part in text[start:end].split('{', 1)[1].split(',') if part.strip()]


def first_app():
    """GLASS_ICON_FIRST_APP's number, counted from icons.h's enum."""
    text = open(os.path.join(ROOT, 'userland/desktop/wayland/icons.h')).read()
    body = text[text.index('enum glass_icon {'):text.index('GLASS_ICON_COUNT')]
    entries = [line.strip().rstrip(',') for line in body.splitlines()[1:] if line.strip().startswith('GLASS_ICON_')]
    return entries.index('GLASS_ICON_APP_FILES')


def tile(coverage, colour, side):
    """One icon: the coloured circle and the white picture, side pixels, drawn 4 times larger and reduced."""
    big = side * 4
    image = Image.new('RGBA', (big, big), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)
    rgb = ((colour >> 16) & 255, (colour >> 8) & 255, colour & 255)
    draw.ellipse((0, 0, big - 1, big - 1), fill=rgb + (255,))
    if coverage is not None:
        picture = int(big * PICTURE)
        mask = coverage.resize((picture, picture), Image.LANCZOS)
        white = Image.new('RGBA', (picture, picture), (255, 255, 255, 255))
        offset = (big - picture) // 2
        image.paste(white, (offset, offset), mask)
    return image.resize((side, side), Image.LANCZOS)


def main():
    dump, out = sys.argv[1], sys.argv[2]
    order = names()
    base = first_app()
    font = ImageFont.truetype(FONT, 13)
    sizes = [26, 32, 64, 112]
    cell = 132
    width = cell * len(APPS) // 2 + 40
    rows = 2
    band = 40 + sum(sizes) + 30 * len(sizes) + 24
    sheet = Image.new('RGB', (width, band * 2 * rows), (0, 0, 0))
    draw = ImageDraw.Draw(sheet)
    for ground_index, ground in enumerate([(236, 240, 246), (28, 31, 38)]):
        ink = (40, 44, 52) if ground_index == 0 else (225, 228, 235)
        top = ground_index * band * rows
        draw.rectangle((0, top, width, top + band * rows), fill=ground)
        for index, (name, picture, colour) in enumerate(APPS):
            row, column = divmod(index, len(APPS) // 2)
            x = 20 + column * cell
            y = top + row * band + 14
            coverage = None
            if picture in order:
                coverage = Image.open(os.path.join(dump, '%02d.pgm' % (base + order.index(picture)))).convert('L')
            for side in sizes:
                icon = tile(coverage, colour, side)
                sheet.paste(icon, (x + (cell - 20 - side) // 2, y), icon)
                y += side + 30 - 18
            text = name if coverage is not None else name + ' (letter)'
            w = draw.textlength(text, font=font)
            draw.text((x + (cell - 20 - w) / 2, y + 2), text, fill=ink, font=font)
    sheet.save(out)


if __name__ == '__main__':
    main()
