#!/bin/sh
# ws089-p006: runs every Settings guest test in turn on one Venus guest (the lean image, build-settings-image.sh),
# waiting for the guest's SSH first (settings-wait.sh), and prints one PASS or FAIL line a test and the total.
# The older tests (p002, p003, p007, p008) do not wait by themselves, so this waits once before them.
#
#   plan/ws089/tests/settings-guest.sh start
#   plan/ws089/tests/settings-regress.sh [OUTDIR]   (default build/ws089-shots/regress; one directory a test)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
GUEST_RUNTIME="${GUEST_RUNTIME:-$(pwd)/build/ws089-run}"
export GUEST_RUNTIME
out=${1:-build/ws089-shots/regress}
mkdir -p "$out"
guest() { timeout 90 python3 plan/tools/guest/guest.py run "$1" 2>&1 </dev/null; }
status=0
. plan/ws089/tests/settings-wait.sh
wait_guest
failed=""
for test in p006 p002 p003 p004 p005 p007 p008 p009; do
	timeout 1500 sh "plan/ws089/tests/settings-$test.sh" "$out/$test" > "$out/$test.log" 2>&1
	result=$(tail -1 "$out/$test.log")
	echo "$test: $result"
	case $result in
	*PASS*) ;;
	*) failed="$failed $test"; status=1 ;;
	esac
done
[ $status = 0 ] && echo "settings-regress: PASS" || echo "settings-regress: FAIL:$failed"
exit $status
