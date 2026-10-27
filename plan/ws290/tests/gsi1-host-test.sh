#!/bin/sh
# Host test of the GHCI shader compiler (SPIR-V through the shared IR to GSI1 code and the record of
# what it binds): the encoder against amaterrace-gpu's assembler, every module's code through the
# disassembler and back, its record and code against what docs/ghci.md and the driver's convention
# say, and the modules the model runs on tools/gsi1model.py against the IR reference
# (plan/ws290/tests/gsi1-host-test.py).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
#
# usage: plan/ws290/tests/gsi1-host-test.sh   (from anywhere)
#        GSI1_TOOLS=<dir> picks the reference tools (default plan/ws290/tests/gsi1-tools, copied from
#        amaterrace-gpu cf8be08 tools/).
# Needs cc, glslc and python3.  Everything goes in a new directory below build/tmp; nothing is
# removed.  About a minute.
set -eu
repo=$(cd "$(dirname "$0")/../../.." && pwd)
tests="$repo/plan/ws290/tests"
tools="${GSI1_TOOLS:-$tests/gsi1-tools}"
mkdir -p "$repo/build/tmp"
out=$(mktemp -d "$repo/build/tmp/gsi1-host.XXXXXX")
flags="-std=gnu11 -O1 -g -DKERN_USER_ABI_LP64 -I$repo/include -I$repo/src -I$repo -idirafter $repo/libc/include \
-fsanitize=address,undefined -fno-sanitize=alignment -Wall -Wextra -Werror"
compiler="$repo/src/drivers/gpu/ghci/compiler"

# The compiler under the kernel's rule (no declaration after a statement), then the harness, the
# reference and the shared frontend and optimizer (which have their own rules).
objects=""
for source in gsi1-compile gsi1-encode gsi1-io gsi1-memory gsi1-texture gsi1-image gsi1-schedule gsi1-spill; do
	cc $flags -Wdeclaration-after-statement -c -o "$out/$source.o" "$compiler/$source.c"
	objects="$objects $out/$source.o"
done
cc $flags -Wno-declaration-after-statement -o "$out/gsi1-host-test" "$tests/gsi1-host-test.c" "$tests/gsi1-reference.c" \
	$objects "$repo/src/drivers/gpu/compiler/spirv.c" "$repo/src/drivers/gpu/compiler/optimize.c" -lm

# The modules.
for source in "$tests"/gsi1/*.comp "$tests"/gsi1/*.vert "$tests"/gsi1/*.frag; do
	glslc --target-env=vulkan1.0 -o "$out/$(basename "$source").spv" "$source"
done

status=0
export ASAN_OPTIONS=detect_leaks=0
timeout 600 python3 -I "$tests/gsi1-host-test.py" "$out/gsi1-host-test" "$tools" "$out" "$out" || status=1
if [ "$status" -ne 0 ]; then
	echo "FAIL gsi1-host-test ($out)"
	exit 1
fi
echo "PASS gsi1-host-test ($out)"
