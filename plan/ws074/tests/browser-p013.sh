#!/bin/sh
# ws074-p013: positioned boxes in browser's window on the Venus guest (build-browser-image.sh).
# zdesktop runs at 1280x800 with --glass and the wallpaper; the browser opens position.html at 800x450.
# Checks, from the browser's ZBROWSER lines and zdesktop's log:
#  1. position.png: the absolute boxes in the stage's corners, the label, the boxes stacked by z-index and
#     the shifted paragraph (compare with Chromium's picture of the same page).
#  2. Clicks reach the box on top: where z-index 2 covers z-index 1 the click is on "over"; beside it on
#     "middle"; in the top left corner on "corner top-left" (the page logs the target's class).
#  3. Neither log has an ERROR line.
#
#   plan/ws074/tests/browser-guest.sh start      (the Venus guest must be up)
#   plan/ws074/tests/browser-p013.sh [OUTDIR]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
GUEST_RUNTIME="${GUEST_RUNTIME:-$(pwd)/build/ws074-run}"
export GUEST_RUNTIME
out=${1:-build/ws074-p013}
mkdir -p "$out"
guest() { timeout 120 python3 plan/tools/guest/guest.py run "$1" 2>&1; }
check() { python3 plan/ws035/tests/zdesktop-check.py "$@" --runtime "$GUEST_RUNTIME"; }
pointer() { python3 plan/ws035/tests/qmp-pointer.py "$GUEST_RUNTIME/qmp.sock" "$@"; }
pages=/usr/share/browser-tests
stop_all='for p in $(ps -A -o pid,args | grep -E "[z]desktop( |$)|[z]desktop-browser" | awk "{print \$1}"); do kill $p; done; i=0; while ps -A -o args | grep -qE "[z]desktop( |$)|[z]desktop-browser" && [ $i -lt 50 ]; do sleep 0.2; i=$((i+1)); done'
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

# Clicks a point of the page.
click() {
	pointer move $((wx + $1 - 2)) $((wy + $2)) sleep 150 move $((wx + $1)) $((wy + $2)) sleep 300 down sleep 60 up sleep 1000
}

guest "$stop_all" >/dev/null
guest 'export XDG_RUNTIME_DIR=/tmp; rm -f /tmp/wayland-0
picture=; [ -f /usr/share/zdesktop/wallpaper.ppm ] && picture=--wallpaper=/usr/share/zdesktop/wallpaper.ppm
/bin/wayland --timeout=900 --width=1280 --height=800 --glass $picture > /tmp/zdesktop.log 2>&1 </dev/null & sleep 4; echo started' >/dev/null
guest "export XDG_RUNTIME_DIR=/tmp; /bin/browser --width=800 --height=450 $pages/position.html > /tmp/b.log 2>&1 </dev/null & sleep 5; echo started" >/dev/null
set -- $(guest "grep 'ZWL MAP client=' /tmp/zdesktop.log | tail -1" | sed -n 's/.* x=\([-0-9]*\) y=\([-0-9]*\).*/\1 \2/p')
wx=${1:-0}; wy=${2:-0}
echo "browser: window at $wx,$wy"

# 1. The page.
expect_log /tmp/b.log 'ZBROWSER READY width=800 height=450'
pointer move 1270 790 sleep 400
check "$out/position.png" >/dev/null

# 2. The clicks.
click 400 220
expect_log /tmp/b.log 'ZBROWSER CONSOLE level=0 clicked over'
click 500 220
expect_log /tmp/b.log 'ZBROWSER CONSOLE level=0 clicked middle'
click 90 60
expect_log /tmp/b.log 'ZBROWSER CONSOLE level=0 clicked corner top-left'

# 3. No error in either log, then everything ends.
errors=$(guest "grep -c 'ERROR' /tmp/zdesktop.log" | tail -1)
if [ "${errors:-1}" = 0 ]; then echo "zdesktop: no ERROR"; else echo "zdesktop: ERROR lines"; status=1; fi
browser_errors=$(guest "grep -c 'ZBROWSER ERROR' /tmp/b.log" | tail -1)
if [ "${browser_errors:-1}" = 0 ]; then echo "browser: no ERROR"; else echo "browser: ERROR lines"; status=1; fi
guest "$stop_all" >/dev/null
echo "browser-p013: status $status (pictures in $out)"
exit $status
