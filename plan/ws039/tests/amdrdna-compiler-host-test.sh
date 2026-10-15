#!/bin/sh
# Host test of the amdrdna GFX10 shader compiler (ws039-p007): the encoder's known words, compared with LLVM's assembler.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
#
# usage: plan/ws039/tests/amdrdna-compiler-host-test.sh   (from anywhere)
# Needs cc, python3 and llvm-mc-19 (gfx1010).  Everything goes in a new directory below build/tmp; nothing is removed.
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
if [ "$status" -ne 0 ]; then
	echo "FAIL amdrdna-compiler-host-test ($out)"
	exit 1
fi
echo "PASS amdrdna-compiler-host-test ($out)"
