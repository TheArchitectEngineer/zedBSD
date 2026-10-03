#!/bin/sh
# ws056-p002: builds sigev-thread-mask.c on a guest image with its clang and
# runs it a few times (BUG-046).
#   sh plan/tools/posix/guest-sigev.sh IMAGE [RUNS]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
image=$1
runs=${2:-3}
guest="python3 plan/tools/guest/guest.py"
export GUEST_RUNTIME="$(pwd)/build/ws056/sigev-run"

$guest stop >/dev/null 2>&1 || true
$guest start --disk nvme "$image" >/dev/null
$guest wait >/dev/null
$guest put plan/tools/posix/sigev-thread-mask.c /root/sigev-thread-mask.c
$guest run "cd /root && clang -O1 -o sigev-thread-mask sigev-thread-mask.c && n=0 && while [ \$n -lt $runs ]; do n=\$((n + 1)); ./sigev-thread-mask; echo status \$?; done"
$guest stop >/dev/null
