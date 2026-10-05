#!/bin/sh
# ws099-p033: a new window opens docked when the window in front is docked (2026-10-05 UAT).  On the Venus guest of
# plan/ws035/tests/config-amd64-zdesktop.mk built from the commit under test (plan/ws035/tests/zdesktop-guest.sh start
# IMAGE), zdesktop --glass at 1280x800 with wltest windows (420x300).
#  1. Window a floating; a double click on its title bar docks it ("GLASS dock surface=A via=double-click").
#  2. Window b started: it opens docked ("GLASS open-docked surface=B front=A"), its first configure is the docked
#     space (width=1280), and it maps without the floating placement; docked-b.png.
#  3. The bar's restore button brings b back (GLASS undock surface=B via=button) at seven tenths of the docked space
#     (its configure's width=896); floating-b.png.
#  4. Window c started while b (floating) is in front: it does not open docked (no "open-docked surface=C").
#  5. No ERROR in zdesktop's log.
# PASS: the last line "p033: status 0".
#   plan/ws099/tests/p033-guest.sh [OUTDIR]          (default build/ws099-p033)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
export GUEST_RUNTIME="${GUEST_RUNTIME:-$PWD/build/ws035-sq-run}"
out=${1:-build/ws099-p033}
mkdir -p "$out"
guest() { timeout 90 python3 plan/tools/guest/guest.py run "$1" 2>&1; }
check() { python3 plan/ws035/tests/zdesktop-check.py "$@" --runtime "$GUEST_RUNTIME"; }
pointer() { python3 plan/ws035/tests/qmp-pointer.py "$GUEST_RUNTIME/qmp.sock" "$@"; }
stop_all='service stop greeter >/dev/null 2>&1; for p in $(ps -A -o pid,args | grep -E "[w]ayland( |$)|[w]ltest" | awk "{print \$1}"); do kill $p; done; i=0; while ps -A -o args | grep -qE "[w]ayland( |$)|[w]ltest" && [ $i -lt 50 ]; do sleep 0.2; i=$((i+1)); done'
env='export XDG_RUNTIME_DIR=/tmp WAYLAND_DISPLAY=wayland-0;'
status=0

# Fails the run unless the compositor's log has a line matching a pattern.
expect_log() {
	if guest "grep -E '$1' /tmp/zdesktop.log" | grep -q .; then
		echo "log: $1 ok"
	else
		echo "log: $1 MISSING"
		status=1
	fi
}

# The surface, x and y of the latest window mapped.
last_map() {
	guest "grep 'ZWL MAP client=' /tmp/zdesktop.log | tail -1" | sed -n 's/.* surface=\([0-9]*\) x=\([-0-9]*\) y=\([-0-9]*\).*/\1 \2 \3/p'
}

guest "$stop_all" >/dev/null
guest 'export XDG_RUNTIME_DIR=/tmp; rm -f /tmp/wayland-0
/bin/wayland --testing --timeout=600 --width=1280 --height=800 --glass > /tmp/zdesktop.log 2>&1 </dev/null & for w in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do grep -q ZWL.READY /tmp/zdesktop.log 2>/dev/null && break; sleep 0.5; done; sleep 1; echo started' >/dev/null

# 1. a, floating, then docked by a double click on its title bar.
guest "$env /bin/wltest --windowed --size=420x300 --color=f4f7fc --frames=6000 --delay-ms=100 --token=a > /tmp/a.log 2>&1 </dev/null & sleep 4; echo started" >/dev/null
set -- $(last_map)
a=${1:-0}; ax=${2:-0}; ay=${3:-0}
echo "a: surface $a at $ax,$ay"
pointer move $((ax + 150)) $((ay - 30)) sleep 400 down sleep 60 up sleep 60 down sleep 60 up sleep 2000 >/dev/null
expect_log "GLASS dock surface=$a via=double-click"

# 2. b opens docked.
guest "$env /bin/wltest --windowed --size=420x300 --color=e8f0e0 --frames=6000 --delay-ms=100 --token=b > /tmp/b.log 2>&1 </dev/null & sleep 4; echo started" >/dev/null
set -- $(last_map)
b=${1:-0}
echo "b: surface $b"
expect_log "GLASS open-docked surface=$b front=$a x=0 "
expect_log "CONFIGURE client=[0-9]+ surface=$b serial=[0-9]+ width=1280 "
pointer move 1200 780 sleep 500 >/dev/null
check "$out/docked-b.png" >/dev/null

# 3. The bar's restore button brings b back at seven tenths of the docked space.
set -- $(guest "grep 'GLASS dock surface=$a via=double-click' /tmp/zdesktop.log | tail -1" | sed -n 's/.* buttons=\([0-9]*\),\([0-9]*\),\([0-9]*\) title=\([0-9]*\).*/\1 \2 \3 \4/p')
restore=${2:-0}
pointer move "$restore" 17 sleep 400 down sleep 60 up sleep 2000 >/dev/null
expect_log "GLASS undock surface=$b via=button"
expect_log "CONFIGURE client=[0-9]+ surface=$b serial=[0-9]+ width=896 "
pointer move 1200 780 sleep 500 >/dev/null
check "$out/floating-b.png" >/dev/null

# 4. c, with b floating in front, opens as it would.
guest "$env /bin/wltest --windowed --size=420x300 --color=f0e0e8 --frames=6000 --delay-ms=100 --token=c > /tmp/c.log 2>&1 </dev/null & sleep 4; echo started" >/dev/null
set -- $(last_map)
c=${1:-0}
echo "c: surface $c"
if guest "grep -c 'GLASS open-docked surface=$c ' /tmp/zdesktop.log" | tail -1 | grep -qx 0; then echo "c-floating: ok"; else echo "c-floating: FAILED"; status=1; fi
check "$out/floating-c.png" >/dev/null

# 5. No ERROR.
if guest 'grep -c ERROR /tmp/zdesktop.log' | tail -1 | grep -qx 0; then echo "no-error: ok"; else echo "no-error: FAILED"; status=1; fi
guest "grep -E 'open-docked|GLASS (dock|undock)' /tmp/zdesktop.log" | tail -10

echo "p033: status $status"
exit $status
