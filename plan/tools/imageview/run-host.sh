#!/bin/sh
# ws091-p002: Image Viewer's decoding, folder and view on the host, against PIL.
# Builds build/ws091-host/host-imageview (host-imageview.c with image.c, folder.c, view.c and the
# sources of libz-compat, libpng-compat, libjpeg-compat and libgif-compat) and checks:
#  1. unit: the folder's order (digits as numbers), the names shown, the EXIF orientation in both byte orders.
#  2. PNG: RGB, RGBA (over the checkerboard), gray 16-bit and a palette with a transparent colour decode to what PIL
#     reads (composed over the same checkerboard); the levels halve down to 256 pixels.
#  3. JPEG: baseline, progressive and gray decode within 2 of PIL (both are libjpeg's arithmetic); a file with
#     EXIF orientation 6 comes out turned as PIL's exif_transpose turns it; CMYK as RGB.
#  4. GIF: a still one; an animated one's frames (with a transparent colour and DISPOSE_BACKGROUND) equal PIL's
#     composed frames over the checkerboard, with their delays.
#  5. view: fit, zoom about a point, turns, the quad, next and previous, the neighbours decoded ahead, a swipe.
#
# ws090-p008: the chooser is libkeiland's file chooser (a window of its own), so chooser.c is gone.
#   plan/tools/imageview/run-host.sh [OUTDIR]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
out=${1:-build/ws091-host}
mkdir -p "$out/include"
ln -sfn "$(pwd)/include/libc/compat" "$out/include/compat"
cc=${CC:-cc}
"$cc" -O1 -g -std=gnu11 -Wall -Wextra -Werror -Wno-unused-parameter -I"$out/include" -o "$out/host-imageview" \
    plan/tools/imageview/host-imageview.c userland/desktop/imageview/image.c userland/desktop/picture/picture.c userland/desktop/imageview/folder.c \
    userland/desktop/imageview/view.c \
    userland/base/libz-compat/inflate.c userland/base/libz-compat/checksum.c userland/base/libpng-compat/read.c \
    userland/base/libjpeg-compat/decompress.c userland/base/libjpeg-compat/error.c userland/base/libjpeg-compat/huffman.c \
    userland/base/libjpeg-compat/idct.c userland/base/libjpeg-compat/marker.c userland/base/libjpeg-compat/memory.c \
    userland/base/libjpeg-compat/source.c userland/base/libgif-compat/decode.c userland/base/libgif-compat/lzw.c -lm
exec python3 - "$out/host-imageview" "$out" <<'EOF'
import os, random, subprocess, sys
from PIL import Image, ImageOps
tool, out = sys.argv[1], sys.argv[2]
failed = 0
def report(name, ok, detail=""):
	global failed
	print("%s: %s%s" % (name, "ok" if ok else "FAILED", (" " + detail) if detail else ""))
	if not ok:
		failed = 1
def run(*args):
	return subprocess.run([tool] + list(args), capture_output=True, text=True)
def checker(width, height):
	image = Image.new("RGB", (width, height))
	pixels = image.load()
	for y in range(height):
		for x in range(width):
			light = ((x // 8) + (y // 8)) % 2 == 0
			pixels[x, y] = (0xf2, 0xf4, 0xf7) if light else (0xe3, 0xe7, 0xec)
	return image
def over_checker(image):
	rgba = image.convert("RGBA")
	ground = checker(*rgba.size).convert("RGBA")
	return Image.alpha_composite(ground, rgba).convert("RGB")
def compare(name, path, expected, tolerance, extra=""):
	result = run("decode", path, path + ".raw")
	fields = result.stdout.split()
	if result.returncode != 0 or len(fields) < 7 or fields[6] != "0":
		report(name, False, "decode: %s %s" % (result.stdout.strip(), result.stderr.strip()[-200:]))
		return
	width, height = int(fields[0]), int(fields[1])
	if (width, height) != expected.size:
		report(name, False, "size %dx%d expected %dx%d" % (width, height, expected.size[0], expected.size[1]))
		return
	data = open(path + ".raw", "rb").read()
	got = Image.frombytes("RGBA", (width, height), data).convert("RGB").tobytes()
	want = expected.convert("RGB").tobytes()
	worst = max(abs(a - b) for a, b in zip(got, want)) if got else 0
	levels = int(fields[2])
	report(name, worst <= tolerance, "worst %d levels %d %s" % (worst, levels, extra))
	return fields

# 1. unit.
result = run("unit")
bad = [line for line in result.stdout.splitlines() if not line.startswith("ok ")]
report("unit", result.returncode == 0 and not bad, "; ".join(bad[:5]))

# A photo-like picture: gradients, noise and shapes.
random.seed(91)
def picture(width, height):
	image = Image.new("RGB", (width, height))
	pixels = image.load()
	for y in range(height):
		for x in range(width):
			pixels[x, y] = ((x * 255) // max(1, width - 1), (y * 255) // max(1, height - 1), (x * y + random.randrange(64)) % 256)
	return image
base = picture(640, 480)

# 2. PNG.
base.save(os.path.join(out, "rgb.png"))
compare("png rgb", os.path.join(out, "rgb.png"), base, 0)
alpha = base.convert("RGBA")
alpha.putalpha(Image.linear_gradient("L").resize(base.size))
alpha.save(os.path.join(out, "rgba.png"))
compare("png rgba over checker", os.path.join(out, "rgba.png"), over_checker(alpha), 1)
gray16 = Image.linear_gradient("L").resize((300, 200)).convert("I;16")
gray16.save(os.path.join(out, "gray16.png"))
compare("png gray 16", os.path.join(out, "gray16.png"), Image.open(os.path.join(out, "gray16.png")).point(lambda v: v / 256).convert("L").convert("RGB"), 1)
palette = base.convert("P", palette=Image.ADAPTIVE, colors=16)
palette.save(os.path.join(out, "palette.png"), transparency=3)
compare("png palette tRNS", os.path.join(out, "palette.png"), over_checker(Image.open(os.path.join(out, "palette.png"))), 1)
big = picture(3000, 2000)
big.save(os.path.join(out, "big.png"))
fields = compare("png levels", os.path.join(out, "big.png"), big, 0)
report("png level count", fields is not None and int(fields[2]) == 5, "levels %s (3000 -> 1500 -> 750 -> 375 -> 188)" % (fields[2] if fields else "?"))

# 3. JPEG.
base.save(os.path.join(out, "base.jpg"), quality=90)
compare("jpeg baseline", os.path.join(out, "base.jpg"), Image.open(os.path.join(out, "base.jpg")), 2)
base.save(os.path.join(out, "prog.jpg"), quality=85, progressive=True)
compare("jpeg progressive", os.path.join(out, "prog.jpg"), Image.open(os.path.join(out, "prog.jpg")), 2)
base.convert("L").save(os.path.join(out, "gray.jpg"), quality=90)
compare("jpeg gray", os.path.join(out, "gray.jpg"), Image.open(os.path.join(out, "gray.jpg")).convert("RGB"), 2)
exif = Image.Exif()
exif[0x0112] = 6
base.save(os.path.join(out, "exif6.jpg"), quality=90, exif=exif.tobytes())
turned = ImageOps.exif_transpose(Image.open(os.path.join(out, "exif6.jpg")))
fields = compare("jpeg exif 6", os.path.join(out, "exif6.jpg"), turned, 2)
report("jpeg exif 6 size", fields is not None and fields[4] == "480" and fields[5] == "640", "file size %s" % (fields[4:6] if fields else "?"))
base.convert("CMYK").save(os.path.join(out, "cmyk.jpg"), quality=95)
compare("jpeg cmyk", os.path.join(out, "cmyk.jpg"), Image.open(os.path.join(out, "cmyk.jpg")).convert("RGB"), 3)

# 4. GIF.
still = base.convert("P", palette=Image.ADAPTIVE, colors=64)
still.save(os.path.join(out, "still.gif"))
compare("gif still", os.path.join(out, "still.gif"), Image.open(os.path.join(out, "still.gif")).convert("RGB"), 0)
frames = []
for index in range(3):
	frame = Image.new("RGBA", (120, 90), (0, 0, 0, 0))
	for y in range(20 + index * 10, 60 + index * 10):
		for x in range(10 + index * 30, 50 + index * 30):
			frame.putpixel((x, y), (200 - index * 60, 80 + index * 60, 40 + index * 50, 255))
	frames.append(frame)
frames[0].save(os.path.join(out, "anim.gif"), save_all=True, append_images=frames[1:], duration=[120, 250, 80], loop=0, disposal=2, transparency=0)
result = run("frames", os.path.join(out, "anim.gif"), os.path.join(out, "anim"))
lines = result.stdout.splitlines()
head = lines[0].split() if lines else []
report("gif frames", result.returncode == 0 and head[:3] == ["120", "90", "3"] and head[3] == "0", result.stdout.strip()[:120])
delays = [line.split()[2] for line in lines if line.startswith("delay")]
report("gif delays", delays == ["120", "250", "80"], " ".join(delays))
reference = Image.open(os.path.join(out, "anim.gif"))
for index in range(3):
	reference.seek(index)
	want = over_checker(reference.convert("RGBA")).tobytes()
	path = os.path.join(out, "anim-%d" % index)
	got = Image.frombytes("RGBA", (120, 90), open(path, "rb").read()).convert("RGB").tobytes() if os.path.exists(path) else b""
	worst = max(abs(a - b) for a, b in zip(got, want)) if got else 999
	report("gif frame %d" % index, worst <= 1, "worst %d" % worst)

# 5. view.
folder = os.path.join(out, "folder")
os.makedirs(folder, exist_ok=True)
picture(4000, 3000).save(os.path.join(folder, "a.png"))
picture(400, 300).save(os.path.join(folder, "b.png"))
picture(300, 400).save(os.path.join(folder, "c.png"))
open(os.path.join(folder, "notes.txt"), "w").write("not an image\n")
result = run("view", folder)
bad = [line for line in result.stdout.splitlines() if not line.startswith("ok ")]
report("view", result.returncode == 0 and not bad, "; ".join(bad[:6]))

print("host-imageview: %s" % ("PASS" if failed == 0 else "FAIL"))
sys.exit(failed)
EOF
