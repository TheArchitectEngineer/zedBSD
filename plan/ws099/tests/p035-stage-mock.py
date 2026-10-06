#!/usr/bin/env python3
# ws099-p035a: host mock of App Home's stage (BUG-236, the design of plan/ws099/phase035 §1) in three strengths of
# the spotlights and the reflections, for the user to choose, before the stage is built.  The tiles are the
# compositor's own pixels (icons.c zwl_icon_tile at 72 px, tile-dump); the stage is drawn here as the compositor
# would draw it with its shapes: the blurred desktop under dark glass (black, 0.82) a little lighter in the top's
# middle, then for each row of tiles a glossy floor line under them, a soft elliptic spotlight on the floor behind
# each tile, the tile, its reflection (the tile upside down under the floor, fading out downwards) and its name in
# white.  The tile under the pointer (Settings) has its light stronger.
#   p035-stage-mock.py DUMP_DIR OUT_DIR
# writes OUT_DIR/stage-a.png, stage-b.png, stage-c.png (1280x800 each) and OUT_DIR/montage.png (the three side by
# side at half size, with their numbers).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
import importlib.util
import os
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..', '..'))
FONT = os.path.join(ROOT, 'userland/desktop/fonts/Inter.ttf')

# The tiles, the wallpaper and App Home's list as ws128's host pictures read them.
SPEC = importlib.util.spec_from_file_location('tile_screens', os.path.join(ROOT, 'plan/ws128/tests/tile-screens.py'))
TILES = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(TILES)

WIDTH = 1280
HEIGHT = 800
BAR = 44
ICON = 72                 # home.c HOME_ICON
CELL_W = 144              # HOME_CELL_WIDTH
CELL_H = 152              # HOME_CELL_HEIGHT
COLUMNS = 6               # HOME_COLUMNS
FLOOR_GAP = 6             # the floor line this far under a tile's bottom
REFLECTION = 0.35         # the reflection's height, of the tile's
LIT = 2                   # the tile under the pointer (Settings)

# The three strengths: the spotlight on the floor (and lit), the reflection's opacity at the floor, the floor line's.
VARIANTS = {
    'a': {'title': 'A  weak: spotlight 0.08 (lit 0.16), reflection 0.15, floor 0.10',
          'spot': 0.08, 'lit': 0.16, 'reflection': 0.15, 'floor': 0.10, 'beam': 0.0},
    'b': {'title': 'B  medium (the design): spotlight 0.12 (lit 0.22), reflection 0.25, floor 0.18',
          'spot': 0.12, 'lit': 0.22, 'reflection': 0.25, 'floor': 0.18, 'beam': 0.0},
    'c': {'title': 'C  strong: spotlight 0.18 (lit 0.30), reflection 0.38, floor 0.28, a beam from above',
          'spot': 0.18, 'lit': 0.30, 'reflection': 0.38, 'floor': 0.28, 'beam': 0.07},
}


def desktop():
    """The desktop under App Home: the wallpaper and a floating window, blurred as the glass's backdrop is."""
    image = TILES.wallpaper()
    draw = ImageDraw.Draw(image)
    draw.rounded_rectangle((300, 150, 980, 640), radius=12, fill=(236, 240, 246))
    draw.rectangle((300, 150, 980, 196), fill=(250, 251, 253))
    blurred = image.filter(ImageFilter.GaussianBlur(28))
    return np.asarray(blurred, dtype=np.float32) / 255.0


def stage_ground():
    """The dark stage: the blurred desktop under black glass (0.82), lighter in the top's middle (as the bar)."""
    ground = desktop() * (1.0 - 0.82)
    ys, xs = np.mgrid[0:HEIGHT, 0:WIDTH].astype(np.float32)
    distance = np.sqrt(((xs - WIDTH / 2) / (WIDTH * 0.55)) ** 2 + ((ys - BAR) / (HEIGHT * 0.75)) ** 2)
    light = np.clip(1.0 - distance, 0.0, 1.0) ** 1.6 * 0.10
    return ground + light[..., None] * np.array([0.80, 0.86, 1.0], dtype=np.float32)


def add_light(ground, mask, amount, colour=(1.0, 1.0, 1.0)):
    """Adds white light (premultiplied over with the mask as coverage)."""
    cover = mask[..., None] * amount
    ground[:] = np.array(colour, dtype=np.float32) * cover + ground * (1.0 - cover)


def soft_ellipse(cx, cy, rx, ry, soft):
    """A soft elliptic mask over the whole screen."""
    mask = Image.new('L', (WIDTH, HEIGHT), 0)
    ImageDraw.Draw(mask).ellipse((cx - rx, cy - ry, cx + rx, cy + ry), fill=255)
    mask = mask.filter(ImageFilter.GaussianBlur(soft))
    return np.asarray(mask, dtype=np.float32) / 255.0


def beam(cx, top, bottom, half_top, half_bottom):
    """A soft cone of light from above onto a tile (variant C)."""
    mask = Image.new('L', (WIDTH, HEIGHT), 0)
    ImageDraw.Draw(mask).polygon([(cx - half_top, top), (cx + half_top, top), (cx + half_bottom, bottom),
                                  (cx - half_bottom, bottom)], fill=255)
    mask = mask.filter(ImageFilter.GaussianBlur(10))
    array = np.asarray(mask, dtype=np.float32) / 255.0
    ys = np.arange(HEIGHT, dtype=np.float32)[:, None]
    fade = np.clip((ys - top) / max(bottom - top, 1), 0.0, 1.0)
    return array * fade


def floor_line(y, left, right, amount):
    """The glossy floor under a row: a thin bright band fading out towards both ends."""
    mask = np.zeros((HEIGHT, WIDTH), dtype=np.float32)
    xs = np.arange(WIDTH, dtype=np.float32)
    middle = (left + right) / 2.0
    half = (right - left) / 2.0
    along = np.clip(1.0 - np.abs(xs - middle) / half, 0.0, 1.0) ** 0.8
    for offset, weight in ((0, 1.0), (1, 0.55), (-1, 0.35), (2, 0.2)):
        mask[y + offset] = np.maximum(mask[y + offset], along * weight)
    return mask * amount


def reflection(ground, picture, x, floor, amount):
    """The tile upside down under the floor, its opacity amount at the floor fading to 0 at REFLECTION of its height."""
    height = int(ICON * REFLECTION)
    flipped = picture[::-1][:height].copy()
    fade = np.linspace(1.0, 0.0, height, dtype=np.float32)[:, None, None]
    flipped = flipped * fade * amount
    region = ground[floor:floor + height, x:x + ICON]
    region[:] = flipped[..., :3] + region * (1.0 - flipped[..., 3:4])


def stage(dump, variant):
    """App Home on the stage in one strength."""
    settings = VARIANTS[variant]
    order = TILES.names()
    base = TILES.first_app()
    ground = stage_ground()
    listed = TILES.apps()
    columns = min(len(listed), COLUMNS)
    rows = (len(listed) + columns - 1) // columns
    left = (WIDTH - columns * CELL_W) // 2
    top = BAR + (HEIGHT - BAR - rows * CELL_H) * 2 // 5
    places = []
    for row in range(rows):
        y = top + row * CELL_H + 20
        floor = y + ICON + FLOOR_GAP
        count = min(columns, len(listed) - row * columns)
        row_left = left + 12
        row_right = left + count * CELL_W - 12
        add_light(ground, floor_line(floor, row_left, row_right, 1.0), settings['floor'], (0.86, 0.90, 1.0))
        for column in range(count):
            slot = row * columns + column
            name, picture = listed[slot]
            x = left + column * CELL_W + (CELL_W - ICON) // 2
            strength = settings['lit'] if slot == LIT else settings['spot']
            if settings['beam'] > 0.0:
                add_light(ground, beam(x + ICON / 2, BAR, floor, 10, ICON * 0.8), settings['beam'] * (1.6 if slot == LIT else 1.0))
            add_light(ground, soft_ellipse(x + ICON / 2, floor + 8, ICON * 0.8, 18, 8), strength)
            if picture in order:
                art = TILES.tile(dump, base + order.index(picture), ICON)
                TILES.over(ground, art, x, y)
                reflection(ground, art, x, floor + 2, settings['reflection'])
            places.append((x, floor, name))
    image = TILES.to_image(ground)
    draw = ImageDraw.Draw(image)
    font = ImageFont.truetype(FONT, 15)
    for x, floor, name in places:
        width = draw.textlength(name, font=font)
        draw.text((x + ICON / 2 - width / 2, floor + int(ICON * REFLECTION) + 18), name, fill=(242, 244, 248), font=font, anchor='ls')
    draw.text((20, HEIGHT - 16), settings['title'] + '  (host mock, tiles from icons.c at 72 px; Settings lit)',
              fill=(200, 206, 218), font=ImageFont.truetype(FONT, 12), anchor='ls')
    return image


def main():
    dump, out = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)
    pictures = []
    for variant in ('a', 'b', 'c'):
        image = stage(dump, variant)
        image.save(os.path.join(out, 'stage-%s.png' % variant))
        pictures.append(image)
    half = [picture.resize((WIDTH // 2, HEIGHT // 2), Image.LANCZOS) for picture in pictures]
    sheet = Image.new('RGB', (3 * WIDTH // 2 + 40, HEIGHT // 2 + 56), (24, 26, 32))
    draw = ImageDraw.Draw(sheet)
    font = ImageFont.truetype(FONT, 20)
    for index, picture in enumerate(half):
        x = 10 + index * (WIDTH // 2 + 10)
        sheet.paste(picture, (x, 46))
        draw.text((x + 4, 32), 'ABC'[index], fill=(240, 240, 240), font=font, anchor='ls')
    sheet.save(os.path.join(out, 'montage.png'))
    print('p035-stage-mock: %s' % ' '.join(os.path.join(out, name) for name in ('montage.png', 'stage-a.png', 'stage-b.png', 'stage-c.png')))


if __name__ == '__main__':
    main()
