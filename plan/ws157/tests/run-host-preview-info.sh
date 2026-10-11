#!/bin/sh
# ws157-p013, p016: keiland-preview --info (the Linux build's, seccomp) on pictures, a video's start, damaged and cut files,
# and preview_info_read against a stand-in child's bad answers, a failure and a child whose time is up (5 s).
# usage: sh plan/ws157/tests/run-host-preview-info.sh LINUX_BUILD   (the keiland-linux.mk build with keiland-preview)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
linux=$(realpath "$1")
mkdir -p "$repo/build/tmp"
work=$(mktemp -d "$repo/build/tmp/ws157-info.XXXXXX")
preview() { LD_LIBRARY_PATH="$linux/lib" "$linux/libexec/keiland-preview" --info <"$1" >"$work/out"; }
expect() {
	if ! grep -qx "$2" "$work/out"; then echo "FAIL $1: wanted $2"; cat "$work/out"; exit 1; fi
}
python3 - "$work" <<'PY'
import sys, struct
from PIL import Image
w = sys.argv[1]
Image.new("RGB", (37, 19)).save(w + "/a.png")
Image.new("RGB", (21, 13)).save(w + "/a.gif")
img = Image.new("RGB", (640, 480))
exif = Image.Exif(); exif[0x8769] = {0x9003: "2023:11:14 22:13:20"}
img.save(w + "/a.jpg", exif=exif.tobytes())
Image.new("RGB", (300, 200)).save(w + "/plain.jpg")
data = open(w + "/a.jpg", "rb").read()
open(w + "/cut.jpg", "wb").write(data[:40])
open(w + "/a.mp4", "wb").write(b"\x00\x00\x00\x18ftypisom" + bytes(4096))
open(w + "/noise.bin", "wb").write(bytes(range(256)) * 64)
open(w + "/empty", "wb").write(b"")
open(w + "/sof.jpg", "wb").write(b"\xff\xd8\xff\xc0\x00\x11\x08\x00\x10\x00\x20" + bytes(32))
# ws157-p016: MP4s (moov after a 5 MiB mdat with a 64-bit size, versions 0 and 1, a turned track, an audio track first,
# a cut moov) and an AVI's avih.
def box(kind, body): return struct.pack(">I", 8 + len(body)) + kind + body
def matrix(turned):
    if turned: return struct.pack(">9i", 0, 0x10000, 0, -0x10000, 0, 0, 0, 0, 0x40000000)
    return struct.pack(">9i", 0x10000, 0, 0, 0, 0x10000, 0, 0, 0, 0x40000000)
def tkhd(version, width, height, turned=False):
    times = struct.pack(">QQII", 1, 1, 1, 0) + struct.pack(">Q", 0) if version else struct.pack(">IIIII", 1, 1, 1, 0, 0)
    return box(b"tkhd", bytes([version, 0, 0, 3]) + times + bytes(8) + bytes(8) + matrix(turned) + struct.pack(">II", width << 16, height << 16))
def trak(handler, header): return box(b"trak", header + box(b"mdia", box(b"mdhd", bytes(24)) + box(b"hdlr", bytes(8) + handler + bytes(13))))
def mvhd(version, created):
    head = struct.pack(">QQIQ", created, created, 1000, 0) if version else struct.pack(">IIII", created, created, 1000, 0)
    return box(b"mvhd", bytes([version, 0, 0, 0]) + head + bytes(80))
created = 1700000000 + 2082844800
ftyp = box(b"ftyp", b"isom" + bytes(4) + b"isomavc1")
mdat = struct.pack(">I", 1) + b"mdat" + struct.pack(">Q", 16 + 5 * 1024 * 1024) + bytes(5 * 1024 * 1024)
moov = box(b"moov", mvhd(0, created) + trak(b"soun", tkhd(0, 0, 0)) + trak(b"vide", tkhd(0, 1920, 1080)))
open(w + "/end.mp4", "wb").write(ftyp + mdat + moov)
moov = box(b"moov", mvhd(1, created + 86400) + trak(b"vide", tkhd(1, 1920, 1080, True)))
open(w + "/turned.mp4", "wb").write(ftyp + moov + box(b"mdat", bytes(64)))
open(w + "/cut.mp4", "wb").write(ftyp + moov[:60])
open(w + "/nodate.mp4", "wb").write(ftyp + box(b"moov", mvhd(0, 0) + trak(b"vide", tkhd(0, 640, 480))))
avih = struct.pack("<10I", 33333, 0, 0, 0, 10, 0, 1, 0, 720, 576) + bytes(16)
open(w + "/a.avi", "wb").write(b"RIFF" + struct.pack("<I", 200) + b"AVI LIST" + struct.pack("<I", 100) + b"hdrlavih" + struct.pack("<I", 56) + avih)
PY
preview "$work/a.png"; expect png "kind=png"; expect png "width=37"; expect png "height=19"
preview "$work/a.gif"; expect gif "kind=gif"; expect gif "width=21"; expect gif "height=13"
preview "$work/a.jpg"; expect jpeg "kind=jpeg"; expect jpeg "width=640"; expect jpeg "height=480"; expect jpeg "taken=1700000000"
preview "$work/plain.jpg"; expect plain "width=300"; expect plain "taken="
preview "$work/cut.jpg"; expect cut "kind=jpeg"; expect cut "width=0"
preview "$work/sof.jpg"; expect sof "width=32"; expect sof "height=16"
preview "$work/a.mp4"; expect video "kind=video"; expect video "width=0"
preview "$work/end.mp4"; expect end "kind=video"; expect end "width=1920"; expect end "height=1080"; expect end "taken=1700000000"; expect end "zone=utc"
preview "$work/turned.mp4"; expect turned "width=1080"; expect turned "height=1920"; expect turned "taken=1700086400"
preview "$work/cut.mp4"; expect cut-mp4 "kind=video"; expect cut-mp4 "width=0"
preview "$work/nodate.mp4"; expect nodate "width=640"; expect nodate "taken="
if grep -q zone "$work/out"; then echo "FAIL nodate: zone without a date"; exit 1; fi
preview "$work/a.avi"; expect avi "kind=video"; expect avi "width=720"; expect avi "height=576"
preview "$work/noise.bin"; expect noise "kind=none"
preview "$work/empty"; expect empty "kind=none"
echo "ok keiland-preview --info"
# The reader, against a stand-in child.
mkdir -p "$work/libexec"
printf '#!/bin/sh\nprintf "%%b" "$(cat %s/answer)"\nexit $(cat %s/status)\n' "$work" "$work" >"$work/libexec/keiland-preview"
chmod 0755 "$work/libexec/keiland-preview"
cc -std=c11 -D_DEFAULT_SOURCE -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -I"$repo" \
	"-DKEILAND_LIBEXECDIR=\"$work/libexec\"" "$repo/plan/ws157/tests/host-preview-info.c" \
	"$repo/userland/desktop/preview/client.c" "$repo/userland/desktop/preview/linux/spawn.c" -o "$work/reader"
run() { printf '%s' "$2" >"$work/answer"; echo "$3" >"$work/status"; "$work/reader" "$1" "$work/a.jpg"; }
run good 'keiland-preview-info 1\nkind=jpeg\nwidth=640\nheight=480\ntaken=1700000000\nlater=1\n' 0
run utc 'keiland-preview-info 1\nkind=video\nwidth=1920\nheight=1080\ntaken=1700000000\nzone=utc\n' 0
run zone 'keiland-preview-info 1\nkind=video\ntaken=1\nzone=jst\n' 0
run header 'keiland-preview-info 2\nkind=jpeg\n' 0
run kind 'keiland-preview-info 1\nkind=tiff\n' 0
run side 'keiland-preview-info 1\nkind=png\nwidth=99999999\n' 0
run taken 'keiland-preview-info 1\nkind=png\ntaken=12x\n' 0
run status '' 2
printf '#!/bin/sh\nsleep 30\n' >"$work/libexec/keiland-preview"
"$work/reader" hang "$work/a.jpg"
printf '#!/bin/sh\nLD_LIBRARY_PATH=%s exec %s "$@"\n' "$linux/lib" "$linux/libexec/keiland-preview" >"$work/libexec/keiland-preview"
"$work/reader" real "$work/a.jpg"
echo "preview info: all PASS"
