#!/bin/sh
# ws081-p013: builds Notes' touch screen (touch.c) with libkeiland's motion, scroller and gestures for the host
# (plain, and with EXTRA_CFLAGS such as the sanitizers) and runs host-notestouch.
#   plan/ws081/tests/run-notestouch.sh [OUTDIR]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
root=$(cd "$(dirname "$0")/../../.." && pwd)
out=${1:-$root/build/ws081-p013-host}
cc=${CC:-clang}
extra=${EXTRA_CFLAGS:-}
mkdir -p "$out/include"

# Only <keiland.h> is taken from include/libc: the rest of that directory is zedBSD's C library.
ln -sf "$root/userland/desktop/keiland/keiland.h" "$out/include/keiland.h"
ln -sf "$root/userland/desktop/keiland/keiland-ui.h" "$out/include/keiland-ui.h"

flags="-std=gnu11 -O2 -g -Wall -Wextra -Werror -Wconversion -Wno-sign-conversion $extra -I$out/include"
for name in motion scroll gesture; do
	"$cc" $flags -c "$root/userland/desktop/libkeiland/$name.c" -o "$out/keiland-$name.o"
done
"$cc" $flags -c "$root/userland/desktop/notes/touch.c" -o "$out/touch.o"
"$cc" $flags -Wno-conversion -c "$root/plan/ws081/tests/host-notestouch.c" -o "$out/host-notestouch.o"
"$cc" $extra "$out/host-notestouch.o" "$out/touch.o" "$out/keiland-motion.o" "$out/keiland-scroll.o" "$out/keiland-gesture.o" -lm \
	-o "$out/host-notestouch"
"$out/host-notestouch" > "$out/host-notestouch.log" 2>&1 || { grep -E '^(FAIL|host-notestouch)' "$out/host-notestouch.log"; exit 1; }
grep -E '^(FAIL|host-notestouch)' "$out/host-notestouch.log"
