#!/bin/sh
# ws101-p009: host test of libGLESv2's reading of compute shaders (spirv.c's gles_spirv_reflect over the GLSL
# compiler's glsl_link_compute): every plan/ws101/tests/glsl/pass shader is read as a GLCompute stage with its
# uniforms, its storage blocks and shared variables left out of the uniforms' interface; add.comp's two uniforms.
#
#   plan/ws101/tests/gles/run.sh [OUTDIR]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
here=$(cd "$(dirname -- "$0")" && pwd)
root=$here/../../../..
out=${1:-$root/build/ws101-gles-host}
mkdir -p "$out/shim"
for h in EGL GLES2 GLES3 KHR; do ln -sfn "$root/include/libc/$h" "$out/shim/$h"; done
ln -sfn "$root/userland/desktop/include/wayland-egl-core.h" "$out/shim/wayland-egl-core.h"
status=0
cc -std=c11 -g -O1 -Wall -Wextra -Werror -fsanitize=address,undefined -I"$out/shim" \
	-o "$out/reflect-host" "$here/reflect-host.c" "$root/userland/desktop/libglesv2/spirv.c" \
	"$root"/userland/desktop/libglesv2/glsl/*.c -lm || exit 1
ASAN_OPTIONS=detect_leaks=1 "$out/reflect-host" "$root"/plan/ws101/tests/glsl/pass/*.comp > "$out/reflect.txt" 2>&1 || status=1
cat "$out/reflect.txt"
grep -q "uniform n type=0x1405" "$out/reflect.txt" || { echo "FAIL: add.comp's uniform n"; status=1; }
grep -q "uniform k type=0x1405" "$out/reflect.txt" || { echo "FAIL: add.comp's uniform k"; status=1; }
[ $status -eq 0 ] && echo "ws101 gles host test PASS" || echo "ws101 gles host test FAIL"
exit $status
