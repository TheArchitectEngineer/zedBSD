#!/bin/sh
# ws102-p019: the colour glyphs on the host (host-emoji.c): libtruetype's truetype_color_glyph over Noto Color Emoji and
# the shared decoding and scaling (userland/desktop/picture/color-glyph.c) with libpng-compat.  The font is the verified
# download (make noto-color-emoji-download) in build/distfiles.
#   plan/ws102/tests/host-emoji.sh [EMOJI-FONT]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
out=build/ws102-host-emoji
font=${1:-build/distfiles/NotoColorEmoji-2.047.ttf}
mkdir -p "$out/include"
ln -sf "$(pwd)/include/libc/truetype.h" "$out/include/truetype.h"
ln -sfn "$(pwd)/include/libc/compat" "$out/include/compat"
${CC:-cc} -O1 -g -Wall -Wextra -Werror -I"$out/include" -I. -Iuserland/desktop/libtruetype -o "$out/host-emoji" \
    plan/ws102/tests/host-emoji.c userland/desktop/picture/color-glyph.c \
    userland/desktop/libtruetype/face.c userland/desktop/libtruetype/cmap.c userland/desktop/libtruetype/outline.c \
    userland/desktop/libtruetype/render.c userland/desktop/libtruetype/glyph.c userland/desktop/libtruetype/design.c \
    userland/desktop/libtruetype/contour.c userland/desktop/libtruetype/color.c \
    userland/base/libz-compat/inflate.c userland/base/libz-compat/checksum.c userland/base/libpng-compat/read.c -lm
exec "$out/host-emoji" "$font" userland/desktop/fonts/Inter.ttf
