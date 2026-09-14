#!/bin/sh
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
# Builds and runs the WS031 native Vulkan executor host fixtures, plainly and
# under ASan/UBSan.  Add fixtures to the default list as modules land.
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/ws031-vk-host.XXXXXX")
trap "rm -rf -- \"$work\"" EXIT HUP INT TERM
compiler=${CC:-cc}
tests=${1:-"cmd spirv res resdispatch sync eu compile pipe cmdbuf"}
base="-std=gnu11 -Wall -Wextra -Werror -Wdeclaration-after-statement -DKERN_USER_ABI_LP64 -DVK_REPO=\"$repo\" -I$repo/include -I$repo -idirafter $repo/libc/include"

for name in $tests; do
    source="$repo/plan/ws031/tests/i915-vk-$name-test.c"
    "$compiler" $base -O2 "$source" -o "$work/$name-ordinary"
    "$work/$name-ordinary"
    "$compiler" $base -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer "$source" -o "$work/$name-sanitized"
    ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 "$work/$name-sanitized"
done
echo "WS031 vk host fixtures PASS: $tests"
