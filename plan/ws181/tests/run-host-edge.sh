#!/bin/sh
# Builds and runs the host test of the screen edges' gestures (WS181 p003,
# userland/desktop/wayland/edge.c compiled unchanged).
# Usage: plan/ws181/tests/run-host-edge.sh [build-dir]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu

root=$(cd "$(dirname "$0")/../../.." && pwd)
out=${1:-$root/build/ws181-host-edge}
mkdir -p "$out"

cc=${CC:-clang}
extra=${EXTRA_CFLAGS:-}
flags="-std=gnu11 -O1 -g -Wall -Wextra -Werror -I $root/userland/desktop/wayland"
$cc $flags $extra -c "$root/userland/desktop/wayland/edge.c" -o "$out/edge.o"
$cc $flags $extra -c "$root/plan/ws181/tests/host-edge.c" -o "$out/host-edge.o"
$cc $extra "$out/host-edge.o" "$out/edge.o" -o "$out/host-edge"
"$out/host-edge"
