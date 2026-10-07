#!/usr/bin/env python3
# ws181-p007: the host sheet of the arrangement menu's glass and its opening (the 2026-10-07 UAT), drawn with
# ws099-p034's host renderer through p005-host.py (the panel shader's formulas, blur approximated, text through PIL;
# not the real compositor).  The menu's glass is the windows' panels' (white 0.34, rim 0.70, flat) on the scene under
# it blurred (backdrop.c: the wallpaper and the window, an eighth of the size, blurred), as arrange-shell.c draws it;
# opening, it grows from the pill over 180 ms, eased out (1 - (1 - t)^3), dense white (0.92) and half faded becoming
# the glass.  Rows: the menu before (p006: white 0.62 on the blurred wallpaper), then at 45, 90, 135 and 180 ms.
#   p007-host.py TILE_DIR ICON_DIR SLOTS OUT.png
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
import importlib.util
import os
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
SPEC = importlib.util.spec_from_file_location('p005', os.path.join(HERE, 'p005-host.py'))
p5 = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(p5)
bar = p5.bar

OPEN_MS = 180                    # arrange-shell.c ARRANGE_MENU_OPEN_MS
LEAST_SCALE = 0.15               # ARRANGE_MENU_LEAST_SCALE
FIRST_OPACITY = 0.5              # ARRANGE_MENU_FIRST_OPACITY
DENSE = 0.92                     # ARRANGE_MENU_DENSE
WHITE = 0.34                     # ARRANGE_MENU_WHITE (panels.c GLASS_WHITE)
RIM = 0.70                       # ARRANGE_MENU_RIM
RADIUS = 18.0                    # ARRANGE_MENU_RADIUS


def backdrop(screen):
    """backdrop.c: the scene drawn so far at an eighth of the size, blurred twice, for the glass to sample."""
    picture = Image.fromarray((np.clip(screen.pixels, 0, 1) * 255 + 0.5).astype(np.uint8))
    small = picture.resize((bar.W // 8, bar.H // 8), Image.BOX).filter(ImageFilter.GaussianBlur(1.5))
    screen.under = np.asarray(small.resize((bar.W, bar.H), Image.BILINEAR), dtype=np.float64) / 255.0


def grow(layout, grown):
    """arrange_menu_grow: from the pill's width at its middle on the bar to the menu's place and size."""
    pill_middle = layout['desktops_x'] + layout['desktops_width'] // 2
    menu_x = pill_middle - p5.MENU_W // 2
    menu_y = bar.BAR + 6
    first = min(max(layout['desktops_width'] / p5.MENU_W, LEAST_SCALE), 1.0)
    first_x = pill_middle - p5.MENU_W * first * 0.5
    first_y = bar.BAR * 0.5 - p5.MENU_H * first * 0.5
    return {'x': first_x + (menu_x - first_x) * grown, 'y': first_y + (menu_y - first_y) * grown,
            'scale': first + (1 - first) * grown, 'white': DENSE + (WHITE - DENSE) * grown,
            'opacity': FIRST_OPACITY + (1 - FIRST_OPACITY) * grown, 'menu_x': menu_x, 'menu_y': menu_y}


def menu(screen, slots, view, white, lit):
    """kwl_arrange_draw at a view: the shadow, the glass, the seven cells, scaled about the menu's corner, faded."""
    under = screen.pixels.copy()
    s = view['scale']
    x0, y0 = view['x'], view['y']
    screen.shadow((x0, y0 + 6 * s, p5.MENU_W * s, p5.MENU_H * s), (0.10, 0.18, 0.35, 0.20), soft=18.0 * s,
                  radius=RADIUS * s, quad=(x0 - 40, y0 - 40, p5.MENU_W * s + 80, p5.MENU_H * s + 80))
    screen.glass((x0, y0, p5.MENU_W * s, p5.MENU_H * s), (1, 1, 1, white), radius=RADIUS * s, flat=1.0, edge=RIM)
    for item in range(p5.LAYOUTS):
        x = p5.PAD + (item % 2) * (p5.CELL_W + p5.CELL_GAP)
        y = p5.PAD + (item // 2) * (p5.CELL_H + p5.CELL_GAP)
        width = 2 * p5.CELL_W + p5.CELL_GAP if item == p5.LAYOUTS - 1 else p5.CELL_W
        ink = (0.12, 0.16, 0.24, 1.0)
        if item == lit:
            screen.solid((x0 + x * s, y0 + y * s, width * s, p5.CELL_H * s), p5.ACCENT, radius=12.0 * s, light=True)
            ink = (1, 1, 1, 1)
        ix = x0 + (x + (width - p5.ICON_W) // 2) * s
        iy = y0 + (y + (p5.CELL_H - p5.ICON_H) // 2) * s
        for slot in slots[item]:
            screen.solid((ix + slot[0] * s, iy + slot[1] * s, slot[2] * s, slot[3] * s), ink[:3] + (0.55,),
                         radius=3.0 * s, light=True)
    screen.pixels = under * (1 - view['opacity']) + screen.pixels * view['opacity']


def windows(screen):
    """Two floating windows under the menu, as in T1-346's c10-arrange-menu.png: a green body and a yellow one."""
    for x, y, width, height, body in ((264, 44, 400, 332, (0.80, 0.95, 0.80, 1.0)),
                                      (460, 280, 360, 288, (0.96, 0.92, 0.70, 1.0))):
        screen.shadow((x, y + 4, width, height), (0.10, 0.18, 0.35, 0.12), soft=18.0, radius=14.0,
                      quad=(x - 36, y - 32, width + 72, height + 80))
        screen.solid((x, y + 52, width, height - 52), body, radius=14.0, light=True)
        screen.glass((x, y, width, 44), (1, 1, 1, 0.38), radius=14.0, flat=1.0, edge=0.85)


def frame(assets, picture, slots, grown, before):
    """The desktop with a window under the menu, the bar, and the menu as far as it has grown (or as p006 drew it)."""
    screen = bar.Screen(picture, False)
    windows(screen)
    state = {'docked': None, 'apps': [('files', False), ('terminal', False), ('browser', False)], 'lit': -1,
             'current': 1, 'desktop': 1, 'wifi': p5.STATE['wifi']}
    state.update(p5.STATE)
    if not before:
        backdrop(screen)
    bar.draw_bar(screen, assets, state)
    layout = p5.pill_layout()
    view = grow(layout, grown)
    menu(screen, slots, view, 0.62 if before else view['white'], 0)
    return p5.image(screen), view


def main():
    tiles, icons, slots_file, out = sys.argv[1:5]
    assets = bar.Assets(tiles, icons)
    p5.animal_pill()
    slots = [[] for _ in range(p5.LAYOUTS)]
    for line in open(slots_file):
        parts = line.split()
        slots[int(parts[0])].append(tuple(float(value) for value in parts[1:]))
    picture = p5.wallpaper('Birch-Lake.png')
    small = ImageFont.truetype(bar.FONT, 13)
    big = ImageFont.truetype(bar.FONT, 16)
    ink = (31, 41, 61)
    rows = []
    shot, view = frame(assets, picture, slots, 1.0, True)
    left = int(view['menu_x']) - 30
    box = (left, 0, left + p5.MENU_W + 60, bar.BAR + p5.MENU_H + 40)
    rows.append(('Before (p006): white 0.62 on the blurred wallpaper alone (the windows under it do not show)', shot.crop(box)))
    for ms in (45, 90, 135, 180):
        t = ms / OPEN_MS
        grown = 1 - (1 - t) ** 3
        shot, view = frame(assets, picture, slots, grown, False)
        rows.append(('%d ms: grown %.2f, scale %.2f, white %.2f, opacity %.2f%s' % (
            ms, grown, view['scale'], view['white'], view['opacity'],
            ' (the end: the windows\' panels\' glass on the scene blurred)' if ms == OPEN_MS else ''), shot.crop(box)))
    columns = 2
    cell_w = rows[0][1].width
    cell_h = rows[0][1].height + 24
    count = len(rows)
    sheet = Image.new('RGB', (columns * (cell_w + 20) + 20, 60 + ((count + columns - 1) // columns) * (cell_h + 10)),
                      (246, 247, 250))
    draw = ImageDraw.Draw(sheet)
    draw.text((20, 14), 'ws181-p007 (2026-10-07 UAT): the arrangement menu\'s glass and its opening (host render)',
              fill=ink, font=big)
    draw.text((20, 36), 'The p034 host renderer: the panel shader\'s formulas at the code\'s places and colours; blur '
              'approximated; text through PIL.  Not a screenshot of the compositor.', fill=ink, font=small)
    for index, (label, picture_row) in enumerate(rows):
        x = 20 + (index % columns) * (cell_w + 20)
        y = 60 + (index // columns) * (cell_h + 10)
        draw.text((x, y), label, fill=ink, font=small)
        sheet.paste(picture_row, (x, y + 18))
    sheet.save(out)
    print('p007-host: %s' % out)


if __name__ == '__main__':
    main()
