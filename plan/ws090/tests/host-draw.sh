#!/bin/sh
# ws090-p002: builds and runs the host test of libkeiui's drawing layer (host-draw.c) with libkeiui,
# the file manager's canvas, text and icons, Settings' line pictures and libtruetype, on Linux:
# the same scene drawn by both must match to the byte.  Pictures go to build/ws090-shots.
#   sh plan/ws090/tests/host-draw.sh [OUTPUT]   (default build/ws090/host-draw)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -e
cd "$(dirname "$0")/../../.."
out=${1:-build/ws090/host-draw}
shots=build/ws090-shots
mkdir -p "$(dirname "$out")/inc" "$shots"
cp include/libc/truetype.h include/libc/keiland.h include/libc/keiui.h "$(dirname "$out")/inc/"
ln -sfn "$(pwd)/include/libc/compat" "$(dirname "$out")/inc/compat"
U=userland/desktop
cc -std=c11 -D_GNU_SOURCE -O1 -g -Wall -Wextra -Werror -I"$(dirname "$out")/inc" -I$U/libkeiui -I$U/libtruetype \
	plan/ws090/tests/host-draw.c \
	$U/libkeiui/version.c $U/libkeiui/canvas.c $U/libkeiui/text.c $U/libkeiui/icons.c $U/libkeiui/icons-line.c $U/libkeiui/theme.c \
	$U/files/canvas.c $U/files/text.c $U/files/icons.c $U/settings/glyphs.c \
	$U/libtruetype/*.c $U/picture/color-glyph.c \
	userland/base/libz-compat/inflate.c userland/base/libz-compat/checksum.c userland/base/libpng-compat/read.c -lm -o "$out"
F=$U/fonts
"$out" $F/Inter.ttf $F/DroidSansFallbackFull.ttf "$shots/host-draw"
for p in "$shots"/host-draw-*.ppm; do
	python3 -c "import sys; from PIL import Image; Image.open(sys.argv[1]).save(sys.argv[2])" "$p" "${p%.ppm}.png"
	rm -f "$p"
done
