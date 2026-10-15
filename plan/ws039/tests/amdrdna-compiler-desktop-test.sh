#!/bin/sh
# Every desktop shader (userland/desktop/**/shaders.h, as the components ship them) through the amdrdna GFX10
# compiler (ws039-p007 stage F): each module must compile, run on the GFX10 interpreter (with its wait and hazard
# checks) and on the IR reference for 200 random inputs with the same exports, and disassemble with llvm-mc.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
#
# usage: plan/ws039/tests/amdrdna-compiler-desktop-test.sh [TRIALS]   (from anywhere; 200 trials by default)
# Needs cc, python3 and llvm-mc-19.  Everything goes in a new directory below build/tmp; nothing is removed.
# Nothing here is evidence about the GPU.
set -eu
repo=$(cd "$(dirname "$0")/../../.." && pwd)
tests="$repo/plan/ws039/tests"
trials=${1:-200}
mkdir -p "$repo/build/tmp"
out=$(mktemp -d "$repo/build/tmp/amdrdna-desktop.XXXXXX")
mkdir "$out/spv"
python3 -I "$tests/gfx10-desktop-extract.py" "$repo" "$out/spv"
flags="-std=gnu11 -O1 -g -DKERN_USER_ABI_LP64 -I$repo/include -I$repo/src -I$repo -idirafter $repo/libc/include \
-fsanitize=address,undefined -fno-sanitize=alignment -Wall -Wextra -Werror -Wdeclaration-after-statement"
cc $flags -o "$out/gfx10-graphics-test" "$tests/gfx10-graphics-test.c" "$tests/gfx10-machine.c" "$tests/gfx10-reference.c" \
	"$tests/gfx10-texture.c" \
	"$repo/src/drivers/gpu/amdrdna/compiler/gfx10-compile.c" "$repo/src/drivers/gpu/amdrdna/compiler/gfx10-wait.c" \
	"$repo/src/drivers/gpu/amdrdna/compiler/gfx10-encode.c" "$repo/src/drivers/gpu/amdrdna/compiler/gfx10-program.c" \
	"$repo/src/drivers/gpu/compiler/spirv.c" "$repo/src/drivers/gpu/compiler/optimize.c" -lm
status=0
if ls "$out"/spv/*.comp.spv > /dev/null 2>&1 || ls "$out"/spv/*.other.spv > /dev/null 2>&1; then
	echo "FAIL a desktop module of a stage this test does not run"
	status=1
fi
"$out/gfx10-graphics-test" "$out" --trials "$trials" "$out"/spv/*.vert.spv "$out"/spv/*.frag.spv > "$out/result.txt" || status=1
cat "$out/result.txt"
for hex in "$out"/*.hex; do
	if llvm-mc-19 -arch=amdgcn -mcpu=gfx1010 -disassemble "$hex" > "$hex.s" 2> "$hex.err" && [ ! -s "$hex.err" ]; then
		:
	else
		echo "FAIL $(basename "$hex"): llvm-mc does not decode every word"
		status=1
	fi
done
echo "modules: $(ls "$out"/spv/*.spv | wc -l), passed: $(grep -c '^PASS' "$out/result.txt"), disassembled: $(ls "$out"/*.hex | wc -l)"
if [ "$status" -ne 0 ]; then
	echo "FAIL amdrdna-compiler-desktop-test ($out)"
	exit 1
fi
echo "PASS amdrdna-compiler-desktop-test ($out)"
