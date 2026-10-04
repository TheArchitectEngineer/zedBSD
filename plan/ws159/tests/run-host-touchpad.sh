#!/bin/sh
# Builds and runs the host test of the compositor's touch pad layer
# (ws159-p004, userland/desktop/wayland/touchpad.c compiled unchanged).
# Usage: plan/ws159/tests/run-host-touchpad.sh [build-dir]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu

root=$(cd "$(dirname "$0")/../../.." && pwd)
out=${1:-$root/build/ws159-host-touchpad}
mkdir -p "$out"

cc=${CC:-clang}
extra=${EXTRA_CFLAGS:-}
flags="-std=gnu11 -O1 -g -Wall -Wextra -Werror -I $root/userland/desktop/wayland"
$cc $flags $extra -c "$root/userland/desktop/wayland/touchpad.c" -o "$out/touchpad.o"
$cc $flags $extra -c "$root/plan/ws159/tests/host-touchpad.c" -o "$out/host-touchpad.o"
$cc $extra "$out/host-touchpad.o" "$out/touchpad.o" -o "$out/host-touchpad"
"$out/host-touchpad"
