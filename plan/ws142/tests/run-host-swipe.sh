#!/bin/sh
# Builds and runs the host test of one swipe as one step (ws142-p009,
# userland/desktop/wayland/swipe.c compiled unchanged).
# Usage: plan/ws142/tests/run-host-swipe.sh [build-dir]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu

root=$(cd "$(dirname "$0")/../../.." && pwd)
out=${1:-$root/build/ws142-host-swipe}
mkdir -p "$out"

cc=${CC:-clang}
extra=${EXTRA_CFLAGS:-}
flags="-std=gnu11 -O1 -g -Wall -Wextra -Werror -I $root/userland/desktop/wayland"
$cc $flags $extra -c "$root/userland/desktop/wayland/swipe.c" -o "$out/swipe.o"
$cc $flags $extra -c "$root/plan/ws142/tests/host-swipe.c" -o "$out/host-swipe.o"
$cc $extra "$out/host-swipe.o" "$out/swipe.o" -o "$out/host-swipe"
"$out/host-swipe"
