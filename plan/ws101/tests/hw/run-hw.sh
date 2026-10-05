#!/bin/sh
# ws101-p005: runs i915 test scenarios (e.g. vkcs, and the executor's regression vkx vke1 vke2 vkc) on the 5330.
# Each scenario's image is built first, outside the machine's lock (VKLOOP_BUILD_ONLY=1), in OUT/SCENARIO; a scenario
# without a directory starts from a copy of one that has one, so only the files of the test flags are rebuilt.  Then
# plan/ws075/tests/test-hw.sh runs each under flock /tmp/i915-hw.lock and copies its logs to OUT/hw-SCENARIO-TAG.
# Do not edit the tree until the runs end: the make inside the lock picks up any change.
# Each scenario is built with its own test set (plan/ws031/tests/vkloop-hw.sh picks it, q762: no test kernel holds
# every scenario); I915_TEST_SET overrides it.
# The set goes to both makes (the prebuild and the one inside the lock), or the second would relink the other set.
# Each run's time limit includes the wait for the lock, which another WS may hold for a while (hence an hour).
#
#   plan/ws101/tests/hw/run-hw.sh TAG SCENARIO...        OUT (default build/ws101-hw), I915_HOST (default solaris10-man)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
[ $# -ge 2 ] || { echo "usage: $0 TAG SCENARIO..."; exit 2; }
cd "$(dirname -- "$0")/../../../.."
O=${OUT:-build/ws101-hw}
host=${I915_HOST:-solaris10-man}
tag=$1
shift
mkdir -p "$O"
for s in "$@"; do
	if [ ! -d "$O/$s" ]; then
		for d in "$O"/*/hdd-image.img; do
			[ -f "$d" ] && { cp -a "$(dirname -- "$d")" "$O/$s"; break; }
		done
	fi
	BUILD=$O/$s ZEDBSD_CONFIG=plan/ws075/tests/config-test-hw.mk VKLOOP_BUILD_ONLY=1 I915_HOST=$host \
		timeout 3000 plan/ws031/tests/vkloop-hw.sh test "$s" > "$O/prebuild-$s.log" 2>&1
	echo "prebuild $s exit=$?"
done
status=0
for s in "$@"; do
	grep -q "vkloop-hw: built" "$O/prebuild-$s.log" || { echo "skip $s (build failed: $O/prebuild-$s.log)"; status=1; continue; }
	BUILD=$O/$s I915_HOST=$host \
		timeout 3600 plan/ws075/tests/test-hw.sh "$s" "$O/hw-$s-$tag" || status=1
done
exit $status
