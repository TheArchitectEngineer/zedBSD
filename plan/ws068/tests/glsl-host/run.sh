#!/bin/sh
# ws068-p016..p018: host tests of zedBSD's GLSL compiler (userland/base/libglesv2/glsl).
#  1. pass/: every shader compiles (GLSL ES 1.00, or the version its name or #version says).
#  2. fail/: every shader fails with each "// expect:" text in its log.
#  3. link: the pairs of pass/ link; both stages pass spirv-val (vulkan1.0), and so does the vertex stage
#     after libGLESv2's reflection and gl_Position rewrite (spirv.c, as glLinkProgram does).
#  4. i915: the pairs named in I915_PAIRS are accepted by the i915 compiler on the host.
#  5. run: exec/*.frag are drawn on the host's Vulkan (lavapipe) and each must give its expected colour
#     (see exec/README and vk-run.c).
#
#   plan/ws068/tests/glsl-host/run.sh [OUTDIR]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
here=$(cd "$(dirname -- "$0")" && pwd)
root=$here/../../../..
out=${1:-$root/build/ws068-glsl-host}
mkdir -p "$out/shim"
status=0
fail() { echo "FAIL: $*"; status=1; }

# The tools: the compiler's driver (with the sanitizers), libGLESv2's reflection, the i915 compiler.
for h in EGL GLES2 GLES3 KHR wayland-egl-core.h; do ln -sfn "$root/include/libc/$h" "$out/shim/$h"; done
cc -std=c11 -g -O1 -Wall -Wextra -Werror -Wdeclaration-after-statement -fsanitize=address,undefined \
	-o "$out/glsl-test" "$here/glsl-test.c" "$root"/userland/base/libglesv2/glsl/*.c -lm || exit 1
cc -std=c99 -Wall -Wextra -I"$out/shim" -o "$out/spirv-test" "$root/plan/ws068/tests/spirv-host/main.c" \
	"$root/userland/base/libglesv2/spirv.c" || exit 1
cc -std=gnu99 -O0 -g -w -I"$root" -I"$root/include" -DHAL_ARCH_AMD64 -o "$out/i915-check" \
	"$root/plan/ws068/tests/i915-shader-check/main.c" -lm || exit 1

version_of() {
	case $1 in
	*130*|*modern*) echo 130;;
	*300*|*es300*) echo 300;;
	*330*) echo 330;;
	*140*) echo 140;;
	*150*) echo 150;;
	*120*) echo 120;;
	*110*) echo 110;;
	*) echo 100;;
	esac
}

# 1. Every pass shader compiles.
for f in "$here"/pass/*.vert "$here"/pass/*.frag; do
	stage=vert; case $f in *.frag) stage=frag;; esac
	"$out/glsl-test" compile $stage "$f" "$(version_of "$f")" > "$out/compile.txt" 2>&1 || { cat "$out/compile.txt"; fail "compile $f"; }
done
echo "compile: done"

# 2. Every fail shader fails as it expects.
for f in "$here"/fail/*; do
	stage=vert; case $f in *.frag) stage=frag;; esac
	"$out/glsl-test" expect $stage "$f" 100 > "$out/expect.txt" 2>&1 || { cat "$out/expect.txt"; fail "expect $f"; }
done
echo "expect: done"

# 3. The pairs link, and the SPIR-V is valid before and after libGLESv2's rewrite.
for vert in "$here"/pass/*.vert; do
	name=$(basename "$vert" .vert)
	frag=$here/pass/$name.frag
	[ -f "$frag" ] || continue
	"$out/glsl-test" link "$vert" "$frag" "$out/$name" "$(version_of "$vert")" > "$out/$name.link.txt" 2>&1 || { cat "$out/$name.link.txt"; fail "link $name"; continue; }
	for stage in vert frag; do
		spirv-val --target-env vulkan1.0 "$out/$name.$stage.spv" || fail "spirv-val $name.$stage"
		"$out/spirv-test" "$out/$name.$stage.spv" "$out/$name.$stage.linked.spv" > "$out/$name.$stage.reflect.txt" 2>&1 || { cat "$out/$name.$stage.reflect.txt"; fail "reflect $name.$stage"; }
		spirv-val --target-env vulkan1.0 "$out/$name.$stage.linked.spv" || fail "spirv-val linked $name.$stage"
	done
done
echo "link: done"

# 3b. What the link reports of the named uniform blocks (glGetActiveUniformBlockiv, glGetActiveUniformsiv): blocks330's.
for want in "block 0 Scene binding=32 size=320 stages=3 members=11" \
	"block 1 Object binding=33 size=80 stages=1 members=2" \
	"uniform skew base=0 components=4 columns=3 size=1 sampler=0 block=0 offset=64 array_stride=0 matrix_stride=16 row_major=1" \
	"uniform lights\[3\].colour base=0 components=4 columns=1 size=1 sampler=0 block=0 offset=240 " \
	"uniform turns base=0 components=2 columns=2 size=2 sampler=0 block=0 offset=256 array_stride=32 matrix_stride=16 row_major=1" \
	"uniform Object.visible base=3 components=1 columns=1 size=1 sampler=0 block=1 offset=64 "; do
	grep -q "^$want" "$out/blocks330.link.txt" || fail "blocks330 reflection lacks: $want"
done
echo "blocks: done"

# 4. The i915 compiler takes the shaders that stay inside what it supports.
I915_PAIRS=${I915_PAIRS:-scene fixed scene300}
for name in $I915_PAIRS; do
	"$out/i915-check" vertex "$out/$name.vert.linked.spv" || fail "i915 $name.vert"
	"$out/i915-check" fragment "$out/$name.frag.linked.spv" || fail "i915 $name.frag"
done
echo "i915: done"

# 5. The execution tests on the host's Vulkan.
if [ -d "$here/exec" ] && [ -f "$here/vk-run.c" ]; then
	cc -std=c11 -g -O1 -Wall -Wextra -Werror -I"$out/shim" -fsanitize=address,undefined -o "$out/vk-run" "$here/vk-run.c" \
		"$root"/userland/base/libglesv2/glsl/*.c "$root/userland/base/libglesv2/spirv.c" -lvulkan -lm || exit 1
	ASAN_OPTIONS=detect_leaks=0 VK_DRIVER_FILES=${VK_DRIVER_FILES:-/usr/share/vulkan/icd.d/lvp_icd.json} "$out/vk-run" "$here/exec" || fail "exec"
	echo "run: done"
fi

[ $status -eq 0 ] && echo "glsl-host: PASS" || echo "glsl-host: FAIL"
exit $status
