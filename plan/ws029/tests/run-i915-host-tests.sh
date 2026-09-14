#!/bin/sh
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
# Builds and runs the i915 host fixtures (uncore, gtt, irq) plainly and under ASan/UBSan.
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/ws029-i915-host.XXXXXX")
trap 'rm -rf -- "$work"' EXIT HUP INT TERM
compiler=${CC:-cc}
tests=${1:-"uncore gtt irq lrc stream backend"}

for name in $tests; do
    source="$repo/plan/ws029/tests/i915-$name-test.c"
    "$compiler" -std=gnu11 -O2 -Wall -Wextra -Werror -Wdeclaration-after-statement \
        -DKERN_USER_ABI_LP64 -I"$repo/include" -idirafter "$repo/libc/include" \
        "$source" -o "$work/$name-ordinary"
    "$work/$name-ordinary"

    "$compiler" -std=gnu11 -O1 -g -Wall -Wextra -Werror -Wdeclaration-after-statement \
        -fsanitize=address,undefined -fno-omit-frame-pointer \
        -DKERN_USER_ABI_LP64 -I"$repo/include" -idirafter "$repo/libc/include" \
        "$source" -o "$work/$name-sanitized"
    ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 "$work/$name-sanitized"
done
echo "i915 host fixtures PASS: $tests"
