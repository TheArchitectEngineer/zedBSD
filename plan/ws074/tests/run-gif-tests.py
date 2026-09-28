#!/usr/bin/env python3
# zedBSD
# Copyright (C) 2026 Awe Morris
# SPDX-License-Identifier: Zlib
"""Tests libgif-compat (ws074-p051) against giflib on the host.

  run-gif-tests.py [--asan] [--keep DIR]      builds host-gif (gif-driver.c with the library's sources) and ref-gif (the
                                              same driver against the host's giflib, Debian's libgif-dev), makes the
                                              GIF files and compares
  run-gif-tests.py --make-only                only makes the GIF files (build/ws074-gif/files)
  run-gif-tests.py --outputs DIR              compares decodings made elsewhere (the guest, gif-guest.sh): DIR/NAME.txt
                                              and DIR/NAME.raw for each file

The files are made each run (nothing is committed) by Pillow from synthetic pictures: sizes from 1x1 to 640x480,
palettes of 2, 4, 16, 64 and 256 colours, gray, interlaced and not, a transparent colour, animations whose frames
have their own palettes (local colour maps), durations, disposal modes and a loop (the NETSCAPE application
extension), a comment, and noise large enough to fill the LZW table.  Each is read by name and through a callback;
the driver's report (the screen, the colour maps, each image's description, extensions and graphic control block,
the result of DGifSlurp) and the rasters must equal giflib's byte for byte.  Also: a file cut short keeps the pixels
decoded so far (giflib drops it) and reports the error giflib reports, a file that is not a GIF is refused, and damaged files (bytes
changed at random) are read or refused without a crash.
"""

import argparse
import os
import random
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../../.."))
SOURCES = [os.path.join(ROOT, "userland/base/libgif-compat", name) for name in ("decode.c", "lzw.c")]
DRIVER = os.path.join(ROOT, "plan/ws074/tests/gif-driver.c")


def build(asan, out):
    include = os.path.join(out, "include")
    os.makedirs(include, exist_ok=True)
    link = os.path.join(include, "compat")
    if not os.path.islink(link):
        os.symlink(os.path.join(ROOT, "include/libc/compat"), link)
    program = os.path.join(out, "host-gif-asan" if asan else "host-gif")
    flags = ["-O2", "-g", "-Wall", "-Wextra", "-Werror", "-I" + include]
    if asan:
        flags += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    subprocess.run(["cc"] + flags + ["-o", program, DRIVER] + SOURCES, check=True)
    reference = os.path.join(out, "ref-gif")
    subprocess.run(["cc", "-O2", "-Wall", "-Wextra", "-Werror", "-DGIF_DRIVER_GIFLIB", "-o", reference, DRIVER, "-lgif"],
                   check=True)
    return program, reference


def picture(width, height, seed):
    from PIL import Image, ImageDraw
    random.seed(seed)
    image = Image.new("RGB", (width, height))
    pixels = image.load()
    for y in range(height):
        for x in range(width):
            pixels[x, y] = ((x * 255 // max(1, width - 1)) % 256, (y * 255 // max(1, height - 1)) % 256,
                            ((x ^ y) * 5) % 256)
    draw = ImageDraw.Draw(image)
    if width > 8 and height > 8:
        draw.ellipse((width // 5, height // 5, width * 3 // 4, height * 4 // 5), outline=(255, 255, 0), width=2)
        draw.text((2, 2), "zedBSD", fill=(255, 255, 255))
    return image


def make_files(folder):
    from PIL import Image
    os.makedirs(folder, exist_ok=True)
    for old in os.listdir(folder):
        if old.endswith(".gif"):
            os.remove(os.path.join(folder, old))
    files = []
    for index, (width, height) in enumerate([(1, 1), (7, 5), (17, 9), (64, 64), (333, 221), (640, 480)]):
        image = picture(width, height, index)
        for colors in (2, 4, 16, 64, 256):
            quantized = image.quantize(colors=colors)
            for interlace in (False, True):
                name = os.path.join(folder, "pil-%dx%d-c%d-i%d.gif" % (width, height, colors, int(interlace)))
                quantized.save(name, interlace=interlace)
                files.append(name)
        name = os.path.join(folder, "pil-%dx%d-gray.gif" % (width, height))
        image.convert("L").save(name)
        files.append(name)
        name = os.path.join(folder, "pil-%dx%d-transparent.gif" % (width, height))
        image.quantize(colors=32).save(name, transparency=3, comment=b"zedBSD test")
        files.append(name)
    # Animations: frames with their own palettes, sizes and disposal, a loop.
    frames = []
    for step in range(5):
        frame = picture(96, 64, 50 + step).rotate(step * 20).quantize(colors=16 + step * 40)
        frames.append(frame)
    name = os.path.join(folder, "anim-loop.gif")
    frames[0].save(name, save_all=True, append_images=frames[1:], duration=[40, 80, 120, 160, 200], loop=0,
                   disposal=[0, 1, 2, 3, 1])
    files.append(name)
    name = os.path.join(folder, "anim-once.gif")
    frames[0].save(name, save_all=True, append_images=frames[1:3], duration=100, transparency=0, disposal=2)
    files.append(name)
    # Noise that fills the LZW table.
    random.seed(7)
    noise = Image.new("P", (512, 512))
    noise.putdata([random.randrange(256) for _ in range(512 * 512)])
    noise.putpalette([value for index in range(256) for value in (index, 255 - index, (index * 7) % 256)])
    name = os.path.join(folder, "noise-512.gif")
    noise.save(name)
    files.append(name)
    return files


def run(program, path, options, temp):
    out = os.path.join(temp, "raster.raw")
    if os.path.exists(out):
        os.remove(out)
    result = subprocess.run([program] + options + [path, out], stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                            text=True, errors="replace")
    raster = b""
    if os.path.exists(out):
        with open(out, "rb") as stream:
            raster = stream.read()
    return result.returncode, result.stdout, result.stderr, raster


def compare_outputs(temp, outputs, reference):
    failed = 0
    count = 0
    for path in sorted(os.listdir(os.path.join(temp, "files"))):
        name = os.path.splitext(path)[0]
        full = os.path.join(temp, "files", path)
        _, expect_text, _, expect_raster = run(reference, full, [], temp)
        with open(os.path.join(outputs, name + ".txt"), errors="replace") as stream:
            text = stream.read()
        raster = b""
        if os.path.exists(os.path.join(outputs, name + ".raw")):
            with open(os.path.join(outputs, name + ".raw"), "rb") as stream:
                raster = stream.read()
        count += 1
        if text != expect_text or raster != expect_raster:
            print("FAIL %s" % name)
            failed += 1
    print("guest files: %d read, %d equal to giflib" % (count, count - failed))
    print("gif-tests: %s" % ("PASS" if failed == 0 else "FAIL (%d)" % failed))
    return 0 if failed == 0 else 1


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--asan", action="store_true")
    parser.add_argument("--keep", default=os.path.join(ROOT, "build/ws074-gif"))
    parser.add_argument("--make-only", action="store_true")
    parser.add_argument("--outputs")
    args = parser.parse_args()
    temp = args.keep
    os.makedirs(temp, exist_ok=True)
    if args.make_only:
        make_files(os.path.join(temp, "files"))
        return 0
    program, reference = build(args.asan, temp)
    if args.outputs:
        return compare_outputs(temp, args.outputs, reference)
    files = make_files(os.path.join(temp, "files"))
    failed = 0
    equal = 0

    # 1. Every file, by name and through the callback, against giflib.
    for path in files:
        for options in ([], ["--callback"]):
            status, text, errors, raster = run(program, path, options, temp)
            expect = run(reference, path, options, temp)
            if (status, text, raster) != (expect[0], expect[1], expect[3]) or "Sanitizer" in errors \
                    or "runtime error" in errors:
                print("FAIL %s %s" % (os.path.basename(path), " ".join(options)))
                print("  ours:   %s" % text.strip().splitlines()[-3:])
                print("  giflib: %s" % expect[1].strip().splitlines()[-3:])
                failed += 1
            else:
                equal += 1
    print("files: %d, %d reads equal to giflib (report and rasters)" % (len(files), equal))

    # 2. Files cut short: the same report (the error and the images) and the same pixels as giflib.
    for path in [p for p in files if "640x480-c256-i0" in p or "640x480-c16-i1" in p or "anim-loop" in p]:
        with open(path, "rb") as stream:
            data = stream.read()
        cut = os.path.join(temp, "cut-" + os.path.basename(path))
        with open(cut, "wb") as stream:
            stream.write(data[:len(data) * 6 // 10])
        status, text, errors, raster = run(program, cut, [], temp)
        expect = run(reference, cut, [], temp)
        # libgif-compat keeps the image being read (giflib drops it), so it counts one image more.
        slurp = [line for line in text.splitlines() if line.startswith("slurp:")]
        expect_slurp = [line for line in expect[1].splitlines() if line.startswith("slurp:")]
        if expect_slurp and "images=" in expect_slurp[0]:
            head, count = expect_slurp[0].rsplit("images=", 1)
            expect_slurp = ["%simages=%d" % (head, int(count) + 1)]
        if slurp != expect_slurp or "Sanitizer" in errors:
            print("FAIL cut %s: %s / giflib %s" % (os.path.basename(path), slurp, expect_slurp))
            failed += 1
        else:
            print("cut %s: %s (giflib's error, the partial image kept); earlier images equal: %s" %
                  (os.path.basename(path), slurp[0], raster[:len(expect[3])] == expect[3]))

    # 3. Not a GIF.
    other = os.path.join(temp, "not-a-gif.gif")
    with open(other, "wb") as stream:
        stream.write(b"\x89PNG\r\n\x1a\n" + bytes(40))
    status, text, _, _ = run(program, other, [], temp)
    expect = run(reference, other, [], temp)
    if text != expect[1] or status != 2:
        print("FAIL not a GIF: %s / giflib %s" % (text.strip(), expect[1].strip()))
        failed += 1
    else:
        print("not a GIF: %s" % text.strip())

    # 4. Damaged files: read or refused, never a crash.
    random.seed(51)
    crashes = 0
    damaged = 0
    for path in [p for p in files if "64x64" in p or "anim" in p]:
        with open(path, "rb") as stream:
            data = bytearray(stream.read())
        for attempt in range(6):
            copy = bytearray(data)
            for _ in range(1 + attempt):
                copy[random.randrange(6, len(copy))] = random.randrange(256)
            broken = os.path.join(temp, "broken.gif")
            with open(broken, "wb") as stream:
                stream.write(copy)
            status, text, errors, _ = run(program, broken, [], temp)
            damaged += 1
            if status not in (0, 2) or "Sanitizer" in errors or "runtime error" in errors:
                crashes += 1
                print("FAIL damaged %s #%d: status %d %s" % (os.path.basename(path), attempt, status, errors[-300:]))
    if crashes:
        failed += 1
    print("damaged: %d files, %d crashes" % (damaged, crashes))
    print("gif-tests: %s" % ("PASS" if failed == 0 else "FAIL (%d)" % failed))
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
