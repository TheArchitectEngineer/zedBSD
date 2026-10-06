#!/bin/sh
# ws089-p013: About's memory row on the host, through Settings' host renderer (host-build.sh): with the stand-in monitor
# (HOST_MEMORY, host-kl-system.c: 16 GB, 9.5 GB free) About opens the machine's monitor and shows "16 GB (9.5 GB free)";
# without a monitor the row is left out.
#   sh plan/ws089/tests/run-host-about-memory.sh [OUTPUT]   (default build/ws089-about-memory)
# Each run gets a new directory behind OUTPUT (plan/tools/fresh-out.sh); nothing is removed.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
out=${1:-build/ws089-about-memory}
. plan/tools/fresh-out.sh
fresh_out "$out"
sh plan/ws089/tests/host-build.sh >/dev/null
render=build/ws089-host/settings-render
status=0
HOST_ACCOUNT_RESULT=1 HOST_MEMORY=1 timeout 60 "$render" --page=about draw="$out/memory.ppm" draw="$out/memory.ppm" > "$out/memory.log" 2>&1 || status=1
grep -q "ABOUT monitor open=1" "$out/memory.log" && echo "ok About opens the monitor" || { echo "FAIL no monitor"; status=1; }
grep -q "ABOUT memory total=17179869184 free=10200547328" "$out/memory.log" && echo "ok About takes the memory" || { echo "FAIL no memory"; status=1; }
HOST_ACCOUNT_RESULT=1 timeout 60 "$render" --page=about draw="$out/none.ppm" > "$out/none.log" 2>&1 || status=1
grep -q "ABOUT monitor open=0" "$out/none.log" && echo "ok without a monitor, no row" || { echo "FAIL without a monitor"; status=1; }
grep -q "ABOUT memory" "$out/none.log" && { echo "FAIL a memory without a monitor"; status=1; } || true
convert "$out/memory.ppm" "$out/memory.png"
[ $status -eq 0 ] && echo "run-host-about-memory: PASS" || echo "run-host-about-memory: FAIL"
exit $status
