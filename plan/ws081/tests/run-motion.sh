#!/bin/sh
# Builds and runs the host test of libkeiland's touch motion (ws081-p003).
# The library file is compiled as it is; the test includes the design's
# comparison program (motion-compare.c) for its simulated panels and measures.
# Usage: plan/ws081/tests/run-motion.sh [build-dir]
#   EXTRA_CFLAGS (for example the sanitizers) reach the library and the test.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu

root=$(cd "$(dirname "$0")/../../.." && pwd)
out=${1:-$root/build/ws081-p003}
mkdir -p "$out"

cc=${CC:-clang}
extra=${EXTRA_CFLAGS:-}
flags="-std=gnu11 -O2 -g -Wall -Wextra -Werror"
$cc $flags -Wconversion -Wno-sign-conversion $extra \
	-c "$root/userland/desktop/libkeiland/motion.c" -o "$out/motion.o"
$cc $flags $extra -I "$root/userland/desktop/libkeiland" \
	-c "$root/plan/ws081/tests/host-motion.c" -o "$out/host-motion.o"
$cc $extra "$out/host-motion.o" "$out/motion.o" -lm -o "$out/host-motion"
"$out/host-motion"
