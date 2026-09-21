#!/bin/sh
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
# Builds and runs the i915 core host fixtures plainly and under ASan/UBSan.
#
# Each fixture is linked with the one driver file it tests, built as its own
# translation unit exactly as the kernel builds it; i915-host-stubs.inc
# supplies the rest of the kernel.  The fixtures of the retired legacy code
# (uncore, GGTT, irq, lrc, backend) are listed in README-retired.md.
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/ws029-i915-host.XXXXXX")
trap 'rm -rf -- "$work"' EXIT HUP INT TERM
compiler=${CC:-cc}
tests=${1:-"ppgtt stream"}
driver=$repo/src/drivers/gpu/i915
base="-std=gnu11 -Wall -Wextra -Werror -Wdeclaration-after-statement -DKERN_USER_ABI_LP64 -I$repo/include -I$repo -idirafter $repo/libc/include"

for name in $tests; do
    source="$repo/plan/ws029/tests/i915-$name-test.c"
    case $name in
    ppgtt)
        sources="$source $driver/ppgtt.c"
        ;;
    stream)
        sources="$source $driver/command.c"
        ;;
    *)
        echo "run-i915-host-tests.sh: unknown fixture $name" >&2
        exit 1
        ;;
    esac

    "$compiler" $base -O2 $sources -o "$work/$name-ordinary"
    "$work/$name-ordinary"

    "$compiler" $base -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
        $sources -o "$work/$name-sanitized"
    ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 "$work/$name-sanitized"
done
echo "i915 host fixtures PASS: $tests"
