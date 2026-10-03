#!/bin/sh
# ws081-p010: builds Files' touch screen (touch.c) with libkeiland's motion, scroller and gestures for the
# host (plain, and with EXTRA_CFLAGS such as the sanitizers) and runs host-filestouch.
#   plan/ws081/tests/run-filestouch.sh [OUTDIR]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
root=$(cd "$(dirname "$0")/../../.." && pwd)
out=${1:-$root/build/ws081-p010-host}
cc=${CC:-clang}
extra=${EXTRA_CFLAGS:-}
mkdir -p "$out/include"

# Only <keiland.h> is taken from include/libc: the rest of that directory is zedBSD's C library.
ln -sf "$root/userland/desktop/keiland/keiland.h" "$out/include/keiland.h"

flags="-std=gnu11 -O2 -g -Wall -Wextra -Werror -Wconversion -Wno-sign-conversion $extra -I$out/include"
for name in motion scroll gesture; do
	"$cc" $flags -c "$root/userland/desktop/libkeiland/$name.c" -o "$out/keiland-$name.o"
done
"$cc" $flags -c "$root/userland/desktop/files/touch.c" -o "$out/touch.o"
"$cc" $flags -Wno-conversion -c "$root/plan/ws081/tests/host-filestouch.c" -o "$out/host-filestouch.o"
"$cc" $extra "$out/host-filestouch.o" "$out/touch.o" "$out/keiland-motion.o" "$out/keiland-scroll.o" "$out/keiland-gesture.o" -lm \
	-o "$out/host-filestouch"
"$out/host-filestouch" > "$out/host-filestouch.log" 2>&1 || { grep -E '^(FAIL|host-filestouch)' "$out/host-filestouch.log"; exit 1; }
grep -E '^(FAIL|host-filestouch)' "$out/host-filestouch.log"
