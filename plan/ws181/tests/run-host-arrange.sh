#!/bin/sh
# Builds and runs the host test of the arrangement of windows (WS181 p004,
# userland/desktop/wayland/arrange.c compiled unchanged).
# Usage: plan/ws181/tests/run-host-arrange.sh [build-dir]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu

root=$(cd "$(dirname "$0")/../../.." && pwd)
out=${1:-$root/build/ws181-host-arrange}
mkdir -p "$out"

cc=${CC:-clang}
extra=${EXTRA_CFLAGS:-}
flags="-std=gnu11 -O1 -g -Wall -Wextra -Werror -I $root/userland/desktop/wayland"
$cc $flags $extra -c "$root/userland/desktop/wayland/arrange.c" -o "$out/arrange.o"
$cc $flags $extra -c "$root/plan/ws181/tests/host-arrange.c" -o "$out/host-arrange.o"
$cc $extra "$out/host-arrange.o" "$out/arrange.o" -o "$out/host-arrange"
"$out/host-arrange"
