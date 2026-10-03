#!/bin/sh
# ws101-p008: host tests of the GLSL compiler's compute shaders (userland/desktop/libglesv2/glsl, GLSL ES 3.10).
#  1. pass/: every shader compiles and links; its SPIR-V passes spirv-val (vulkan1.0) and i915's native compiler
#     (plan/ws101/tests/host/compute-dump.c: scoreboard, descriptors), and Mesa's brw_disasm / brw_asm round trip
#     gives the kernel's bytes back.
#  2. fail/: every shader fails with each "// expect:" text in its log.
#  3. run: the pass/ shaders run on the host's Vulkan (lavapipe) and leave what C computes (vk-compute.c).
#
#   plan/ws101/tests/glsl/run.sh [OUTDIR]     (BRW_TOOLS: a Mesa 25.0.7 build's src/intel/compiler)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
here=$(cd "$(dirname -- "$0")" && pwd)
root=$here/../../../..
out=${1:-$root/build/ws101-glsl}
tools=${BRW_TOOLS:-/home/awe/p014-c/mesa/build-asm/src/intel/compiler}
mkdir -p "$out"
status=0
fail() { echo "FAIL: $*"; status=1; }

# The tools: the compiler's driver and the Vulkan runner (with the sanitizers), i915's compiler.
cc -std=c11 -g -O1 -Wall -Wextra -Werror -Wdeclaration-after-statement -fsanitize=address,undefined \
	-o "$out/glsl-compute" "$here/glsl-compute.c" "$root"/userland/desktop/libglesv2/glsl/*.c -lm || exit 1
cc -std=c11 -g -O1 -Wall -Wextra -Werror -Wdeclaration-after-statement -fsanitize=address,undefined \
	-o "$out/vk-compute" "$here/vk-compute.c" "$root"/userland/desktop/libglesv2/glsl/*.c -lvulkan -lm || exit 1
cc -std=gnu99 -O0 -Wall -Wextra -Wno-unused-function -I"$root" -I"$root/include" -DHAL_ARCH_AMD64 \
	-o "$out/dump" "$root/plan/ws101/tests/host/compute-dump.c" -lm || exit 1

# 1. Every pass shader links; spirv-val, i915 and Mesa's disassembler take it.
for f in "$here"/pass/*.comp; do
	name=$(basename "$f" .comp)
	"$out/glsl-compute" link "$f" "$out/$name.spv" > "$out/$name.link.txt" 2>&1 || { cat "$out/$name.link.txt"; fail "link $name"; continue; }
	spirv-val --target-env vulkan1.0 "$out/$name.spv" || { fail "spirv-val $name"; continue; }
	"$out/dump" "$out/$name.spv" "$out/$name.bin" > "$out/$name.dump" 2>&1 || { cat "$out/$name.dump"; fail "i915 $name"; continue; }
	"$tools/brw_disasm" --gen=adl --input-path="$out/$name.bin" > "$out/$name.asm" 2>&1
	if grep -q 'ERROR\|illegal' "$out/$name.asm"; then fail "disasm $name"; continue; fi
	"$tools/brw_asm" --gen=adl -o "$out/$name.re" "$out/$name.asm" > /dev/null 2>&1
	cmp -s "$out/$name.bin" "$out/$name.re" || { fail "re-assembled $name"; continue; }
	echo "$name: linked, valid, i915 $(grep -o '[0-9]* instructions' "$out/$name.dump"), disassembled and re-assembled"
done

# 2. Every fail shader fails as it expects.
for f in "$here"/fail/*.comp; do
	"$out/glsl-compute" expect "$f" > "$out/expect.txt" 2>&1 || { cat "$out/expect.txt"; fail "expect $f"; continue; }
done
echo "expect: $(ls "$here"/fail/*.comp | wc -l) shaders fail as expected"

# 3. The pass shaders on the host's Vulkan.
ASAN_OPTIONS=detect_leaks=0 VK_DRIVER_FILES=${VK_DRIVER_FILES:-/usr/share/vulkan/icd.d/lvp_icd.json} \
	"$out/vk-compute" "$here/pass" || fail "run"

[ $status -eq 0 ] && echo "ws101 glsl compute host test PASS" || echo "ws101 glsl compute host test FAIL"
exit $status
