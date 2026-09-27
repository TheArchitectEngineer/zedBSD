#!/bin/sh
# ws074-p030: scripts in a page in browser's window on the Venus guest (build-browser-image.sh).
# zdesktop runs at 1280x800 with --glass and the wallpaper; the browser opens script.html at 900x640.
# Checks, from the browser's ZBROWSER lines and zdesktop's log:
#  1. start.png: the page's script ran while the page loaded (it rewrote a line and built a list; the
#     load listener set the title, which NAVIGATE shows).
#  2. ticks.png: setInterval ticks on the real clock (CONSOLE tick 3), and the page is drawn again.
#  3. clicked.png: a click on the "Click me" box reaches the click listener (CONSOLE clicked 1): the
#     box turns green, says "Clicked 1 time", and the list grows; a second click gives clicked 2.
#  4. Neither log has an ERROR line.
# The box's place comes from the host build's --dump=layout of script.html at the same width.
#
#   plan/ws074/tests/browser-guest.sh start      (the Venus guest must be up)
#   plan/ws074/tests/browser-p030.sh [OUTDIR]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
GUEST_RUNTIME="${GUEST_RUNTIME:-$(pwd)/build/ws074-run}"
export GUEST_RUNTIME
out=${1:-build/ws074-p030}
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

# Takes a picture with the pointer out of the way.
shot() {
	pointer move 1270 790 sleep 400
	check "$out/$1" >/dev/null
}

# The middle of the "Click me" box's text in the page at 900 wide, from the host's layout: "x y".
f=build/ws035-fonts
box=$(build/ws074-host/plain/browser --dump=layout --width=900 --height=640 --font=$f/Inter.ttf \
    --mono-font=$f/JetBrainsMono-Regular.ttf --fallback-font=$f/DroidSansFallbackFull.ttf plan/ws074/tests/pages/script.html 2>/dev/null |
    awk '$1 == "line" { y = $3; h = $5 } $1 == "text" && $NF == "\"Click\"" { printf "%d %d\n", $2 + $4 / 2, y + h / 2; exit }')
echo "box: at $box in the page"

guest "$stop_all" >/dev/null
guest 'export XDG_RUNTIME_DIR=/tmp; rm -f /tmp/wayland-0
picture=; [ -f /usr/share/zdesktop/wallpaper.ppm ] && picture=--wallpaper=/usr/share/zdesktop/wallpaper.ppm
/bin/wayland --timeout=900 --width=1280 --height=800 --glass $picture > /tmp/zdesktop.log 2>&1 </dev/null & sleep 4; echo started' >/dev/null
guest "export XDG_RUNTIME_DIR=/tmp; /bin/browser --width=900 --height=640 $pages/script.html > /tmp/b.log 2>&1 </dev/null & sleep 5; echo started" >/dev/null
set -- $(guest "grep 'ZWL MAP client=' /tmp/zdesktop.log | tail -1" | sed -n 's/.* x=\([-0-9]*\) y=\([-0-9]*\).*/\1 \2/p')
wx=${1:-0}; wy=${2:-0}
echo "browser: window at $wx,$wy"

# 1. The script ran while the page loaded, and its load listener set the title.
expect_log /tmp/b.log 'ZBROWSER READY width=900 height=640'
expect_log /tmp/b.log 'ZBROWSER CONSOLE level=0 loaded complete'
expect_log /tmp/b.log "ZBROWSER NAVIGATE path=$pages/script.html title=browser: scripts ran"
shot start.png

# 2. The interval ticks, and each tick draws a frame.
sleep 3
expect_log /tmp/b.log 'ZBROWSER CONSOLE level=0 tick 3'
frames=$(guest "grep -c 'ZBROWSER FRAME' /tmp/b.log" | tail -1)
if [ "${frames:-0}" -ge 4 ] 2>/dev/null; then echo "frames: $frames ok"; else echo "frames: $frames (too few)"; status=1; fi
shot ticks.png

# 3. Two clicks on the box.
set -- $box
pointer move $((wx + $1 - 2)) $((wy + $2)) sleep 150 move $((wx + $1)) $((wy + $2)) sleep 300 down sleep 60 up sleep 1200
expect_log /tmp/b.log 'ZBROWSER CONSOLE level=0 clicked 1'
shot clicked.png
pointer move $((wx + $1 - 2)) $((wy + $2)) sleep 150 move $((wx + $1)) $((wy + $2)) sleep 300 down sleep 60 up sleep 1200
expect_log /tmp/b.log 'ZBROWSER CONSOLE level=0 clicked 2'
shot clicked-twice.png

# 4. No error in either log, then everything ends.
errors=$(guest "grep -c 'ERROR' /tmp/zdesktop.log" | tail -1)
if [ "${errors:-1}" = 0 ]; then echo "zdesktop: no ERROR"; else echo "zdesktop: ERROR lines"; status=1; fi
browser_errors=$(guest "grep -c 'ZBROWSER ERROR' /tmp/b.log" | tail -1)
if [ "${browser_errors:-1}" = 0 ]; then echo "browser: no ERROR"; else echo "browser: ERROR lines"; status=1; fi
guest "$stop_all" >/dev/null
echo "browser-p030: status $status (pictures in $out)"
exit $status
