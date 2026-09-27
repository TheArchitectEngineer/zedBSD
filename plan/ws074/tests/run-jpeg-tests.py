#!/usr/bin/env python3
# zedBSD
# Copyright (C) 2026 Awe Morris
# SPDX-License-Identifier: Zlib
"""Tests libjpeg-compat (ws074-p019) against libjpeg-turbo on the host.

  run-jpeg-tests.py [--asan] [--keep DIR]            builds host-jpeg (jpeg-driver.c with the library's sources), makes
                                                     the JPEG files and compares
  run-jpeg-tests.py --make-only                      only makes the JPEG files (build/ws074-jpeg/files)
  run-jpeg-tests.py --outputs DIR                    compares decodings made elsewhere (the guest, jpeg-guest.sh):
                                                     DIR/NAME.pnm for each file, and DIR/NAME.log for the refused ones

The files are made each run (nothing is committed) from synthetic pictures (gradients, noise, edges, text) of several
sizes: Pillow at qualities 10, 75 and 95 with 4:4:4, 4:2:2 and 4:2:0 and optimized tables, gray, and cjpeg
(libjpeg-turbo's) with the sampling factors 1x1, 2x1, 1x2, 2x2, 4x1, 4x2 and 3x1 (luma) and mixed chroma factors,
restart intervals in rows and blocks, RGB (no transform), gray, quality 100, and odd sizes.  Each is decoded to RGB
(or gray) and compared with djpeg's decoding (libjpeg-turbo, the accurate integer IDCT and fancy upsampling, as the
library does): the count of files equal byte for byte, and the largest difference, which must be at most 2 (and with
Pillow's decoding too).  Also: every JCS_EXT_* order against the RGB decoding, gray output of colour files against
djpeg -grayscale, no fancy upsampling against djpeg -nosmooth, the stdio source against the memory one, a file cut at
60% against djpeg's decoding of it (both warn), progressive and arithmetic files refused with their messages, and
damaged files (bytes changed at random) decoded or refused without a crash.
"""

import argparse
import glob
import io
import os
import random
import shlex
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../../.."))
SOURCES = sorted(glob.glob(os.path.join(ROOT, "userland/base/libjpeg-compat/*.c")))
ORDERS = {"rgb": "012", "bgr": "210", "rgbx": "012X", "bgrx": "210X", "xbgr": "X210", "xrgb": "X012",
          "rgba": "012X", "bgra": "210X", "abgr": "X210", "argb": "X012"}


def build(asan, out):
    include = os.path.join(out, "include")
    os.makedirs(include, exist_ok=True)
    link = os.path.join(include, "compat")
    if not os.path.islink(link):
        os.symlink(os.path.join(ROOT, "include/libc/compat"), link)
    program = os.path.join(out, "host-jpeg-asan" if asan else "host-jpeg")
    flags = ["-O2", "-g", "-Wall", "-Wextra", "-Werror", "-I" + include]
    if asan:
        flags += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    subprocess.run(["cc"] + flags + ["-o", program, os.path.join(ROOT, "plan/ws074/tests/jpeg-driver.c")] + SOURCES,
                   check=True)
    return program


def picture(width, height, seed):
    from PIL import Image, ImageDraw
    random.seed(seed)
    image = Image.new("RGB", (width, height))
    pixels = image.load()
    for y in range(height):
        for x in range(width):
            r = (x * 255 // max(1, width - 1) + random.randint(-12, 12)) % 256
            g = (y * 255 // max(1, height - 1) + random.randint(-12, 12)) % 256
            b = ((x ^ y) * 7 + random.randint(-30, 30)) % 256
            pixels[x, y] = (r, g, b)
    draw = ImageDraw.Draw(image)
    if width > 8 and height > 8:
        draw.ellipse((width // 5, height // 5, width * 3 // 4, height * 4 // 5), outline=(255, 255, 0), width=2)
        draw.rectangle((width // 3, height // 3, width // 2, height // 2), fill=(10, 200, 30))
        draw.text((2, 2), "zedBSD", fill=(255, 255, 255))
    return image


def make_files(folder):
    from PIL import Image
    os.makedirs(folder, exist_ok=True)
    files = []
    sizes = [(1, 1), (7, 5), (17, 9), (64, 64), (333, 221), (640, 480)]
    for index, (width, height) in enumerate(sizes):
        image = picture(width, height, index)
        ppm = os.path.join(folder, "src-%dx%d.ppm" % (width, height))
        image.save(ppm)
        for quality in (10, 75, 95):
            for sampling in (0, 1, 2):
                name = os.path.join(folder, "pil-%dx%d-q%d-s%d.jpg" % (width, height, quality, sampling))
                image.save(name, quality=quality, subsampling=sampling)
                files.append(name)
        name = os.path.join(folder, "pil-%dx%d-gray.jpg" % (width, height))
        image.convert("L").save(name, quality=80)
        files.append(name)
        name = os.path.join(folder, "pil-%dx%d-opt.jpg" % (width, height))
        image.save(name, quality=85, optimize=True)
        files.append(name)
        cases = [("s11", ["-sample", "1x1"]), ("s21", ["-sample", "2x1"]), ("s12", ["-sample", "1x2"]),
                 ("s22", ["-sample", "2x2"]), ("s41", ["-sample", "4x1"]), ("s42", ["-sample", "4x2"]),
                 ("s31", ["-sample", "3x1"]), ("mixed", ["-sample", "2x2,2x1,1x2"]),
                 ("rst1", ["-restart", "1"]), ("rst5b", ["-restart", "5B"]), ("rgb", ["-rgb"]),
                 ("gray", ["-grayscale"]), ("q100", ["-quality", "100"]), ("optimize", ["-optimize"])]
        for label, options in cases:
            name = os.path.join(folder, "cjpeg-%dx%d-%s.jpg" % (width, height, label))
            with open(name, "wb") as stream:
                subprocess.run(["cjpeg"] + options + [ppm], stdout=stream, check=True)
            files.append(name)
    refused = []
    image = picture(64, 48, 99)
    ppm = os.path.join(folder, "src-refused.ppm")
    image.save(ppm)
    for label, options in (("progressive", ["-progressive"]), ("arithmetic", ["-arithmetic"])):
        name = os.path.join(folder, "refused-%s.jpg" % label)
        with open(name, "wb") as stream:
            subprocess.run(["cjpeg"] + options + [ppm], stdout=stream, check=True)
        refused.append((name, label))
    return files, refused


def read_pnm(path):
    with open(path, "rb") as stream:
        data = stream.read()
    if data.startswith(b"RAW "):
        header, rest = data.split(b"\n", 1)
        _, width, height, components = header.split()
        return int(width), int(height), int(components), rest
    fields = []
    position = 0
    while len(fields) < 4:
        while data[position:position + 1].isspace():
            position += 1
        if data[position:position + 1] == b"#":
            position = data.index(b"\n", position)
            continue
        end = position
        while not data[end:end + 1].isspace():
            end += 1
        fields.append(data[position:end])
        position = end
    position += 1
    components = 1 if fields[0] == b"P5" else 3
    return int(fields[1]), int(fields[2]), components, data[position:]


def difference(a, b):
    if len(a) != len(b):
        return None
    worst = 0
    for x, y in zip(a, b):
        d = abs(x - y)
        if d > worst:
            worst = d
    return worst


class Runner:
    def __init__(self, driver, temp):
        self.driver = driver
        self.temp = temp
        self.count = 0

    def decode(self, path, options=()):
        self.count += 1
        out = os.path.join(self.temp, "out-%d.pnm" % self.count)
        run = subprocess.run(self.driver + list(options) + [path, out], stdout=subprocess.PIPE,
                             stderr=subprocess.PIPE, text=True, errors="replace")
        if run.returncode != 0 or "runtime error" in run.stderr or "Sanitizer" in run.stderr:
            return None, run.stdout + run.stderr
        return read_pnm(out), run.stdout + run.stderr


def djpeg(path, options, temp):
    out = os.path.join(temp, "djpeg.pnm")
    with open(out, "wb") as stream:
        run = subprocess.run(["djpeg"] + options + [path], stdout=stream, stderr=subprocess.DEVNULL)
    if run.returncode not in (0, 2):
        raise RuntimeError("djpeg failed on %s" % path)
    return read_pnm(out)


def compare_outputs(temp, outputs):
    from PIL import Image
    files = sorted(glob.glob(os.path.join(temp, "files", "*.jpg")))
    failed = 0
    exact = 0
    decoded_count = 0
    for path in files:
        name = os.path.splitext(os.path.basename(path))[0]
        if name.startswith("refused-"):
            with open(os.path.join(outputs, name + ".log"), errors="replace") as stream:
                log = stream.read()
            expected = "Progressive" if "progressive" in name else "Arithmetic"
            if expected not in log:
                print("FAIL %s: %s" % (name, log.strip()[-200:]))
                failed += 1
            continue
        out = os.path.join(outputs, name + ".pnm")
        if not os.path.exists(out):
            print("FAIL %s: no output" % name)
            failed += 1
            continue
        decoded = read_pnm(out)
        reference = djpeg(path, [], temp)
        worst = difference(decoded[3], reference[3])
        if worst is None or worst > 2:
            print("FAIL %s: djpeg %s" % (name, worst))
            failed += 1
            continue
        decoded_count += 1
        if worst == 0:
            exact += 1
    print("guest files: %d decoded, %d equal to djpeg byte for byte" % (decoded_count, exact))
    print("jpeg-tests: %s" % ("PASS" if failed == 0 else "FAIL (%d)" % failed))
    return 0 if failed == 0 else 1


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--asan", action="store_true")
    parser.add_argument("--keep", default=os.path.join(ROOT, "build/ws074-jpeg"))
    parser.add_argument("--driver")
    parser.add_argument("--make-only", action="store_true")
    parser.add_argument("--outputs")
    args = parser.parse_args()
    if args.make_only:
        make_files(os.path.join(args.keep, "files"))
        return 0
    if args.outputs:
        return compare_outputs(args.keep, args.outputs)
    temp = args.keep
    os.makedirs(temp, exist_ok=True)
    if args.driver:
        driver = shlex.split(args.driver)
    else:
        driver = [build(args.asan, temp)]
    files, refused = make_files(os.path.join(temp, "files"))
    runner = Runner(driver, temp)
    failed = 0
    exact = 0
    worst_all = 0

    from PIL import Image
    # 1. Each file against djpeg and Pillow.
    for path in files:
        decoded, log = runner.decode(path)
        if decoded is None:
            print("FAIL %s: %s" % (os.path.basename(path), log.strip()[-200:]))
            failed += 1
            continue
        reference = djpeg(path, [], temp)
        worst = difference(decoded[3], reference[3])
        pil = Image.open(path)
        pil = pil.convert("L" if decoded[2] == 1 else "RGB")
        worst_pil = difference(decoded[3], pil.tobytes())
        if worst is None or worst_pil is None or worst > 2 or worst_pil > 2 or decoded[:2] != reference[:2]:
            print("FAIL %s: djpeg %s pillow %s" % (os.path.basename(path), worst, worst_pil))
            failed += 1
            continue
        if worst == 0:
            exact += 1
        worst_all = max(worst_all, worst, worst_pil)
    print("files: %d decoded, %d equal to djpeg byte for byte, largest difference %d" %
          (len(files) - failed, exact, worst_all))

    # 2. The output orders, gray, no fancy upsampling and the stdio source.
    sample = [p for p in files if "333x221-q75-s2" in p or "cjpeg-333x221-s22" in p or "cjpeg-333x221-mixed" in p
              or "cjpeg-333x221-s21" in p or "cjpeg-333x221-rgb" in p or "pil-333x221-gray" in p]
    for path in sample:
        base, _ = runner.decode(path)
        name = os.path.basename(path)
        if base[2] == 3:
            for order, layout in ORDERS.items():
                decoded, log = runner.decode(path, ["--space=" + order])
                expect = bytearray()
                for pixel in range(len(base[3]) // 3):
                    for place in layout:
                        expect.append(255 if place == "X" else base[3][pixel * 3 + int(place)])
                if decoded is None or decoded[3] != bytes(expect):
                    print("FAIL %s order %s" % (name, order))
                    failed += 1
            decoded, log = runner.decode(path, ["--space=gray"])
            reference = djpeg(path, ["-grayscale"], temp)
            worst = difference(decoded[3], reference[3]) if decoded else None
            if worst is None or worst > 2:
                print("FAIL %s gray: %s" % (name, worst))
                failed += 1
        decoded, log = runner.decode(path, ["--no-fancy"])
        reference = djpeg(path, ["-nosmooth"], temp)
        worst = difference(decoded[3], reference[3]) if decoded else None
        if worst is None or worst > 2:
            print("FAIL %s no-fancy: %s" % (name, worst))
            failed += 1
        decoded, log = runner.decode(path, ["--stdio"])
        if decoded is None or decoded[3] != base[3]:
            print("FAIL %s stdio" % name)
            failed += 1
    print("orders, gray, no-fancy, stdio: %d files" % len(sample))

    # 3. A cut file: both decoders warn and give the same rows.
    for path in [p for p in files if "640x480-q75-s2" in p or "cjpeg-640x480-rst1" in p]:
        with open(path, "rb") as stream:
            data = stream.read()
        cut = os.path.join(temp, "cut-" + os.path.basename(path))
        with open(cut, "wb") as stream:
            stream.write(data[:len(data) * 6 // 10])
        decoded, log = runner.decode(cut)
        reference = djpeg(cut, [], temp)
        worst = difference(decoded[3], reference[3]) if decoded else None
        warned = "warnings=0" not in log
        if worst is None or worst > 2 or not warned:
            print("FAIL cut %s: %s warned=%s" % (os.path.basename(path), worst, warned))
            failed += 1
        else:
            print("cut %s: equal to djpeg (largest difference %d), warned" % (os.path.basename(path), worst))

    # 4. Refused kinds.
    for path, label in refused:
        decoded, log = runner.decode(path)
        expected = {"progressive": "Progressive", "arithmetic": "Arithmetic"}[label]
        if decoded is not None or expected not in log:
            print("FAIL refused %s: %s" % (label, log.strip()[-200:]))
            failed += 1
        else:
            print("refused %s: %s" % (label, log.strip().splitlines()[-1]))

    # 5. Damaged files: decoded or refused, never a crash.
    random.seed(19)
    crashes = 0
    damaged = 0
    for path in [p for p in files if "-64x64-" in p][:12]:
        with open(path, "rb") as stream:
            data = bytearray(stream.read())
        for attempt in range(8):
            copy = bytearray(data)
            for _ in range(1 + attempt):
                copy[random.randrange(2, len(copy))] = random.randrange(256)
            broken = os.path.join(temp, "broken.jpg")
            with open(broken, "wb") as stream:
                stream.write(copy)
            out = os.path.join(temp, "broken.pnm")
            run = subprocess.run(driver + [broken, out], stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                 text=True, errors="replace")
            damaged += 1
            if run.returncode not in (0, 2) or "Sanitizer" in run.stderr or "runtime error" in run.stderr:
                crashes += 1
                print("FAIL damaged %s #%d: status %d %s" % (os.path.basename(path), attempt, run.returncode,
                                                             run.stderr[-300:]))
    if crashes:
        failed += 1
    print("damaged: %d files, %d crashes" % (damaged, crashes))
    print("jpeg-tests: %s" % ("PASS" if failed == 0 else "FAIL (%d)" % failed))
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
