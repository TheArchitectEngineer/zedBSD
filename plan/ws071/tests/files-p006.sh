#!/bin/sh
# ws071-p006: the home dashboard of zdesktop-files on the Venus guest (the lean image,
# build-files-image.sh).  zdesktop --glass at 1280x800 with the wallpaper; zdesktop-files at
# 1000x640 on the sample home (/tmp/fhome), opened on the dashboard.
#  1. dashboard.png: the hero card (the wallpaper, the greeting), the folder cards, no recent files.
#  2. The Pictures card opens Pictures (LOCATION .../Pictures items=2).
#  3. Report.pdf opened in Documents (OPEN); the Home button: dashboard-recent.png shows it
#     among the recent files.
#  4. A click on the recent file shows it in its folder, selected (LOCATION Documents, SELECT count=1).
#  5. Show all opens the home folder's listing (LOCATION kind=folder path=/tmp/fhome items=7).
#
#   plan/ws071/tests/files-guest.sh start     (the guest must be up)
#   plan/ws071/tests/files-p006.sh [OUTDIR]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
GUEST_RUNTIME="${GUEST_RUNTIME:-$(pwd)/build/ws071-run}"
export GUEST_RUNTIME
out=${1:-build/ws071-p006}
mkdir -p "$out"
guest() { timeout 90 python3 plan/tools/guest/guest.py run "$1" 2>&1; }
check() { python3 plan/ws035/tests/zdesktop-check.py "$@" --runtime "$GUEST_RUNTIME"; }
pointer() { python3 plan/ws035/tests/qmp-pointer.py "$GUEST_RUNTIME/qmp.sock" "$@"; }
keys() { python3 plan/ws035/tests/qmp-keys.py "$GUEST_RUNTIME/qmp.sock" "$@"; }
stop_all='for p in $(ps -A -o pid,args | grep -E "[z]desktop( |$)|[z]desktop-files" | awk "{print \$1}"); do kill $p; done; i=0; while ps -A -o args | grep -qE "[z]desktop( |$)|[z]desktop-files" && [ $i -lt 50 ]; do sleep 0.2; i=$((i+1)); done'
status=0

# Fails the run unless a log has a line matching a pattern.
expect_log() {
	found=$(guest "grep -cE '$2' $1" | tail -1)
	if [ "${found:-0}" -gt 0 ] 2>/dev/null; then
		echo "log: $2 ok"
	else
		echo "log: $2 MISSING"
		status=1
	fi
}

# Clicks a point of the window's body (x, y from its top left) and waits.
click() {
	pointer move $((wx + $1 - 2)) $((wy + $2)) sleep 150 move $((wx + $1)) $((wy + $2)) sleep 300 down sleep 60 up sleep "${3:-700}"
}
double() {
	pointer move $((wx + $1)) $((wy + $2)) sleep 300 down sleep 50 up sleep 80 down sleep 50 up sleep "${3:-900}"
}
shot() {
	pointer move 1270 790 sleep 400
	check "$out/$1" >/dev/null
}

guest "$stop_all" >/dev/null
guest 'rm -f /tmp/wayland-0 /tmp/zdesktop-files.clipboard; rm -rf /tmp/fhome; sh /usr/share/zdesktop-files-tests/make-home.sh /tmp/fhome >/dev/null'
guest 'export XDG_RUNTIME_DIR=/tmp
picture=; [ -f /usr/share/zdesktop/wallpaper.ppm ] && picture=--wallpaper=/usr/share/zdesktop/wallpaper.ppm
/bin/zdesktop --timeout=900 --width=1280 --height=800 --glass $picture > /tmp/zdesktop.log 2>&1 </dev/null & sleep 4
HOME=/tmp/fhome /bin/zdesktop-files --token=f1 --timeout-s=800 --width=1000 --height=640 > /tmp/f.log 2>&1 </dev/null & sleep 6; echo started' >/dev/null
set -- $(guest "grep 'ZWL MAP client=1 ' /tmp/zdesktop.log | tail -1" | sed -n 's/.* surface=\([0-9]*\) x=\([-0-9]*\) y=\([-0-9]*\).*/\1 \2 \3/p')
surface=${1:-0}; wx=${2:-0}; wy=${3:-0}
echo "files: surface $surface at $wx,$wy"

# 1. The dashboard.
expect_log /tmp/f.log 'ZFILES LOCATION kind=home path=/tmp/fhome items=0 error=0'
shot dashboard.png

# 2. The Pictures card (the second).
click 520 362
expect_log /tmp/f.log 'ZFILES LOCATION kind=folder path=/tmp/fhome/Pictures items=2 error=0'

# 3. A file opened, then Home.
click 100 177
double 779 172
expect_log /tmp/f.log 'ZFILES OPEN path=/tmp/fhome/Documents/Report.pdf'
click 109 32
shot dashboard-recent.png

# 4. The recent file (the first row under Recent Files).
click 500 590
found=$(guest "grep -c 'LOCATION kind=folder path=/tmp/fhome/Documents ' /tmp/f.log" | tail -1)
[ "${found:-0}" -ge 2 ] && echo "recent: ok" || { echo "recent: MISSING"; status=1; }
expect_log /tmp/f.log 'ZFILES SELECT count=1 '

# 5. Show all.
click 109 32
click 920 299
expect_log /tmp/f.log 'ZFILES LOCATION kind=folder path=/tmp/fhome items=7 error=0'

errors=$(guest "grep -c ERROR /tmp/zdesktop.log" | tail -1)
[ "${errors:-1}" = 0 ] && echo "zdesktop: no ERROR" || { echo "zdesktop: ERROR lines"; status=1; }
guest "$stop_all" >/dev/null
guest 'cat /tmp/f.log' > "$out/f.log"
[ $status = 0 ] && echo "files-p006: PASS" || echo "files-p006: FAIL"
exit $status
