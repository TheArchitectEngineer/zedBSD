#!/bin/sh
# ws075: one run of a test scenario of the i915 test build on the 5330 (plan/ws031/tests/vkloop-hw.sh test SCENARIO,
# built with plan/ws075/tests/config-test-hw.mk into BUILD), holding the machine's lock for the whole run and copying
# what the run leaves in /tmp (shared by every user of the machine) into OUTDIR before the lock is released:
# run.log (vkloop-hw.sh's output: the scenario's verdict lines first), serial.log (the guest's whole log) and
# build.log.
#
#   plan/ws075/tests/test-hw.sh SCENARIO OUTDIR        e.g. plan/ws075/tests/test-hw.sh vke2 build/ws075-p004/hw-vke2
#   BUILD (default build/resident-vkx) and I915_HOST (default awe@10.0.30.3) pass through.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
[ $# -eq 2 ] || { echo "usage: $0 SCENARIO OUTDIR"; exit 2; }
scenario=$1
out=$2
cd "$(dirname -- "$0")/../../.."
rm -rf "$out"
mkdir -p "$out"
BUILD=${BUILD:-build/resident-vkx}
ZEDBSD_CONFIG=${ZEDBSD_CONFIG:-plan/ws075/tests/config-test-hw.mk}
I915_HOST=${I915_HOST:-awe@10.0.30.3}
export BUILD ZEDBSD_CONFIG I915_HOST

# The machine, for the whole run; what the run left in /tmp is copied before the lock goes.
exec 9>/tmp/i915-hw.lock
flock 9
rm -f /tmp/vkloop-last.log
plan/ws031/tests/vkloop-hw.sh test "$scenario" > "$out/run.log" 2>&1
status=$?
[ -f /tmp/vkloop-last.log ] && cp /tmp/vkloop-last.log "$out/serial.log"
[ -f /tmp/resident-build.log ] && cp /tmp/resident-build.log "$out/build.log"
flock -u 9
echo "test-hw: $scenario vkloop-hw.sh exit=$status; $out/run.log"
grep -aE 'verdict' "$out/run.log" | head -5
exit $status
