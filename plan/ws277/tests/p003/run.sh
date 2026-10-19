#!/bin/sh
# Host checks of ws277-p003: the DC8200's commits, vertical-blank count and
# fatal stop on fake register windows and a fake interrupt (dc8200.c), and
# the flip bookkeeping of holds, taken commits and losses (dc8200-flip.c).
#   plan/ws277/tests/p003/run.sh OUTDIR
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
root=$(cd -- "$(dirname -- "$0")/../../../.." && pwd)
here=$root/plan/ws277/tests/p003
out=$1
mkdir -p "$out"
cc -std=c99 -D_POSIX_C_SOURCE=200809L -O2 -Wall -Wextra -Werror -I"$root/include" -I"$root/src" \
	"$root/src/drivers/gpu/powervr/dc8200-adopt.c" \
	"$root/src/drivers/gpu/powervr/dc8200.c" \
	"$here/fatal-host-test.c" -o "$out/fatal-host-test"
cc -std=c99 -O2 -Wall -Wextra -Werror -I"$root/include" -I"$root/src" \
	"$root/src/drivers/gpu/powervr/dc8200-flip.c" \
	"$here/flip-host-test.c" -o "$out/flip-host-test"
"$out/fatal-host-test"
"$out/flip-host-test"
FLAGS="-std=c11 -O1 -Wall -Wextra -Werror -I$root/include -I$root/src -I$root -DHAL_ARCH_AMD64 -DHAL_BOARD_PCAT -DKERN_USER_ABI_LP64 -D_POSIX_C_SOURCE=200809L -include time.h -ffunction-sections -fdata-sections"
cc $FLAGS -c "$root/src/kern/boot.c" -o "$out/boot.o"
cc $FLAGS -c "$here/boot-zerocopy-host-test.c" -o "$out/boot-zerocopy-host-test.o"
cc -Wl,--gc-sections "$out/boot.o" "$out/boot-zerocopy-host-test.o" -o "$out/boot-zerocopy-host-test"
"$out/boot-zerocopy-host-test"
