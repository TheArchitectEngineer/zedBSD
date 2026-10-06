#!/bin/sh
# ws071-p018: new windows are seen whole on a 1280x800 output (the Venus guest, the lean image).
# zdesktop --glass tells windows of xdg-shell version 4 the space for bodies (configure_bounds: the output
# less the system bar, a floating title bar and the margins, 1256x680 under the 44-pixel system bar since 2026-10-05) and keeps the cascade inside it:
#  1. one.png: files at its own size (1120x720) takes the bounds' height (KWL BOUNDS ... width=1256
#     height=680, ZFILES READY width=1120 height=680) and maps under the title bar (y=108).
#  2. two.png: a second files of 1000x640 would hide the first one's title at the centre, so it goes down and
#     right of it (the cascade, ws035-p092) and ends inside the space (x > 80, y > 108, x + 1000 <= 1268,
#     y + 640 <= 788; the place itself is zdesktop's choice among its cascade places).
#  3. home.png: files started from App Home fits too (the case of files-p011's files.png).
#
#   plan/tools/files/files-guest.sh start     (the guest must be up)
#   plan/tools/files/files-p018.sh [OUTDIR]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
GUEST_RUNTIME="${GUEST_RUNTIME:-$(pwd)/build/ws071-run}"
export GUEST_RUNTIME
out=${1:-build/ws071-p018}
mkdir -p "$out"
guest() { timeout 90 python3 plan/tools/guest/guest.py run "$1" 2>&1; }
. plan/tools/guest/zwl-clients.sh
check() { python3 plan/ws035/tests/zdesktop-check.py "$@" --runtime "$GUEST_RUNTIME"; }
pointer() { python3 plan/ws035/tests/qmp-pointer.py "$GUEST_RUNTIME/qmp.sock" "$@"; }
stop_all='service stop greeter >/dev/null 2>&1; for p in $(ps -A -o pid,args | grep -E "[w]ayland( |$)|[f]iles" | awk "{print \$1}"); do kill $p; done; i=0; while ps -A -o args | grep -qE "[w]ayland( |$)|[f]iles" && [ $i -lt 50 ]; do sleep 0.2; i=$((i+1)); done'
start='export XDG_RUNTIME_DIR=/tmp; rm -f /tmp/wayland-0
picture=; [ -f /usr/share/keiland/wallpaper.png ] && picture=--wallpaper=/usr/share/keiland/wallpaper.png
/bin/wayland --testing --timeout=300 --width=1280 --height=800 --glass $picture > /tmp/zdesktop.log 2>&1 </dev/null & sleep 4; echo started'
status=0

# Fails the run unless a log has a line matching a pattern (within a few seconds).
expect_log() {
	tries=0
	found=0
	while [ $tries -lt 8 ]; do
		found=$(guest "grep -cE '$2' $1" | tail -1)
		[ "${found:-0}" -gt 0 ] 2>/dev/null && break
		tries=$((tries + 1))
		sleep 1
	done
	if [ "${found:-0}" -gt 0 ] 2>/dev/null; then
		echo "log: $2 ok"
	else
		echo "log: $2 MISSING"
		status=1
	fi
}
shot() {
	pointer move 1270 790 sleep 500
	check "$out/$1" >/dev/null
}

# 1. One window at its own size.
guest "$stop_all; rm -f /etc/keiland/apps.conf; sh /usr/share/files-tests/make-home.sh /tmp/fhome >/dev/null 2>&1" >/dev/null
guest "$start" >/dev/null
guest 'export XDG_RUNTIME_DIR=/tmp; HOME=/tmp/fhome /bin/files --token=f1 --timeout-s=200 > /tmp/f1.log 2>&1 </dev/null & sleep 6; echo started' >/dev/null
zwl_app_clients
expect_log /tmp/zdesktop.log "KWL BOUNDS client=$zc1 surface=[0-9]+ width=1256 height=680"
expect_log /tmp/f1.log 'ZFILES READY width=1120 height=680'
expect_log /tmp/zdesktop.log "KWL MAP client=$zc1 surface=[0-9]+ x=80 y=108"
shot one.png

# 2. A second window, cascaded and kept inside.
guest 'export XDG_RUNTIME_DIR=/tmp; HOME=/tmp/fhome /bin/files --token=f2 --timeout-s=200 --width=1000 --height=640 > /tmp/f2.log 2>&1 </dev/null & sleep 6; echo started' >/dev/null
expect_log /tmp/f2.log 'ZFILES READY width=1000 height=640'
expect_log /tmp/zdesktop.log "KWL MAP client=$zc2 surface=[0-9]+ x=[0-9]+ y=[0-9]+"
set -- $(guest "grep 'KWL MAP client=$zc2 ' /tmp/zdesktop.log | tail -1" | sed -n 's/.* x=\([0-9]*\) y=\([0-9]*\).*/\1 \2/p')
if [ "${1:-0}" -gt 80 ] && [ "${2:-0}" -gt 108 ] && [ $(( ${1:-9999} + 1000 )) -le 1268 ] && [ $(( ${2:-9999} + 640 )) -le 788 ]; then
	echo "second window at ${1},${2}: down and right of the first, inside the space ok"
else
	echo "second window at ${1:-?},${2:-?}: not down and right of the first inside the space MISSING"
	status=1
fi
shot two.png
errors=$(guest "grep -c ERROR /tmp/zdesktop.log" | tail -1)
[ "${errors:-1}" = 0 ] && echo "zdesktop: no ERROR" || { echo "zdesktop: ERROR lines"; status=1; }

# 3. From App Home.
guest "$stop_all" >/dev/null
guest "$start" >/dev/null
pointer move 23 17 sleep 300 down sleep 60 up sleep 1500
set -- $(guest "grep 'KWL HOME icon name=\"Files\"' /tmp/zdesktop.log | tail -1" | sed -n 's/.* x=\([0-9]*\) y=\([0-9]*\).*/\1 \2/p')
pointer move ${1:-0} ${2:-0} sleep 400 down sleep 60 up sleep 8000
zwl_app_clients
expect_log /tmp/zdesktop.log "KWL BOUNDS client=$zc1 surface=[0-9]+ width=1256 height=680"
expect_log /tmp/zdesktop.log "KWL MAP client=$zc1 surface=[0-9]+ x=80 y=108"
shot home.png

errors=$(guest "grep -c ERROR /tmp/zdesktop.log" | tail -1)
[ "${errors:-1}" = 0 ] && echo "zdesktop: no ERROR" || { echo "zdesktop: ERROR lines"; status=1; }
guest "$stop_all" >/dev/null
[ $status = 0 ] && echo "files-p018: PASS" || echo "files-p018: FAIL"
exit $status
