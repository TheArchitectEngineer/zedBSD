#!/bin/sh
# ws210-p014 F1: builds and runs the host test of the lighting's part in libkeiland's widgets' input (host-kl-ui-lit.c with
# libkeiland's drawing, text and widgets and libtruetype) on Linux.
#   sh plan/ws210/tests/run-host-kl-ui-lit.sh [OUTPUT]   (default build/ws210/host-kl-ui-lit)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -e
cd "$(dirname "$0")/../../.."
out=${1:-build/ws210/host-kl-ui-lit}
dir=$(dirname "$out")
mkdir -p "$dir/inc/truetype" "$dir/inc/keiland"
cp userland/desktop/include/truetype/truetype.h "$dir/inc/truetype/"
cp userland/desktop/include/keiland/keiland.h "$dir/inc/keiland/"
ln -sfn "$(pwd)/include/libc/compat" "$dir/inc/compat"
U=userland/desktop
K=$U/libkeiland/ui
L=$U/libkeiland
cc -std=c11 -D_GNU_SOURCE -O2 -g -Wall -Wextra -Werror -I"$dir/inc" -I. -I$K -I$U/libtruetype \
	plan/ws210/tests/host-kl-ui-lit.c \
	$K/canvas.c $K/text.c $K/icons.c $K/icons-line.c $K/theme.c $K/input.c $K/scroll.c $K/scroll-bar.c \
	$K/text-touch.c $K/text-bar.c $K/text-select.c $K/ui.c $K/widgets.c $K/field.c $K/text-area.c $K/list.c $K/cards.c \
	$L/gesture.c $L/motion.c $L/scroll.c \
	$U/libtruetype/*.c $U/picture/color-glyph.c \
	userland/base/libz-compat/inflate.c userland/base/libz-compat/checksum.c userland/desktop/libpng-compat/read.c -lm -o "$out"
F=$U/fonts
"$out" $F/Mahora-Regular.ttf $F/DroidSansFallbackFull.ttf
