#!/usr/bin/env python3
# ws181-p005 and p006: the host sheet of the 2026-10-07 UAT's bar and Home changes, drawn with ws099-p034's host
# renderer (plan/ws099/tests/p034-bar-host.py: the panel shader's formulas, blur approximated, text through PIL; not
# the real compositor): (1) App Home's first page: the status and the clock in white without pills, the large clock
# and the date, two rows of icons at the bottom, the pages' dots; (2) the arrangement menu, twice as wide and five
# times as tall, seven layout drawings two to a row (the grid alone), frosted glass, no words; (3) the desktops' pill:
# three desktops, a cat, a bird (the middle, where a session starts) and a rabbit, the shown one in the accent.
#   p005-host.py TILE_DIR ICON_DIR SLOTS OUT.png
# SLOTS: p005-icon-dump's lines "layout x y w h" (arrange.c's slots of each layout's drawing).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
import importlib.util
import os
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
SPEC = importlib.util.spec_from_file_location('bar', os.path.join(HERE, '..', '..', 'ws099', 'tests', 'p034-bar-host.py'))
bar = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(bar)

W = bar.W
H = bar.H
MID = bar.MID
BAR = bar.BAR
ACCENT = (0x2f / 255.0, 0x7c / 255.0, 0xf6 / 255.0, 1.0)    # the default blue accent (WS179)
WHITE = (1.0, 1.0, 1.0, 0.96)                                  # shell.c draw_home_status
DESKTOP_DOT = 8                                                # shell.c (ws181-p006)
CELL_W = 258                                                   # arrange-shell.c (ws181-p006)
CELL_H = 56
CELL_GAP = 8
PAD = 6
MENU_W = 2 * PAD + 2 * CELL_W + CELL_GAP
MENU_H = 2 * PAD + 4 * CELL_H + 3 * CELL_GAP
ICON_W = 60
ICON_H = 40
LAYOUTS = 7
HOME_COLUMNS = 6                                               # home.c
HOME_CELL_W = 144
HOME_CELL_H = 152
HOME_ICON = 72
HOME_GRID_BOTTOM = 72
APPS = ['files', 'terminal', 'browser', 'notes', 'settings', 'calendar', 'mail', 'video', 'music', 'photos',
        'calculator', 'text']
STATE = {'clock': 'Wed Oct 7 21:40', 'ime': 'A', 'media': False, 'battery': 76, 'charging': False, 'wifi': 3}


def wallpaper(name):
    picture = Image.open(os.path.join(bar.WALLPAPERS, name)).convert('RGB')
    scale = max(W / picture.width, H / picture.height)
    picture = picture.resize((round(picture.width * scale), round(picture.height * scale)), Image.LANCZOS)
    left = (picture.width - W) // 2
    top = (picture.height - H) // 2
    return picture.crop((left, top, left + W, top + H))


def status(screen, assets, ink, pills):
    """draw_status (and its pills when the bar is drawn)."""
    layout = bar.bar_layout(STATE['clock'], STATE['ime'], STATE['media'], STATE['battery'], STATE['charging'])
    screen.keep = True
    if pills:
        bar.bar_group(screen, layout['status_x'], layout['status_width'], bar.BAR_GROUP_HEIGHT)
        bar.bar_group(screen, layout['clock_pill_x'], layout['clock_pill_width'], bar.BAR_GROUP_HEIGHT)
    screen.text(layout['clock_x'], MID + 5, STATE['clock'], 14, ink)
    x = layout['battery_x']
    screen.ring((x, MID - 6, 22, 12), ink, radius=3.5, thickness=1.3, quad=(x - 1, MID - 7, 24, 14))
    screen.solid((x + 3, MID - 3, 16.0 * STATE['battery'] / 100.0, 6), ink, radius=1.5)
    screen.solid((x + 23, MID - 2, 2, 4), ink, radius=1.0)
    faint = ink[:3] + (ink[3] * 0.30,)
    screen.coverage(assets.icon('WIFI_4'), layout['signal_x'], MID - 10, faint)
    screen.coverage(assets.icon('WIFI_%d' % STATE['wifi']), layout['signal_x'], MID - 10, ink)
    screen.coverage(assets.icon('VOLUME_2'), layout['volume_x'], MID - 10, ink)
    screen.solid((layout['ime_x'], MID - 13, 26, 26), ink[:3] + (0.16,), radius=13)
    width = bar.text_width(STATE['ime'], 14)
    screen.text(layout['ime_x'] + (26 - width) // 2, MID + 5, STATE['ime'], 14, ink)
    screen.keep = False


def home(assets, picture, tiles72):
    """App Home's first page: the dark stage, the clock and the date, two rows of icons at the bottom, the dots."""
    screen = bar.Screen(picture, False)
    screen.glass((0, 0, W, H), (0, 0, 0, 0.82), flat=1.0, light=True)
    screen.shadow((W * 0.3, -H * 0.3, W * 0.4, H * 0.6), (0.80, 0.86, 1.0, 0.10), soft=W * 0.3, radius=H * 0.3,
                  quad=(0, 0, W, H))
    status(screen, assets, WHITE, False)
    grid_top = H - HOME_GRID_BOTTOM - 2 * HOME_CELL_H
    baseline = grid_top // 2 + 16
    width = bar.text_width('21:40', 64)
    screen.text(W // 2 - width // 2, baseline, '21:40', 64, (1, 1, 1, 0.96), light=True)
    date = 'Wednesday, October 7'
    width = bar.text_width(date, 24)
    screen.text(W // 2 - width // 2, baseline + 46, date, 24, (1, 1, 1, 0.72), light=True)
    left = (W - HOME_COLUMNS * HOME_CELL_W) // 2
    for slot, name in enumerate(APPS):
        row = slot // HOME_COLUMNS
        x = left + (slot % HOME_COLUMNS) * HOME_CELL_W + (HOME_CELL_W - HOME_ICON) // 2
        y = H - HOME_GRID_BOTTOM - (2 - row) * HOME_CELL_H + 20
        if name in tiles72:
            screen.image(tiles72[name], x, y)
        label = name.capitalize()
        width = bar.text_width(label, 15)
        screen.text(x + HOME_ICON // 2 - width // 2, y + HOME_ICON + 30, label, 15, (1, 1, 1, 0.90), light=True)
    for row in range(2):
        y = H - HOME_GRID_BOTTOM - (2 - row) * HOME_CELL_H + 20 + HOME_ICON + 6
        screen.solid((left + 36, y, HOME_COLUMNS * HOME_CELL_W - 72, 1), (1, 1, 1, 0.10), light=True)
    for page in range(2):
        colour = ACCENT if page == 0 else (1, 1, 1, 0.32)
        screen.solid(((W - 18) / 2.0 - 4 + page * 18, H - 44, 8, 8), colour, radius=4.0, light=True)
    return screen


def animal_pill():
    """draw_desktops as shell.c draws it (ws181-p006): three desktops, a cat, a bird and a rabbit, the shown one in the accent."""
    import inspect
    bar.DESKTOPS = 3
    bar.ACCENT_PILL = ACCENT
    source = inspect.getsource(bar.draw_bar)
    start = source.index('    for desktop in range(DESKTOPS):')
    end = source.index('    # draw_status')
    new = ('    for desktop in range(DESKTOPS):\n'
           '        x = bar[\'desktops_x\'] + DESKTOPS_PAD + desktop * (DESKTOP_WIDTH + DESKTOP_GAP) + (DESKTOP_WIDTH - 20) // 2\n'
           '        colour = ACCENT_PILL if desktop == state[\'desktop\'] else (1, 1, 1, 0.42)\n'
           '        screen.coverage(assets.icon([\'DESKTOP_CAT\', \'DESKTOP_BIRD\', \'DESKTOP_RABBIT\'][desktop]), x, MID - 10, colour)\n\n')
    exec(compile(source[:start] + new + source[end:], 'draw_bar', 'exec'), bar.__dict__)


def pill_layout():
    return bar.bar_layout(STATE['clock'], STATE['ime'], STATE['media'], STATE['battery'], STATE['charging'])


def menu(screen, layout, slots, lit, windows):
    """kwl_arrange_draw: the shadow, the frosted glass, the seven drawings two to a row, the lit one on the accent."""
    x0 = layout['desktops_x'] + layout['desktops_width'] // 2 - MENU_W // 2
    y0 = BAR + 6
    screen.shadow((x0, y0 + 6, MENU_W, MENU_H), (0.10, 0.18, 0.35, 0.20), soft=18.0, radius=18.0,
                  quad=(x0 - 40, y0 - 34, MENU_W + 80, MENU_H + 80))
    screen.glass((x0, y0, MENU_W, MENU_H), (1, 1, 1, 0.62), radius=18.0, edge=0.70)
    for item in range(LAYOUTS):
        x = x0 + PAD + (item % 2) * (CELL_W + CELL_GAP)
        y = y0 + PAD + (item // 2) * (CELL_H + CELL_GAP)
        width = 2 * CELL_W + CELL_GAP if item == LAYOUTS - 1 else CELL_W
        ink = (0.12, 0.16, 0.24, 1.0) if windows else (0.40, 0.46, 0.56, 1.0)
        if item == lit:
            screen.solid((x, y, width, CELL_H), ACCENT, radius=12.0, light=True)
            ink = (1, 1, 1, 1)
        ix = x + (width - ICON_W) // 2
        iy = y + (CELL_H - ICON_H) // 2
        for slot in slots[item]:
            screen.solid((ix + slot[0], iy + slot[1], slot[2], slot[3]), ink[:3] + (0.55,), radius=3.0, light=True)
    return (x0, y0)


def desktop(assets, picture, slots, lit, desktop_shown):
    """The desktop with a floating window, the bar (p034's drawing), the new pill and the open menu."""
    screen = bar.Screen(picture, False)
    bar.draw_window(screen, assets, False, False, 'terminal', 'Terminal')
    state = {'docked': None, 'apps': [('files', False), ('terminal', False), ('browser', False)], 'lit': -1,
             'current': 1, 'desktop': desktop_shown, 'wifi': STATE['wifi']}
    state.update(STATE)
    bar.draw_bar(screen, assets, state)
    layout = pill_layout()
    origin = menu(screen, layout, slots, lit, True) if lit is not None else None
    return screen, layout, origin


def image(screen):
    return Image.fromarray((np.clip(screen.pixels, 0, 1) * 255 + 0.5).astype(np.uint8))


def main():
    tiles, icons, slots_file, out = sys.argv[1:5]
    assets = bar.Assets(tiles, icons)
    tiles72 = {}
    for name in APPS:
        try:
            tiles72[name] = assets.tile(name, 72)
        except (ValueError, OSError):
            pass
    animal_pill()
    slots = [[] for _ in range(LAYOUTS)]
    for line in open(slots_file):
        parts = line.split()
        slots[int(parts[0])].append(tuple(float(value) for value in parts[1:]))
    picture = wallpaper('Birch-Lake.png')
    small = ImageFont.truetype(bar.FONT, 13)
    big = ImageFont.truetype(bar.FONT, 16)
    ink = (31, 41, 61)
    rows = []
    shot = image(home(assets, picture, tiles72))
    rows.append(('(1) App Home, the first page: the clock, two rows of icons at the bottom, the pages\' dots '
                 '(half size)', shot.resize((W // 2, H // 2), Image.LANCZOS)))
    screen, layout, origin = desktop(assets, picture, slots, 3, 1)
    shot = image(screen)
    left = origin[0] - 30
    rows.append(('(2) The arrangement menu, two to a row, the grid alone ("One on the Right" lit), and the pill',
                 shot.crop((left, 0, left + MENU_W + 60, BAR + MENU_H + 40))))
    for shown in (1, 0, 2):
        screen, layout, _ = desktop(assets, picture, slots, None, shown)
        shot = image(screen)
        x0 = layout['desktops_x'] - 10
        rows.append(('(3) 4x: the pill, the %s desktop shown (cat, bird, rabbit; the shown one in the accent)'
                     % ['left', 'middle', 'right'][shown],
                     shot.crop((x0, 0, x0 + layout['desktops_width'] + 20, BAR)).resize(
                         ((layout['desktops_width'] + 20) * 4, BAR * 4), Image.NEAREST)))
    height = 60 + sum(row[1].height + 30 for row in rows)
    sheet = Image.new('RGB', (max(row[1].width for row in rows) + 40, height), (246, 247, 250))
    draw = ImageDraw.Draw(sheet)
    draw.text((20, 14), 'ws181-p006 (2026-10-07 UAT): App Home, arrangement menu, desktops pill (host render)',
              fill=ink, font=big)
    draw.text((20, 36), 'The p034 host renderer: the panel shader\'s formulas at the code\'s places and colours; blur '
              'approximated; text through PIL.  Not a screenshot of the compositor.', fill=ink, font=small)
    y = 60
    for label, picture_row in rows:
        draw.text((20, y), label, fill=ink, font=small)
        sheet.paste(picture_row, (20, y + 18))
        y += picture_row.height + 30
    sheet.save(out)
    print('p005-host: %s' % out)


if __name__ == '__main__':
    main()
