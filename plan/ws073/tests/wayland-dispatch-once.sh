#!/bin/sh
# BUG-123 regression: builds wayland-dispatch-once.c against libwayland-client's sources on the host, plainly and under
# ASan/UBSan, and runs it.  (The protocol headers the library includes by their repository path are found from the
# repository root; wayland/zed-*.h are found beside the library.)
#   sh plan/ws073/tests/wayland-dispatch-once.sh [OUTDIR]     (default build/ws073-wayland-dispatch)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
out=${1:-build/ws073-wayland-dispatch}
mkdir -p "$out/include/wayland"
cp userland/desktop/libwayland/zed-*-client-protocol.h "$out/include/wayland/"
sources=$(ls userland/desktop/libwayland/*.c)
flags="-std=c99 -D_GNU_SOURCE -Wall -Wextra -Werror -Wno-cast-function-type -Iuserland/desktop/include/wayland -I$out/include -I. -idirafter userland/desktop/include -idirafter include/libc -pthread"
cc $flags $sources plan/ws073/tests/wayland-dispatch-once.c -o "$out/dispatch-once"
timeout 30 "$out/dispatch-once"
cc $flags -fsanitize=address,undefined -fno-omit-frame-pointer -g $sources plan/ws073/tests/wayland-dispatch-once.c -o "$out/dispatch-once-asan"
ASAN_OPTIONS=detect_leaks=1 timeout 30 "$out/dispatch-once-asan"
