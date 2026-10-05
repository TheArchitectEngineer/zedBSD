#!/usr/bin/env python3
# BUG-237: host picture and check of what an application tile's cut-out picture shows.
# The tiles are the compositor's own pixels (tile-dump, icons.c zwl_icon_tile); the grounds are drawn with the panel
# shader's formulas (shaders/panel.frag: MODE_GLASS with the luma lift and the sheen, MODE_SOLID, MODE_IMAGE) and its
# blend (premultiplied over), in the order the compositor draws them:
#   App Home: white glass 0.48 over the blurred wallpaper, the blue tint 0.22, then each tile (home.c);
#   the light system bar: white glass 0.55 (shell.c), the tiles 28 px; Alt+Tab: white glass 0.66 (switcher-shell.c), 48 px.
# "before" is main's GLASS_HOLE_GROUND (the hole shows the ground drawn under the tile), "after" is GLASS_HOLE_SCENE
# (glass.c first draws the blurred scene as it is, MODE_GLASS of no colour, flat, inset 0.08 of the side, then the tile).
# The blurred wallpaper is approximated (a quarter size, Gaussian, scaled back); both cases use the same one.
# Checks: inside every cut-out (tile alpha 0) the "after" pixel equals the blurred scene there, and the "before" pixel
# equals the ground; outside the inset (the tile's rim) "after" equals "before".
#   hole-host.py DUMP_DIR OUT.png
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
import os
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = os.path.normpath(os.path.join(os.path.dirname(__file__), '..', '..', '..'))
FONT = os.path.join(ROOT, 'userland/desktop/fonts/Inter.ttf')
WALLPAPERS = os.path.join(ROOT, 'userland/desktop/keiland/wallpapers')
WIDTH = 1280
HEIGHT = 800
BAR = 44
LUMA = np.array([0.2126, 0.7152, 0.0722], dtype=np.float64)
TILE_RADIUS = 0.24
SCENE_INSET = 0.08


def names():
    text = open(os.path.join(ROOT, 'userland/desktop/wayland/icons.c')).read()
    start = text.index('icon_app_names[GLASS_ICON_APPS] = {')
    end = text.index('};', start)
    return [part.strip().strip('"') for part in text[start:end].split('{', 1)[1].split(',') if part.strip()]


def first_app():
    text = open(os.path.join(ROOT, 'userland/desktop/wayland/icons.h')).read()
    body = text[text.index('enum glass_icon {'):text.index('GLASS_ICON_COUNT')]
    entries = [line.strip().rstrip(',') for line in body.splitlines()[1:] if line.strip().startswith('GLASS_ICON_')]
    return entries.index('GLASS_ICON_APP_FILES')


def tile(dump, number, size):
    raw = np.fromfile(os.path.join(dump, '%02d-%d.rgba' % (number, size)), dtype=np.uint8)
    return raw.reshape(size, size, 4).astype(np.float64) / 255.0


def wallpaper(name):
    image = Image.open(os.path.join(WALLPAPERS, name)).convert('RGB')
    scale = max(WIDTH / image.width, HEIGHT / image.height)
    image = image.resize((round(image.width * scale), round(image.height * scale)), Image.LANCZOS)
    left = (image.width - WIDTH) // 2
    top = (image.height - HEIGHT) // 2
    return image.crop((left, top, left + WIDTH, top + HEIGHT))


def blurred(image):
    """The blurred scene the glass samples (glass.c: a quarter size, blurred, sampled smoothly)."""
    small = image.resize((WIDTH // 4, HEIGHT // 4), Image.BOX).filter(ImageFilter.GaussianBlur(6))
    return np.asarray(small.resize((WIDTH, HEIGHT), Image.BILINEAR), dtype=np.float64) / 255.0


def cover(box, radius):
    """panel.frag's rounded(): the coverage of a rounded box at each pixel centre."""
    x, y, w, h = box
    ys, xs = np.mgrid[0:HEIGHT, 0:WIDTH].astype(np.float64)
    px = xs + 0.5
    py = ys + 0.5
    cx = x + w / 2.0
    cy = y + h / 2.0
    qx = np.abs(px - cx) - w / 2.0 + radius
    qy = np.abs(py - cy) - h / 2.0 + radius
    outside = np.sqrt(np.maximum(qx, 0.0) ** 2 + np.maximum(qy, 0.0) ** 2)
    distance = outside + np.minimum(np.maximum(qx, qy), 0.0) - radius
    return np.clip(0.5 - distance, 0.0, 1.0), distance, py


def blend(target, colour, alpha):
    """Premultiplied over (ONE, ONE_MINUS_SRC_ALPHA)."""
    target[:] = colour + target * (1.0 - alpha[..., None])


def glass(target, under, box, colour, radius=0.0, flat=0.0, edge=0.0, opacity=1.0):
    """MODE_GLASS in the light appearance."""
    c, distance, py = cover(box, radius)
    g = under * (1.0 - colour[3]) + np.array(colour[:3]) * colour[3]
    luma = (g * LUMA).sum(axis=2)
    white = 1.0 if min(colour[:3]) >= 0.9 else 0.0
    lift = white * np.clip((0.85 - luma) / np.maximum(1.0 - luma, 0.001), 0.0, 1.0)
    g = g * (1.0 - lift[..., None]) + lift[..., None]
    depth = np.clip((py - box[1]) / max(box[3], 1.0), 0.0, 1.0)
    rim = np.clip(1.0 - np.abs(distance + 1.0), 0.0, 1.0)
    g = g + 0.05 * (1.0 - depth[..., None]) * (1.0 - min(max(flat, 0.0), 1.0))
    g = g * (1.0 - rim[..., None] * edge) + rim[..., None] * edge
    a = c * opacity
    blend(target, g * a[..., None], a)


def solid(target, box, colour, radius=0.0):
    c, _, _ = cover(box, radius)
    a = c * colour[3]
    blend(target, np.array(colour[:3]) * a[..., None], a)


def put_tile(target, under, picture, x, y, scene):
    """glass_draw_app_tile at its own size: the scene window (GLASS_HOLE_SCENE), then the tile's image."""
    size = picture.shape[0]
    if scene:
        inset = size * SCENE_INSET
        glass(target, under, (x + inset, y + inset, size - 2 * inset, size - 2 * inset), (0.0, 0.0, 0.0, 0.0),
              radius=size * TILE_RADIUS - inset, flat=1.0)
    region = target[y:y + size, x:x + size]
    region[:] = picture[..., :3] + region * (1.0 - picture[..., 3:4])


def home_places(count):
    columns = min(count, 6)
    rows = (count + columns - 1) // columns
    left = (WIDTH - columns * 144) // 2
    top = BAR + (HEIGHT - BAR - rows * 152) * 2 // 5
    return [(left + (slot % columns) * 144 + 36, top + (slot // columns) * 152 + 20) for slot in range(count)]


def check(label, before, after, under, ground, places, pictures):
    """Compares the cut-out and the rim pixels; returns a line of the result."""
    worst_scene = 0.0
    worst_ground = 0.0
    worst_rim = 0.0
    holes = 0
    for (x, y), picture in zip(places, pictures):
        size = picture.shape[0]
        hole = picture[..., 3] == 0.0
        inset = int(np.ceil(size * SCENE_INSET)) + 1
        inner = np.zeros_like(hole)
        inner[inset:size - inset, inset:size - inset] = True
        hole = hole & inner
        holes += int(hole.sum())
        a = after[y:y + size, x:x + size][hole]
        b = before[y:y + size, x:x + size][hole]
        worst_scene = max(worst_scene, float(np.abs(a - under[y:y + size, x:x + size][hole]).max()) * 255)
        worst_ground = max(worst_ground, float(np.abs(b - ground[y:y + size, x:x + size][hole]).max()) * 255)
        rim = np.ones_like(hole)
        rim[inset:size - inset, inset:size - inset] = False
        worst_rim = max(worst_rim, float(np.abs(after[y:y + size, x:x + size][rim] - before[y:y + size, x:x + size][rim]).max()) * 255)
    ok = worst_scene < 0.5 and worst_ground < 0.5 and worst_rim < 0.5
    line = '%s: %s holes=%d after-scene=%.3f before-ground=%.3f rim=%.3f (of 255)' % (
        label, 'PASS' if ok else 'FAIL', holes, worst_scene, worst_ground, worst_rim)
    return ok, line


def to_image(target):
    return Image.fromarray((np.clip(target, 0.0, 1.0) * 255 + 0.5).astype(np.uint8))


def home(dump, wallpaper_name, scene):
    order = names()
    base = first_app()
    under = blurred(wallpaper(wallpaper_name))
    target = np.zeros((HEIGHT, WIDTH, 3))
    glass(target, under, (0, 0, WIDTH, HEIGHT), (1.0, 1.0, 1.0, 0.48))
    solid(target, (0, 0, WIDTH, HEIGHT), (0.86, 0.92, 1.0, 0.22))
    ground = target.copy()
    pictures = [tile(dump, base + index, 72) for index in range(len(order))]
    places = home_places(len(order))
    for (x, y), picture in zip(places, pictures):
        put_tile(target, under, picture, x, y, scene)
    return target, under, ground, places, pictures


def strip(dump, wallpaper_name, scene):
    """The light bar's 28-pixel tiles and Alt+Tab's panel with 48-pixel tiles."""
    order = names()
    base = first_app()
    image = wallpaper(wallpaper_name)
    under = blurred(image)
    target = np.asarray(image, dtype=np.float64) / 255.0
    glass(target, under, (0, 0, WIDTH, BAR), (1.0, 1.0, 1.0, 0.55))
    solid(target, (0, BAR - 1, WIDTH, 1), (1.0, 1.0, 1.0, 0.55))
    glass(target, under, (200, 120, 8 * 72 + 24, 96), (1.0, 1.0, 1.0, 0.66), radius=22.0, edge=0.8)
    ground = target.copy()
    places = []
    pictures = []
    for slot in range(10):
        places.append((61 + slot * 36 + 4, BAR // 2 - 14))
        pictures.append(tile(dump, base + slot, 28))
    for slot in range(8):
        places.append((200 + 12 + slot * 72 + 12, 120 + 24))
        pictures.append(tile(dump, base + 10 + slot, 48))
    for (x, y), picture in zip(places, pictures):
        put_tile(target, under, picture, x, y, scene)
    return target, under, ground, places, pictures


def main():
    dump, out = sys.argv[1], sys.argv[2]
    font = ImageFont.truetype(FONT, 15)
    small = ImageFont.truetype(FONT, 12)
    ink = (31, 41, 61)
    lines = []
    passed = True
    panels = []
    for name, label in (('Birch-Lake.png', 'Birch Lake'), ('Lakeside.png', 'Lakeside')):
        before, under, ground, places, pictures = home(dump, name, False)
        after, _, _, _, _ = home(dump, name, True)
        ok, line = check('App Home ' + label, before, after, under, ground, places, pictures)
        passed = passed and ok
        lines.append(line)
        panels.append(('App Home, %s' % label, to_image(before).crop((200, 140, 1080, 620)),
                       to_image(after).crop((200, 140, 1080, 620))))
        before, under, ground, places, pictures = strip(dump, name, False)
        after, _, _, _, _ = strip(dump, name, True)
        ok, line = check('bar and Alt+Tab ' + label, before, after, under, ground, places, pictures)
        passed = passed and ok
        lines.append(line)
        panels.append(('light bar (28 px) and Alt+Tab (48 px), %s' % label, to_image(before).crop((40, 0, 920, 240)),
                       to_image(after).crop((40, 0, 920, 240))))
    sheet_height = 70 + sum(panel[1].height + 40 for panel in panels) + 20 * len(lines)
    sheet = Image.new('RGB', (40 + 880 * 2 + 20, sheet_height), (246, 247, 250))
    draw = ImageDraw.Draw(sheet)
    draw.text((20, 14), 'BUG-237: what the cut-out picture shows. Left: main (the hole shows the near-white glass under the tile). '
              'Right: fixed (the hole shows the blurred scene the glass frosts).', fill=ink, font=font)
    draw.text((20, 36), 'Tiles: icons.c zwl_icon_tile pixels; grounds: panel.frag formulas and premultiplied over; '
              'blurred wallpaper approximated.', fill=ink, font=small)
    top = 70
    for title, left_image, right_image in panels:
        draw.text((20, top), title + ' - before', fill=ink, font=small)
        draw.text((40 + 880, top), title + ' - after', fill=ink, font=small)
        sheet.paste(left_image, (20, top + 18))
        sheet.paste(right_image, (40 + 880, top + 18))
        top += left_image.height + 40
    for line in lines:
        draw.text((20, top), line, fill=ink, font=small)
        top += 20
    sheet.save(out)
    for line in lines:
        print('hole-host: ' + line)
    print('hole-host: %s %s' % ('PASS' if passed else 'FAIL', out))
    return 0 if passed else 1


if __name__ == '__main__':
    sys.exit(main())
