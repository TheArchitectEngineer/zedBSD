#!/bin/sh
# ws170-p000: builds and runs the host test of Phone's view (host-phone.c) with libkeiland's drawing, text and
# widgets and libtruetype, on Linux, and turns the pictures into PNG files.
#   sh plan/ws170/tests/run-host-phone.sh [OUTPUT]   (default build/ws170/host-phone; pictures beside it)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -e
cd "$(dirname "$0")/../../.."
out=${1:-build/ws170/host-phone}
dir=$(dirname "$out")
mkdir -p "$dir/inc"
cp userland/desktop/keiland/truetype.h userland/desktop/keiland/keiland.h userland/desktop/keiland/keiland-ui.h "$dir/inc/"
ln -sfn "$(pwd)/include/libc/compat" "$dir/inc/compat"
U=userland/desktop
K=$U/libkeiland/ui
L=$U/libkeiland
cc -std=c11 -D_GNU_SOURCE -O1 -g -Wall -Wextra -Werror -I"$dir/inc" -I. -I$K -I$U/libtruetype \
	plan/ws170/tests/host-phone.c $U/phone/view.c $U/phone/data.c \
	$K/canvas.c $K/text.c $K/icons.c $K/icons-line.c $K/theme.c $K/input.c $K/scroll.c $K/scroll-bar.c \
	$K/text-touch.c $K/ui.c $K/widgets.c $K/field.c $K/list.c $K/cards.c \
	$L/gesture.c $L/motion.c $L/scroll.c \
	$U/libtruetype/*.c $U/picture/color-glyph.c \
	userland/base/libz-compat/inflate.c userland/base/libz-compat/checksum.c userland/base/libpng-compat/read.c -lm -o "$out"
F=$U/fonts
"$out" $F/Inter.ttf $F/DroidSansFallbackFull.ttf "$out"
for p in "$out"-*.ppm; do
	python3 -c "import sys; from PIL import Image; Image.open(sys.argv[1]).save(sys.argv[2])" "$p" "${p%.ppm}.png"
	rm -f "$p"
done
