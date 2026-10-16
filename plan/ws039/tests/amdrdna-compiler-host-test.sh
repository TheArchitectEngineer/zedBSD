#!/bin/sh
# Host test of the amdrdna GFX10 shader compiler (ws039-p007): the encoder's known words, compared with LLVM's assembler;
# compute, vertex and pixel modules compiled and run on a GFX10 interpreter and on the IR reference; the code
# disassembled by llvm-mc.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
#
# usage: plan/ws039/tests/amdrdna-compiler-host-test.sh   (from anywhere)
# Needs cc, glslc, python3 and llvm-mc-19 (gfx1010).  Everything goes in a new directory below build/tmp; nothing is removed.
set -eu
repo=$(cd "$(dirname "$0")/../../.." && pwd)
tests="$repo/plan/ws039/tests"
mkdir -p "$repo/build/tmp"
out=$(mktemp -d "$repo/build/tmp/amdrdna-compiler.XXXXXX")
flags="-std=gnu11 -O1 -g -DKERN_USER_ABI_LP64 -I$repo/include -I$repo/src -I$repo -idirafter $repo/libc/include \
-fsanitize=address,undefined -fno-sanitize=alignment -Wall -Wextra -Werror -Wdeclaration-after-statement"

# 1. The encoder's known words.
cc $flags -o "$out/gfx10-encode-test" "$tests/gfx10-encode-test.c" \
	"$repo/src/drivers/gpu/amdrdna/compiler/gfx10-encode.c"
status=0
"$out/gfx10-encode-test" > "$out/encode.txt" || status=1
cut -f2 "$out/encode.txt" | llvm-mc-19 -arch=amdgcn -mcpu=gfx1010 -show-encoding > "$out/llvm.txt" 2> "$out/llvm-errors.txt"
python3 -I "$tests/gfx10-compare-words.py" "$out/encode.txt" "$out/llvm.txt" || status=1

# 2. Compute modules on the GFX10 interpreter and the IR reference.
for source in "$tests"/shaders/*.comp; do
	name=$(basename "$source" .comp)
	glslc --target-env=vulkan1.0 -fshader-stage=compute -o "$out/$name.spv" "$source"
done
cc $flags -o "$out/gfx10-compute-test" "$tests/gfx10-compute-test.c" "$tests/gfx10-machine.c" "$tests/gfx10-reference.c" \
	"$tests/gfx10-texture.c" "$repo/src/drivers/gpu/amdrdna/compiler/gfx10-program.c" \
	"$repo/src/drivers/gpu/amdrdna/compiler/gfx10-compile.c" "$repo/src/drivers/gpu/amdrdna/compiler/gfx10-wait.c" \
	"$repo/src/drivers/gpu/amdrdna/compiler/gfx10-encode.c" "$repo/src/drivers/gpu/compiler/spirv.c" \
	"$repo/src/drivers/gpu/compiler/optimize.c" -lm
"$out/gfx10-compute-test" "$out" "$out/ids.spv:2,2,2" "$out/float.spv:2,1,1:float" "$out/integer.spv:3,1,1" \
	"$out/loop.spv:2,1,1" "$out/uniform.spv:2,1,1" "$out/division.spv:3,1,1" \
	"$out/atomic.spv:2,1,1" "$out/shared.spv:2,1,1" "$out/cache.spv:2,1,1" "$out/condition.spv:2,1,1" "$out/trig.spv:2,1,1:float" "$out/half.spv:2,1,1:float" || status=1

# 3. Vertex and pixel modules on the GFX10 interpreter and the IR reference.
graphics=""
for source in "$tests"/shaders/*.vert "$tests"/shaders/*.frag; do
	name=$(basename "$source")
	glslc --target-env=vulkan1.0 -o "$out/$name.spv" "$source"
	graphics="$graphics $out/$name.spv"
done
cc $flags -o "$out/gfx10-graphics-test" "$tests/gfx10-graphics-test.c" "$tests/gfx10-machine.c" "$tests/gfx10-reference.c" \
	"$tests/gfx10-texture.c" \
	"$repo/src/drivers/gpu/amdrdna/compiler/gfx10-compile.c" "$repo/src/drivers/gpu/amdrdna/compiler/gfx10-wait.c" \
	"$repo/src/drivers/gpu/amdrdna/compiler/gfx10-encode.c" "$repo/src/drivers/gpu/amdrdna/compiler/gfx10-program.c" \
	"$repo/src/drivers/gpu/compiler/spirv.c" "$repo/src/drivers/gpu/compiler/optimize.c" -lm
# shellcheck disable=SC2086
"$out/gfx10-graphics-test" "$out" $graphics || status=1

# 4. Every word of every program is an instruction llvm-mc knows.
for hex in "$out"/*.hex; do
	if llvm-mc-19 -arch=amdgcn -mcpu=gfx1010 -disassemble "$hex" > "$hex.s" 2> "$hex.err" && [ ! -s "$hex.err" ]; then
		:
	else
		echo "FAIL $(basename "$hex"): llvm-mc does not decode every word"
		head -5 "$hex.err"
		status=1
	fi
done
echo "disassembled: $(ls "$out"/*.hex | wc -l) programs"

if [ "$status" -ne 0 ]; then
	echo "FAIL amdrdna-compiler-host-test ($out)"
	exit 1
fi
echo "PASS amdrdna-compiler-host-test ($out)"
