#!/bin/sh
# ws079-p009: libtruetype's glyph drawing (truetype_render_glyph(), which reads outlines through outline.c) before and
# after a change of outline.c: the library is built twice on the host -- with outline.c as it is, and with outline.c
# of a git revision -- and every glyph of some fonts at several sizes is dumped (outcome, box, advance, a hash of the
# bitmap) by truetype-render-dump.c.  The dumps must be the same.  The current build also runs under ASan and UBSan.
#
#   sh plan/ws079/tests/truetype-render-compare.sh REVISION [FONT...]
# The fonts default to DejaVu Sans, Serif and Sans Mono Bold and Noto Sans Balinese (composites with transforms) and
# Liberation Serif under /usr/share/fonts.  No font is committed.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
revision=$1
shift
[ $# -gt 0 ] || set -- /usr/share/fonts/truetype/dejavu/DejaVuSans.ttf /usr/share/fonts/truetype/dejavu/DejaVuSerif.ttf \
    /usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf /usr/share/fonts/truetype/noto/NotoSansBalinese-Regular.ttf \
    /usr/share/fonts/truetype/liberation/LiberationSerif-Regular.ttf
out=build/ws079-render-compare
mkdir -p "$out/include" "$out/old"
mkdir -p "$out/include/truetype"
cp userland/desktop/include/truetype/truetype.h "$out/include/truetype/"
git show "$revision:userland/desktop/libtruetype/outline.c" > "$out/old/outline.c"
cp userland/desktop/libtruetype/internal.h "$out/old/"
others="userland/desktop/libtruetype/face.c userland/desktop/libtruetype/cmap.c userland/desktop/libtruetype/render.c
	userland/desktop/libtruetype/glyph.c userland/desktop/libtruetype/design.c userland/desktop/libtruetype/contour.c userland/desktop/libtruetype/companion.c"
flags="-std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -I$out/include"
# shellcheck disable=SC2086
cc $flags -Werror -O2 $others userland/desktop/libtruetype/outline.c plan/ws079/tests/truetype-render-dump.c -lm -o "$out/new"
# shellcheck disable=SC2086
cc $flags -O2 -Iuserland/desktop/libtruetype $others "$out/old/outline.c" plan/ws079/tests/truetype-render-dump.c -lm -o "$out/old/dump"
# shellcheck disable=SC2086
cc $flags -Werror -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all $others userland/desktop/libtruetype/outline.c \
    plan/ws079/tests/truetype-render-dump.c -lm -o "$out/new-asan"
status=0
for font in "$@"; do
	[ -f "$font" ] || continue
	name=$(basename "$font" .ttf)
	"$out/new" "$font" 9 13 16 24 40 > "$out/$name-new.txt"
	"$out/old/dump" "$font" 9 13 16 24 40 > "$out/$name-old.txt"
	"$out/new-asan" "$font" 13 40 > "$out/$name-asan.txt" 2>&1 || { echo "$name: ASan/UBSan failed"; status=1; }
	if cmp -s "$out/$name-new.txt" "$out/$name-old.txt"; then
		echo "$name: $(wc -l < "$out/$name-new.txt") glyph drawings the same ($(grep -c 'error=0 ' "$out/$name-new.txt") drawn)"
	else
		echo "$name: DIFFERENT"
		diff "$out/$name-old.txt" "$out/$name-new.txt" | head -5
		status=1
	fi
done
[ "$status" = 0 ] && echo "truetype-render-compare: ok" || echo "truetype-render-compare: FAILED"
exit "$status"
