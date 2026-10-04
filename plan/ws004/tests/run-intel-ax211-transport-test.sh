#!/bin/sh
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
# Runs the real command transport against bounded fake CSR and DMA callbacks.
set -eu
repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
python3 "$repo_root/plan/tools/driver-fragments/prepare.py" \
    --source src/drivers/wifi/intel-ax211/intel-ax211.c
fixture_dir=$(mktemp -d "${TMPDIR:-/tmp}/zedbsd-ax211-transport.XXXXXX")
trap 'rm -rf "$fixture_dir"' EXIT HUP INT TERM
cc=${CC:-cc}
core="$repo_root/build/driver-fragments/src/drivers/intel-ax211.c"
transport="$repo_root/src/drivers/wifi/intel-ax211/intel-ax211-transport.c"
fixture="$repo_root/plan/ws004/tests/intel-ax211-transport-test.c"

# Exercises the production path with ordinary ANSI C host compilation.
"$cc" -std=c89 -D_DEFAULT_SOURCE -pedantic -Wno-long-long \
    -Wall -Wextra -Werror -O2 -I"$repo_root/include" \
    "$core" "$transport" "$fixture" -o "$fixture_dir/ordinary"
timeout 30 "$fixture_dir/ordinary"

# Checks bounds when hardware pointers advance beyond the DMA slot array.
"$cc" -std=c89 -D_DEFAULT_SOURCE -pedantic -Wno-long-long \
    -Wall -Wextra -Werror -O1 -g -fno-omit-frame-pointer \
    -fsanitize=address,undefined -I"$repo_root/include" \
    "$core" "$transport" "$fixture" -o "$fixture_dir/sanitize"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
    UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
    timeout 30 "$fixture_dir/sanitize"
