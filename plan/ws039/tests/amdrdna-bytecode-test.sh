#!/bin/sh
# Host test (j) of ws039-p014: the amdrdna runner of the video BIOS's command routines against the reference runner
# (Linux v6.19 drivers/gpu/drm/amd/amdgpu/atom.c, built here from the temp tree with the shims of bytecode-oracle/,
# never kept in the tree), on the board's real image (every routine, three parameter patterns, the same register
# model), and synthetic routines for the limits.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
#
# usage: plan/ws039/tests/amdrdna-bytecode-test.sh [IMAGE]
#   IMAGE defaults to plan/ws039/temp/dump/cold/rom.bin (the board's video BIOS, read by hw-dump.py); the Linux tree is
#   plan/ws039/temp/linux (AMDRDNA_LINUX overrides it).  Needs cc.  Everything goes in a new directory below build/tmp;
#   nothing is removed.
set -eu
repo=$(cd "$(dirname "$0")/../../.." && pwd)
tests="$repo/plan/ws039/tests"
image=${1:-$repo/plan/ws039/temp/dump/cold/rom.bin}
linux=${AMDRDNA_LINUX:-$repo/plan/ws039/temp/linux}
amd="$linux/drivers/gpu/drm/amd"
mkdir -p "$repo/build/tmp"
out=$(mktemp -d "$repo/build/tmp/amdrdna-bytecode.XXXXXX")
# The reference, copied out so that its own directory does not supply the kernel's amdgpu.h.
cp "$amd/amdgpu/atom.c" "$out/reference-atom.c"
cc -std=gnu11 -O1 -g -c -w -include "$tests/bytecode-oracle/oracle.h" -I"$tests/bytecode-oracle" -I"$amd/include" \
	-I"$amd/amdgpu" "$out/reference-atom.c" -o "$out/reference-atom.o"
cc -std=gnu11 -O1 -g -DKERN_USER_ABI_LP64 -DKERN_UAPI_NATIVE -I"$repo/include" -I"$repo/src" -I"$repo" \
	-fsanitize=address,undefined -fno-sanitize=alignment -Wall -Wextra -Werror -Wdeclaration-after-statement \
	-include uapi/errno.h -o "$out/amdrdna-bytecode-test" "$tests/amdrdna-bytecode-test.c" \
	"$repo/src/drivers/gpu/amdrdna/bytecode.c" "$out/reference-atom.o"
if ASAN_OPTIONS=detect_leaks=0 "$out/amdrdna-bytecode-test" "$image"; then
	echo "PASS amdrdna-bytecode-test ($out)"
else
	echo "FAIL amdrdna-bytecode-test ($out)"
	exit 1
fi
