#!/bin/sh
# Builds and runs the host test of the power dialog's layout (ws099-p037,
# userland/desktop/wayland/power-layout.c compiled unchanged).
# Usage: plan/ws099/tests/host-power-layout.sh [build-dir]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu

root=$(cd "$(dirname "$0")/../../.." && pwd)
out=${1:-$root/build/ws099-host-power-layout}
mkdir -p "$out"

cc=${CC:-clang}
extra=${EXTRA_CFLAGS:-}
flags="-std=gnu11 -O1 -g -Wall -Wextra -Werror -I $root/userland/desktop/wayland"
$cc $flags $extra -c "$root/userland/desktop/wayland/power-layout.c" -o "$out/power-layout.o"
$cc $flags $extra -c "$root/plan/ws099/tests/host-power-layout.c" -o "$out/host-power-layout.o"
$cc $extra "$out/host-power-layout.o" "$out/power-layout.o" -o "$out/host-power-layout"
"$out/host-power-layout"
