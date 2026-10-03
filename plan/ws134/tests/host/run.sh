#!/bin/sh
# ws134-p002: builds and runs the host tests of the System Monitor (host-test.c) with the host's compiler; ws134-p004:
# and of its input (interact-test.c, with libkeiland's gestures; the monitor's log goes to OUTDIR/interact.log).
#   plan/ws134/tests/host/run.sh [OUTDIR]     (prints "monitor-host: PASS", "monitor-interact: PASS" and "monitor-rate: PASS", or FAIL)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../../.."
out=${1:-build/ws134-host}
mkdir -p "$out"
m=userland/desktop/monitor
cc -std=c11 -D_DEFAULT_SOURCE -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -I"$m" \
    plan/ws134/tests/host/host-test.c "$m/source.c" "$m/history.c" "$m/rules.c" "$m/format.c" -lm -o "$out/host-test"
"$out/host-test" plan/ws134/tests/replay/normal.txt
cc -std=gnu11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -I"$m" -Iuserland/desktop/keiland \
    plan/ws134/tests/host/interact-test.c "$m/interact.c" userland/desktop/libkeiland/gesture.c \
    userland/desktop/libkeiland/motion.c -lm -o "$out/interact-test"
"$out/interact-test" > "$out/interact.log"
# ws134-p012: libkeiland's monitor rates (rate-test.c with system-monitor-rate.c).
cc -std=gnu11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -Iuserland/desktop/keiland \
    -Iuserland/desktop/libkeiland/system plan/ws134/tests/host/rate-test.c userland/desktop/libkeiland/system/system-monitor-rate.c \
    -lm -o "$out/rate-test"
"$out/rate-test" > "$out/rate.log"
tail -1 "$out/rate.log"
