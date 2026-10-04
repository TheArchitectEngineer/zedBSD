#!/bin/sh
# Builds and runs the host test of the touch pad layer's gestures
# (ws142-p003, userland/desktop/wayland/touchpad.c compiled unchanged).
# Usage: plan/ws142/tests/run-host-gesture.sh [build-dir]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu

root=$(cd "$(dirname "$0")/../../.." && pwd)
out=${1:-$root/build/ws142-host-gesture}
mkdir -p "$out"

cc=${CC:-clang}
extra=${EXTRA_CFLAGS:-}
flags="-std=gnu11 -O1 -g -Wall -Wextra -Werror -I $root/userland/desktop/wayland"
$cc $flags $extra -c "$root/userland/desktop/wayland/touchpad.c" -o "$out/touchpad.o"
$cc $flags $extra -c "$root/plan/ws142/tests/host-gesture.c" -o "$out/host-gesture.o"
$cc $extra "$out/host-gesture.o" "$out/touchpad.o" -o "$out/host-gesture"
"$out/host-gesture"
