#!/bin/sh
# ws052-p010: builds and runs the host test of networkd's sleep record (userland/base/networkd/sleep-state.c,
# compiled unchanged).  Last line: host-networkd-sleep: PASS.
#   sh plan/ws052/tests/run-host-networkd-sleep.sh [OUT_DIR]   (default build/ws052-host/networkd-sleep)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
out=${1:-build/ws052-host/networkd-sleep}
mkdir -p "$out"
cc=${CC:-cc}
flags="-std=gnu11 -O1 -g -Wall -Wextra -Werror -I."
"$cc" $flags -c userland/base/networkd/sleep-state.c -o "$out/sleep-state.o"
"$cc" $flags -c plan/ws052/tests/host-networkd-sleep.c -o "$out/main.o"
"$cc" -o "$out/host-networkd-sleep" "$out/main.o" "$out/sleep-state.o"
exec "$out/host-networkd-sleep"
