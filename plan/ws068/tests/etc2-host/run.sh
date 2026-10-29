#!/bin/sh
# ws068-p039: host test of libGLESv2's ETC2 and EAC decoder (userland/desktop/libglesv2/etc2.c) against an independent
# reference (reference.py): random blocks of each format, some in the T, H and planar modes and in punch-through
# transparency, decoded texel by texel.
#
#   plan/ws068/tests/etc2-host/run.sh [OUTDIR]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
here=$(cd "$(dirname -- "$0")" && pwd)
root=$here/../../../..
out=${1:-$root/build/ws068-etc2-host}
mkdir -p "$out/shim"
for h in EGL GLES2 GLES3 KHR; do ln -sfn "$root/include/libc/$h" "$out/shim/$h"; done
ln -sfn "$root/userland/desktop/include/wayland-egl-core.h" "$out/shim/wayland-egl-core.h"
cc -std=c99 -g -O1 -Wall -Wextra -Werror -fsanitize=address,undefined -I"$out/shim" -o "$out/etc2-test" "$here/etc2-test.c" \
	"$root/userland/desktop/libglesv2/etc2.c" "$root/userland/desktop/libglesv2/format.c" -lvulkan -lm || { echo "etc2-host: FAIL (build)"; exit 1; }
python3 -I "$here/reference.py" "$out/data" || { echo "etc2-host: FAIL (reference)"; exit 1; }
ASAN_OPTIONS=detect_leaks=0 "$out/etc2-test" "$out/data"
