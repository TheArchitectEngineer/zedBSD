#!/bin/sh
# ws081-p006: builds the browser shell's touch screen (userland/desktop/browser/shell/touch.c) with libkeiland's
# motion, scroller and gestures for the host (plain, and with EXTRA_CFLAGS such as the sanitizers) and runs
# host-browsertouch.
#   plan/ws081/tests/run-browsertouch.sh [OUTDIR]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
root=$(cd "$(dirname "$0")/../../.." && pwd)
out=${1:-$root/build/ws081-p006-touch}
cc=${CC:-clang}
extra=${EXTRA_CFLAGS:-}
mkdir -p "$out/include"

# Only <keiland.h> is taken from include/libc: the rest of that directory is zedBSD's C library.
ln -sf "$root/include/libc/keiland.h" "$out/include/keiland.h"

flags="-std=gnu11 -O2 -g -Wall -Wextra -Werror -Wconversion -Wno-sign-conversion $extra -I$out/include -I$root/userland/desktop/browser"
for name in motion scroll gesture; do
	"$cc" $flags -c "$root/userland/desktop/libkeiland/$name.c" -o "$out/keiland-$name.o"
done
"$cc" $flags -c "$root/userland/desktop/browser/shell/touch.c" -o "$out/touch.o"
"$cc" $flags -Wno-conversion -c "$root/plan/ws081/tests/host-browsertouch.c" -o "$out/host-browsertouch.o"
"$cc" $extra "$out/host-browsertouch.o" "$out/touch.o" "$out/keiland-motion.o" "$out/keiland-scroll.o" "$out/keiland-gesture.o" -lm \
	-o "$out/host-browsertouch"
"$out/host-browsertouch" > "$out/host-browsertouch.log" 2>&1 || { grep -E '^(FAIL|host-browsertouch)' "$out/host-browsertouch.log"; exit 1; }
grep -E '^(FAIL|host-browsertouch)' "$out/host-browsertouch.log"
