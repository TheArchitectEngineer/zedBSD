#!/usr/bin/env python3
# zedBSD
# Copyright (C) 2026 Awe Morris
# SPDX-License-Identifier: Zlib
"""Draws a page with browser's CPU renderer and with Chromium, and compares the pictures.

  render-compare.py PAGE.html [--width W] [--height H] [--program PATH] [--out DIR] [--shots DIR --tag TAG]

browser runs `--render --output=OUT.ppm` with the fonts of build/ws035-fonts; Chromium takes
a screenshot through chrome-shot.sh (the same fonts through build/ws074-chrome/fonts.conf).  Both
pictures and a side-by-side picture (ours | Chromium | difference) go to DIR (default
build/ws074-render).  A pixel agrees when no channel differs by more than 16 (design.md §14.5);
prints the share that agrees.  With --shots the side-by-side picture is also copied to
SHOTS/TAG-NAME.png.
"""

import argparse
import os
import shutil
import subprocess
import sys

from PIL import Image, ImageChops

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../../.."))

# ws074-p080: localStorage goes under XDG_DATA_HOME; the browsers this tool starts keep theirs under build/,
# away from the user's ~/.local/share.
os.environ["XDG_DATA_HOME"] = os.path.join(ROOT, "build/ws074-host-data")
THRESHOLD = 16


def render_ours(program, page, width, height, out):
    fonts = os.path.join(ROOT, "build/ws035-fonts")
    ppm = out + ".ppm"
    subprocess.run([program, "--render", "--output=" + ppm, "--width=%d" % width, "--height=%d" % height,
                    "--font=" + os.path.join(fonts, "Inter.ttf"),
                    "--mono-font=" + os.path.join(fonts, "JetBrainsMono-Regular.ttf"),
                    "--fallback-font=" + os.path.join(fonts, "DroidSansFallbackFull.ttf"), page], check=True)
    image = Image.open(ppm).convert("RGB")
    image.save(out)
    os.remove(ppm)
    return image


def render_chrome(page, width, height, out):
    subprocess.run(["sh", os.path.join(ROOT, "plan/ws074/tests/chrome-shot.sh"), page, out, str(width), str(height)],
                   check=True, stdout=subprocess.DEVNULL)
    return Image.open(out).convert("RGB")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("page")
    parser.add_argument("--width", type=int, default=800)
    parser.add_argument("--height", type=int, default=600)
    parser.add_argument("--program", default=os.path.join(ROOT, "build/ws074-host/plain/browser"))
    parser.add_argument("--out", default=os.path.join(ROOT, "build/ws074-render"))
    parser.add_argument("--shots")
    parser.add_argument("--tag", default="render")
    args = parser.parse_args()

    os.makedirs(args.out, exist_ok=True)
    page = os.path.abspath(args.page)
    name = os.path.splitext(os.path.basename(page))[0]
    ours = render_ours(args.program, page, args.width, args.height, os.path.join(args.out, name + "-ours.png"))
    chrome = render_chrome(page, args.width, args.height, os.path.join(args.out, name + "-chrome.png"))
    if chrome.size != ours.size:
        chrome = chrome.crop((0, 0, ours.size[0], ours.size[1]))

    difference = ImageChops.difference(ours, chrome)
    total = ours.size[0] * ours.size[1]
    agree = 0
    for pixel in difference.getdata():
        if max(pixel) <= THRESHOLD:
            agree += 1

    # The difference is shown amplified, so small ones are visible.
    shown = difference.point(lambda value: min(255, value * 4))
    side = Image.new("RGB", (ours.size[0] * 3, ours.size[1]), (255, 255, 255))
    side.paste(ours, (0, 0))
    side.paste(chrome, (ours.size[0], 0))
    side.paste(shown, (ours.size[0] * 2, 0))
    side_path = os.path.join(args.out, name + "-side.png")
    side.save(side_path)
    if args.shots:
        os.makedirs(args.shots, exist_ok=True)
        shutil.copy(side_path, os.path.join(args.shots, "%s-%s.png" % (args.tag, name)))
        shutil.copy(os.path.join(args.out, name + "-ours.png"), os.path.join(args.shots, "%s-%s-ours.png" % (args.tag, name)))

    print("render-compare %s: %d/%d pixels agree within %d (%.2f%%); pictures in %s"
          % (os.path.basename(page), agree, total, THRESHOLD, 100.0 * agree / total, args.out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
