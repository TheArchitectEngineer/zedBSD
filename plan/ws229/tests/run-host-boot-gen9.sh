#!/bin/sh
# ws229-p002: builds and runs the host test of i915.gen9=on and i915.gen9=off
# (host-boot-gen9.c with src/kern/boot.c, ASan/UBSan, --gc-sections) in a new directory under build/tmp
# (nothing is removed here; Q1's plan/tools/q1-clean.sh removes the old runs).
#   sh plan/ws229/tests/run-host-boot-gen9.sh
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
. "$repo/plan/tools/fresh-out.sh"
fresh_out "$repo/build/tmp/ws229-boot-gen9"
out=$fresh_dir
CC=${CC:-clang}
FLAGS="-std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=all -ffunction-sections -fdata-sections -I$repo/include -I$repo/src -I$repo -DHAL_ARCH_AMD64 -DHAL_BOARD_PCAT -DKERN_USER_ABI_LP64 -D_POSIX_C_SOURCE=200809L -include time.h"
$CC $FLAGS -c "$repo/src/kern/boot.c" -o "$out/boot.o"
$CC $FLAGS -c "$repo/plan/ws229/tests/host-boot-gen9.c" -o "$out/host-boot-gen9.o"
$CC -fsanitize=address,undefined -Wl,--gc-sections "$out/boot.o" "$out/host-boot-gen9.o" -o "$out/host-boot-gen9"
"$out/host-boot-gen9"
