#!/bin/sh
# Builds and runs the host test of libkeiland's scroller and gestures (ws081-p005).
# Usage: plan/ws081/tests/run-scroll.sh [build-dir]
#   EXTRA_CFLAGS (for example the sanitizers) reach the library and the test.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu

root=$(cd "$(dirname "$0")/../../.." && pwd)
out=${1:-$root/build/ws081-p005}
mkdir -p "$out/include"

# Only <keiland.h> is taken from include/libc: the rest of that directory is
# zedBSD's C library, which must not stand in for the host's.
ln -sf "$root/userland/desktop/keiland/keiland.h" "$out/include/keiland.h"
ln -sf "$root/userland/desktop/keiland/keiland-ui.h" "$out/include/keiland-ui.h"

cc=${CC:-clang}
extra=${EXTRA_CFLAGS:-}
flags="-std=gnu11 -O2 -g -Wall -Wextra -Werror"
for name in motion scroll gesture; do
	$cc $flags -Wconversion -Wno-sign-conversion $extra -I "$out/include" \
		-c "$root/userland/desktop/libkeiland/$name.c" -o "$out/$name.o"
done
$cc $flags $extra -I "$out/include" -c "$root/plan/ws081/tests/host-scroll.c" -o "$out/host-scroll.o"
$cc $extra "$out/host-scroll.o" "$out/motion.o" "$out/scroll.o" "$out/gesture.o" -lm -o "$out/host-scroll"
"$out/host-scroll"
