#!/usr/bin/env python3
# ws099-p034: host montage of the system bar as the code on this branch draws it (shell.c draw_system_bar,
# draw_bar_strip, draw_bar_group, draw_desktops, draw_status; apps-bar.c; network.c's fan; volume.c; input-method.c's
# chip), with the montage-4 tiles (icons.c kwl_icon_tile, 26 px in the bar, 20 px by a title).  Each shape is drawn
# with the panel shader's formula (shaders/panel.frag: glass with the dark glass's darkening, shadow, solid, ring,
# image, text coverage) and its blend (premultiplied over), at the code's places and colours.  Not the real
# compositor: the blur of the wallpaper is approximated, the text is Inter through PIL (not the glass's rasterizer),
# and a window's menu text is a stand-in for titlebar-shell.c.
# The sheet: Birch Lake and Lakeside, the light and the dark appearance, floating windows and a docked window.
#   p034-bar-host.py TILE_DIR ICON_DIR OUT.png
# TILE_DIR: tile-dump's NN-PIXELS.rgba (20 and 26); ICON_DIR: icon-dump's NN.pgm (20 px) and mark-N.pgm (104 px).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
import os
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = os.path.normpath(os.path.join(os.path.dirname(__file__), '..', '..', '..'))
FONT = os.path.join(ROOT, 'userland/desktop/fonts/Mahora-Regular.ttf')
WALLPAPERS = os.path.join(ROOT, 'userland/desktop/keiland/wallpapers')
W = 1280
H = 800
SHOWN = 190                      # the height of the screen shown under each bar's top
BAR = 44                         # KWL_GLASS_BAR
MID = BAR // 2                   # KWL_GLASS_BAR_MIDDLE
LUMA = np.array([0.2126, 0.7152, 0.0722])

# shell.c's layout constants (ws099-p034).
BAR_LAUNCHER_X = 10
BAR_LAUNCHER_SIZE = 26
BAR_LINE_TOP = MID - 8
BAR_LINE_LENGTH = 16
BAR_PILL_HEIGHT = 26
BAR_GROUP_HEIGHT = 34
BAR_BUTTONS_HEIGHT = 30
BAR_SLOT = 34
BAR_STATUS_PAD = 8
BAR_CLOCK_PAD = 16
BAR_PILL_GAP = 10
BAR_EDGE = 8
BAR_BUTTON_SPACING = 34
DESKTOPS = 4
DESKTOP_WIDTH = 30
DESKTOP_GAP = 4
DESKTOPS_PAD = 10
DESKTOP_DOT_WIDTH = 18
DESKTOP_DOT_HEIGHT = 7
DESKTOP_SHOWN_HEIGHT = 12
ICON_WIDTH = 34                  # apps-bar.h
ICON_MARK = 26
ICON_PILL_PAD = 6
IME_W = 26
IME_H = 26
# The Kei mark's bar colours (glass.c GLASS_MARK_BAR: bar, bar shade, leaf, leaf shade, overlap, rim, sheen).
MARK = [(0.455, 0.600, 0.925, 0.92), (0.290, 0.451, 0.878, 0.55), (0.400, 0.690, 0.945, 0.90),
        (0.145, 0.408, 0.890, 0.90), (0.106, 0.349, 0.839, 0.96), (1, 1, 1, 0.50), (1, 1, 1, 0.18)]


def enum_index(name):
    text = open(os.path.join(ROOT, 'userland/desktop/wayland/icons.h')).read()
    body = text[text.index('enum glass_icon {'):text.index('GLASS_ICON_COUNT')]
    entries = [line.strip().rstrip(',') for line in body.splitlines()[1:] if line.strip().startswith('GLASS_ICON_')]
    return entries.index(name)


def dark_colour(colour):
    """glass.c glass_dark_color: a colour of low saturation has its lightness turned over."""
    most = max(colour[:3])
    least = min(colour[:3])
    if most - least >= 0.25:
        return tuple(colour)
    shift = 1.0 - (most + least)
    return tuple(min(max(c + shift, 0.0), 1.0) for c in colour[:3]) + (colour[3],)


class Screen:
    """The output, premultiplied RGB, with the blurred scene the glass samples."""

    def __init__(self, wallpaper, dark):
        self.pixels = np.asarray(wallpaper, dtype=np.float64) / 255.0
        small = wallpaper.resize((W // 4, H // 4), Image.BOX).filter(ImageFilter.GaussianBlur(6))
        self.under = np.asarray(small.resize((W, H), Image.BILINEAR), dtype=np.float64) / 255.0
        self.dark = dark
        self.keep = False

    def colour(self, colour, light=False):
        """The colour a shape is drawn in (glass_shape_draw: mapped in the dark appearance unless kept)."""
        if self.dark and not self.keep and not light:
            return dark_colour(colour)
        return tuple(colour)

    def region(self, quad):
        x0 = max(int(np.floor(quad[0])), 0)
        y0 = max(int(np.floor(quad[1])), 0)
        x1 = min(int(np.ceil(quad[0] + quad[2])), W)
        y1 = min(int(np.ceil(quad[1] + quad[3])), H)
        ys, xs = np.mgrid[y0:y1, x0:x1].astype(np.float64)
        return x0, y0, x1, y1, xs + 0.5, ys + 0.5

    @staticmethod
    def distance(px, py, box, radius):
        cx = box[0] + box[2] / 2.0
        cy = box[1] + box[3] / 2.0
        qx = np.abs(px - cx) - box[2] / 2.0 + radius
        qy = np.abs(py - cy) - box[3] / 2.0 + radius
        outside = np.sqrt(np.maximum(qx, 0.0) ** 2 + np.maximum(qy, 0.0) ** 2)
        return outside + np.minimum(np.maximum(qx, qy), 0.0) - radius

    def blend(self, x0, y0, x1, y1, rgb, alpha):
        target = self.pixels[y0:y1, x0:x1]
        target[:] = rgb + target * (1.0 - alpha[..., None])

    def glass(self, box, colour, radius=0.0, flat=0.0, edge=0.0, dark_glass=False, light=False, quad=None):
        quad = quad or box
        x0, y0, x1, y1, px, py = self.region(quad)
        d = self.distance(px, py, box, radius)
        cover = np.clip(0.5 - d, 0.0, 1.0)
        dark = dark_glass or (self.dark and not self.keep and not light)
        colour = self.colour(colour, light)
        under = self.under[y0:y1, x0:x1]
        g = under * (1 - colour[3]) + np.array(colour[:3]) * colour[3]
        luma = (g * LUMA).sum(axis=2)
        white = 1.0 if min(colour[:3]) >= 0.9 else 0.0
        lift = white * np.clip((0.85 - luma) / np.maximum(1 - luma, 0.001), 0, 1)
        g = g * (1 - lift[..., None]) + lift[..., None]
        if dark:
            shade = np.clip((luma - 0.15) / np.maximum(luma, 0.001), 0, 1)
            g = g * (1 - shade[..., None])
        depth = np.clip((py - box[1]) / max(box[3], 1.0), 0, 1)
        rim = np.clip(1 - np.abs(d + 1), 0, 1)
        g = g + 0.05 * (1 - depth[..., None]) * (1 - min(max(flat, 0.0), 1.0))
        g = g * (1 - rim[..., None] * edge) + rim[..., None] * edge
        self.blend(x0, y0, x1, y1, g * cover[..., None], cover)

    def shadow(self, box, colour, soft, radius=0.0, quad=None):
        quad = quad or box
        x0, y0, x1, y1, px, py = self.region(quad)
        d = self.distance(px, py, box, radius)
        soft = max(soft, 1.0)
        t = np.clip((d + soft * 0.5) / (soft * 1.5), 0, 1)
        alpha = colour[3] * (1 - t * t * (3 - 2 * t))
        self.blend(x0, y0, x1, y1, np.array(colour[:3]) * alpha[..., None], alpha)

    def solid(self, box, colour, radius=0.0, light=False):
        colour = self.colour(colour, light)
        x0, y0, x1, y1, px, py = self.region(box)
        cover = np.clip(0.5 - self.distance(px, py, box, radius), 0, 1) * colour[3]
        self.blend(x0, y0, x1, y1, np.array(colour[:3]) * cover[..., None], cover)

    def ring(self, box, colour, radius, thickness, quad=None):
        colour = self.colour(colour)
        quad = quad or (box[0] - 1, box[1] - 1, box[2] + 2, box[3] + 2)
        x0, y0, x1, y1, px, py = self.region(quad)
        d = self.distance(px, py, box, radius)
        cover = (np.clip(0.5 - d, 0, 1) - np.clip(0.5 - (d + thickness), 0, 1)) * colour[3]
        self.blend(x0, y0, x1, y1, np.array(colour[:3]) * cover[..., None], cover)

    def image(self, picture, x, y, opacity=1.0):
        """MODE_IMAGE at its own size on whole pixels (the linear sampler then reads each texel as it is)."""
        size = picture.shape[0]
        target = self.pixels[y:y + size, x:x + size]
        target[:] = picture[..., :3] * opacity + target * (1 - picture[..., 3:4] * opacity)

    def coverage(self, mask, x, y, colour, light=False):
        """MODE_TEXT: a coverage (0..1) in a colour."""
        colour = self.colour(colour, light)
        h, w = mask.shape
        target = self.pixels[y:y + h, x:x + w]
        cover = mask * colour[3]
        target[:] = np.array(colour[:3]) * cover[..., None] + target * (1 - cover[..., None])

    def text(self, x, baseline, text, pixels, colour, light=False):
        font = ImageFont.truetype(FONT, pixels)
        left, top, right, bottom = font.getbbox(text, anchor='ls')
        mask = Image.new('L', (right - left + 2, bottom - top + 2), 0)
        ImageDraw.Draw(mask).text((-left + 1, -top + 1), text, fill=255, font=font, anchor='ls')
        self.coverage(np.asarray(mask, dtype=np.float64) / 255.0, x + left - 1, baseline + top - 1, colour, light)
        return int(font.getlength(text))


def text_width(text, pixels):
    return int(ImageFont.truetype(FONT, pixels).getlength(text))


class Assets:
    def __init__(self, tiles, icons):
        self.tiles = tiles
        self.icons = icons
        self.first_app = enum_index('GLASS_ICON_APP_FILES')

    def tile(self, name, size):
        number = enum_index('GLASS_ICON_APP_' + name.upper())
        raw = np.fromfile(os.path.join(self.tiles, '%02d-%d.rgba' % (number, size)), dtype=np.uint8)
        return raw.reshape(size, size, 4).astype(np.float64) / 255.0

    def icon(self, name):
        number = enum_index('GLASS_ICON_' + name)
        image = Image.open(os.path.join(self.icons, '%02d.pgm' % number)).convert('L')
        return np.asarray(image, dtype=np.float64) / 255.0

    def mark(self, layer):
        image = Image.open(os.path.join(self.icons, 'mark-%d.pgm' % layer)).convert('L')
        image = image.resize((BAR_LAUNCHER_SIZE, BAR_LAUNCHER_SIZE), Image.LANCZOS)
        return np.asarray(image, dtype=np.float64) / 255.0


def app_tile(screen, assets, name, x, y, size, opacity=1.0, hole_scene=False):
    """glass_draw_app_tile: the see-through window on light glass (BUG-237), then the tile."""
    if hole_scene:
        inset = size * 0.08
        screen.glass((x + inset, y + inset, size - 2 * inset, size - 2 * inset), (0, 0, 0, 0),
                     radius=size * 0.24 - inset, flat=1.0, light=True)
    screen.image(assets.tile(name, size), x, y, opacity)


def bar_group(screen, x, width, height):
    """draw_bar_group: a darker pill with a faint light edge."""
    top = MID - height // 2
    screen.solid((x, top, width, height), (0, 0, 0, 0.25), radius=height * 0.5)
    screen.ring((x, top, width, height), (1, 1, 1, 0.12), radius=height * 0.5, thickness=1.0)


def bar_layout(clock, ime, media, battery, charging):
    bar = {}
    bar['clock_pill_width'] = text_width(clock, 14) + 2 * BAR_CLOCK_PAD
    bar['clock_pill_x'] = W - BAR_EDGE - bar['clock_pill_width']
    bar['clock_x'] = bar['clock_pill_x'] + BAR_CLOCK_PAD
    slots = 2 * BAR_SLOT + (BAR_SLOT if ime else 0) + (BAR_SLOT if media else 0)
    if battery is not None:
        slots += BAR_SLOT + (10 if charging else 0)
    bar['status_width'] = slots + 2 * BAR_STATUS_PAD
    bar['status_x'] = bar['clock_pill_x'] - BAR_PILL_GAP - bar['status_width']
    slot = bar['status_x'] + BAR_STATUS_PAD
    bar['ime_x'] = slot + (BAR_SLOT - 26) // 2
    if ime:
        slot += BAR_SLOT
    bar['media_x'] = slot + (BAR_SLOT - 20) // 2
    if media:
        slot += BAR_SLOT
    bar['signal_x'] = slot + (BAR_SLOT - 20) // 2
    slot += BAR_SLOT
    bar['volume_x'] = slot + (BAR_SLOT - 20) // 2
    slot += BAR_SLOT
    bar['battery_x'] = slot + (BAR_SLOT - 26) // 2
    bar['desktops_width'] = 2 * DESKTOPS_PAD + DESKTOPS * DESKTOP_WIDTH + (DESKTOPS - 1) * DESKTOP_GAP
    bar['desktops_x'] = (W - bar['desktops_width']) // 2
    bar['desktops_line'] = bar['desktops_x'] - 12
    bar['buttons'] = [bar['desktops_line'] - 30 - button * BAR_BUTTON_SPACING for button in range(3)]
    bar['menu_line'] = BAR_LAUNCHER_X + BAR_LAUNCHER_SIZE + 10
    bar['title_x'] = bar['menu_line'] + 14
    return bar


def draw_bar(screen, assets, state):
    ink = (1, 1, 1, 0.94)
    line = (1, 1, 1, 0.18)
    bar = bar_layout(state['clock'], state['ime'], state['media'], state['battery'], state['charging'])
    screen.keep = True

    # draw_bar_strip: dark glass in both appearances, the lighter middle, the hairline.
    screen.glass((0, 0, W, BAR), (0.07, 0.094, 0.14, 0.64), flat=1.0, dark_glass=True)
    screen.shadow((W / 3.0, -200.0, W / 3.0, BAR + 400.0), (1, 1, 1, 0.07), soft=W / 3.0, quad=(0, 0, W, BAR))
    screen.solid((0, BAR - 1, W, 1), (1, 1, 1, 0.09))

    # The launcher's Kei mark and the line after it.
    for layer, colour in enumerate(MARK):
        screen.coverage(assets.mark(layer), BAR_LAUNCHER_X, MID - BAR_LAUNCHER_SIZE // 2, colour)
    screen.solid((bar['menu_line'], BAR_LINE_TOP, 1, BAR_LINE_LENGTH), line)

    if state['docked'] is not None:
        name, title, menu = state['docked']
        left = bar['buttons'][2] - BAR_SLOT // 2 - 3
        available = left - 12 - bar['title_x'] - 30
        end = min(text_width(title, 15), available // 2)
        bar_group(screen, bar['title_x'] - 7, 30 + end + 7 + 12, BAR_GROUP_HEIGHT)
        app_tile(screen, assets, name, bar['title_x'], MID - 10, 20)
        screen.text(bar['title_x'] + 30, MID + 6, title, 15, ink)
        x = bar['title_x'] + 30 + end + 24
        for word in menu:
            screen.text(x, MID + 5, word, 14, (1, 1, 1, 0.80))
            x += text_width(word, 14) + 18
        bar_group(screen, left, bar['buttons'][0] + BAR_SLOT // 2 + 3 - left, BAR_BUTTONS_HEIGHT)
        cx = bar['buttons'][2]
        screen.solid((cx - 6, MID - 0.75, 12, 1.5), ink, radius=0.75)
        cx = bar['buttons'][1]
        screen.ring((cx - 7, MID - 3, 9, 9), ink, radius=2.5, thickness=1.4, quad=(cx - 10, MID - 8, 16, 16))
        screen.solid((cx - 4, MID - 5, 9, 1.4), ink, radius=0.7)
        screen.solid((cx + 4 - 0.4, MID - 5, 1.4, 9), ink, radius=0.7)
        cx = bar['buttons'][0]
        screen.text(cx - text_width('×', 20) // 2, MID + 7, '×', 20, ink)
    else:
        # apps-bar.c: one pill behind the icons, the tiles, a short line under the current one.
        view_left = bar['title_x'] - 2
        apps = state['apps']
        pill_x = view_left + (ICON_WIDTH - ICON_MARK) // 2 - ICON_PILL_PAD
        pill_width = len(apps) * ICON_WIDTH - (ICON_WIDTH - ICON_MARK) + 2 * ICON_PILL_PAD
        screen.solid((pill_x, MID - 17, pill_width, 34), (0, 0, 0, 0.25), radius=17)
        screen.ring((pill_x, MID - 17, pill_width, 34), (1, 1, 1, 0.12), radius=17, thickness=1.0)
        for slot, (name, minimized) in enumerate(apps):
            x = view_left + slot * ICON_WIDTH
            if slot == state['lit']:
                screen.solid((x + 2, MID - ICON_WIDTH // 2 + 2, ICON_WIDTH - 4, ICON_WIDTH - 4), (1, 1, 1, 0.20),
                             radius=(ICON_WIDTH - 4) * 0.24)
            app_tile(screen, assets, name, x + (ICON_WIDTH - ICON_MARK) // 2, MID - ICON_MARK // 2, ICON_MARK,
                     0.45 if minimized else 1.0)
            if slot == state['current']:
                screen.solid((x + ICON_WIDTH // 2 - 4, MID + 14, 8, 2.5), ink, radius=1.25)

    # draw_desktops: the pill, a dot each, the shown one an outlined pill.
    bar_group(screen, bar['desktops_x'], bar['desktops_width'], BAR_PILL_HEIGHT)
    for desktop in range(DESKTOPS):
        x = bar['desktops_x'] + DESKTOPS_PAD + desktop * (DESKTOP_WIDTH + DESKTOP_GAP)
        if desktop == state['desktop']:
            screen.ring((x, MID - DESKTOP_SHOWN_HEIGHT // 2, DESKTOP_WIDTH, DESKTOP_SHOWN_HEIGHT), (1, 1, 1, 0.95),
                        radius=DESKTOP_SHOWN_HEIGHT * 0.5, thickness=1.6)
        else:
            screen.solid((x + (DESKTOP_WIDTH - DESKTOP_DOT_WIDTH) // 2, MID - DESKTOP_DOT_HEIGHT * 0.5,
                          DESKTOP_DOT_WIDTH, DESKTOP_DOT_HEIGHT), (1, 1, 1, 0.42), radius=DESKTOP_DOT_HEIGHT * 0.5)

    # draw_status: the two pills, the clock, the battery, the fan, the volume, the media, the language.
    bar_group(screen, bar['status_x'], bar['status_width'], BAR_GROUP_HEIGHT)
    bar_group(screen, bar['clock_pill_x'], bar['clock_pill_width'], BAR_GROUP_HEIGHT)
    screen.text(bar['clock_x'], MID + 5, state['clock'], 14, ink)
    if state['battery'] is not None:
        x = bar['battery_x']
        screen.ring((x, MID - 6, 22, 12), ink, radius=3.5, thickness=1.3, quad=(x - 1, MID - 7, 24, 14))
        fill = max(16.0 * state['battery'] / 100.0, 2.0)
        screen.solid((x + 3, MID - 3, fill, 6), ink, radius=1.5)
        screen.solid((x + 23, MID - 2, 2, 4), ink, radius=1.0)
    faint = ink[:3] + (ink[3] * 0.30,)
    screen.coverage(assets.icon('WIFI_4'), bar['signal_x'], MID - 10, faint)
    screen.coverage(assets.icon('WIFI_%d' % state['wifi']), bar['signal_x'], MID - 10, ink)
    screen.coverage(assets.icon('VOLUME_2'), bar['volume_x'], MID - 10, ink)
    if state['media']:
        screen.coverage(assets.icon('USB'), bar['media_x'], MID - 10, ink)
    if state['ime']:
        screen.solid((bar['ime_x'], MID - IME_H // 2, IME_W, IME_H), ink[:3] + (0.16,), radius=IME_H * 0.5)
        width = text_width(state['ime'], 14)
        screen.text(bar['ime_x'] + (IME_W - width) // 2, MID + 5, state['ime'], 14, ink)
    screen.keep = False


def draw_window(screen, assets, dark, docked, name, title):
    """The context under the bar: a floating window's title bar, or the docked window's body (appearance colours)."""
    ink = (0.12, 0.16, 0.24, 1.0)
    if docked:
        top = BAR + 4
        body = (0.97, 0.975, 0.985, 1.0) if not dark else (0.11, 0.125, 0.16, 1.0)
        screen.shadow((8, top + 4, W - 16, H - top), (0.10, 0.18, 0.35, 0.12), soft=18.0, radius=14.0,
                      quad=(0, top - 30, W, H - top + 30))
        screen.solid((8, top, W - 16, H - top), body, radius=14.0, light=True)
        text = (0.25, 0.28, 0.35, 1.0) if not dark else (0.80, 0.83, 0.90, 1.0)
        for row in range(5):
            screen.solid((40, top + 30 + row * 28, 18, 18), (0.91, 0.71, 0.24, 1.0), radius=4.0, light=True)
            screen.text(70, top + 44 + row * 28, ['Documents', 'Downloads', 'Music', 'Pictures', 'Videos'][row], 15,
                        text, light=True)
        return
    x, y, width = 180, 92, 760
    screen.shadow((x, y + 4, width, 300), (0.10, 0.18, 0.35, 0.12), soft=18.0, radius=14.0,
                  quad=(x - 36, y - 32, width + 72, 380))
    body = (0.97, 0.975, 0.985, 1.0) if not dark else (0.11, 0.125, 0.16, 1.0)
    screen.solid((x, y + 44, width, 260), body, radius=0.0, light=True)
    screen.glass((x, y, width, 44), (1, 1, 1, 0.64), radius=14.0, edge=0.85)
    hole_scene = not dark
    app_tile(screen, assets, name, x + 14, y + 22 - 10, 20, hole_scene=hole_scene)
    screen.text(x + 14 + 30, y + 22 + 6, title, 15, ink)
    cx = x + width - 22
    screen.text(cx - text_width('×', 20) // 2, y + 22 + 7, '×', 20, ink)


def frame(assets, wallpaper, dark, docked):
    screen = Screen(wallpaper, dark)
    if docked:
        draw_window(screen, assets, dark, True, 'files', 'Documents')
        state = {'docked': ('files', 'Documents', ['File', 'Edit', 'View', 'Go']), 'apps': [], 'lit': -1,
                 'current': -1}
    else:
        draw_window(screen, assets, dark, False, 'terminal', 'Terminal')
        state = {'docked': None, 'apps': [('files', False), ('terminal', False), ('browser', False),
                                          ('notes', False), ('calendar', False), ('mail', False),
                                          ('video', True)],
                 'lit': 2, 'current': 1}
    state.update({'clock': 'Tue Oct 6 14:05', 'ime': 'A', 'media': docked, 'battery': 76, 'charging': False,
                  'wifi': 3, 'desktop': 0})
    draw_bar(screen, assets, state)
    image = Image.fromarray((np.clip(screen.pixels, 0, 1) * 255 + 0.5).astype(np.uint8))
    return image.crop((0, 0, W, SHOWN))


def main():
    tiles, icons, out = sys.argv[1], sys.argv[2], sys.argv[3]
    assets = Assets(tiles, icons)
    small = ImageFont.truetype(FONT, 13)
    big = ImageFont.truetype(FONT, 16)
    ink = (31, 41, 61)
    rows = []
    for wallpaper_name, wallpaper_label in (('Birch-Lake.png', 'Birch Lake'), ('Lakeside.png', 'Lakeside')):
        wallpaper = Image.open(os.path.join(WALLPAPERS, wallpaper_name)).convert('RGB')
        scale = max(W / wallpaper.width, H / wallpaper.height)
        wallpaper = wallpaper.resize((round(wallpaper.width * scale), round(wallpaper.height * scale)), Image.LANCZOS)
        left = (wallpaper.width - W) // 2
        top = (wallpaper.height - H) // 2
        wallpaper = wallpaper.crop((left, top, left + W, top + H))
        for dark in (False, True):
            for docked in (False, True):
                label = '%s, %s appearance, %s' % (wallpaper_label, 'dark' if dark else 'light',
                                                   'a docked window (Files)' if docked else 'floating windows')
                rows.append((label, frame(assets, wallpaper, dark, docked)))
    zoom_left = rows[0][1].crop((0, 0, 640, BAR)).resize((1280, BAR * 2), Image.NEAREST)
    zoom_right = rows[0][1].crop((640, 0, 1280, BAR)).resize((1280, BAR * 2), Image.NEAREST)
    zoom_docked = rows[1][1].crop((0, 0, 640, BAR)).resize((1280, BAR * 2), Image.NEAREST)
    height = 60 + len(rows) * (SHOWN + 30) + 3 * (BAR * 2 + 30) + 10
    sheet = Image.new('RGB', (W + 40, height), (246, 247, 250))
    draw = ImageDraw.Draw(sheet)
    draw.text((20, 14), 'ws099-p034 system bar with the montage-4 tiles (host render of this branch\'s drawing code; '
              'the bar stays dark in both appearances)', fill=ink, font=big)
    draw.text((20, 36), 'Shapes by the panel shader\'s formulas at the code\'s places and colours; tiles from icons.c; '
              'blur approximated; text through PIL; the docked window\'s menu words are stand-ins.', fill=ink, font=small)
    y = 60
    for label, image in rows:
        draw.text((20, y), label, fill=ink, font=small)
        sheet.paste(image, (20, y + 18))
        y += SHOWN + 30
    for label, image in (('2x: left half (floating, Birch Lake, light)', zoom_left),
                         ('2x: right half (floating, Birch Lake, light)', zoom_right),
                         ('2x: left half (docked, Birch Lake, light)', zoom_docked)):
        draw.text((20, y), label, fill=ink, font=small)
        sheet.paste(image, (20, y + 18))
        y += BAR * 2 + 30
    sheet.save(out)
    print('p034-bar-host: %s' % out)


if __name__ == '__main__':
    main()
