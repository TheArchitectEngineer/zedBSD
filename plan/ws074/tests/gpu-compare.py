#!/usr/bin/env python3
# zedBSD
# Copyright (C) 2026 Awe Morris
# SPDX-License-Identifier: Zlib
"""Compares zdesktop-browser's GPU renderer with its CPU reference renderer (design.md §8.2).

  gpu-compare.py PAGE.html... [--width W] [--height H] [--program PATH] [--out DIR]
  gpu-compare.py --pictures GPU.ppm CPU.ppm [--out DIR]

The first form draws each page with `--render-gpu` and `--render` (the fonts of build/ws035-fonts;
on the host the GPU is the one Vulkan picks, lavapipe with VK_DRIVER_FILES); the second compares two
pictures already drawn (the guest's).  A pair agrees when no channel of any pixel differs by more
than 2, allowing 0.1% of the pixels to differ by more.  A side-by-side picture (GPU | CPU |
difference x32) goes to DIR (default build/ws074-gpu).  Exits 1 when a pair does not agree.
"""

import argparse
import os
import subprocess
import sys

from PIL import Image, ImageChops

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../../.."))
CHANNEL_LIMIT = 2
SHARE_LIMIT = 0.001


def draw(program, mode, page, width, height, out):
    fonts = os.path.join(ROOT, "build/ws035-fonts")
    subprocess.run([program, mode, "--output=" + out, "--width=%d" % width, "--height=%d" % height,
                    "--font=" + os.path.join(fonts, "Inter.ttf"),
                    "--mono-font=" + os.path.join(fonts, "JetBrainsMono-Regular.ttf"),
                    "--fallback-font=" + os.path.join(fonts, "DroidSansFallbackFull.ttf"), page], check=True)


def compare(name, gpu_path, cpu_path, out):
    gpu = Image.open(gpu_path).convert("RGB")
    cpu = Image.open(cpu_path).convert("RGB")
    difference = ImageChops.difference(gpu, cpu)
    total = gpu.size[0] * gpu.size[1]
    over = 0
    largest = 0
    for pixel in difference.getdata():
        channel = max(pixel)
        if channel > largest:
            largest = channel
        if channel > CHANNEL_LIMIT:
            over += 1
    side = Image.new("RGB", (gpu.size[0] * 3, gpu.size[1]), (255, 255, 255))
    side.paste(gpu, (0, 0))
    side.paste(cpu, (gpu.size[0], 0))
    side.paste(difference.point(lambda value: min(255, value * 32)), (gpu.size[0] * 2, 0))
    side.save(os.path.join(out, name + "-gpu-cpu.png"))
    agree = over <= total * SHARE_LIMIT
    print("gpu-compare %s: largest channel difference %d, %d/%d pixels over %d (%.4f%%): %s"
          % (name, largest, over, total, CHANNEL_LIMIT, 100.0 * over / total, "agree" if agree else "DIFFER"))
    return agree


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("pages", nargs="*")
    parser.add_argument("--pictures", nargs=2)
    parser.add_argument("--width", type=int, default=800)
    parser.add_argument("--height", type=int, default=600)
    parser.add_argument("--program", default=os.path.join(ROOT, "build/ws074-host/plain/zdesktop-browser"))
    parser.add_argument("--out", default=os.path.join(ROOT, "build/ws074-gpu"))
    args = parser.parse_args()
    os.makedirs(args.out, exist_ok=True)

    ok = True
    if args.pictures:
        name = os.path.splitext(os.path.basename(args.pictures[0]))[0]
        ok = compare(name, args.pictures[0], args.pictures[1], args.out)
    for page in args.pages:
        name = os.path.splitext(os.path.basename(page))[0]
        gpu_path = os.path.join(args.out, name + "-gpu.ppm")
        cpu_path = os.path.join(args.out, name + "-cpu.ppm")
        draw(args.program, "--render-gpu", os.path.abspath(page), args.width, args.height, gpu_path)
        draw(args.program, "--render", os.path.abspath(page), args.width, args.height, cpu_path)
        if not compare(name, gpu_path, cpu_path, args.out):
            ok = False
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
