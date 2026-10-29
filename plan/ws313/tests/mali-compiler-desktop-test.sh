#!/bin/sh
# Every desktop shader (userland/desktop/**/shaders.h, as the components ship them) through the Mali Valhall
# compiler: each module must compile, every word must decode with the test's own decoder (generated from Mesa's
# ISA.xml), and the programs must agree with the IR reference on the one-lane Valhall model (with its wait checks)
# for TRIALS random inputs.  Listings of the programs are written next to the results.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
#
# usage: plan/ws313/tests/mali-compiler-desktop-test.sh [TRIALS]   (from anywhere; 200 trials by default)
# Needs cc and python3.  Output goes in a new directory below build/pc; nothing is removed.
# Nothing here is evidence about the GPU.
set -eu
repo=$(cd "$(dirname "$0")/../../.." && pwd)
trials=${1:-200}
mkdir -p "$repo/build/pc"
out=$(mktemp -d "$repo/build/pc/mali-compiler-desktop.XXXXXX")
mkdir "$out/spv"
python3 -I "$repo/plan/tools/nvrtx/sm75-desktop-extract.py" "$repo" "$out/spv" > /dev/null
flags="-std=gnu11 -O1 -g -DKERN_USER_ABI_LP64 -I$repo/include -I$repo/src -I$repo -idirafter $repo/libc/include \
-fsanitize=address,undefined -fno-sanitize=alignment -Wall -Wextra -Werror -Wdeclaration-after-statement"
cc $flags -o "$out/mali-compiler-host-test" "$repo/tests/mali-compiler-host-test.c" "$repo/tests/mali-compiler-model.c" \
	"$repo/src/drivers/gpu/mali/compiler/va-compile.c" "$repo/src/drivers/gpu/mali/compiler/va-encode.c" \
	"$repo/src/drivers/gpu/mali/compiler/va-schedule.c" "$repo/src/drivers/gpu/compiler/spirv.c" \
	"$repo/src/drivers/gpu/compiler/optimize.c" -lm
status=0
"$out/mali-compiler-host-test" "$out" --trials "$trials" "$out"/spv/*.spv > "$out/result.txt" 2>&1 || status=1
grep -v '^PASS' "$out/result.txt" || true
echo "modules: $(ls "$out"/spv/*.spv | wc -l), passed: $(grep -c '^PASS .*\.spv$' "$out/result.txt" || true)"
if [ "$status" -ne 0 ]; then
	echo "FAIL mali-compiler-desktop-test ($out)"
	exit 1
fi
echo "PASS mali-compiler-desktop-test ($out)"
