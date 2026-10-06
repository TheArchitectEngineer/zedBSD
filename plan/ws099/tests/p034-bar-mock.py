#!/usr/bin/env python3
# ws099-p034: the mock of the system bar's new design (the 2026-10-06 user decisions), drawn without QEMU before it is
# built: dark glass over the blurred wallpaper, a little lighter in the middle; the Kei mark, a line and the open
# applications' icons in one pill on the left; the virtual desktops' dots in a pill in the middle (the one shown an
# outlined larger pill, no change for windows); the status icons in a pill and the clock in a pill of its own on the
# right.  The application pictures and the Kei mark are the compositor's own rasters (icon-dump, p034-mark-dump);
# the rest is drawn here as the compositor would draw it with its shapes (solid, ring, glass).
#   p034-bar-mock.py ICON_DIR OUT.png
# ICON_DIR holds NN.pgm from plan/ws128/tests/icon-dump (448 pixels) and mark-N.pgm from p034-mark-dump (104).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
import math
import os
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = os.path.normpath(os.path.join(os.path.dirname(__file__), '..', '..', '..'))
INTER = os.path.join(ROOT, 'userland/desktop/fonts/Inter.ttf')
JAPANESE = os.path.join(ROOT, 'userland/desktop/fonts/DroidSansFallbackFull.ttf')
WALLPAPERS = os.path.join(ROOT, 'userland/desktop/keiland/wallpapers')

W = 1280
H = 44                   # KWL_GLASS_BAR (ws099-p031)
MID = H // 2
K = 4                    # drawn 4 times larger, then reduced (smooth edges, as the shader's coverage)
CONTEXT = 70             # wallpaper shown under the bar

# glass.c's bar colours of the Kei mark (layers bar, bar shade, leaf, leaf shade, overlap, rim, sheen).
MARK = [(0.455, 0.600, 0.925, 0.92), (0.290, 0.451, 0.878, 0.55), (0.400, 0.690, 0.945, 0.90),
        (0.145, 0.408, 0.890, 0.90), (0.106, 0.349, 0.839, 0.96), (1, 1, 1, 0.50), (1, 1, 1, 0.18)]

# The applications (icons.h order from GLASS_ICON_APP_FILES = 21) and their colours (icons.c's icon_app_ids).
APP = {'files': (21, 0x2f7cf6), 'notes': (22, 0xe0a526), 'terminal': (23, 0x323a4e), 'pdf': (24, 0xd9534f),
       'image': (25, 0x3fa36b), 'browser': (26, 0x3a8fd8), 'text': (32, 0x1f9e9a), 'settings': (33, 0x6b7a8f),
       'video': (34, 0x7a4fd0), 'phone': (35, 0x34c759), 'calendar': (36, 0xe8483f), 'mail': (37, 0x2f6fd6),
       'monitor': (38, 0x4a6a8f)}
ICON_BACK, ICON_FORWARD, ICON_HOME, ICON_SEARCH, ICON_VOLUME_2, ICON_USB = 0, 1, 3, 4, 17, 20


class Look:
    """The colours of one appearance of the bar."""

    def __init__(self, dark):
        self.dark = dark
        if dark:
            self.tint = (18, 24, 36)
            self.amount = 0.64
            self.most = 0.22             # the brightest the dark glass may be (luma), for the white text
            self.lift = 0.085            # the lighter middle
            self.ink = (255, 255, 255, 240)
            self.faint = (255, 255, 255, 110)
            self.pill = (0, 0, 0, 62)
            self.pill_edge = (255, 255, 255, 30)
            self.line = (255, 255, 255, 46)
            self.dot = (255, 255, 255, 105)
            self.hairline = (255, 255, 255, 22)
        else:
            self.tint = (255, 255, 255)
            self.amount = 0.58
            self.most = 1.0
            self.lift = 0.0
            self.ink = (30, 38, 52, 240)
            self.faint = (30, 38, 52, 110)
            self.pill = (20, 30, 50, 22)
            self.pill_edge = (255, 255, 255, 120)
            self.line = (30, 40, 60, 40)
            self.dot = (30, 40, 60, 80)
            self.hairline = (255, 255, 255, 140)


def wallpaper(name):
    """The wallpaper as the compositor shows it on a 1280x800 output: scaled to cover, centred."""
    image = Image.open(os.path.join(WALLPAPERS, name)).convert('RGB')
    scale = max(W / image.width, 800 / image.height)
    image = image.resize((round(image.width * scale), round(image.height * scale)), Image.LANCZOS)
    left = (image.width - W) // 2
    top = (image.height - 800) // 2
    return image.crop((left, top, left + W, top + 800))


def glass(under, look):
    """The bar's strip: the blurred scene under it, tinted, lighter in the middle, a sheen from the top."""
    blurred = np.asarray(under.filter(ImageFilter.GaussianBlur(18)), dtype=np.float32) / 255.0
    strip = blurred[:H].copy()
    tint = np.array(look.tint, dtype=np.float32) / 255.0
    strip = strip * (1 - look.amount) + tint * look.amount
    luma = strip @ np.array([0.2126, 0.7152, 0.0722], dtype=np.float32)
    if look.dark:
        # As the shader's dark glass (GLASS_MOST_LUMA): over a bright wallpaper darkened further, to keep the light
        # text's contrast.
        shade = np.clip((luma - look.most) / np.maximum(luma, 1e-3), 0, 1)[..., None]
        strip = strip * (1 - shade)
    else:
        lift = np.clip((0.85 - luma) / np.maximum(1 - luma, 1e-3), 0, 1)[..., None]
        strip = strip + (1 - strip) * lift
    x = np.arange(W, dtype=np.float32)
    middle = look.lift * np.exp(-((x - W / 2) / (0.34 * W)) ** 2)
    y = np.arange(H, dtype=np.float32)
    sheen = 0.035 * (1 - y / H)
    strip = strip + middle[None, :, None] + sheen[:, None, None]
    return np.clip(strip, 0, 1)


def coverage(directory, name, side):
    """A raster from the dumps, reduced to side pixels (already K times larger)."""
    return Image.open(os.path.join(directory, name)).convert('L').resize((side, side), Image.LANCZOS)


def paint(layer, mask, colour, x, y):
    """Puts a colour through a coverage mask at (x, y) of a K-times layer."""
    solid = Image.new('RGBA', mask.size, colour[:3] + (255,))
    alpha = mask.point(lambda value: value * colour[3] // 255)
    solid.putalpha(alpha)
    layer.alpha_composite(solid, (int(x), int(y)))


def rounded(draw, x, y, w, h, radius, fill=None, outline=None, width=1):
    """A rounded rectangle in bar pixels (drawn K times larger)."""
    draw.rounded_rectangle((x * K, y * K, (x + w) * K - 1, (y + h) * K - 1), radius=radius * K, fill=fill,
                           outline=outline, width=int(width * K))


def pill(draw, look, x, w, h=34):
    """A group's pill: darker than the bar, a faint light edge."""
    y = MID - h / 2
    rounded(draw, x, y, w, h, h / 2, fill=look.pill)
    rounded(draw, x, y, w, h, h / 2, outline=look.pill_edge, width=1)


def app_icon(layer, draw, icons, name, x, size=26):
    """An application's icon: its coloured square with rounded corners and the white picture (0.7 of it)."""
    index, rgb = APP[name]
    colour = ((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255, 255)
    y = MID - size / 2
    rounded(draw, x, y, size, size, size * 0.3, fill=colour)
    picture = int(size * 0.7 * K)
    paint(layer, coverage(icons, '%02d.pgm' % index, picture), (255, 255, 255, 255),
          x * K + (size * K - picture) / 2, y * K + (size * K - picture) / 2)


def mark(layer, icons, x, size=26):
    """The Kei mark, the launcher, in the bar's colours."""
    y = MID - size / 2
    for index, colour in enumerate(MARK):
        mask = coverage(icons, 'mark-%d.pgm' % index, size * K)
        paint(layer, mask, tuple(int(c * 255) for c in colour[:3]) + (int(colour[3] * 255),), x * K, y * K)


def line(draw, look, x):
    """A separator, 16 pixels tall across the middle."""
    draw.rectangle((x * K, (MID - 8) * K, x * K + K - 1, (MID + 8) * K), fill=look.line)


def symbol(layer, icons, index, x, size, colour):
    """A titlebar-style icon (icons.c) in the ink."""
    paint(layer, coverage(icons, '%02d.pgm' % index, size * K), colour, x * K, (MID - size / 2) * K)


def wifi(draw, look, cx):
    """Wi-Fi: three arcs over a dot, the usual fan."""
    base = (MID + 6) * K
    for radius in (4.5, 9.0, 13.5):
        r = radius * K
        draw.arc((cx * K - r, base - r, cx * K + r, base + r), 225, 315, fill=look.ink, width=int(2.2 * K))
    r = 1.6 * K
    draw.ellipse((cx * K - r, base - r - 0.4 * K, cx * K + r, base + r - 0.4 * K), fill=look.ink)


def battery(draw, look, x, percent=72):
    """The battery: an outline, its charge, its terminal."""
    rounded(draw, x, MID - 6, 23, 12, 3.5, outline=look.ink, width=1.4)
    fill = (23 - 5) * percent / 100
    rounded(draw, x + 2.5, MID - 3.5, fill, 7, 1.6, fill=look.ink)
    rounded(draw, x + 23.8, MID - 2.5, 2, 5, 1, fill=look.ink)


def ime(draw, look, font, cx):
    """The input method's language: its letter on a round chip."""
    r = 12 * K
    chip = (255, 255, 255, 42) if look.dark else (30, 40, 60, 24)
    draw.ellipse((cx * K - r, MID * K - r, cx * K + r, MID * K + r), fill=chip)
    draw.text((cx * K, MID * K + 0.5 * K), 'A', font=font, fill=look.ink, anchor='mm')


def draw_bar(under, look, icons, docked, media, context=CONTEXT):
    """The bar over a wallpaper, with the wallpaper under it, at 1x: (bar image, places of the parts)."""
    strip = glass(under, look)
    base = np.asarray(under, dtype=np.float32) / 255.0
    picture = base[:H + context].copy()
    # A soft shadow under the bar.
    shadow = 0.16 * np.exp(-np.arange(context, dtype=np.float32) / 5.0)
    picture[H:] *= (1 - shadow)[:, None, None]
    picture[:H] = strip
    image = Image.fromarray((picture * 255).astype(np.uint8)).convert('RGBA')
    image = image.resize((W * K, (H + context) * K), Image.NEAREST)
    layer = Image.new('RGBA', image.size, (0, 0, 0, 0))
    draw = ImageDraw.Draw(layer)
    text = ImageFont.truetype(INTER, 14 * K)
    title = ImageFont.truetype(INTER, 15 * K)
    letter = ImageFont.truetype(INTER, 13 * K)

    # The hairline along the bottom.
    draw.rectangle((0, (H - 1) * K, W * K, H * K - 1), fill=look.hairline)

    # Left: the launcher, a line, then the applications' pill (or the docked window).
    mark(layer, icons, 10)
    line(draw, look, 46)
    if not docked:
        names = ['files', 'phone', 'settings', 'image', 'text', 'notes', 'mail', 'calendar']
        x = 56
        width = 2 * 6 + len(names) * 26 + (len(names) - 1) * 8
        pill(draw, look, x, width)
        for index, name in enumerate(names):
            ix = x + 6 + index * 34
            app_icon(layer, draw, icons, name, ix)
            if index == 0:
                # The current application: a small light bar under its icon.
                rounded(draw, ix + 9, MID + 15, 8, 2.5, 1.25, fill=look.ink)
    else:
        # The docked window: its icon and title in a pill, its controls after it, its buttons at the end.
        x = 56
        pill(draw, look, x, 96)
        app_icon(layer, draw, icons, 'files', x + 5, 24)
        draw.text(((x + 38) * K, MID * K + 0.5 * K), 'Files', font=title, fill=look.ink, anchor='lm')
        cx = x + 112
        for index in (ICON_BACK, ICON_FORWARD, ICON_HOME):
            symbol(layer, icons, index, cx, 18, look.faint if index == ICON_FORWARD else look.ink)
            cx += 30
        draw.text(((cx + 6) * K, MID * K + 0.5 * K), 'Today', font=text, fill=look.ink, anchor='lm')
        # The window's buttons in their own small pill, before the desktops.
        bx = W / 2 - 80 - 16 - 104
        pill(draw, look, bx, 104, 30)
        signs = ImageFont.truetype(INTER, 17 * K)
        for index, sign in enumerate(['–', '❐', '×']):
            sx = bx + 20 + index * 32
            if index == 1:
                rounded(draw, sx - 5, MID - 5, 10, 10, 1.5, outline=look.ink, width=1.4)
            else:
                draw.text((sx * K, MID * K), sign, font=signs, fill=look.ink, anchor='mm')

    # Middle: the desktops' dots, the one shown an outlined larger pill.
    dots = [18, 30, 18, 18]
    shown = 1
    width = 2 * 14 + sum(dots) + 3 * 12
    x = (W - width) / 2
    pill(draw, look, x, width, 26)
    dx = x + 14
    for index, dot_width in enumerate(dots):
        if index == shown:
            rounded(draw, dx, MID - 6, dot_width, 12, 6, outline=look.ink, width=1.6)
        else:
            rounded(draw, dx, MID - 3.5, dot_width, 7, 3.5, fill=look.dot)
        dx += dot_width + 12

    # Right: the clock's pill at the edge, then the status pill left of it.
    clock = 'Mon Oct 5  13:43'
    clock_width = draw.textlength(clock, font=text) / K
    cw = clock_width + 2 * 16
    cx = W - 8 - cw
    pill(draw, look, cx, cw)
    draw.text(((cx + 16) * K, MID * K + 0.5 * K), clock, font=text, fill=look.ink, anchor='lm')
    slots = 5 if media else 4
    sw = 2 * 8 + slots * 34
    sx = cx - 10 - sw
    pill(draw, look, sx, sw)
    px = sx + 8 + 17
    ime(draw, look, letter, px)
    px += 34
    if media:
        symbol(layer, icons, ICON_USB, px - 10, 20, look.ink)
        px += 34
    wifi(draw, look, px)
    px += 34
    symbol(layer, icons, ICON_VOLUME_2, px - 10, 20, look.ink)
    px += 34
    battery(draw, look, px - 13)

    image.alpha_composite(layer)
    return image.resize((W, H + context), Image.LANCZOS)


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
    icons, out = sys.argv[1], sys.argv[2]
    birch = wallpaper('Birch-Lake.png')
    lake = wallpaper('Lakeside.png')
    dark = Look(True)
    light = Look(False)
    rows = [
        ('A. 暗い glass（提案）・浮いた窓の時: app の pill（今の app に下線）・中央の点・状態の pill・時計の pill（Birch Lake）',
         draw_bar(birch, dark, icons, False, False)),
        ('B. 同じ・別の壁紙（Lakeside）、USB の icon が出ている時', draw_bar(lake, dark, icons, False, True)),
        ('C. 窓を dock した時: app の icon は出さず、窓の icon・title・操作、窓の button の pill（中央の点の左）',
         draw_bar(birch, dark, icons, True, False)),
        ('D. 参考: light の外観で bar を明るい glass にした場合（提案は light の外観でも A の暗い bar）',
         draw_bar(birch, light, icons, False, False)),
    ]
    label = ImageFont.truetype(JAPANESE, 16)
    latin = ImageFont.truetype(INTER, 15)
    zoom = 2
    span = 620
    crops = [0, W // 2 - span // 2, W - span]
    height = sum(28 + row.height + 16 for _, row in rows) + 28 + len(crops) * ((H + 12) * zoom + 10) + 20
    sheet = Image.new('RGB', (W + 40, height), (246, 247, 250))
    draw = ImageDraw.Draw(sheet)
    y = 14
    for text, row in rows:
        caption(draw, 20, y, text, latin, label, (30, 34, 44))
        y += 28
        sheet.paste(row.convert('RGB'), (20, y))
        y += row.height + 16
    caption(draw, 20, y, 'A の左・中央・右を 2 倍に拡大', latin, label, (30, 34, 44))
    y += 28
    first = rows[0][1]
    for left in crops:
        part = first.crop((left, 0, left + span, H + 12)).resize((span * zoom, (H + 12) * zoom), Image.LANCZOS)
        sheet.paste(part.convert('RGB'), (20, y))
        y += part.height + 10
    sheet.save(out)
    draw_bar(birch, dark, icons, False, False, 800 - H).convert('RGB').save(out.replace('.png', '-screen.png'))


if __name__ == '__main__':
    main()
