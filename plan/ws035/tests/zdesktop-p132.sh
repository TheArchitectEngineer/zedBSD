#!/bin/sh
# ws035-p132 (BUG-112): xdg_wm_base.destroy only counts the xdg_surfaces made from the binding destroyed.
# /bin/popup-probe --wm-probe binds the shell twice (A and B), shows its window from A, then:
#  1. destroys B (no xdg_surface of its own): the connection stays (WM-PROBE other-binding ok);
#  2. destroys A under its window: zdesktop answers xdg_wm_base's defunct_surfaces and closes the
#     connection (WM-PROBE same-binding error ok ... code=1; one ZWL ERROR in zdesktop's log).
#
#   plan/ws035/tests/zdesktop-guest.sh start     (the guest must be up)
#   plan/ws035/tests/zdesktop-p132.sh [OUTDIR]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
GUEST_RUNTIME="${GUEST_RUNTIME:-$(pwd)/build/ws035-sq-run}"
export GUEST_RUNTIME
out=${1:-build/ws035-p132}
mkdir -p "$out"
guest() { timeout 90 python3 plan/tools/guest/guest.py run "$1" 2>&1; }
check() { python3 plan/ws035/tests/zdesktop-check.py "$@" --runtime "$GUEST_RUNTIME"; }
stop_all='service stop greeter >/dev/null 2>&1; for p in $(ps -A -o pid,args | grep -E "[w]ayland( |$)|[p]opup-probe" | awk "{print \$1}"); do kill $p; done; i=0; while ps -A -o args | grep -qE "[w]ayland( |$)|[p]opup-probe" && [ $i -lt 50 ]; do sleep 0.2; i=$((i+1)); done'
status=0

# Fails the run unless a log has (within a few seconds) as many lines matching a pattern as asked (default 1).
expect_log() {
	tries=0
	found=0
	while [ $tries -lt 5 ]; do
		found=$(guest "grep -cE '$2' $1" | tail -1)
		[ "${found:-0}" -ge "${3:-1}" ] 2>/dev/null && break
		tries=$((tries + 1))
		sleep 1
	done
	if [ "${found:-0}" -ge "${3:-1}" ] 2>/dev/null; then
		echo "log: $2 ok"
	else
		echo "log: $2 MISSING (found ${found:-0})"
		status=1
	fi
}

guest "$stop_all" >/dev/null
guest 'export XDG_RUNTIME_DIR=/tmp; rm -f /tmp/wayland-0
/bin/wayland --timeout=120 --width=1280 --height=800 --glass > /tmp/zdesktop.log 2>&1 </dev/null & for w in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30; do grep -q ZWL.READY /tmp/zdesktop.log 2>/dev/null && break; sleep 0.5; done; sleep 1
/bin/popup-probe --wm-probe --timeout-s=60 --token=w > /tmp/w.log 2>&1 </dev/null; echo "probe exit=$?" >> /tmp/w.log; echo done' >/dev/null

# The probe's two steps and its verdict.
expect_log /tmp/w.log 'POPUPPROBE ready run=w'
expect_log /tmp/w.log 'WM-PROBE other-binding ok'
expect_log /tmp/w.log 'WM-PROBE same-binding error ok interface=xdg_wm_base code=1'
expect_log /tmp/w.log 'WM-PROBE:PASS'
expect_log /tmp/w.log 'probe exit=0'

# zdesktop refused only the second destroy, with the defunct_surfaces code, and lives on.
expect_log /tmp/zdesktop.log 'ZWL ERROR client=[0-9]+ object=[0-9]+ code=1 reason=xdg_surfaces of this binding live'
errors=$(guest "grep -c 'ZWL ERROR' /tmp/zdesktop.log" | tail -1)
[ "${errors:-0}" = 1 ] || { echo "zdesktop logged ${errors:-0} protocol errors (1 expected)"; status=1; }
alive=$(guest "ps -A -o args | grep -cE '[w]ayland( |$)'" | tail -1)
[ "${alive:-0}" -ge 1 ] 2>/dev/null || { echo "zdesktop is gone"; status=1; }
check "$out/after.png" >/dev/null

guest 'cat /tmp/w.log' > "$out/probe.log"
guest 'grep -E "ZWL (ERROR|MAP|CLIENT)" /tmp/zdesktop.log' > "$out/zdesktop.log"
guest "$stop_all" >/dev/null
[ $status -eq 0 ] && echo "p132: PASS" || echo "p132: FAIL"
exit $status
