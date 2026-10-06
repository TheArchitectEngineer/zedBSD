#!/bin/sh
# ws090-p020 (q812): builds and runs the host test of the Mahora fonts and libtruetype's companions (host-mahora.c)
# with libkeiland's text, on Linux.  The fonts are copied under their installed names into OUTDIR/share/fonts (the
# program is built with KEILAND_DATADIR there, so that libkeiland finds Mahora Bold and the monospaced fallback the
# way an installed desktop does); the picture of the samples is OUTDIR/mahora.png.  Nothing is removed.
#   sh plan/ws090/tests/host-mahora.sh [OUTDIR]   (default build/ws090-p020/host)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -e
cd "$(dirname "$0")/../../.."
out=${1:-build/ws090-p020/host}
U=userland/desktop
F=$U/fonts
K=$U/libkeiland/ui
mkdir -p "$out/inc" "$out/share/fonts"
cp $F/Mahora-Regular.ttf "$out/share/fonts/keiland.ttf"
cp $F/Mahora-Bold.ttf "$out/share/fonts/keiland-bold.ttf"
cp $F/Mahora-Mono.ttf "$out/share/fonts/keiland-mono.ttf"
cp $F/JetBrainsMono-Regular.ttf "$out/share/fonts/keiland-fallback-mono.ttf"
cp $F/DroidSansFallbackFull.ttf "$out/share/fonts/keiland-fallback.ttf"
cp $U/keiland/truetype.h $U/keiland/keiland.h $U/keiland/keiland-ui.h $U/keiland/keiui.h "$out/inc/"
ln -sfn "$(pwd)/include/libc/compat" "$out/inc/compat"
cc -std=c11 -D_GNU_SOURCE -O1 -g -Wall -Wextra -Werror -DKEILAND_DATADIR="\"$(pwd)/$out/share\"" \
	-I"$out/inc" -I. -I$K -I$U/libtruetype \
	plan/ws090/tests/host-mahora.c $K/canvas.c $K/text.c \
	$U/libtruetype/*.c $U/picture/color-glyph.c \
	userland/base/libz-compat/inflate.c userland/base/libz-compat/checksum.c userland/base/libpng-compat/read.c \
	-lm -o "$out/host-mahora"
"$out/host-mahora" "$out/share/fonts" "$out/mahora.ppm"
python3 -c "import sys; from PIL import Image; Image.open(sys.argv[1]).save(sys.argv[2])" "$out/mahora.ppm" "$out/mahora.png"
echo "picture: $out/mahora.png"
