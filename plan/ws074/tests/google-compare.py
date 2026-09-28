#!/usr/bin/env python3
# zedBSD
# Copyright (C) 2026 Awe Morris
# SPDX-License-Identifier: Zlib
"""Draws a live page (the Google demo goal) with browser and with Chromium, and compares them.

  google-compare.py [URL] [--width W] [--height H] [--program PATH] [--out DIR] [--shots DIR --tag TAG]

URL defaults to https://www.google.com/.  browser runs `--render` with the fonts of build/ws035-fonts;
Chromium (headless, the same fonts through build/ws074-chrome/fonts.conf from chrome-fonts.sh) takes a
screenshot with browser's own User-Agent, so both get the same variant of the page, and runs the page's
scripts for five seconds of virtual time.  Both pictures and a side-by-side picture (ours | Chromium |
differing pixels in red) go to DIR (default build/ws074-google).  A pixel agrees when no channel differs by
more than 16; prints the share of all pixels that agree and the share of the pixels that are not white in
either picture ("ink"), which is the number to watch.  With --shots the side-by-side picture is also copied
to SHOTS/TAG.png.

Nothing fetched is kept in the source tree: the pictures and the page stay under build/.
"""

import argparse
import os
import shutil
import subprocess
import sys

from PIL import Image, ImageChops

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../../.."))
THRESHOLD = 16
AGENT = "browser/0.1 (Kei)"


def render_ours(program, url, width, height, out):
    """Draws the page with browser's CPU renderer into a PNG."""
    fonts = os.path.join(ROOT, "build/ws035-fonts")
    ppm = out + ".ppm"
    command = [program, "--render", "--output=" + ppm, "--width=%d" % width, "--height=%d" % height,
               "--font=" + os.path.join(fonts, "Inter.ttf"),
               "--mono-font=" + os.path.join(fonts, "JetBrainsMono-Regular.ttf"),
               "--fallback-font=" + os.path.join(fonts, "DroidSansFallbackFull.ttf"), url]
    result = subprocess.run(command, capture_output=True, text=True, timeout=120)
    if result.returncode != 0:
        sys.stderr.write(result.stderr)
        raise SystemExit("google-compare: browser failed")
    Image.open(ppm).save(out)
    os.unlink(ppm)
    return result.stderr


def render_chromium(url, width, height, out):
    """Takes Chromium's screenshot of the page with browser's User-Agent and fonts."""
    fonts = os.path.join(ROOT, "build/ws074-chrome/fonts.conf")
    if not os.path.exists(fonts):
        subprocess.run(["sh", os.path.join(ROOT, "plan/ws074/tests/chrome-fonts.sh")], check=True)
    env = dict(os.environ)
    env["FONTCONFIG_FILE"] = fonts
    profile = os.path.join(ROOT, "build/ws074-google/chrome-profile")
    shutil.rmtree(profile, ignore_errors=True)
    command = ["chromium", "--headless", "--no-sandbox", "--disable-gpu", "--hide-scrollbars",
               "--force-device-scale-factor=1", "--user-data-dir=" + profile, "--virtual-time-budget=5000",
               "--user-agent=" + AGENT, "--window-size=%d,%d" % (width, height), "--screenshot=" + out, url]
    subprocess.run(command, env=env, capture_output=True, timeout=120)
    shutil.rmtree(profile, ignore_errors=True)
    if not os.path.exists(out):
        raise SystemExit("google-compare: Chromium made no screenshot")


def compare(ours, theirs, side):
    """Counts the agreeing pixels and writes the side-by-side picture."""
    a = Image.open(ours).convert("RGB")
    b = Image.open(theirs).convert("RGB")
    width = min(a.width, b.width)
    height = min(a.height, b.height)
    a = a.crop((0, 0, width, height))
    b = b.crop((0, 0, width, height))
    difference = ImageChops.difference(a, b).load()
    pa = a.load()
    pb = b.load()
    mask = Image.new("RGB", (width, height), (255, 255, 255))
    pm = mask.load()
    agree = 0
    ink = 0
    ink_agree = 0
    for y in range(height):
        for x in range(width):
            same = max(difference[x, y]) <= THRESHOLD
            if same:
                agree += 1
                c = pa[x, y]
                pm[x, y] = (200 + c[0] // 5, 200 + c[1] // 5, 200 + c[2] // 5)
            else:
                pm[x, y] = (230, 30, 30)
            if min(pa[x, y]) >= 240 and min(pb[x, y]) >= 240:
                continue
            ink += 1
            if same:
                ink_agree += 1
    out = Image.new("RGB", (width * 3 + 20, height), (128, 128, 128))
    out.paste(a, (0, 0))
    out.paste(b, (width + 10, 0))
    out.paste(mask, (2 * width + 20, 0))
    out.save(side)
    return agree, width * height, ink_agree, ink


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("url", nargs="?", default="https://www.google.com/")
    parser.add_argument("--width", type=int, default=1280)
    parser.add_argument("--height", type=int, default=900)
    parser.add_argument("--program", default=os.path.join(ROOT, "build/ws074-host/plain/browser"))
    parser.add_argument("--out", default=os.path.join(ROOT, "build/ws074-google"))
    parser.add_argument("--shots")
    parser.add_argument("--tag", default="google")
    args = parser.parse_args()
    os.makedirs(args.out, exist_ok=True)
    ours = os.path.join(args.out, args.tag + "-ours.png")
    theirs = os.path.join(args.out, args.tag + "-chromium.png")
    side = os.path.join(args.out, args.tag + "-side.png")
    console = render_ours(args.program, args.url, args.width, args.height, ours)
    render_chromium(args.url, args.width, args.height, theirs)
    agree, total, ink_agree, ink = compare(ours, theirs, side)
    errors = [line for line in console.splitlines() if "Uncaught" in line]
    print("%s: pixels %.2f%% (%d/%d), ink %.2f%% (%d/%d), script errors %d" % (
        args.url, 100.0 * agree / total, agree, total, 100.0 * ink_agree / max(ink, 1), ink_agree, ink, len(errors)))
    for line in errors[:10]:
        print("  " + line)
    if args.shots:
        os.makedirs(args.shots, exist_ok=True)
        shutil.copy(side, os.path.join(args.shots, args.tag + ".png"))
    print(side)


if __name__ == "__main__":
    main()
