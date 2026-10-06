#!/usr/bin/env python3
# ws128-p012: host pictures of the applications' tiles as the compositor places them, the tiles themselves drawn by
# the compositor's own icons.c (tile-dump, raw premultiplied RGBA at the sizes glass.c keeps: 20, 28, 48, 72):
#   OUT_HOME.png: App Home (home.c's grid: 6 columns, cells 144x152, tiles 72, labels 26 under) over the whitened
#                 blurred wallpaper, the system bar on top; one tile lit as under the pointer (home.c's HOME_LIT).
#   OUT_BAR.png:  the desktop with the system bar's applications (apps-bar.c: from x 61, 36 apart, marks 28 at the
#                 bar's middle), a window's title bar with its 20-pixel mark, Alt+Tab's 48-pixel marks, and the bar
#                 three times larger.
# The grounds (glass, blur, wallpaper) are approximations of the shader's; the tiles are the compositor's pixels,
# composited premultiplied "over" as the panel shader's MODE_IMAGE does.
#   tile-screens.py DUMP_DIR OUT_HOME.png OUT_BAR.png
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
import os
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = os.path.normpath(os.path.join(os.path.dirname(__file__), '..', '..', '..'))
FONT = os.path.join(ROOT, 'userland/desktop/fonts/Inter.ttf')
WALLPAPER = os.path.join(ROOT, 'userland/desktop/keiland/wallpapers/Birch-Lake.png')
APPS_CONF = os.path.join(ROOT, 'userland/desktop/wayland/apps.conf')

WIDTH = 1280
HEIGHT = 800
BAR = 44
INK = (31, 41, 61)


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


def apps():
    """App Home's applications from apps.conf: name and picture."""
    result = []
    for line in open(APPS_CONF):
        line = line.strip()
        if not line or line.startswith('#'):
            continue
        fields = line.split('|')
        result.append((fields[0], fields[4] if len(fields) > 4 else ''))
    return result


def tile(dump, number, size):
    """One tile as a float premultiplied RGBA array (0..1)."""
    raw = np.fromfile(os.path.join(dump, '%02d-%d.rgba' % (number, size)), dtype=np.uint8)
    return raw.reshape(size, size, 4).astype(np.float32) / 255.0


def over(ground, picture, x, y, opacity=1.0):
    """Premultiplied "over" of a tile onto a float RGB ground at (x, y)."""
    h, w = picture.shape[:2]
    region = ground[y:y + h, x:x + w]
    alpha = picture[..., 3:4] * opacity
    region[:] = picture[..., :3] * opacity + region * (1.0 - alpha)


def lighten(ground, picture, x, y, amount):
    """White over the tile as much as it covers (glass_draw_app_tile's lighten, MODE_TEXT on the tile's alpha)."""
    h, w = picture.shape[:2]
    region = ground[y:y + h, x:x + w]
    cover = picture[..., 3:4] * amount
    region[:] = cover + region * (1.0 - cover)


def wallpaper():
    image = Image.open(WALLPAPER).convert('RGB')
    scale = max(WIDTH / image.width, HEIGHT / image.height)
    image = image.resize((round(image.width * scale), round(image.height * scale)), Image.LANCZOS)
    left = (image.width - WIDTH) // 2
    top = (image.height - HEIGHT) // 2
    return image.crop((left, top, left + WIDTH, top + HEIGHT))


def glass(ground, x0, y0, x1, y1, whiten):
    """The glass shader's white glass, roughly: the blurred ground mixed with white, at least a luma of 0.85."""
    blurred = np.asarray(Image.fromarray((ground * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(24)),
                         dtype=np.float32) / 255.0
    part = blurred[y0:y1, x0:x1] * (1.0 - whiten) + whiten
    luma = (part * np.array([0.2126, 0.7152, 0.0722])).sum(axis=2, keepdims=True)
    lift = np.clip((0.85 - luma) / np.maximum(1.0 - luma, 0.001), 0.0, 1.0)
    ground[y0:y1, x0:x1] = part * (1.0 - lift) + lift


def bar(ground):
    """The system bar's strip of white glass and its light line."""
    glass(ground, 0, 0, WIDTH, BAR, 0.55)
    ground[BAR - 1, :] = ground[BAR - 1, :] * 0.85


def to_image(ground):
    return Image.fromarray((np.clip(ground, 0.0, 1.0) * 255 + 0.5).astype(np.uint8))


def home(dump, out):
    """App Home as home.c lays it out (one page of 14 applications)."""
    order = names()
    base = first_app()
    ground = np.asarray(wallpaper(), dtype=np.float32) / 255.0
    glass(ground, 0, 0, WIDTH, HEIGHT, 0.48)
    ground = ground * (1.0 - 0.22) + np.array([0.86, 0.92, 1.0]) * 0.22
    bar(ground)
    listed = apps()
    columns = min(len(listed), 6)
    rows = (len(listed) + columns - 1) // columns
    left = (WIDTH - columns * 144) // 2
    top = BAR + (HEIGHT - BAR - rows * 152) * 2 // 5
    places = []
    for slot, (name, picture) in enumerate(listed):
        x = left + (slot % columns) * 144 + (144 - 72) // 2
        y = top + (slot // columns) * 152 + 20
        places.append((x, y, name))
        if picture in order:
            number = base + order.index(picture)
            over(ground, tile(dump, number, 72), x, y)
            if slot == 2:
                lighten(ground, tile(dump, number, 72), x, y, 0.15)
    image = to_image(ground)
    draw = ImageDraw.Draw(image)
    font = ImageFont.truetype(FONT, 15)
    for x, y, name in places:
        width = draw.textlength(name, font=font)
        draw.text((x + 36 - width / 2, y + 72 + 26), name, fill=INK, font=font, anchor='ls')
    draw.text((20, HEIGHT - 16), 'App Home (host picture, tiles from icons.c kwl_icon_tile at 72 px; Settings lit as under the pointer)',
              fill=INK, font=ImageFont.truetype(FONT, 12), anchor='ls')
    image.save(out)


def desktop(dump, out):
    """The bar's applications, a title bar's mark and Alt+Tab, then the bar three times larger."""
    order = names()
    base = first_app()
    ground = np.asarray(wallpaper(), dtype=np.float32) / 255.0
    bar(ground)
    shown = ['files', 'terminal', 'browser', 'notes', 'calendar', 'phone', 'mail', 'settings', 'monitor', 'video']
    for slot, picture in enumerate(shown):
        x = 61 + slot * 36 + (36 - 28) // 2
        over(ground, tile(dump, base + order.index(picture), 28), x, BAR // 2 - 14,
             0.45 if picture == 'video' else 1.0)
    # A window's title bar (white glass) with its mark, 20 pixels, as draw_picture_mark.
    glass(ground, 200, 160, 760, 204, 0.62)
    over(ground, tile(dump, base + order.index('files'), 20), 214, 182 - 10)
    # Alt+Tab's marks, 48 pixels on the switcher's glass.
    glass(ground, 360, 330, 360 + 8 * 72 + 24, 330 + 96, 0.55)
    for slot, picture in enumerate(['files', 'terminal', 'browser', 'calendar', 'phone', 'mail', 'image', 'pdf']):
        over(ground, tile(dump, base + order.index(picture), 48), 360 + 12 + slot * 72 + 12, 330 + 24)
    image = to_image(ground)
    draw = ImageDraw.Draw(image)
    font = ImageFont.truetype(FONT, 15)
    draw.text((244, 188), 'Files', fill=INK, font=font)
    small = ImageFont.truetype(FONT, 12)
    draw.text((200, 150), 'title bar mark (20 px)', fill=(255, 255, 255), font=small)
    draw.text((360, 320), 'Alt+Tab (48 px)', fill=(255, 255, 255), font=small)
    draw.text((61, 52), 'bar applications (28 px; Video minimized, faint)', fill=(255, 255, 255), font=small)
    zoom = image.crop((0, 0, 460, BAR)).resize((460 * 3, BAR * 3), Image.NEAREST)
    sheet = Image.new('RGB', (WIDTH, HEIGHT + BAR * 3 + 30), (246, 247, 250))
    sheet.paste(image, (0, 0))
    sheet.paste(zoom, (0, HEIGHT + 24))
    ImageDraw.Draw(sheet).text((8, HEIGHT + 4), 'the bar, 3x (nearest neighbour)', fill=INK, font=small)
    sheet.save(out)


def main():
    dump, out_home, out_bar = sys.argv[1], sys.argv[2], sys.argv[3]
    home(dump, out_home)
    desktop(dump, out_bar)


if __name__ == '__main__':
    main()
