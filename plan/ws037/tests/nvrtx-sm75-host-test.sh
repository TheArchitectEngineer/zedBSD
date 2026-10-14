#!/bin/sh
# Host test of the nvrtx SM75 shader compiler (ws037-p008): known instruction words, fields, and compute modules
# compiled and run on an interpreter of the emitted instructions.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
#
# usage: plan/ws037/tests/nvrtx-sm75-host-test.sh   (from anywhere)
# Needs glslc.  The modules are plan/ws037/tests/sm75/*.comp and ws101's add.comp and ids.comp.  Everything goes in a
# new directory below build/tmp; nothing is removed.
set -eu
repo=$(cd "$(dirname "$0")/../../.." && pwd)
mkdir -p "$repo/build/tmp"
out=$(mktemp -d "$repo/build/tmp/nvrtx-sm75.XXXXXX")
for source in "$repo"/plan/ws037/tests/sm75/*.comp "$repo/plan/ws101/tests/host/shaders/add.comp" \
	"$repo/plan/ws101/tests/host/shaders/ids.comp"; do
	name=$(basename "$source" .comp)
	glslc --target-env=vulkan1.0 -fshader-stage=compute -o "$out/$name.spv" "$source"
done
flags="-std=gnu11 -O1 -g -DKERN_USER_ABI_LP64 -I$repo/include -I$repo/src -I$repo -idirafter $repo/libc/include \
-fsanitize=address,undefined -fno-sanitize=alignment"
cc $flags -Wall -Wextra -Werror -Wdeclaration-after-statement -o "$out/nvrtx-sm75-host-test" \
	"$repo/plan/ws037/tests/nvrtx-sm75-host-test.c" \
	"$repo/src/drivers/gpu/nvrtx/compiler/sm75-encode.c" \
	"$repo/src/drivers/gpu/nvrtx/compiler/sm75-compile.c" \
	"$repo/src/drivers/gpu/compiler/spirv.c" \
	"$repo/src/drivers/gpu/compiler/optimize.c" -lm
"$out/nvrtx-sm75-host-test" "$out"
