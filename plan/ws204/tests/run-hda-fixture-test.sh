#!/bin/sh
# ws204-p002 (from ws035-p007's): compiles the production HD Audio driver against a register and codec model, once
# normally and once with ASan/UBSan, in a new folder of the build (old folders are Q1's to remove), and runs it.
# The last line is "hda fixture: all PASS".
# usage: sh plan/ws204/tests/run-hda-fixture-test.sh   (from anywhere)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
mkdir -p "$repo/build/tmp"
work=$(mktemp -d "$repo/build/tmp/ws204-hda.XXXXXX")
flags="-std=c11 -Wall -Wextra -Werror -DKERN_USER_ABI_LP64 -DHAL_ARCH_AMD64
    -I$repo/include -I$repo/src -I$repo/include/libc -DKERN_UAPI_NATIVE
    -include $repo/include/libc/sys/ioctl.h"
# shellcheck disable=SC2086
cc $flags -O2 "$repo/plan/ws204/tests/hda-fixture.c" "$repo/src/drivers/pci/pci-hda.c" -o "$work/ordinary"
"$work/ordinary"
# shellcheck disable=SC2086
cc $flags -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer "$repo/plan/ws204/tests/hda-fixture.c" \
    "$repo/src/drivers/pci/pci-hda.c" -o "$work/sanitized"
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 "$work/sanitized"
