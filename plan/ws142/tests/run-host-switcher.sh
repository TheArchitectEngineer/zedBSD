#!/bin/sh
# Builds and runs the host test of the application switcher's state (ws142-p005,
# userland/desktop/wayland/switcher.c and apps.c compiled unchanged).
# Usage: plan/ws142/tests/run-host-switcher.sh [build-dir]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu

root=$(cd "$(dirname "$0")/../../.." && pwd)
out=${1:-$root/build/ws142-host-switcher}
mkdir -p "$out"

cc=${CC:-clang}
extra=${EXTRA_CFLAGS:-}
flags="-std=gnu11 -O1 -g -Wall -Wextra -Werror -I $root/userland/desktop/wayland"
$cc $flags $extra -c "$root/userland/desktop/wayland/apps.c" -o "$out/apps.o"
$cc $flags $extra -c "$root/userland/desktop/wayland/switcher.c" -o "$out/switcher.o"
$cc $flags $extra -c "$root/plan/ws142/tests/host-switcher.c" -o "$out/host-switcher.o"
$cc $extra "$out/host-switcher.o" "$out/switcher.o" "$out/apps.o" -o "$out/host-switcher"
"$out/host-switcher"
