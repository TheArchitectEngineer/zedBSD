#!/usr/bin/env python3
# ws128-p012: the two montages the user chooses between (the 2026-10-06 user answer to montage-2), both from the
# compositor's own pictures (icon-dump of userland/desktop/wayland/icons.c, the white filled shapes):
#   W: a white tile with rounded corners, the picture cut out of it, so that the ground shows through the picture;
#      shown over the dark system bar, App Home's ground and the two wallpapers.
#   B: a tile with rounded corners in diagonal bands of a quantised gradient (three flat bands), pastel-leaning but
#      vivid colours; the picture white, a few in a colour of their own.
#   icon-montage-3.py DUMP_DIR OUT_W.png OUT_B.png [OUT_4.png]
# With OUT_4.png only montage 4 is drawn (the user's choice: B's banded tile with W's cut-out picture).
# DUMP_DIR holds NN.pgm from "icon-dump 448 DUMP_DIR".
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
import os
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = os.path.normpath(os.path.join(os.path.dirname(__file__), '..', '..', '..'))
FONT = os.path.join(ROOT, 'userland/desktop/fonts/Mahora-Regular.ttf')
JAPANESE = os.path.join(ROOT, 'userland/desktop/fonts/DroidSansFallbackFull.ttf')
WALLPAPERS = os.path.join(ROOT, 'userland/desktop/wallpapers')

# The standard applications: name, picture (icon_app_names), B's two colours of the bands, B's picture colour.
WHITE = (255, 255, 255)
CREAM = (255, 246, 222)
APPS = [
    ('Files', 'files', 0xffd86b, 0xf2b53a, WHITE), ('Notes', 'notes', 0xffb38a, 0xff8a65, CREAM),
    ('Settings', 'settings', 0xb8c4d6, 0x8e9bb3, WHITE), ('Terminal', 'terminal', 0x5b6b8c, 0x3b4660, WHITE),
    ('PDF Viewer', 'pdf', 0xff8a80, 0xf0605a, WHITE), ('Image Viewer', 'image', 0x7ee0b5, 0x3cc48d, WHITE),
    ('Video Player', 'video', 0xb39dff, 0x8c6cf2, WHITE), ('Phone', 'phone', 0x7ee89a, 0x3fcb6b, WHITE),
    ('Calendar', 'calendar', 0x7fb3ff, 0x4a8bf5, WHITE), ('Mail', 'mail', 0xc49bff, 0x9c6cf0, CREAM),
    ('Text Editor', 'text', 0x9be3e0, 0x4cc4c0, WHITE), ('System Monitor', 'monitor', 0xff9ec4, 0xf06a9b, WHITE),
    ('Browser', 'browser', 0x7fd8f0, 0x3ab3d8, WHITE), ('X terminal', 'xterm', 0x8a93b8, 0x626c96, WHITE),
    ('Model Viewer', 'model', 0xffc09f, 0xf59a73, CREAM), ('Gears', 'gears', 0xe6b48a, 0xc98a55, WHITE),
    ('Lock Screen', 'lock', 0xc7cdd8, 0xa0a8b8, WHITE), ('Log Out', 'logout', 0xc7cdd8, 0xa0a8b8, WHITE),
]

# Shorter names for the labels under 64-pixel icons.
SHORT = {'PDF Viewer': 'PDF', 'Image Viewer': 'Image', 'Video Player': 'Video', 'Text Editor': 'Text',
         'System Monitor': 'Monitor', 'X terminal': 'X term', 'Model Viewer': 'Model', 'Lock Screen': 'Lock'}

# The tile's corner radius and the picture's 24-unit grid, as parts of the tile's side.
RADIUS = 0.24
PICTURE = 0.66
K = 4


def names():
    """The pictures' names in icons.c's order (icon_app_names)."""
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


def rgb(value):
    return ((value >> 16) & 255, (value >> 8) & 255, value & 255)


def picture_mask(coverage, side):
    """The picture's coverage placed in the middle of a tile of side pixels (K times larger)."""
    mask = Image.new('L', (side, side), 0)
    size = int(side * PICTURE)
    mask.paste(coverage.resize((size, size), Image.LANCZOS), ((side - size) // 2, (side - size) // 2))
    return mask


def tile_mask(side):
    mask = Image.new('L', (side, side), 0)
    ImageDraw.Draw(mask).rounded_rectangle((0, 0, side - 1, side - 1), radius=int(side * RADIUS), fill=255)
    return mask


def tile_w(coverage, side):
    """W: white, the picture cut out (transparent)."""
    big = side * K
    shape = np.asarray(tile_mask(big), dtype=np.float32)
    cut = np.asarray(picture_mask(coverage, big), dtype=np.float32)
    alpha = shape * (1.0 - cut / 255.0)
    image = Image.new('RGBA', (big, big), WHITE + (0,))
    image.putalpha(Image.fromarray(alpha.astype(np.uint8)))
    return image.resize((side, side), Image.LANCZOS)


def tile_b(coverage, side, first, second, ink):
    """B: three diagonal bands from the first colour to the second, a light stripe, the picture in its ink."""
    big = side * K
    ys, xs = np.mgrid[0:big, 0:big].astype(np.float32)
    along = (xs + (big - ys)) / (2.0 * big)            # 0 at the lower left, 1 at the upper right ("/" bands)
    band = np.clip((along * 3.0).astype(int), 0, 2)
    a = np.array(rgb(first), dtype=np.float32)
    b = np.array(rgb(second), dtype=np.float32)
    steps = [b, (a + b) / 2.0, a]
    colour = np.zeros((big, big, 3), dtype=np.float32)
    for index in range(3):
        colour[band == index] = steps[index]
    # A thin lighter stripe across the middle band, as on the reference.
    stripe = np.abs(along - 0.58) < 0.035
    colour[stripe] = colour[stripe] * 0.8 + 255.0 * 0.2
    image = Image.fromarray(colour.astype(np.uint8)).convert('RGBA')
    image.putalpha(tile_mask(big))
    solid = Image.new('RGBA', (big, big), ink + (255,))
    image.paste(solid, (0, 0), picture_mask(coverage, big))
    image.putalpha(Image.fromarray(np.minimum(np.asarray(image)[..., 3], np.asarray(tile_mask(big)))))
    return image.resize((side, side), Image.LANCZOS)


def tile_cut(coverage, side, first, second):
    """Montage 4 (the user's choice): B's tile with its diagonal bands, the picture cut out of it as in W."""
    big = side * K
    full = tile_b(coverage, side, first, second, (255, 255, 255)).resize((big, big), Image.LANCZOS)
    cut = np.asarray(picture_mask(coverage, big), dtype=np.float32) / 255.0
    pixels = np.asarray(full).copy()
    pixels[..., 3] = (pixels[..., 3].astype(np.float32) * (1.0 - cut)).astype(np.uint8)
    return Image.fromarray(pixels).resize((side, side), Image.LANCZOS)


def montage4(coverages, out):
    """Montage 4 over the dark bar, App Home, both wallpapers, a light and a dark ground, at 26, 64 and 112 pixels."""
    latin = ImageFont.truetype(FONT, 14)
    japanese = ImageFont.truetype(JAPANESE, 15)
    label = ImageFont.truetype(FONT, 12)
    width = 18 * 76 + 40
    grounds = [('暗い system bar', 'bar'), ('App Home の地', 'home'), ('壁紙 Birch Lake', 'Birch-Lake.png'),
               ('壁紙 Lakeside', 'Lakeside.png'), ('明るい地', 'light'), ('暗い地', 'dark')]
    band_height = 26 + 20 + 64 + 30 + 112 + 30
    height = 40 + len(grounds) * (26 + band_height + 10)
    sheet = Image.new('RGB', (width, height), (246, 247, 250))
    draw = ImageDraw.Draw(sheet)
    caption(draw, 20, 8, '案 4: 斜めの帯の量子化 gradient の角の丸い四角から記号を中抜き（下の地が透ける）。26・64・112 px', latin, japanese, (30, 34, 44))
    y = 36
    for text, ground in grounds:
        caption(draw, 20, y, text, latin, japanese, (30, 34, 44))
        y += 24
        if ground == 'bar':
            back = Image.new('RGB', (width - 40, band_height), (29, 33, 42))
        elif ground == 'dark':
            back = Image.new('RGB', (width - 40, band_height), (18, 20, 26))
        elif ground == 'light':
            back = Image.new('RGB', (width - 40, band_height), (246, 247, 250))
        elif ground == 'home':
            back = wallpaper('Birch-Lake.png', width - 40, band_height).filter(ImageFilter.GaussianBlur(24))
            back = Image.blend(back, Image.new('RGB', back.size, (236, 240, 246)), 0.45)
        else:
            back = wallpaper(ground, width - 40, band_height)
        sheet.paste(back, (20, y))
        ink = (220, 224, 232) if ground in ('bar', 'dark') else (40, 44, 54)
        top = y + 10
        for side in (26, 64):
            for index, (name, _, first, second, _) in enumerate(APPS):
                icon = tile_cut(coverages[index], side, first, second)
                sheet.paste(icon, (20 + 12 + index * 76 + (64 - side) // 2, top), icon)
                if side == 64:
                    short = SHORT.get(name, name)
                    w = draw.textlength(short, font=label)
                    draw.text((20 + 12 + index * 76 + 32 - w / 2, top + 66), short, fill=ink, font=label)
            top += side + 20 if side == 26 else side + 30
        for index, (name, _, first, second, _) in enumerate(APPS[:11]):
            icon = tile_cut(coverages[index], 112, first, second)
            sheet.paste(icon, (28 + index * 124, top), icon)
        y += band_height + 10
    sheet.save(out)


def wallpaper(name, width, height):
    image = Image.open(os.path.join(WALLPAPERS, name)).convert('RGB')
    scale = max(width / image.width, height / image.height)
    image = image.resize((round(image.width * scale), round(image.height * scale)), Image.LANCZOS)
    left = (image.width - width) // 2
    top = (image.height - height) // 2
    return image.crop((left, top, left + width, top + height))


def caption(draw, x, y, text, latin, japanese, fill):
    """Writes a caption, each run of ASCII in Inter and the rest in the Japanese font."""
    run = ''
    ascii_run = None
    for character in text + '\0':
        is_ascii = ord(character) < 128
        if character == '\0' or (ascii_run is not None and is_ascii != ascii_run):
            font = latin if ascii_run else japanese
            draw.text((x, y), run, font=font, fill=fill)
            x += draw.textlength(run, font=font)
            run = ''
        run += character
        ascii_run = is_ascii


def main():
    dump, out_w, out_b = sys.argv[1], sys.argv[2], sys.argv[3]
    order = names()
    base = first_app()
    coverages = [Image.open(os.path.join(dump, '%02d.pgm' % (base + order.index(p)))).convert('L') for _, p, _, _, _ in APPS]
    if len(sys.argv) > 4:
        montage4(coverages, sys.argv[4])
        return
    latin = ImageFont.truetype(FONT, 14)
    japanese = ImageFont.truetype(JAPANESE, 15)
    label = ImageFont.truetype(FONT, 12)
    width = 18 * 76 + 40

    # W over four grounds: the dark bar (26 px, as in the bar's pill), App Home's ground, and the two wallpapers (64 px).
    rows = [('暗い system bar（26 px、bar の pill の大きさ）', 'bar', 26), ('App Home の地（64 px）', 'home', 64),
            ('壁紙 Birch Lake（64 px）', 'Birch-Lake.png', 64), ('壁紙 Lakeside（64 px）', 'Lakeside.png', 64)]
    height = 20 + sum(28 + max(side, 26) + 44 + 20 for _, _, side in rows) + 28 + 112 + 40
    sheet = Image.new('RGB', (width, height), (246, 247, 250))
    draw = ImageDraw.Draw(sheet)
    caption(draw, 20, 8, '案 W: 白の角の丸い四角、記号を中抜き（下の地が透ける）', latin, japanese, (30, 34, 44))
    y = 36
    for text, ground, side in rows:
        caption(draw, 20, y, text, latin, japanese, (30, 34, 44))
        y += 26
        band_height = max(side, 26) + 40
        if ground == 'bar':
            back = Image.new('RGB', (width - 40, band_height), (29, 33, 42))
        elif ground == 'home':
            back = wallpaper('Birch-Lake.png', width - 40, band_height).filter(ImageFilter.GaussianBlur(24))
            back = Image.blend(back, Image.new('RGB', back.size, (236, 240, 246)), 0.45)
        else:
            back = wallpaper(ground, width - 40, band_height)
        sheet.paste(back, (20, y))
        for index, coverage in enumerate(coverages):
            icon = tile_w(coverage, side)
            sheet.paste(icon, (20 + 12 + index * 76 + (64 - side) // 2, y + 20 + (max(side, 26) - side) // 2), icon)
        y += band_height + 4
        for index, (name, _, _, _, _) in enumerate(APPS):
            short = SHORT.get(name, name)
            w = draw.textlength(short, font=label)
            draw.text((20 + 12 + index * 76 + 32 - w / 2, y), short, fill=(60, 64, 74), font=label)
        y += 20
    caption(draw, 20, y, '大きく（112 px、Birch Lake の上）', latin, japanese, (30, 34, 44))
    y += 26
    back = wallpaper('Birch-Lake.png', width - 40, 130)
    sheet.paste(back, (20, y))
    for index, coverage in enumerate(coverages[:11]):
        icon = tile_w(coverage, 112)
        sheet.paste(icon, (28 + index * 124, y + 9), icon)
    sheet.save(out_w)

    # B on a light and a dark ground at 26, 64 and 112 px.
    height = 40 + 2 * (26 + 30 + 64 + 30 + 112 + 50)
    sheet = Image.new('RGB', (width, height), (246, 247, 250))
    draw = ImageDraw.Draw(sheet)
    caption(draw, 20, 8, '案 B: 角の丸い四角、斜めの帯の量子化 gradient（3 段）、パステル寄りの鮮やかな色、記号は白（Notes・Mail・Model は淡いクリーム）',
            latin, japanese, (30, 34, 44))
    y = 36
    for ground, ink in (((246, 247, 250), (60, 64, 74)), ((29, 33, 42), (220, 224, 232))):
        draw.rectangle((20, y, width - 20, y + 26 + 30 + 64 + 30 + 112 + 40), fill=ground)
        top = y + 12
        for side in (26, 64):
            for index, (name, picture, first, second, colour) in enumerate(APPS):
                icon = tile_b(coverages[index], side, first, second, colour)
                sheet.paste(icon, (20 + 12 + index * 76 + (64 - side) // 2, top), icon)
                if side == 64:
                    short = SHORT.get(name, name)
                    w = draw.textlength(short, font=label)
                    draw.text((20 + 12 + index * 76 + 32 - w / 2, top + 66), short, fill=ink, font=label)
            top += side + 30
        for index, (name, picture, first, second, colour) in enumerate(APPS[:11]):
            icon = tile_b(coverages[index], 112, first, second, colour)
            sheet.paste(icon, (28 + index * 124, top - 10), icon)
        y = top + 112 + 30
    sheet.save(out_b)


if __name__ == '__main__':
    main()
