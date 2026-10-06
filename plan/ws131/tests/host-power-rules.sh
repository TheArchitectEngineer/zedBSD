#!/bin/sh
# Builds and runs the host test of who may end the machine through sessiond (ws131-p027,
# userland/desktop/sessiond/power-rules.c compiled unchanged).
# Usage: plan/ws131/tests/host-power-rules.sh [build-dir]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu

root=$(cd "$(dirname "$0")/../../.." && pwd)
out=${1:-$root/build/ws131-host-power-rules}
mkdir -p "$out"

cc=${CC:-clang}
extra=${EXTRA_CFLAGS:-}
flags="-std=gnu11 -O1 -g -Wall -Wextra -Werror -I $root/userland/desktop/sessiond"
$cc $flags $extra -c "$root/userland/desktop/sessiond/power-rules.c" -o "$out/power-rules.o"
$cc $flags $extra -c "$root/plan/ws131/tests/host-power-rules.c" -o "$out/host-power-rules.o"
$cc $extra "$out/host-power-rules.o" "$out/power-rules.o" -o "$out/host-power-rules"
"$out/host-power-rules"
