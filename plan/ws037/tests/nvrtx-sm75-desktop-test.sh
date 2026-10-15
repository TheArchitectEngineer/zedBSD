#!/bin/sh
# Every desktop shader (userland/desktop/**/shaders.h, as the components ship them) through the nvrtx SM75
# compiler (ws037-p008) for Turing, Ampere and Ada (SM75, SM86, SM89; ws242-p002): prints each module compiled or
# refused with the reason, and runs each compiled one on the interpreter (with that shader model's dependence check) and
# its IR on a reference interpreter for many inputs; they must agree.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
#
# usage: plan/ws037/tests/nvrtx-sm75-desktop-test.sh [--strict] [--sm 75|86|89]   (from anywhere)
# --strict fails when any module is refused; --sm compiles for one shader model only (all three by default).
# Everything goes in a new directory below build/tmp; nothing is removed.
set -eu
repo=$(cd "$(dirname "$0")/../../.." && pwd)
mkdir -p "$repo/build/tmp"
out=$(mktemp -d "$repo/build/tmp/nvrtx-sm75-desktop.XXXXXX")
mkdir "$out/spv"
python3 -I "$repo/plan/ws037/tests/sm75-desktop-extract.py" "$repo" "$out/spv"
# The compiler's own graphics test modules, run the same way.
for source in "$repo"/plan/ws037/tests/sm75/*.vert "$repo"/plan/ws037/tests/sm75/*.frag; do
	name=$(basename "$source" | tr . -)
	glslc --target-env=vulkan1.0 -O -o "$out/spv/test-$name.spv" "$source"
done
flags="-std=gnu11 -O1 -g -DKERN_USER_ABI_LP64 -I$repo/include -I$repo/src -I$repo -idirafter $repo/libc/include \
-fsanitize=address,undefined -fno-sanitize=alignment"
cc $flags -Wall -Wextra -Werror -Wdeclaration-after-statement -o "$out/nvrtx-sm75-desktop-test" \
	"$repo/plan/ws037/tests/nvrtx-sm75-desktop-test.c" "$repo/plan/ws037/tests/sm75-machine.c" \
	"$repo/plan/ws037/tests/sm75-reference.c" \
	"$repo/src/drivers/gpu/nvrtx/compiler/sm75.c" \
	"$repo/src/drivers/gpu/compiler/spirv.c" \
	"$repo/src/drivers/gpu/compiler/optimize.c" -lm
"$out/nvrtx-sm75-desktop-test" "$@" "$out"/spv/*.spv
