#!/usr/bin/env python3
# ws099-p034b: host montage of the system bar's second design (the 2026-10-06 user decisions on bar-montage-4): in the
# light appearance the bar has the floating title bar's colours (its white glass, 0.64, the title bar's dark ink, the
# Kei mark's bar colours, the tiles over the glass as on a title bar), in the dark appearance it stays the dark glass of
# p034; with a docked window the window's buttons (minimize, restore, close) are in a pill at the screen's top-right
# end, where the clock was, and the status and the clock pills move left of it (the desktops' dots stay in the middle).
# Drawn as p034-bar-host.py draws (the panel shader's formulas, tiles from icons.c, blur approximated, text by PIL).
#   p034b-bar-host.py TILE_DIR ICON_DIR OUT.png
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
import importlib.util
import os
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
SPEC = importlib.util.spec_from_file_location('bar_host', os.path.join(HERE, 'p034-bar-host.py'))
HOST = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(HOST)

W, H, BAR, MID, SHOWN = HOST.W, HOST.H, HOST.BAR, HOST.MID, HOST.SHOWN

# The light bar's colours: the floating title bar's glass and ink (shell.c draw_title_bar, as p034-bar-host draws it).
LIGHT_GLASS = (1.0, 1.0, 1.0, 0.64)
LIGHT_INK = (0.12, 0.16, 0.24, 0.94)
LIGHT_LINE = (0.12, 0.16, 0.24, 0.16)
LIGHT_PILL = (0.12, 0.16, 0.24, 0.06)
LIGHT_RING = (0.12, 0.16, 0.24, 0.10)
DARK_INK = (1.0, 1.0, 1.0, 0.94)
DARK_LINE = (1.0, 1.0, 1.0, 0.18)


def tile(screen, assets, name, x, y, size, opacity, light):
    """An application's tile in the bar: on the light bar its cut-out picture shows the title bar's dark ink under it
    (the symbol stays readable on the pale tiles), on the dark bar the dark glass (GLASS_HOLE_GROUND)."""
    if light:
        inset = size * 0.08
        screen.solid((x + inset, y + inset, size - 2 * inset, size - 2 * inset), (0.16, 0.20, 0.28, opacity),
                     radius=size * 0.24 - inset, light=True)
    screen.image(assets.tile(name, size), x, y, opacity)


def group(screen, x, width, height, light):
    """draw_bar_group: a pill a little darker than the bar, with a faint edge (light: of the title bar's ink)."""
    top = MID - height // 2
    if light:
        screen.solid((x, top, width, height), LIGHT_PILL, radius=height * 0.5, light=True)
        screen.ring((x, top, width, height), LIGHT_RING, radius=height * 0.5, thickness=1.0)
        return
    HOST.bar_group(screen, x, width, height)


def layout(state, docked):
    """The bar's places; with a docked window the buttons' pill at the right end and the status and clock left of it."""
    bar = HOST.bar_layout(state['clock'], state['ime'], state['media'], state['battery'], state['charging'])
    if not docked:
        return bar
    buttons_width = 3 * HOST.BAR_SLOT + 6
    bar['buttons_x'] = W - HOST.BAR_EDGE - buttons_width
    bar['buttons_width'] = buttons_width
    bar['buttons'] = [bar['buttons_x'] + 3 + HOST.BAR_SLOT // 2 + (2 - button) * HOST.BAR_SLOT for button in range(3)]
    shift = W - HOST.BAR_EDGE - (bar['buttons_x'] - HOST.BAR_PILL_GAP)
    for key in ('clock_pill_x', 'clock_x', 'status_x', 'ime_x', 'media_x', 'signal_x', 'volume_x', 'battery_x'):
        bar[key] -= shift
    return bar


def draw_bar(screen, assets, state, light):
    ink = LIGHT_INK if light else DARK_INK
    line = LIGHT_LINE if light else DARK_LINE
    docked = state['docked'] is not None
    bar = layout(state, docked)
    screen.keep = True

    # The strip: the title bar's white glass in the light appearance, p034's dark glass in the dark one.
    if light:
        screen.glass((0, 0, W, BAR), LIGHT_GLASS, flat=0.0, light=True)
        screen.solid((0, BAR - 1, W, 1), (0.12, 0.16, 0.24, 0.10), light=True)
    else:
        screen.glass((0, 0, W, BAR), (0.07, 0.094, 0.14, 0.64), flat=1.0, dark_glass=True)
        screen.shadow((W / 3.0, -200.0, W / 3.0, BAR + 400.0), (1, 1, 1, 0.07), soft=W / 3.0, quad=(0, 0, W, BAR))
        screen.solid((0, BAR - 1, W, 1), (1, 1, 1, 0.09))

    # The launcher's Kei mark (the bar's colours, made for a light bar) and the line after it.
    for index, colour in enumerate(HOST.MARK):
        screen.coverage(assets.mark(index), HOST.BAR_LAUNCHER_X, MID - HOST.BAR_LAUNCHER_SIZE // 2, colour, light=True)
    screen.solid((bar['menu_line'], HOST.BAR_LINE_TOP, 1, HOST.BAR_LINE_LENGTH), line, light=light)

    if docked:
        name, title, menu = state['docked']
        end = HOST.text_width(title, 15)
        group(screen, bar['title_x'] - 7, 30 + end + 7 + 12, HOST.BAR_GROUP_HEIGHT, light)
        tile(screen, assets, name, bar['title_x'], MID - 10, 20, 1.0, light)
        screen.text(bar['title_x'] + 30, MID + 6, title, 15, ink, light=light)
        x = bar['title_x'] + 30 + end + 24
        for word in menu:
            screen.text(x, MID + 5, word, 14, ink[:3] + (0.80,), light=light)
            x += HOST.text_width(word, 14) + 18

        # The buttons' pill at the right end: minimize, restore, close.
        group(screen, bar['buttons_x'], bar['buttons_width'], HOST.BAR_BUTTONS_HEIGHT, light)
        cx = bar['buttons'][2]
        screen.solid((cx - 6, MID - 0.75, 12, 1.5), ink, radius=0.75, light=light)
        cx = bar['buttons'][1]
        screen.ring((cx - 7, MID - 3, 9, 9), ink, radius=2.5, thickness=1.4, quad=(cx - 10, MID - 8, 16, 16))
        screen.solid((cx - 4, MID - 5, 9, 1.4), ink, radius=0.7, light=light)
        screen.solid((cx + 4 - 0.4, MID - 5, 1.4, 9), ink, radius=0.7, light=light)
        cx = bar['buttons'][0]
        screen.text(cx - HOST.text_width('×', 20) // 2, MID + 7, '×', 20, ink, light=light)
    else:
        view_left = bar['title_x'] - 2
        apps = state['apps']
        pill_x = view_left + (HOST.ICON_WIDTH - HOST.ICON_MARK) // 2 - HOST.ICON_PILL_PAD
        pill_width = len(apps) * HOST.ICON_WIDTH - (HOST.ICON_WIDTH - HOST.ICON_MARK) + 2 * HOST.ICON_PILL_PAD
        group(screen, pill_x, pill_width, 34, light)
        for slot, (name, minimized) in enumerate(apps):
            x = view_left + slot * HOST.ICON_WIDTH
            if slot == state['lit']:
                lit = (0.12, 0.16, 0.24, 0.10) if light else (1, 1, 1, 0.20)
                screen.solid((x + 2, MID - HOST.ICON_WIDTH // 2 + 2, HOST.ICON_WIDTH - 4, HOST.ICON_WIDTH - 4), lit,
                             radius=(HOST.ICON_WIDTH - 4) * 0.24, light=light)
            tile(screen, assets, name, x + (HOST.ICON_WIDTH - HOST.ICON_MARK) // 2, MID - HOST.ICON_MARK // 2,
                 HOST.ICON_MARK, 0.45 if minimized else 1.0, light)
            if slot == state['current']:
                screen.solid((x + HOST.ICON_WIDTH // 2 - 4, MID + 14, 8, 2.5), ink, radius=1.25, light=light)

    # The desktops' dots in the middle.
    group(screen, bar['desktops_x'], bar['desktops_width'], HOST.BAR_PILL_HEIGHT, light)
    for desktop in range(HOST.DESKTOPS):
        x = bar['desktops_x'] + HOST.DESKTOPS_PAD + desktop * (HOST.DESKTOP_WIDTH + HOST.DESKTOP_GAP)
        if desktop == state['desktop']:
            screen.ring((x, MID - HOST.DESKTOP_SHOWN_HEIGHT // 2, HOST.DESKTOP_WIDTH, HOST.DESKTOP_SHOWN_HEIGHT),
                        ink[:3] + (0.95,), radius=HOST.DESKTOP_SHOWN_HEIGHT * 0.5, thickness=1.6)
        else:
            screen.solid((x + (HOST.DESKTOP_WIDTH - HOST.DESKTOP_DOT_WIDTH) // 2, MID - HOST.DESKTOP_DOT_HEIGHT * 0.5,
                          HOST.DESKTOP_DOT_WIDTH, HOST.DESKTOP_DOT_HEIGHT), ink[:3] + (0.36,),
                         radius=HOST.DESKTOP_DOT_HEIGHT * 0.5, light=light)

    # The status and the clock pills.
    group(screen, bar['status_x'], bar['status_width'], HOST.BAR_GROUP_HEIGHT, light)
    group(screen, bar['clock_pill_x'], bar['clock_pill_width'], HOST.BAR_GROUP_HEIGHT, light)
    screen.text(bar['clock_x'], MID + 5, state['clock'], 14, ink, light=light)
    if state['battery'] is not None:
        x = bar['battery_x']
        screen.ring((x, MID - 6, 22, 12), ink, radius=3.5, thickness=1.3, quad=(x - 1, MID - 7, 24, 14))
        fill = max(16.0 * state['battery'] / 100.0, 2.0)
        screen.solid((x + 3, MID - 3, fill, 6), ink, radius=1.5, light=light)
        screen.solid((x + 23, MID - 2, 2, 4), ink, radius=1.0, light=light)
    faint = ink[:3] + (ink[3] * 0.30,)
    screen.coverage(assets.icon('WIFI_4'), bar['signal_x'], MID - 10, faint, light=light)
    screen.coverage(assets.icon('WIFI_%d' % state['wifi']), bar['signal_x'], MID - 10, ink, light=light)
    screen.coverage(assets.icon('VOLUME_2'), bar['volume_x'], MID - 10, ink, light=light)
    if state['media']:
        screen.coverage(assets.icon('USB'), bar['media_x'], MID - 10, ink, light=light)
    if state['ime']:
        screen.solid((bar['ime_x'], MID - HOST.IME_H // 2, HOST.IME_W, HOST.IME_H), ink[:3] + (0.12,),
                     radius=HOST.IME_H * 0.5, light=light)
        width = HOST.text_width(state['ime'], 14)
        screen.text(bar['ime_x'] + (HOST.IME_W - width) // 2, MID + 5, state['ime'], 14, ink, light=light)
    screen.keep = False


def frame(assets, wallpaper, dark, docked):
    screen = HOST.Screen(wallpaper, dark)
    if docked:
        HOST.draw_window(screen, assets, dark, True, 'files', 'Documents')
        state = {'docked': ('files', 'Documents', ['File', 'Edit', 'View', 'Go']), 'apps': [], 'lit': -1, 'current': -1}
    else:
        HOST.draw_window(screen, assets, dark, False, 'terminal', 'Terminal')
        state = {'docked': None, 'apps': [('files', False), ('terminal', False), ('browser', False), ('notes', False),
                                          ('calendar', False), ('mail', False), ('video', True)],
                 'lit': 2, 'current': 1}
    state.update({'clock': 'Tue Oct 6 14:05', 'ime': 'A', 'media': docked, 'battery': 76, 'charging': False,
                  'wifi': 3, 'desktop': 0})
    draw_bar(screen, assets, state, not dark)
    image = Image.fromarray((np.clip(screen.pixels, 0, 1) * 255 + 0.5).astype(np.uint8))
    return image.crop((0, 0, W, SHOWN))


def main():
    tiles, icons, out = sys.argv[1], sys.argv[2], sys.argv[3]
    assets = HOST.Assets(tiles, icons)
    small = ImageFont.truetype(HOST.FONT, 13)
    big = ImageFont.truetype(HOST.FONT, 16)
    ink = (31, 41, 61)
    rows = []
    for wallpaper_name, wallpaper_label in (('Birch-Lake.png', 'Birch Lake'), ('Lakeside.png', 'Lakeside')):
        wallpaper = Image.open(os.path.join(HOST.WALLPAPERS, wallpaper_name)).convert('RGB')
        scale = max(W / wallpaper.width, H / wallpaper.height)
        wallpaper = wallpaper.resize((round(wallpaper.width * scale), round(wallpaper.height * scale)), Image.LANCZOS)
        left = (wallpaper.width - W) // 2
        top = (wallpaper.height - H) // 2
        wallpaper = wallpaper.crop((left, top, left + W, top + H))
        for dark in (False, True):
            for docked in (False, True):
                label = '%s, %s appearance, %s' % (wallpaper_label, 'dark' if dark else 'light',
                                                   'a docked window (Files): its buttons at the right end, the clock left of them'
                                                   if docked else 'floating windows')
                rows.append((label, frame(assets, wallpaper, dark, docked)))
    zooms = [('2x: left half (floating, Birch Lake, light)', rows[0][1].crop((0, 0, 640, BAR))),
             ('2x: right half (floating, Birch Lake, light)', rows[0][1].crop((640, 0, 1280, BAR))),
             ('2x: right half (docked, Birch Lake, light)', rows[1][1].crop((640, 0, 1280, BAR)))]
    height = 60 + len(rows) * (SHOWN + 30) + len(zooms) * (BAR * 2 + 30) + 10
    sheet = Image.new('RGB', (W + 40, height), (246, 247, 250))
    draw = ImageDraw.Draw(sheet)
    draw.text((20, 14), 'ws099-p034b system bar: light appearance in the title bar\'s colours, dark unchanged; '
              'docked: the window\'s buttons at the top-right end', fill=ink, font=big)
    draw.text((20, 36), 'Host render (the panel shader\'s formulas; tiles from icons.c; blur approximated; text through PIL; '
              'the docked window\'s menu words are stand-ins).', fill=ink, font=small)
    y = 60
    for label, image in rows:
        draw.text((20, y), label, fill=ink, font=small)
        sheet.paste(image, (20, y + 18))
        y += SHOWN + 30
    for label, image in zooms:
        draw.text((20, y), label, fill=ink, font=small)
        sheet.paste(image.resize((1280, BAR * 2), Image.NEAREST), (20, y + 18))
        y += BAR * 2 + 30
    sheet.save(out)
    print('p034b-bar-host: %s' % out)


if __name__ == '__main__':
    main()
