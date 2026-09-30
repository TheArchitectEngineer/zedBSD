#!/bin/sh
# ws094-p013: the file manager's JPEG and GIF pictures on the host, against Python's PIL: JPEGs (RGB, grey, CMYK) with
# every EXIF orientation turned upright, a still GIF with a transparent colour, an animated GIF's first frame, and damaged
# files refused with EINVAL (a JPEG cut short, a JPEG and a GIF of garbage after their signatures).
#   plan/ws094/tests/host-thumb.sh [OUTDIR]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
sh plan/tools/files/host-build.sh >/dev/null || exit 1
host=build/ws071-host
out=${1:-build/ws094-host-thumb}
mkdir -p "$out"
objects=$(ls $host/obj/*.o | grep -v '/host-')
${CC:-cc} -O2 -g -Wall -Wextra -Werror -Wno-unused-parameter -D_GNU_SOURCE -I$host/include -Iuserland/desktop/files \
    -o $host/host-thumb plan/ws094/tests/host-thumb.c $objects -lm || exit 1
exec python3 - "$host/host-thumb" "$out" <<'PY'
import os, struct, subprocess, sys
from PIL import Image
tool, out = sys.argv[1], sys.argv[2]
failed = 0
EINVAL = 22
def report(name, ok, detail=""):
	global failed
	print("%s: %s%s" % (name, "ok" if ok else "FAILED", (" " + detail) if detail else ""))
	if not ok:
		failed = 1
def load(path):
	result = os.path.join(out, "result.raw")
	if os.path.exists(result):
		os.remove(result)
	done = subprocess.run([tool, path, result], capture_output=True, text=True)
	fields = dict(item.split("=") for item in done.stdout.split())
	error, width, height = int(fields["error"]), int(fields["width"]), int(fields["height"])
	words = []
	if error == 0 and done.returncode == 0:
		data = open(result, "rb").read()
		words = list(struct.unpack("<%dI" % (len(data) // 4), data))
	return error, width, height, words
def premultiplied(image):
	words = []
	for red, green, blue, alpha in image.convert("RGBA").getdata():
		words.append((alpha << 24) | (((red * alpha + 127) // 255) << 16) | (((green * alpha + 127) // 255) << 8) | ((blue * alpha + 127) // 255))
	return words
def worst(a, b):
	most = 0
	for x, y in zip(a, b):
		for shift in (0, 8, 16, 24):
			most = max(most, abs(((x >> shift) & 255) - ((y >> shift) & 255)))
	return most
def compare(name, path, expected, tolerance):
	error, width, height, words = load(path)
	same_size = error == 0 and (width, height) == expected.size
	difference = worst(words, premultiplied(expected)) if same_size else -1
	report(name, same_size and 0 <= difference <= tolerance, "error=%d size=%dx%d worst=%d" % (error, width, height, difference))

# A picture whose every corner differs, so that a wrong turn shows.
base = Image.new("RGB", (48, 32))
pixels = base.load()
for y in range(32):
	for x in range(48):
		pixels[x, y] = ((x * 5) % 256, (y * 7) % 256, 40 if x < 24 else 200)
for y in range(8):
	for x in range(8):
		pixels[x, y] = (255, 0, 0)

# JPEGs: RGB, grey and CMYK, compared with PIL's decoding (libjpeg's too) within a small tolerance.
for name, image in (("rgb", base), ("grey", base.convert("L")), ("cmyk", base.convert("CMYK"))):
	path = os.path.join(out, "jpeg-%s.jpg" % name)
	image.save(path, quality=92)
	compare("jpeg %s" % name, path, Image.open(path), 3)

# Every EXIF orientation: the picture comes upright (as PIL's exif_transpose turns it), 5 to 8 with their sides swapped.
from PIL import ImageOps
for orientation in range(1, 9):
	path = os.path.join(out, "jpeg-exif-%d.jpg" % orientation)
	exif = Image.Exif()
	exif[0x0112] = orientation
	base.save(path, quality=92, exif=exif.tobytes())
	expected = ImageOps.exif_transpose(Image.open(path))
	compare("jpeg exif %d" % orientation, path, expected, 3)

# A still GIF with a transparent colour (its pixels clear), and an animated GIF's first frame.
still = base.quantize(32)
path = os.path.join(out, "still.gif")
still.save(path, transparency=0)
expected = Image.open(path).convert("RGBA")
compare("gif still, transparent colour", path, expected, 0)
frames = [base.quantize(32), base.transpose(Image.Transpose.FLIP_LEFT_RIGHT).quantize(32), base.transpose(Image.Transpose.FLIP_TOP_BOTTOM).quantize(32)]
path = os.path.join(out, "anim.gif")
frames[0].save(path, save_all=True, append_images=frames[1:], duration=[100, 100, 100], loop=0)
first = Image.open(path)
first.seek(0)
compare("gif animated, first frame", path, first.convert("RGBA"), 0)

# Damaged files: refused with EINVAL.
whole = open(os.path.join(out, "jpeg-rgb.jpg"), "rb").read()
damaged = {
	"jpeg cut short": whole[:len(whole) // 3],
	"jpeg of garbage": b"\xff\xd8\xff" + bytes((index * 37) % 251 for index in range(4000)),
	"gif of garbage": b"GIF89a" + bytes((index * 53) % 249 for index in range(4000)),
	"gif without a frame": b"GIF89a\x10\x00\x10\x00\x00\x00\x00\x3b",
}
for name, data in damaged.items():
	path = os.path.join(out, name.replace(" ", "-") + ".bin")
	open(path, "wb").write(data)
	error, width, height, words = load(path)
	report("%s refused" % name, error == EINVAL, "error=%d" % error)

print("host-thumb: %s" % ("PASS" if failed == 0 else "FAIL"))
sys.exit(failed)
PY
