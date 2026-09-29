#!/bin/sh
# libkeiland (ws092-p003): builds and runs the host tests of the file chooser's model and drawing
# (host-chooser.c) with libkeiland's chooser sources and libtruetype, on Linux, and turns
# the pictures into PNG files.
#   sh plan/tools/keiland/host-chooser.sh [OUTPUT]   (default build/keiland/host-chooser; pictures in build/keiland-shots)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -e
cd "$(dirname "$0")/../../.."
out=${1:-build/keiland/host-chooser}
shots=build/keiland-shots
mkdir -p "$(dirname "$out")/inc" "$shots"
cp include/libc/truetype.h include/libc/keiland.h "$(dirname "$out")/inc/"
K=userland/desktop/libkeiland
cc -std=c11 -D_GNU_SOURCE -O1 -g -Wall -Wextra -Werror -I$K -I"$(dirname "$out")/inc" -Iuserland/desktop/libtruetype \
	plan/tools/keiland/host-chooser.c $K/chooser-model.c $K/chooser-draw.c $K/paint.c $K/paint-text.c $K/recent.c \
	userland/desktop/libtruetype/*.c -lm -o "$out"
F=userland/desktop/fonts
"$out" $F/Inter.ttf $F/DroidSansFallbackFull.ttf "$shots/host-chooser"
for p in "$shots"/host-chooser-*.ppm; do
	python3 -c "import sys; from PIL import Image; Image.open(sys.argv[1]).save(sys.argv[2])" "$p" "${p%.ppm}.png"
	rm -f "$p"
done
