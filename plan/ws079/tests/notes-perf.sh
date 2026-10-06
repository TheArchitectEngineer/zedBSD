#!/bin/sh
# ws079-p011: Notes' frame time on a full page, on the Venus guest (the image of build-notes-image.sh).
# Builds plan/ws079/tests/notes-many.c on the host and writes a Notes PDF with COUNT strokes on page 1,
# copies it into the guest, starts zdesktop --glass at 1280x800 and /bin/notes --fullscreen on it, and draws
# ROUNDS strokes with QMP pointer drags (60 moves each, 20 ms apart).  Each drag ends with Notes' line
#   NOTES FRAMES count=N strokes=S build_us=B draw_us=D frame_us=F longest_us=L
# (the frames drawn during the contact: the average time to build the frame's geometry on the CPU, to draw
# it (submit, present and wait for the GPU), their sum, and the longest frame), which is printed here.
#
#   GUEST_RUNTIME=build/ws079-p011-run plan/ws035/tests/zdesktop-guest.sh start build/amd64/hdd-image.img
#   plan/ws079/tests/notes-perf.sh [OUTDIR] [COUNT] [ROUNDS] [SHOT]
# NOTES_BINARY=... copies another build of Notes into the running guest first (to compare two builds on
# the same guest).  SHOT, when given, is a PNG path for the screen after the drags.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
GUEST_RUNTIME="${GUEST_RUNTIME:-$(pwd)/build/ws079-p011-run}"
export GUEST_RUNTIME
out=${1:-build/ws079-p011-perf}
count=${2:-500}
rounds=${3:-5}
shot=${4:-}
mkdir -p "$out/include"
guest() { timeout 90 python3 plan/tools/guest/guest.py run "$1" 2>&1 </dev/null; }
check() { python3 plan/ws035/tests/zdesktop-check.py "$@" --runtime "$GUEST_RUNTIME"; }
pointer() { python3 plan/ws035/tests/qmp-pointer.py "$GUEST_RUNTIME/qmp.sock" "$@"; }
stop_all='for p in $(ps -A -o pid,args | grep -E "[w]ayland( |$)|/bin/[n]otes" | awk "{print \$1}"); do kill $p; done; i=0; while ps -A -o args | grep -qE "[w]ayland( |$)|/bin/[n]otes" && [ $i -lt 50 ]; do sleep 0.2; i=$((i+1)); done'

# The PDF with COUNT strokes, made on the host with Notes' own model and libpdf.
ln -sf "$(pwd)/include/libc/pdf.h" "$out/include/pdf.h"
ln -sf "$(pwd)/include/libc/sha2.h" "$out/include/sha2.h"
# ws079-p008: the security handler (crypt.c) uses the C library's MD5, which openbsd-digest.c has with SHA-1.
ln -sf "$(pwd)/include/libc/md5.h" "$out/include/md5.h"
ln -sf "$(pwd)/include/libc/sha1.h" "$out/include/sha1.h"
ln -sfn "$(pwd)/include/libc/compat" "$out/include/compat"
ln -sf "$(pwd)/userland/desktop/keiland/truetype.h" "$out/include/truetype.h"
cc -std=gnu99 -O1 -D_DEFAULT_SOURCE -I"$out/include" -c src/libc/openbsd-sha2.c -o "$out/sha2.o" || exit 1
cc -std=gnu99 -O1 -w -D_DEFAULT_SOURCE -I"$out/include" -c src/libc/openbsd-digest.c -o "$out/digest.o" || exit 1
# ws079-p007: the reader needs filter.c and libz-compat (cross-reference and object streams); p014: save.c needs update.c.
zlib=
# ws175-p007: the edits (edit.c) use libpdf's editor: the whole of libpdf, with libjpeg-compat and libtruetype.
for file in userland/base/libz-compat/*.c userland/base/libjpeg-compat/*.c userland/desktop/libtruetype/*.c; do
	object="$out/$(basename "$(dirname "$file")")-$(basename "$file" .c).o"
	cc -std=gnu11 -O1 -w -D_DEFAULT_SOURCE -I"$out/include" -I"$(dirname "$file")" -c "$file" -o "$object" || exit 1
	zlib="$zlib $object"
done
cc -std=c99 -pedantic -O1 -Wall -Wextra -Werror -Wno-overlength-strings -D_DEFAULT_SOURCE -I"$out/include" -Iuserland/desktop/notes \
    userland/desktop/notes/document.c userland/desktop/notes/edit.c userland/desktop/notes/encode.c userland/desktop/notes/journal.c \
    userland/desktop/notes/save.c userland/base/libpdf/writer.c userland/base/libpdf/update.c userland/base/libpdf/outline.c \
    userland/base/libpdf/object.c userland/base/libpdf/reader.c userland/base/libpdf/filter.c userland/base/libpdf/ccitt.c userland/base/libpdf/crypt.c \
    userland/base/libpdf/image.c userland/base/libpdf/display.c userland/base/libpdf/content.c userland/base/libpdf/editor.c \
    userland/base/libpdf/tounicode.c userland/base/libpdf/intake.c userland/base/libpdf/stroke.c userland/base/libpdf/raster.c \
    userland/base/libpdf/font.c userland/base/libpdf/encoding.c userland/base/libpdf/shading.c userland/base/libpdf/charstrings.c \
    userland/base/libpdf/type1.c userland/base/libpdf/cff.c userland/base/libpdf/cffdata.c plan/ws079/tests/notes-many.c \
    "$out/sha2.o" "$out/digest.o" $zlib -lm \
    -o "$out/notes-many" || exit 1
"$out/notes-many" "$out/many.pdf" "$count" || exit 1

# The guest: the PDF, Notes (when another build is given), zdesktop and Notes on the PDF.
guest "$stop_all" >/dev/null
if [ -n "${NOTES_BINARY:-}" ]; then
	timeout 60 python3 plan/tools/guest/guest.py put "$NOTES_BINARY" /bin/notes >/dev/null 2>&1 </dev/null ||
	    { echo "notes-perf: cannot copy $NOTES_BINARY into the guest"; exit 1; }
fi
guest 'cksum /bin/notes'
guest 'rm -rf /tmp/notes-perf /tmp/notes-perf.log /root/.local/share/keiland/notes; mkdir -p /tmp/notes-perf' >/dev/null
timeout 60 python3 plan/tools/guest/guest.py put "$out/many.pdf" /tmp/notes-perf/many.pdf >/dev/null 2>&1 </dev/null
guest 'export XDG_RUNTIME_DIR=/tmp; rm -f /tmp/wayland-0; picture=; [ -f /usr/share/keiland/wallpaper.png ] && picture=--wallpaper=/usr/share/keiland/wallpaper.png
/bin/wayland --testing --timeout=900 --width=1280 --height=800 --glass $picture > /tmp/zdesktop.log 2>&1 </dev/null & sleep 4; echo started' >/dev/null
guest 'export XDG_RUNTIME_DIR=/tmp; /bin/notes --fullscreen /tmp/notes-perf/many.pdf >> /tmp/notes-perf.log 2>&1 </dev/null & sleep 6; echo started' >/dev/null
guest 'grep -E "NOTES (START|OPEN|LAYOUT)" /tmp/notes-perf.log'
set -- $(guest "grep 'NOTES LAYOUT' /tmp/notes-perf.log | tail -1" | sed -n 's/.* page=\([0-9]*\),\([0-9]*\),[0-9]*,[0-9]* scale=\([0-9.]*\).*/\1 \2 \3/p')
px=${1:-387}; py=${2:-68}; scale=${3:-0.85}

# ROUNDS drags across the lower part of the page, each a wave of 60 moves.
round=0
while [ $round -lt "$rounds" ]; do
	steps=$(python3 - "$px" "$py" "$scale" "$round" <<'EOF'
import math, sys
px, py, scale, round_ = float(sys.argv[1]), float(sys.argv[2]), float(sys.argv[3]), int(sys.argv[4])
words = []
for i in range(61):
    x = 60 + 470 * i / 60.0
    y = 700 + 20 * round_ + 12 * math.sin(i / 60.0 * 6 * math.pi)
    words += ["move", str(int(px + x * scale)), str(int(py + y * scale)), "sleep", "20"]
    if i == 0:
        words += ["down", "sleep", "40"]
words += ["up", "sleep", "400"]
print(" ".join(words))
EOF
)
	pointer $steps
	round=$((round + 1))
done
sleep 1
guest 'grep -E "NOTES (FRAMES|STROKE)" /tmp/notes-perf.log' | tee "$out/frames.txt"
if [ -n "$shot" ]; then
	pointer move 1270 790 sleep 300
	sleep 0.6
	check "$shot" >/dev/null
fi
guest 'cat /tmp/notes-perf.log' > "$out/notes.log"
guest "$stop_all" >/dev/null
echo "notes-perf: done"
