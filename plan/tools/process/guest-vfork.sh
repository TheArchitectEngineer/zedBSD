#!/bin/sh
# Builds vfork-test.c on a guest image with its clang and runs it (fork
# copy on write, vfork, posix_spawn, concurrent forks).  From ws064-p003.
#   sh plan/tools/process/guest-vfork.sh IMAGE
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
image=$1
guest="python3 plan/tools/guest/guest.py"
export GUEST_RUNTIME="$(pwd)/build/guest-vfork-run"

$guest stop >/dev/null 2>&1 || true
$guest start --disk nvme "$image" >/dev/null
$guest wait >/dev/null
$guest put plan/tools/process/vfork-test.c /root/vfork-test.c
$guest run 'cd /root && clang -O1 -o vfork-test vfork-test.c && ./vfork-test; echo status $?'
$guest stop >/dev/null
