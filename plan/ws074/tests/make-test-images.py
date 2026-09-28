#!/usr/bin/env python3
# zedBSD
# Copyright (C) 2026 Awe Morris
# SPDX-License-Identifier: Zlib
"""Makes the browser's image test page and its images (ws074-p021) in build/ws074-images.

  make-test-images.py [DIR]

The images are drawn here from shapes and gradients (nothing third-party, nothing committed): photo.jpg (320x200,
4:2:0), photo-prog.jpg (progressive), cmyk.jpg (Adobe CMYK), gray.jpg, logo.png (RGBA with a soft alpha edge),
palette.png (8-bit palette), anim.gif (two frames, a transparent colour).  plan/ws074/tests/images/images.html is
copied beside them, with plan/ws074/tests/pages/second.html (its link's target).  build-browser-image.sh puts the
directory in the guest image at /usr/share/browser-images/.
"""

import os
import shutil
import sys

from PIL import Image, ImageDraw

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../../.."))


def photo(width, height):
    image = Image.new("RGB", (width, height))
    pixels = image.load()
    for y in range(height):
        for x in range(width):
            pixels[x, y] = (40 + x * 200 // width, 60 + y * 160 // height, 200 - (x + y) * 150 // (width + height))
    draw = ImageDraw.Draw(image)
    draw.ellipse((width // 4, height // 5, width * 3 // 4, height * 4 // 5), fill=(250, 210, 60), outline=(90, 40, 0),
                 width=3)
    draw.rectangle((10, height - 40, 90, height - 10), fill=(20, 120, 40))
    draw.line((0, 0, width - 1, height - 1), fill=(255, 255, 255), width=2)
    draw.text((12, 10), "Kei", fill=(255, 255, 255))
    return image


def logo(size):
    image = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    pixels = image.load()
    centre = (size - 1) / 2.0
    for y in range(size):
        for x in range(size):
            distance = ((x - centre) ** 2 + (y - centre) ** 2) ** 0.5
            alpha = max(0, min(255, int((size / 2.0 - distance) * 32)))
            pixels[x, y] = (200 - x * 2, 30 + y * 2, 60, alpha)
    draw = ImageDraw.Draw(image)
    draw.ellipse((size // 3, size // 3, size * 2 // 3, size * 2 // 3), fill=(255, 255, 255, 255))
    return image


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "build/ws074-images")
    os.makedirs(out, exist_ok=True)
    picture = photo(320, 200)
    picture.save(os.path.join(out, "photo.jpg"), quality=85, subsampling=2)
    picture.save(os.path.join(out, "photo-prog.jpg"), quality=85, progressive=True)
    picture.convert("CMYK").save(os.path.join(out, "cmyk.jpg"), quality=90)
    picture.convert("L").save(os.path.join(out, "gray.jpg"), quality=85)
    logo(96).save(os.path.join(out, "logo.png"))
    picture.resize((80, 50)).quantize(colors=16).save(os.path.join(out, "palette.png"))
    frames = []
    for step in range(2):
        frame = Image.new("P", (64, 48), 0)
        frame.putpalette([255, 0, 255] + [value for index in range(1, 256) for value in (index, 200 - index % 200, 80)])
        draw = ImageDraw.Draw(frame)
        draw.rectangle((4 + step * 8, 4, 40 + step * 8, 40), fill=30 + step * 60)
        draw.ellipse((30, 10, 60, 44), fill=150)
        frames.append(frame)
    frames[0].save(os.path.join(out, "anim.gif"), save_all=True, append_images=frames[1:], duration=200, loop=0,
                   transparency=0)
    shutil.copy(os.path.join(ROOT, "plan/ws074/tests/images/images.html"), out)
    shutil.copy(os.path.join(ROOT, "plan/ws074/tests/pages/second.html"), out)
    print("make-test-images: %s" % out)


if __name__ == "__main__":
    main()
