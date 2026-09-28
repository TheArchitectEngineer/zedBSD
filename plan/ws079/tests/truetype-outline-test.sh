#!/bin/sh
# Host test of libtruetype's truetype_glyph_outline(): builds the library's sources with the host
# compiler, plain and with AddressSanitizer/UBSan, and compares every glyph of the given fonts
# (default: DejaVu Sans, Serif and Sans Mono Bold, and Noto Sans Balinese -- the last two have components
# with a scale or matrix -- under /usr/share/fonts) with fontTools. No font is committed.
#
#   plan/ws079/tests/truetype-outline-test.sh [BUILD-DIR] [FONT...]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
out=${1:-build/ws079-outline-host}
[ $# -gt 0 ] && shift
[ $# -gt 0 ] || set -- /usr/share/fonts/truetype/dejavu/DejaVuSans.ttf /usr/share/fonts/truetype/dejavu/DejaVuSerif.ttf \
    /usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf /usr/share/fonts/truetype/noto/NotoSansBalinese-Regular.ttf
mkdir -p "$out/include"
# Only the public header: the rest of include/libc is the target libc, not the host one.
cp include/libc/truetype.h "$out/include/"
src="userland/desktop/libtruetype/face.c userland/desktop/libtruetype/cmap.c
     userland/desktop/libtruetype/outline.c userland/desktop/libtruetype/render.c
     userland/desktop/libtruetype/glyph.c userland/desktop/libtruetype/design.c
     userland/desktop/libtruetype/contour.c plan/ws079/tests/truetype-outline-dump.c"
flags="-std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Werror -I$out/include"
# shellcheck disable=SC2086
cc $flags -O2 $src -lm -o "$out/dump"
# shellcheck disable=SC2086
cc $flags -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all $src -lm -o "$out/dump-asan"
echo "== plain"
python3 plan/ws079/tests/truetype-outline-check.py "$out/dump" "$@"
echo "== asan+ubsan"
python3 plan/ws079/tests/truetype-outline-check.py "$out/dump-asan" "$@"
