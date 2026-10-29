#!/bin/sh
# Host test of the SPIR-V parser's inlining of function calls (src/drivers/gpu/compiler/spirv-inline.inc): each shader
# of plan/ws313/tests/spirv-inline-shaders/ compiled without -O (helper functions stay) is rewritten, validated with
# spirv-val, compiled by the Mali compiler and run on its one-lane model against the IR of the rewritten module and of
# the same source compiled with -O (one function); a helper that returns early is refused; every desktop shader and
# every shader of mali-compiler-shaders/ (one function) parses to the same IR with and without the feature.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
#
# usage: plan/ws313/tests/spirv-inline-host-test.sh [TRIALS]   (from anywhere; 100 trials by default)
# Needs cc, python3, glslc and spirv-val.  Output goes in a new directory below build/pc; nothing is removed.
# Nothing here is evidence about any GPU.
set -eu
repo=$(cd "$(dirname "$0")/../../.." && pwd)
trials=${1:-100}
mkdir -p "$repo/build/pc"
out=$(mktemp -d "$repo/build/pc/spirv-inline-host.XXXXXX")
shaders=$repo/plan/ws313/tests/spirv-inline-shaders
commands=""
for source in "$shaders"/helpers.*; do
	name=$(basename "$source")
	glslc --target-env=vulkan1.0 -o "$out/$name.spv" "$source"
	glslc --target-env=vulkan1.0 -O -o "$out/$name.O.spv" "$source"
	commands="$commands pair $out/$name.spv $out/$name.O.spv"
done
glslc --target-env=vulkan1.0 -o "$out/early-return.frag.spv" "$shaders/early-return.frag"
commands="$commands refuse $out/early-return.frag.spv returns"
mkdir "$out/same"
python3 -I "$repo/plan/tools/nvrtx/sm75-desktop-extract.py" "$repo" "$out/same" > /dev/null
for source in "$repo"/plan/ws313/tests/mali-compiler-shaders/*; do
	glslc --target-env=vulkan1.0 -o "$out/same/mali-$(basename "$source").spv" "$source"
done
for module in "$out"/same/*.spv; do
	commands="$commands same $module"
done
flags="-std=gnu11 -O1 -g -DKERN_USER_ABI_LP64 -I$repo/include -I$repo/src -I$repo -idirafter $repo/libc/include \
-fsanitize=address,undefined -fno-sanitize=alignment -Wall -Wextra -Werror -Wno-unused-function"
cc $flags -o "$out/spirv-inline-host-test" "$repo/tests/spirv-inline-host-test.c" "$repo/tests/mali-compiler-model.c" \
	"$repo/src/drivers/gpu/mali/compiler/va-compile.c" "$repo/src/drivers/gpu/mali/compiler/va-encode.c" \
	"$repo/src/drivers/gpu/mali/compiler/va-schedule.c" "$repo/src/drivers/gpu/compiler/optimize.c" -lm
status=0
# shellcheck disable=SC2086
"$out/spirv-inline-host-test" "$out" "$trials" $commands > "$out/result.txt" 2>&1 || status=1
for module in "$out"/*.inlined.spv; do
	if spirv-val --target-env vulkan1.0 "$module" >> "$out/result.txt" 2>&1; then
		echo "PASS spirv-val $(basename "$module")" >> "$out/result.txt"
	else
		echo "FAIL spirv-val $(basename "$module")" >> "$out/result.txt"
		status=1
	fi
done
grep -v '^PASS same' "$out/result.txt" || true
echo "same: $(grep -c '^PASS same' "$out/result.txt" || true) of $(ls "$out"/same/*.spv | wc -l)"
if [ "$status" -ne 0 ] || grep -q '^FAIL' "$out/result.txt"; then
	echo "FAIL spirv-inline-host-test ($out)"
	exit 1
fi
echo "PASS spirv-inline-host-test ($out)"
