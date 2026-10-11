#!/bin/sh
# ws157-p013: keiland-preview --info (the Linux build's, seccomp) on pictures, a video's start, damaged and cut files,
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
PY
preview "$work/a.png"; expect png "kind=png"; expect png "width=37"; expect png "height=19"
preview "$work/a.gif"; expect gif "kind=gif"; expect gif "width=21"; expect gif "height=13"
preview "$work/a.jpg"; expect jpeg "kind=jpeg"; expect jpeg "width=640"; expect jpeg "height=480"; expect jpeg "taken=1700000000"
preview "$work/plain.jpg"; expect plain "width=300"; expect plain "taken="
preview "$work/cut.jpg"; expect cut "kind=jpeg"; expect cut "width=0"
preview "$work/sof.jpg"; expect sof "width=32"; expect sof "height=16"
preview "$work/a.mp4"; expect video "kind=video"; expect video "width=0"
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
