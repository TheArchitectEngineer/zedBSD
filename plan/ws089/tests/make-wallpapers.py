#!/usr/bin/env python3
"""ws089-p004: makes two test pictures for the Wallpaper page (build/ws089-wallpapers/, not in git).

    plan/ws089/tests/make-wallpapers.py

"Dusk" is the Kei wallpaper (build/ws035-wallpaper/original-1280.png) tinted towards evening; "Mist" is a
quiet blue-green gradient drawn here.  Both are binary PPMs of 1280x800, as zdesktop reads them.
Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
"""
import os
from PIL import Image, ImageEnhance

out = 'build/ws089-wallpapers'
os.makedirs(out, exist_ok=True)
source = 'build/ws035-wallpaper/original-1280.png'
if os.path.exists(source):
    picture = Image.open(source).convert('RGB').resize((1280, 800))
    evening = Image.new('RGB', picture.size, (255, 140, 90))
    dusk = Image.blend(picture, evening, 0.35)
    ImageEnhance.Brightness(dusk).enhance(0.85).save(os.path.join(out, 'Dusk.ppm'))
mist = Image.new('RGB', (1280, 800))
pixels = mist.load()
for y in range(800):
    for x in range(1280):
        t = y / 799.0
        u = x / 1279.0
        pixels[x, y] = (int(200 - 60 * t + 20 * u), int(222 - 30 * t), int(240 - 50 * t - 30 * u))
mist.save(os.path.join(out, 'Mist.ppm'))
print('made', sorted(os.listdir(out)))
