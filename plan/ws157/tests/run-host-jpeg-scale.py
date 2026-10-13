#!/usr/bin/env python3
# zedBSD
# Copyright (C) 2026 Awe Morris
# SPDX-License-Identifier: Zlib
"""Tests libjpeg-compat's scaled decoding (ws157-p017) against libjpeg-turbo on the host.

  run-host-jpeg-scale.py [--out DIR]

Builds host-jpeg-scale (host-jpeg-scale.c with the library's sources, ASan and UBSan), makes JPEG files (Pillow at
4:4:4, 4:2:2 and 4:2:0, gray, odd sizes; cjpeg with the sampling factors 2x1, 1x2, 2x2, 4x1 and 3x1, restarts, and
progressive) from synthetic pictures, and decodes each at 1/1, 1/2, 1/4 and 1/8.  For each: the size equals djpeg's
(-scale 1/N), the largest difference from djpeg's samples is at most 2 (the reduced transforms round a little
differently from libjpeg's), and the PSNR against djpeg's is at least 55 dB.  Damaged files (bytes of the
sequential and progressive files changed at random, fixed seeds) are decoded at each scale or refused without a crash
(ASan and UBSan stop the driver with another status).  The last line is
"jpeg scale: all PASS".
"""

import argparse
import io
import math
import os
import random
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../../.."))
LIBRARY = ["error.c", "memory.c", "source.c", "marker.c", "huffman.c", "idct.c", "decompress.c"]


def build(out):
    include = os.path.join(out, "include")
    os.makedirs(include, exist_ok=True)
    link = os.path.join(include, "compat")
    if not os.path.islink(link):
        os.symlink(os.path.join(ROOT, "include/libc/compat"), link)
    program = os.path.join(out, "host-jpeg-scale")
    flags = ["-std=gnu11", "-O1", "-g", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
             "-fno-sanitize-recover=all", "-I" + include]
    sources = [os.path.join(ROOT, "userland/desktop/libjpeg-compat", name) for name in LIBRARY]
    subprocess.run(["cc"] + flags + ["-o", program, os.path.join(ROOT, "plan/ws157/tests/host-jpeg-scale.c")] + sources,
                   check=True)
    return program


def picture(width, height, seed):
    from PIL import Image, ImageDraw
    rng = random.Random(seed)
    image = Image.new("RGB", (width, height))
    pixels = image.load()
    for y in range(height):
        for x in range(width):
            pixels[x, y] = ((x * 255) // max(width - 1, 1), (y * 255) // max(height - 1, 1),
                            (x * y + rng.randrange(64)) % 256)
    draw = ImageDraw.Draw(image)
    for index in range(12):
        x0 = rng.randrange(width)
        y0 = rng.randrange(height)
        draw.rectangle([x0, y0, x0 + rng.randrange(1, 40), y0 + rng.randrange(1, 40)],
                       fill=(rng.randrange(256), rng.randrange(256), rng.randrange(256)))
    draw.text((3, 3), "zedBSD", fill=(255, 255, 255))
    return image


def make_files(directory):
    from PIL import Image
    os.makedirs(directory, exist_ok=True)
    files = []
    base = picture(203, 157, 1)
    for name, sampling in (("pil-444", 0), ("pil-422", 1), ("pil-420", 2)):
        path = os.path.join(directory, name + ".jpg")
        base.save(path, quality=90, subsampling=sampling)
        files.append(path)
    path = os.path.join(directory, "pil-gray.jpg")
    base.convert("L").save(path, quality=85)
    files.append(path)
    path = os.path.join(directory, "pil-odd.jpg")
    picture(17, 9, 2).save(path, quality=75, subsampling=2)
    files.append(path)
    path = os.path.join(directory, "pil-tiny.jpg")
    picture(3, 2, 3).save(path, quality=75, subsampling=2)
    files.append(path)
    ppm = os.path.join(directory, "base.ppm")
    picture(241, 133, 4).save(ppm)
    for name, options in (("cjpeg-2x1", ["-sample", "2x1"]), ("cjpeg-1x2", ["-sample", "1x2"]),
                          ("cjpeg-2x2", ["-sample", "2x2"]), ("cjpeg-4x1", ["-sample", "4x1"]),
                          ("cjpeg-3x1", ["-sample", "3x1"]), ("cjpeg-4x2", ["-sample", "4x2"]),
                          ("cjpeg-restart", ["-sample", "2x2", "-restart", "3B"]),
                          ("cjpeg-progressive", ["-progressive"]),
                          ("cjpeg-progressive-444", ["-progressive", "-sample", "1x1"]),
                          ("cjpeg-q100", ["-quality", "100", "-sample", "1x1"])):
        path = os.path.join(directory, name + ".jpg")
        subprocess.run(["cjpeg"] + options + ["-outfile", path, ppm], check=True)
        files.append(path)
    return files


def read_pnm(data):
    parts = []
    at = 0
    while len(parts) < 4:
        while data[at:at + 1].isspace():
            at += 1
        start = at
        while not data[at:at + 1].isspace():
            at += 1
        parts.append(data[start:at])
    return parts[0].decode(), int(parts[1]), int(parts[2]), data[at + 1:]


def compare(program, path, denom, work):
    out = os.path.join(work, "out.pnm")
    run = subprocess.run([program, str(denom), path, out], capture_output=True, text=True)
    if run.returncode != 0:
        return "decode failed: " + run.stdout + run.stderr
    with open(out, "rb") as handle:
        mine = read_pnm(handle.read())
    theirs = read_pnm(subprocess.run(["djpeg", "-scale", "1/%d" % denom, "-pnm", path], capture_output=True,
                                     check=True).stdout)
    if mine[:3] != theirs[:3]:
        return "size %s %dx%d, djpeg %s %dx%d" % (mine[0], mine[1], mine[2], theirs[0], theirs[1], theirs[2])
    largest = 0
    squares = 0
    for a, b in zip(mine[3], theirs[3]):
        difference = abs(a - b)
        squares += difference * difference
        if difference > largest:
            largest = difference
    count = len(theirs[3])
    psnr = 99.0
    if squares != 0:
        psnr = 10.0 * math.log10(255.0 * 255.0 * count / squares)
    line = "%dx%d largest %d psnr %.1f" % (mine[1], mine[2], largest, psnr)
    if largest > 2 or psnr < 55.0:
        return "FAIL " + line
    return "ok " + line


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--out", default=os.path.join(ROOT, "build/ws157-p017-jpeg"))
    arguments = parser.parse_args()
    program = build(arguments.out)
    files = make_files(os.path.join(arguments.out, "files"))
    failed = 0
    for path in files:
        for denom in (1, 2, 4, 8):
            result = compare(program, path, denom, arguments.out)
            print("%s 1/%d: %s" % (os.path.basename(path), denom, result))
            if not result.startswith("ok"):
                failed += 1
    crashes = 0
    damaged = 0
    for path in files:
        if "pil-420" not in path and "progressive" not in path and "4x2" not in path:
            continue
        with open(path, "rb") as handle:
            original = handle.read()
        for seed in range(12):
            rng = random.Random(seed)
            data = bytearray(original)
            for index in range(rng.randrange(1, 8)):
                data[rng.randrange(2, len(data))] = rng.randrange(256)
            broken = os.path.join(arguments.out, "damaged.jpg")
            with open(broken, "wb") as handle:
                handle.write(data)
            for denom in (2, 4, 8):
                damaged += 1
                run = subprocess.run([program, str(denom), broken, os.path.join(arguments.out, "damaged.pnm")],
                                     capture_output=True, text=True)
                if run.returncode not in (0, 2):
                    crashes += 1
                    print("damaged %s seed %d 1/%d: status %d\n%s" % (os.path.basename(path), seed, denom,
                                                                       run.returncode, run.stderr[-2000:]))
    print("damaged: %d decodings, %d crashes" % (damaged, crashes))
    failed += crashes
    if failed:
        print("jpeg scale: %d FAILED" % failed)
        return 1
    print("jpeg scale: all PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
