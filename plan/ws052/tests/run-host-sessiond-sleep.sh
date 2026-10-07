#!/bin/sh
# ws052-p011: builds and runs the host test of sessiond's sleep answers (userland/desktop/sessiond/sleep-rules.c,
# compiled unchanged).  Last line: host-sessiond-sleep: PASS.
#   sh plan/ws052/tests/run-host-sessiond-sleep.sh [OUT_DIR]   (default build/ws052-host/sessiond-sleep)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
out=${1:-build/ws052-host/sessiond-sleep}
mkdir -p "$out"
cc=${CC:-cc}
flags="-std=gnu11 -O1 -g -Wall -Wextra -Werror -I."
"$cc" $flags -c userland/desktop/sessiond/sleep-rules.c -o "$out/sleep-rules.o"
"$cc" $flags -c plan/ws052/tests/host-sessiond-sleep.c -o "$out/main.o"
"$cc" -o "$out/host-sessiond-sleep" "$out/main.o" "$out/sleep-rules.o"
exec "$out/host-sessiond-sleep"
