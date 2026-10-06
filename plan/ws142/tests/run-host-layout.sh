#!/bin/sh
# Builds and runs the host test of the session's layout mode (ws142-p008,
# userland/desktop/wayland/layout.c compiled unchanged).
# Usage: plan/ws142/tests/run-host-layout.sh [build-dir]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu

root=$(cd "$(dirname "$0")/../../.." && pwd)
out=${1:-$root/build/ws142-host-layout}
mkdir -p "$out"

cc=${CC:-clang}
extra=${EXTRA_CFLAGS:-}
flags="-std=gnu11 -O1 -g -Wall -Wextra -Werror -I $root/userland/desktop/wayland"
$cc $flags $extra -c "$root/userland/desktop/wayland/layout.c" -o "$out/layout.o"
$cc $flags $extra -c "$root/plan/ws142/tests/host-layout.c" -o "$out/host-layout.o"
$cc $extra "$out/host-layout.o" "$out/layout.o" -o "$out/host-layout"
"$out/host-layout"
