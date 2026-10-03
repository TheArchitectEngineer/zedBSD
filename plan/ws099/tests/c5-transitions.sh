#!/bin/sh
# ws099-p001, C5: App Home and Wiseview open and close without stopping with 10 windows, and the first frame after
# the request comes within C5_FIRST_FRAME_MS (QEMU's number; the criterion's 100 ms is the machine's, WS075).
# zdesktop --glass --log-frames at 1280x800 with C5_WINDOWS popup-probe windows; C5_ROUNDS times: Wiseview opened
# with Super+Tab and closed with Esc, App Home opened with the launcher (top-left) and closed with Esc.
# plan/ws099/tests/c5-parse.py reads the at_ms of the requests, the frames and the settled lines.
#
#   plan/ws035/tests/zdesktop-guest.sh start build/ws099-criteria.img
#   plan/ws099/tests/c5-transitions.sh [OUTDIR]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
GUEST_RUNTIME="${GUEST_RUNTIME:-$(pwd)/build/ws035-sq-run}"
export GUEST_RUNTIME
out=${1:-build/ws099-shots/c5}
C5_WINDOWS=${C5_WINDOWS:-10}
C5_ROUNDS=${C5_ROUNDS:-3}
C5_FIRST_FRAME_MS=${C5_FIRST_FRAME_MS:-100}
C5_GAP_MS=${C5_GAP_MS:-150}
mkdir -p "$out"
guest() { timeout 120 python3 plan/tools/guest/guest.py run "$1" 2>&1 </dev/null; }
pointer() { python3 plan/ws035/tests/qmp-pointer.py "$GUEST_RUNTIME/qmp.sock" "$@"; }
keys() { python3 plan/ws035/tests/qmp-keys.py "$GUEST_RUNTIME/qmp.sock" "$@"; }
check() { python3 plan/ws035/tests/zdesktop-check.py "$@" --runtime "$GUEST_RUNTIME"; }
stop_all='service stop greeter >/dev/null 2>&1; for p in $(ps -A -o pid,args | grep -E "[w]ayland( |$)|[p]opup-probe" | awk "{print \$1}"); do kill $p; done; i=0; while ps -A -o args | grep -qE "[w]ayland( |$)|[p]opup-probe" && [ $i -lt 50 ]; do sleep 0.2; i=$((i+1)); done'

guest "$stop_all" >/dev/null
guest 'export XDG_RUNTIME_DIR=/tmp; rm -f /tmp/wayland-0; picture=; [ -f /usr/share/keiland/wallpaper.ppm ] && picture=--wallpaper=/usr/share/keiland/wallpaper.ppm
/bin/wayland --timeout=900 --width=1280 --height=800 --glass --log-frames $picture > /tmp/zdesktop.log 2>&1 </dev/null & i=0; while [ ! -S /tmp/wayland-0 ] && [ $i -lt 60 ]; do sleep 0.5; i=$((i+1)); done; sleep 1; echo started' >/dev/null
guest "export XDG_RUNTIME_DIR=/tmp; i=0; while [ \$i -lt $C5_WINDOWS ]; do /bin/popup-probe --timeout-s=800 --token=w\$i > /tmp/w\$i.log 2>&1 </dev/null & sleep 1.5; i=\$((i+1)); done; echo started" >/dev/null
sleep 3
mapped=$(guest "grep -c 'ZWL MAP client=' /tmp/zdesktop.log" | tail -1)
echo "windows mapped: $mapped"
pointer move 640 790 sleep 300
check "$out/windows.png" >/dev/null

round=1
while [ $round -le "$C5_ROUNDS" ]; do
	keys '<super-tab>'; sleep 2
	[ $round -eq 1 ] && check "$out/wiseview.png" >/dev/null
	keys '<esc>'; sleep 2
	pointer move 23 17 sleep 300 down sleep 60 up sleep 2000
	[ $round -eq 1 ] && check "$out/home.png" >/dev/null
	keys '<esc>'; sleep 2
	round=$((round + 1))
done

guest 'cat /tmp/zdesktop.log' > "$out/zdesktop.log"
guest 'grep -cE "ZWL ERROR|FAILED" /tmp/zdesktop.log' | tail -1 | sed 's/^/errors: /'
guest "$stop_all" >/dev/null
python3 plan/ws099/tests/c5-parse.py "$out/zdesktop.log" "$C5_FIRST_FRAME_MS" "$C5_GAP_MS"
status=$?
[ "${mapped:-0}" -ge "$C5_WINDOWS" ] || { echo "only ${mapped:-0} windows mapped"; status=1; }
[ $status -eq 0 ] && echo "C5: PASS" || echo "C5: FAIL"
exit $status
