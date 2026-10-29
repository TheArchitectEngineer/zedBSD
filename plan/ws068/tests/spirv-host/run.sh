#!/bin/sh
# ws068-p008: host test of the SPIR-V reflection and the gl_Position rewrite (needs glslc, spirv-val, cc).
#   plan/ws068/tests/spirv-host/run.sh [OUTDIR]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
here=$(cd "$(dirname -- "$0")" && pwd)
root=$here/../../../..
out=${1:-$root/build/ws068-p008-host}
mkdir -p "$out/shim"
for h in EGL GLES2 KHR; do ln -sfn "$root/include/libc/$h" "$out/shim/$h"; done
ln -sfn "$root/userland/desktop/include/wayland-egl-core.h" "$out/shim/wayland-egl-core.h"
cc -std=c99 -Wall -Wextra -I"$out/shim" -o "$out/spirv-test" "$here/main.c" "$root/userland/desktop/libglesv2/spirv.c"
status=0
for stage in vert frag; do
	glslc --target-env=vulkan1.0 -o "$out/tri.$stage.spv" "$here/tri.$stage"
	"$out/spirv-test" "$out/tri.$stage.spv" "$out/tri.$stage.patched.spv" | tee "$out/tri.$stage.txt"
	spirv-val --target-env vulkan1.0 "$out/tri.$stage.patched.spv" && echo "spirv-val $stage: ok" || status=1
done
grep -q "uniform u_offsets type=0x8b50 size=3" "$out/tri.vert.txt" || status=1
grep -q "uniform u_texture type=0x8b5e size=1 offset=0 stride=0 mstride=0 sampler=1 binding=1" "$out/tri.frag.txt" || status=1
grep -q "in a_uv location=2 type=0x8b50 components=2" "$out/tri.vert.txt" || status=1
spirv-dis "$out/tri.vert.patched.spv" | grep -c "OpFNegate" | grep -qx 1 || status=1
# The clip control's rewrite with z kept: valid, and without the (z + w) * 0.5 of the rewrite above.
"$out/spirv-test" "$out/tri.vert.spv" "$out/tri.vert.patched.spv" "$out/tri.vert.zero.spv" > /dev/null || status=1
spirv-val --target-env vulkan1.0 "$out/tri.vert.zero.spv" && echo "spirv-val vert zero: ok" || status=1
half=$(spirv-dis "$out/tri.vert.patched.spv" | grep -c "OpFMul" || true)
zero=$(spirv-dis "$out/tri.vert.zero.spv" | grep -c "OpFMul" || true)
[ "$zero" -eq $((half - 1)) ] && echo "zero rewrite: ok ($half -> $zero OpFMul)" || { echo "zero rewrite: FAIL ($half, $zero)"; status=1; }
[ $status -eq 0 ] && echo "spirv-host: PASS" || echo "spirv-host: FAIL"
exit $status
