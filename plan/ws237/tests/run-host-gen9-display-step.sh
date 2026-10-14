#!/bin/sh
# ws237 (the safe default): builds and runs the host test of the Gen9 display's step when a step fails
# (host-gen9-display-step.c with src/kern/boot.c and src/drivers/gpu/i915/display/gen9-display.c, ASan/UBSan,
# --gc-sections) in a new directory under build/tmp (nothing is removed here; Q1's plan/tools/q1-clean.sh removes
# the old runs).
#   sh plan/ws237/tests/run-host-gen9-display-step.sh
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
. "$repo/plan/tools/fresh-out.sh"
fresh_out "$repo/build/tmp/ws237-gen9-display-step"
out=$fresh_dir
CC=${CC:-clang}
FLAGS="-std=c11 -O1 -g -Wall -Wextra -Werror -pthread -fsanitize=address,undefined -fno-sanitize-recover=all -ffunction-sections -fdata-sections -I$repo/include -I$repo/src -I$repo -DHAL_ARCH_AMD64 -DHAL_BOARD_PCAT -DKERN_USER_ABI_LP64 -D_POSIX_C_SOURCE=200809L -include time.h"
$CC $FLAGS -c "$repo/src/kern/boot.c" -o "$out/boot.o"
$CC $FLAGS -c "$repo/src/drivers/gpu/i915/display/gen9-display.c" -o "$out/gen9-display.o"
$CC $FLAGS -c "$repo/plan/ws237/tests/host-gen9-display-step.c" -o "$out/host-gen9-display-step.o"
$CC -pthread -fsanitize=address,undefined -Wl,--gc-sections "$out/boot.o" "$out/gen9-display.o" "$out/host-gen9-display-step.o" -o "$out/host-gen9-display-step"
"$out/host-gen9-display-step"
