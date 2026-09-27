#!/bin/sh
# ws056-p002: builds spawn-probe.c on the guest and runs it against ELF put
# at /bin/posix-r2, over SSH, with a time limit.
#   sh plan/ws056/tests/guest-spawn-probe.sh IMAGE ELF
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
image=$1
elf=$2
guest="python3 plan/tools/guest/guest.py"
export GUEST_RUNTIME="$(pwd)/build/ws056/spawn-run"

$guest stop >/dev/null 2>&1 || true
$guest start --disk nvme "$image" >/dev/null
$guest wait >/dev/null
$guest put "$elf" /bin/posix-r2
$guest put plan/ws056/tests/spawn-probe.c /root/spawn-probe.c
$guest run 'chmod 755 /bin/posix-r2 && cd /root && clang -o spawn-probe spawn-probe.c && timeout 20 ./spawn-probe; echo probe $?; timeout 20 ./spawn-probe /bin/echo; echo probe-echo $?'
$guest run 'cd /root && timeout 60 /bin/posix-r2 </dev/null >/root/r2.out 2>&1; echo r2 $?; tail -3 /root/r2.out'
$guest stop >/dev/null
