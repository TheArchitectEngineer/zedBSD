#!/bin/sh
# ws090-p005: builds and runs the host test of libkeiui's widgets (host-widgets.c) with the drawing layer,
# the input, libkeiland's scroller and gestures and libtruetype, on Linux.  The gallery (the page of
# widgets, and with a dialog) goes to build/ws090-shots.
#   sh plan/ws090/tests/host-widgets.sh [OUTPUT]   (default build/ws090/host-widgets)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -e
cd "$(dirname "$0")/../../.."
out=${1:-build/ws090/host-widgets}
shots=build/ws090-shots
mkdir -p "$(dirname "$out")/inc" "$shots"
cp include/libc/truetype.h include/libc/keiland.h include/libc/keiui.h "$(dirname "$out")/inc/"
U=userland/desktop
K=$U/libkeiui
cc -std=c11 -D_GNU_SOURCE -O1 -g -Wall -Wextra -Werror -I"$(dirname "$out")/inc" -I$K -I$U/libtruetype \
	plan/ws090/tests/host-widgets.c \
	$K/version.c $K/canvas.c $K/text.c $K/icons.c $K/icons-line.c $K/theme.c $K/input.c $K/scroll.c \
	$K/text-touch.c $K/ui.c $K/widgets.c $K/field.c $K/list.c $K/cards.c \
	$U/libkeiland/gesture.c $U/libkeiland/motion.c $U/libkeiland/scroll.c \
	$U/libtruetype/*.c -lm -o "$out"
F=$U/fonts
"$out" $F/Inter.ttf $F/DroidSansFallbackFull.ttf "$shots/host-widgets"
for p in "$shots"/host-widgets-*.ppm; do
	python3 -c "import sys; from PIL import Image; Image.open(sys.argv[1]).save(sys.argv[2])" "$p" "${p%.ppm}.png"
	rm -f "$p"
done
