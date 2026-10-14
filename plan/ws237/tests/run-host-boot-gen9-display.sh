#!/bin/sh
# ws237-p002: builds and runs the host test of i915.gen9.display=flip and =copy
# (host-boot-gen9-display.c with src/kern/boot.c, ASan/UBSan, --gc-sections) in a new directory under build/tmp
# (nothing is removed here; Q1's plan/tools/q1-clean.sh removes the old runs).
#   sh plan/ws237/tests/run-host-boot-gen9-display.sh
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
. "$repo/plan/tools/fresh-out.sh"
fresh_out "$repo/build/tmp/ws237-boot-gen9-display"
out=$fresh_dir
CC=${CC:-clang}
FLAGS="-std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=all -ffunction-sections -fdata-sections -I$repo/include -I$repo/src -I$repo -DHAL_ARCH_AMD64 -DHAL_BOARD_PCAT -DKERN_USER_ABI_LP64 -D_POSIX_C_SOURCE=200809L -include time.h"
$CC $FLAGS -c "$repo/src/kern/boot.c" -o "$out/boot.o"
$CC $FLAGS -c "$repo/plan/ws237/tests/host-boot-gen9-display.c" -o "$out/host-boot-gen9-display.o"
$CC -fsanitize=address,undefined -Wl,--gc-sections "$out/boot.o" "$out/host-boot-gen9-display.o" -o "$out/host-boot-gen9-display"
"$out/host-boot-gen9-display"
