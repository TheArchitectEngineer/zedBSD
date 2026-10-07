#!/bin/sh
# ws052-p011: builds and runs the host test of the backend's reading of sessiond's sleep answers
# (userland/desktop/libkeiland-backend-zedbsd/power-outcome.c, compiled unchanged).  Last line: host-backend-sleep: PASS.
#   sh plan/ws052/tests/run-host-backend-sleep.sh [OUT_DIR]   (default build/ws052-host/backend-sleep)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
out=${1:-build/ws052-host/backend-sleep}
mkdir -p "$out"
cc=${CC:-cc}
flags="-std=gnu11 -O1 -g -Wall -Wextra -Werror -I."
"$cc" $flags -c userland/desktop/libkeiland-backend-zedbsd/power-outcome.c -o "$out/power-outcome.o"
"$cc" $flags -c plan/ws052/tests/host-backend-sleep.c -o "$out/main.o"
"$cc" -o "$out/host-backend-sleep" "$out/main.o" "$out/power-outcome.o"
exec "$out/host-backend-sleep"
