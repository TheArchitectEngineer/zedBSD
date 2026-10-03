#!/bin/sh
# ws071-p010: libz-compat's inflate and libpng-compat's reading on the host, against Python's zlib and PIL.
# Builds build/ws071-host/files-png (host-png.c with the two libraries' sources) and checks:
#  1. inflate: zlib streams Python made at levels 0, 1, 6 and 9 of empty, one-byte, repetitive, text and
#     random data (stored, fixed and dynamic blocks) come back byte for byte, at once and a few bytes at a time;
#     damaged streams (a wrong Adler-32, a cut stream) are refused.
#  2. PNG: files PIL wrote -- gray 1, 2, 4, 8 and 16 bits, gray with alpha, RGB 8 and 16, RGBA 8 and 16,
#     palette 1, 2, 4 and 8 bits with and without tRNS, each filter type -- read into RGBA equal PIL's reading
#     (16-bit channels: their high byte), and into gray, RGB, BGRA and ARGB in their orders; an Adam7
#     interlaced RGBA file is reconstructed, and a damaged CRC is refused.
#
#   plan/tools/files/host-png.sh [OUTDIR]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
out=${1:-build/ws071-p010-host}
host=build/ws071-host
mkdir -p "$out" "$host/include"
ln -sfn "$(pwd)/include/libc/compat" "$host/include/compat"
cc=${CC:-cc}
"$cc" -O2 -g -Wall -Wextra -Werror -I"$host/include" -o "$host/files-png" plan/tools/files/host-png.c \
    userland/base/libz-compat/inflate.c userland/base/libz-compat/checksum.c userland/base/libpng-compat/read.c
exec python3 - "$host/files-png" "$out" <<'EOF'
import os, random, subprocess, sys, zlib
from PIL import Image
tool, out = sys.argv[1], sys.argv[2]
failed = 0
def run(*args):
	return subprocess.run([tool] + list(args), capture_output=True, text=True)
def report(name, ok, detail=""):
	global failed
	print("%s: %s%s" % (name, "ok" if ok else "MISSING", (" " + detail) if detail else ""))
	if not ok:
		failed = 1

# 1. inflate.
random.seed(71)
samples = {
	"empty": b"",
	"one": b"x",
	"repeat": b"ab" * 40000,
	"text": open("plan/ws071/design.md", "rb").read(),
	"random": bytes(random.getrandbits(8) for _ in range(70000)),
}
for name, data in samples.items():
	for level in (0, 1, 6, 9):
		stream = zlib.compress(data, level)
		source = os.path.join(out, "%s-%d.z" % (name, level))
		result = os.path.join(out, "%s-%d.out" % (name, level))
		open(source, "wb").write(stream)
		for mode in ("inflate", "pieces"):
			if os.path.exists(result):
				os.remove(result)
			done = run(mode, source, result)
			same = done.returncode == 0 and open(result, "rb").read() == data
			report("%s %s level %d" % (mode, name, level), same, done.stdout.strip() if not same else "")
bad = bytearray(zlib.compress(samples["text"], 6))
bad[-1] ^= 1
open(os.path.join(out, "bad.z"), "wb").write(bytes(bad))
report("wrong adler-32 refused", run("inflate", os.path.join(out, "bad.z"), os.path.join(out, "bad.out")).returncode != 0)
open(os.path.join(out, "cut.z"), "wb").write(zlib.compress(samples["text"], 6)[:-40])
report("cut stream refused", run("inflate", os.path.join(out, "cut.z"), os.path.join(out, "cut.out")).returncode != 0)

# 2. PNG.
def picture(mode, size=(37, 23)):
	image = Image.new("RGBA", size)
	pixels = image.load()
	for y in range(size[1]):
		for x in range(size[0]):
			pixels[x, y] = ((x * 7) % 256, (y * 11) % 256, (x * y) % 256, (x * 13 + y * 5) % 256)
	return image
cases = []
base = picture("RGBA")
cases.append(("rgba8", base, {}))
cases.append(("rgb8", base.convert("RGB"), {}))
cases.append(("gray8", base.convert("L"), {}))
cases.append(("ga8", base.convert("LA"), {}))
cases.append(("gray1", base.convert("1"), {}))
cases.append(("pal8", base.convert("RGB").quantize(200), {}))
cases.append(("pal4", base.convert("RGB").quantize(16), {"bits": 4}))
cases.append(("pal2", base.convert("RGB").quantize(4), {"bits": 2}))
cases.append(("pal1", base.convert("RGB").quantize(2), {"bits": 1}))
trans = base.convert("RGB").quantize(64)
trans.info["transparency"] = bytes(range(0, 256, 4))
cases.append(("pal8-trns", trans, {"transparency": bytes(range(0, 256, 4))}))
cases.append(("gray16", base.convert("I;16"), {}))
for index, (name, image, options) in enumerate(cases):
	path = os.path.join(out, name + ".png")
	image.save(path, compress_level=[0, 1, 6, 9][index % 4], **options)
	expected = Image.open(path).convert("RGBA")
	if name == "gray16":
		raw16 = Image.open(path)
		expected = Image.new("RGBA", raw16.size)
		data = [ (v >> 8) for v in raw16.getdata() ]
		expected.putdata([(v, v, v, 255) for v in data])
	result = os.path.join(out, name + ".rgba")
	done = run("png", path, result, "rgba")
	same = done.returncode == 0 and open(result, "rb").read() == expected.tobytes()
	report("png %s rgba" % name, same, done.stdout.strip() if not same else "")

# 16-bit RGB and RGBA, written by hand (PIL does not write them): the high bytes must come back.
import struct
def chunk(kind, body):
	return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xffffffff)
def png16(path, colour_type, channels, width, height, filter_of_row):
	rows = b""
	values = []
	for y in range(height):
		row = b""
		for x in range(width):
			pixel = [((x * 1031 + y * 257 + c * 4099) * 13) % 65536 for c in range(channels)]
			values.append(pixel)
			row += b"".join(struct.pack(">H", v) for v in pixel)
		rows += bytes([0]) + row
	data = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 16, colour_type, 0, 0, 0))
	data += chunk(b"IDAT", zlib.compress(rows, 6)) + chunk(b"IEND", b"")
	open(path, "wb").write(data)
	return values
for name, colour_type, channels in (("rgb16", 2, 3), ("rgba16", 6, 4)):
	path = os.path.join(out, name + ".png")
	values = png16(path, colour_type, channels, 19, 7, 0)
	expected = b"".join(bytes([p[0] >> 8, p[1] >> 8, p[2] >> 8, (p[3] >> 8) if channels == 4 else 255]) for p in values)
	result = os.path.join(out, name + ".rgba")
	done = run("png", path, result, "rgba")
	report("png %s rgba" % name, done.returncode == 0 and open(result, "rb").read() == expected, done.stdout.strip())

# Every filter type: PIL's optimizing writer picks filters per row; a larger picture uses all of them.
path = os.path.join(out, "filters.png")
big = picture("RGBA", (160, 90))
big.save(path, optimize=True)
done = run("png", path, os.path.join(out, "filters.rgba"), "rgba")
report("png filters rgba", done.returncode == 0 and open(os.path.join(out, "filters.rgba"), "rb").read() == Image.open(path).convert("RGBA").tobytes())

# The other orders and gray, from the RGBA file.
rgba = Image.open(os.path.join(out, "rgba8.png")).convert("RGBA").tobytes()
def pixels(data, n):
	return [data[i:i + n] for i in range(0, len(data), n)]
orders = {
	"bgra": b"".join(bytes([p[2], p[1], p[0], p[3]]) for p in pixels(rgba, 4)),
	"argb": b"".join(bytes([p[3], p[0], p[1], p[2]]) for p in pixels(rgba, 4)),
	"rgb": b"".join(bytes([p[0], p[1], p[2]]) for p in pixels(rgba, 4)),
}
for fmt, expected in orders.items():
	result = os.path.join(out, "order-" + fmt)
	done = run("png", os.path.join(out, "rgba8.png"), result, fmt)
	report("png rgba8 %s" % fmt, done.returncode == 0 and open(result, "rb").read() == expected)
gray = open(os.path.join(out, "gray8.rgba"), "rb").read()
done = run("png", os.path.join(out, "gray8.png"), os.path.join(out, "gray8.gray"), "gray")
report("png gray8 gray", done.returncode == 0 and open(os.path.join(out, "gray8.gray"), "rb").read() == gray[0::4])

# Adam7 interlacing: write the seven filtered passes directly and reconstruct the original RGBA image.
path = os.path.join(out, "interlaced.png")
starts = ((0, 0), (4, 0), (0, 4), (2, 0), (0, 2), (1, 0), (0, 1))
steps = ((8, 8), (8, 8), (4, 8), (4, 4), (2, 4), (2, 2), (1, 2))
pixels = base.convert("RGBA").load()
rows = bytearray()
for (start_x, start_y), (step_x, step_y) in zip(starts, steps):
	for y in range(start_y, base.height, step_y):
		rows.append(0)
		for x in range(start_x, base.width, step_x):
			rows.extend(pixels[x, y])
data = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", base.width, base.height, 8, 6, 0, 0, 1))
data += chunk(b"IDAT", zlib.compress(bytes(rows), 6)) + chunk(b"IEND", b"")
open(path, "wb").write(data)
done = run("png", path, os.path.join(out, "interlaced.rgba"), "rgba")
same = done.returncode == 0 and open(os.path.join(out, "interlaced.rgba"), "rb").read() == base.convert("RGBA").tobytes()
report("interlaced rgba", same, done.stdout.strip() if not same else "")
# Refused: a damaged CRC.
data = bytearray(open(os.path.join(out, "rgb8.png"), "rb").read())
data[40] ^= 0x55
open(os.path.join(out, "crc.png"), "wb").write(bytes(data))
done = run("png", os.path.join(out, "crc.png"), os.path.join(out, "crc.rgba"), "rgba")
report("damaged CRC refused", done.returncode != 0, done.stdout.strip())

print("host-png: %s" % ("PASS" if failed == 0 else "FAIL"))
sys.exit(failed)
EOF
