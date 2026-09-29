#!/bin/sh
# Text Editor (ws092): builds and runs the host tests of Text Editor's core (host-core.c) with the
# editor's sources and libtruetype, on Linux; the fonts are the tree's.
#   sh plan/tools/textedit/host-core.sh [OUTPUT]   (default build/textedit/host-core)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -e
cd "$(dirname "$0")/../../.."
out=${1:-build/textedit/host-core}
mkdir -p "$(dirname "$out")/inc"
cp include/libc/truetype.h "$(dirname "$out")/inc/"
D=userland/desktop/textedit
cc -std=c11 -D_GNU_SOURCE -O1 -g -Wall -Wextra -I$D -I"$(dirname "$out")/inc" -Iuserland/desktop/libtruetype \
	plan/tools/textedit/host-core.c $D/buffer.c $D/undo.c $D/file.c $D/layout.c $D/find.c $D/edit.c \
	$D/app.c $D/draw.c $D/canvas.c $D/text.c $D/keys.c userland/desktop/libtruetype/*.c -lm -o "$out"
"$out"
