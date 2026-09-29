#!/usr/bin/env python3
"""Makes the pictures the guest test of Image Viewer shows (ws091-p002).

    plan/tools/imageview/make-images.py OUTDIR

From the Kei splash in the tree (userland/desktop/artwork/kei-boot-splash.png) and drawn shapes:
01-splash.png (the splash as it is), 02-landscape.jpg (the splash enlarged to 4032x2268, a phone
photo's size), 03-portrait.jpg (stored on its side with EXIF orientation 6, shown upright),
04-mark.png (a transparent picture), 05-anim.gif (an animated GIF of four frames), 06-pixels.png
(a 24x24 pixel picture, for the sharp enlargement) and 07-broken.png (not an image at all).
Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
"""
import os
import sys

from PIL import Image, ImageDraw

out = sys.argv[1]
os.makedirs(out, exist_ok=True)
root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "../../..")
splash = Image.open(os.path.join(root, "userland/desktop/artwork/kei-boot-splash.png")).convert("RGB")

# The splash, and the splash as a large photo.
splash.save(os.path.join(out, "01-splash.png"))
splash.resize((4032, 2268), Image.LANCZOS).save(os.path.join(out, "02-landscape.jpg"), quality=88)

# A portrait photo stored on its side: the camera's orientation 6 turns it upright.
portrait = splash.crop((500, 0, 1030, 941)).resize((1060, 1882), Image.LANCZOS)
stored = portrait.transpose(Image.ROTATE_90)
exif = Image.Exif()
exif[0x0112] = 6
stored.save(os.path.join(out, "03-portrait.jpg"), quality=88, exif=exif.tobytes())

# A transparent picture: two overlapping translucent panes.
mark = Image.new("RGBA", (600, 600), (0, 0, 0, 0))
draw = ImageDraw.Draw(mark, "RGBA")
draw.rounded_rectangle((150, 60, 290, 520), 70, fill=(120, 160, 240, 170))
draw.ellipse((220, 230, 520, 460), fill=(58, 134, 245, 170))
mark.save(os.path.join(out, "04-mark.png"))

# An animated GIF: a dot going round, four frames.
frames = []
for index in range(4):
    frame = Image.new("RGB", (320, 240), (236, 242, 250))
    draw = ImageDraw.Draw(frame)
    x = [80, 240, 240, 80][index]
    y = [60, 60, 180, 180][index]
    draw.ellipse((x - 30, y - 30, x + 30, y + 30), fill=(47, 124, 246))
    draw.text((140, 110), "%d" % (index + 1), fill=(51, 65, 85))
    frames.append(frame.convert("P", palette=Image.ADAPTIVE, colors=16))
frames[0].save(os.path.join(out, "05-anim.gif"), save_all=True, append_images=frames[1:], duration=400, loop=0)

# A tiny pixel picture.
pixels = Image.new("RGB", (24, 24), (255, 255, 255))
for y in range(24):
    for x in range(24):
        if (x // 4 + y // 4) % 2 == 0:
            pixels.putpixel((x, y), (63, 163, 107))
pixels.putpixel((12, 12), (217, 83, 79))
pixels.save(os.path.join(out, "06-pixels.png"))

# Not an image.
open(os.path.join(out, "07-broken.png"), "w").write("this is not a picture\n")
print("made %s" % out)
