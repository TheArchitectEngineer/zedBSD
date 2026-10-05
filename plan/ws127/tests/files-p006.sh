#!/bin/sh
# ws127-p006 (F-039): a drag that rests on another tab brings it to the front, on the Venus guest
# (plan/tools/files/build-files-image.sh; start the guest first, e.g. plan/tools/files/files-guest.sh start).
# zdesktop --glass at 1280x800, files at 1000x640 on the sample home (/tmp/fhome), opened on Projects/zedBSD.
#  1. Ctrl+T opens a second tab (TABS new index=1); in it docs is opened (Enter on docs); the first tab is clicked
#     again (TABS select index=0): tabs.png.
#  2. README.md (the fourth item) dragged onto the second tab (the right half of the tab bar) and held: the tab
#     comes to the front (DRAG spring tab=1, TABS select index=1) while the button is still down (tab-spring.png);
#     the release on its empty part moves README.md into docs.
#  The window's points are those of files-p010.sh (the items at y 110); the tab bar is at y 8 (host-spring.sh's 27
#  less the 20 the guest's window places differ by, as for the items).
#
#   sh plan/ws127/tests/files-p006.sh [OUTDIR]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
GUEST_RUNTIME="${GUEST_RUNTIME:-$(pwd)/build/ws071-run}"
export GUEST_RUNTIME
out=${1:-build/ws127-p006}
mkdir -p "$out"
guest() { timeout 90 python3 plan/tools/guest/guest.py run "$1" 2>&1; }
. plan/tools/guest/zwl-clients.sh
check() { python3 plan/ws035/tests/zdesktop-check.py "$@" --runtime "$GUEST_RUNTIME"; }
pointer() { python3 plan/ws035/tests/qmp-pointer.py "$GUEST_RUNTIME/qmp.sock" "$@"; }
keys() { python3 plan/ws035/tests/qmp-keys.py "$GUEST_RUNTIME/qmp.sock" "$@"; sleep 0.7; }
stop_all='service stop greeter >/dev/null 2>&1; for p in $(ps -A -o pid,args | grep -E "[w]ayland( |$)|[f]iles" | awk "{print \$1}"); do kill $p; done; i=0; while ps -A -o args | grep -qE "[w]ayland( |$)|[f]iles" && [ $i -lt 50 ]; do sleep 0.2; i=$((i+1)); done'
status=0

# Fails the run unless a log has a line matching a pattern (within a few seconds).
expect_log() {
	tries=0
	found=0
	while [ $tries -lt 5 ]; do
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

# Fails the run unless a guest test command succeeds.
expect_guest() {
	if guest "$1 && echo YES" | grep -q YES; then
		echo "guest: $2 ok"
	else
		echo "guest: $2 FAILED"
		status=1
	fi
}

# Clicks a point of the window's body (x, y from its top left) and waits.
click() {
	pointer move $((wx + $1 - 2)) $((wy + $2)) sleep 150 move $((wx + $1)) $((wy + $2)) sleep 300 down sleep 60 up sleep "${3:-700}"
}
# Presses a point and moves, with the button held, to another in a few steps; the button stays down.
drag() {
	pointer move $((wx + $1 - 2)) $((wy + $2)) sleep 150 move $((wx + $1)) $((wy + $2)) sleep 300 down sleep 120 \
	    move $((wx + $1 - 12)) $((wy + $2 + 4)) sleep 80 \
	    move $((wx + ($1 + $3) / 2)) $((wy + ($2 + $4) / 2)) sleep 80 \
	    move $((wx + $3 + 3)) $((wy + $4 + 1)) sleep 80 \
	    move $((wx + $3)) $((wy + $4)) sleep "${5:-600}"
}
shot() {
	pointer move 1270 790 sleep 400
	check "$out/$1" >/dev/null
}

guest "$stop_all" >/dev/null
guest 'export XDG_RUNTIME_DIR=/tmp; rm -f /tmp/wayland-0 /tmp/files.clipboard; rm -rf /tmp/fhome; sh /usr/share/files-tests/make-home.sh /tmp/fhome >/dev/null
picture=; [ -f /usr/share/keiland/wallpaper.png ] && picture=--wallpaper=/usr/share/keiland/wallpaper.png
/bin/wayland --timeout=600 --width=1280 --height=800 --glass $picture > /tmp/zdesktop.log 2>&1 </dev/null & sleep 4
HOME=/tmp/fhome /bin/files --token=f1 --timeout-s=500 --width=1000 --height=640 /tmp/fhome/Projects/zedBSD > /tmp/f.log 2>&1 </dev/null & sleep 5; echo started' >/dev/null
zwl_app_clients
set -- $(guest "grep 'ZWL MAP client=$zc1 ' /tmp/zdesktop.log | tail -1" | sed -n 's/.* surface=\([0-9]*\) x=\([-0-9]*\) y=\([-0-9]*\).*/\1 \2 \3/p')
surface=${1:-0}; wx=${2:-0}; wy=${3:-0}
echo "files: surface $surface at $wx,$wy"
expect_log /tmp/f.log 'ZFILES LOCATION kind=folder path=/tmp/fhome/Projects/zedBSD items=4 error=0'

# 1. A second tab on docs, then the first tab again.
click 600 400
keys '<ctrl-t>'
expect_log /tmp/f.log 'ZFILES TABS new index=1 count=2'
click 330 110
keys '<ret>'
expect_log /tmp/f.log 'ZFILES LOCATION kind=folder path=/tmp/fhome/Projects/zedBSD/docs items=0 error=0'
click 400 8
expect_log /tmp/f.log 'ZFILES TABS select index=0 count=2'
shot tabs.png

# 2. README.md held on the second tab: it comes to the front; the release on its empty part moves README.md there.
drag 666 110 820 8 1500
expect_log /tmp/f.log 'ZFILES DRAG start items=1$'
expect_log /tmp/f.log 'ZFILES DRAG spring tab=1$'
expect_log /tmp/f.log 'ZFILES TABS select index=1 count=2'
check "$out/tab-spring.png" >/dev/null
pointer move $((wx + 680)) $((wy + 400)) sleep 300 move $((wx + 700)) $((wy + 420)) sleep 600 up sleep 1500
expect_log /tmp/f.log 'ZFILES DRAG drop operation=move items=1 destination=/tmp/fhome/Projects/zedBSD/docs$'
expect_guest '[ -f /tmp/fhome/Projects/zedBSD/docs/README.md ] && [ ! -e /tmp/fhome/Projects/zedBSD/README.md ]' 'README.md moved into docs'
shot moved.png

errors=$(guest "grep -c ERROR /tmp/zdesktop.log" | tail -1)
[ "${errors:-1}" = 0 ] && echo "zdesktop: no ERROR" || { echo "zdesktop: ERROR lines"; status=1; }
guest "$stop_all" >/dev/null
[ $status = 0 ] && echo "files-p006: PASS" || echo "files-p006: FAIL"
exit $status
