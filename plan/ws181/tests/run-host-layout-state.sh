#!/bin/sh
# Builds and runs the host test of the windows' states and the docked owner's
# check (WS181 p002, userland/desktop/wayland/layout.c compiled unchanged).
# Usage: plan/ws181/tests/run-host-layout-state.sh [build-dir]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu

root=$(cd "$(dirname "$0")/../../.." && pwd)
out=${1:-$root/build/ws181-host-layout-state}
mkdir -p "$out"

cc=${CC:-clang}
extra=${EXTRA_CFLAGS:-}
flags="-std=gnu11 -O1 -g -Wall -Wextra -Werror -I $root/userland/desktop/wayland"
$cc $flags $extra -c "$root/userland/desktop/wayland/layout.c" -o "$out/layout.o"
$cc $flags $extra -c "$root/plan/ws181/tests/host-layout-state.c" -o "$out/host-layout-state.o"
$cc $extra "$out/host-layout-state.o" "$out/layout.o" -o "$out/host-layout-state"
"$out/host-layout-state"
