#!/bin/sh
# ws159-p004: the compositor's touch pad layer on the pen test guest
# (plan/ws079/tests/config-amd64-pen.mk built from this branch: the
# injector's test touch pad and touchinject's pad, ws159-p003).  The
# compositor under test (BUILD/bin/wayland) is copied into the running
# guest; the output is 1280x800.  The pad is declared with the Latitude
# 5330's size (1336x760 units; the injector gives a pad 12 units a
# millimetre, INPUT_INJECT_PAD_RESOLUTION).  Each script waits 2.6 s
# first: the compositor looks for new evdev nodes every 2 s.
#
#  1. The compositor takes the injector's touch pad as a touch pad (not as
#     a touch screen): "KWL INPUT device=... kind=touchpad abs=0
#     resolution=12,12".
#  2. A tap and then a drag (a touch 100 ms after the tap that moves 30 mm
#     right) on a wltest window's title bar, where the mouse (QMP) first put
#     the pointer: the window moves right (GLASS moved with a larger x),
#     the BUG-166 tap-drag.
#  3. The pad pressed and moved 30 mm left on the same title bar: the
#     window moves left (the click pad's drag; left, as the first drag may
#     have taken the window to where it stops at the right).
#  4. The compositor stays up, with no ERROR in its log.
#
#   plan/ws079/tests/pen-guest.sh start IMAGE
#   plan/ws159/tests/p004-guest.sh BUILD [OUTDIR]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
export GUEST_RUNTIME="${GUEST_RUNTIME:-$PWD/build/ws079-run}"
build=${1:?usage: p004-guest.sh BUILD [OUTDIR]}
out=${2:-build/ws159-p004}
mkdir -p "$out"
guest() { timeout 120 python3 plan/tools/guest/guest.py run "$1" 2>&1; }
put() { timeout 90 python3 plan/tools/guest/guest.py put "$1" "$2" >/dev/null 2>&1; }
pointer() { python3 plan/ws035/tests/qmp-pointer.py "$GUEST_RUNTIME/qmp.sock" "$@"; }
stop_all='for p in $(ps -A -o pid,args | grep -E "[w]ayland( |$)|[w]ltest|[t]ouchinject" | awk "{print \$1}"); do kill $p; done; i=0; while ps -A -o args | grep -qE "[w]ayland( |$)|[w]ltest" && [ $i -lt 50 ]; do sleep 0.2; i=$((i+1)); done'
env='export XDG_RUNTIME_DIR=/tmp WAYLAND_DISPLAY=wayland-0;'
status=0

# The x of the last move of a surface in the compositor's log.
moved_x() {
	guest "grep 'GLASS moved surface=$1 ' /tmp/zdesktop.log | tail -1" | sed -n 's/.* x=\([-0-9]*\) .*/\1/p' | tail -1
}

# The compositor under test, and one wltest window.
guest "$stop_all" >/dev/null
put "$build/bin/wayland" /bin/wayland
guest 'chmod 755 /bin/wayland; export XDG_RUNTIME_DIR=/tmp; rm -f /tmp/wayland-0
/bin/wayland --testing --timeout=600 --width=1280 --height=800 --glass > /tmp/zdesktop.log 2>&1 </dev/null & sleep 5; echo started' >/dev/null
guest "$env /bin/wltest --windowed --size=420x300 --color=f4f7fc --frames=3600 --delay-ms=250 > /tmp/w.log 2>&1 </dev/null & sleep 4; echo started" >/dev/null
set -- $(guest "grep 'KWL MAP client=' /tmp/zdesktop.log | tail -1" | sed -n 's/.* client=\([0-9]*\) surface=\([0-9]*\) x=\([-0-9]*\) y=\([-0-9]*\).*/\2 \3 \4/p')
surface=${1:-0}; wx=${2:-0}; wy=${3:-0}
echo "window: surface=$surface at $wx,$wy"

# 1 and 2. The pad declared; the mouse puts the pointer on the title bar; a tap, then a drag 30 mm right.
pointer move $((wx + 150)) $((wy - 30)) sleep 300 >/dev/null
before=$wx
guest 'printf "pad 1336 760 5 scan\nwait 2600\ndown 0 400 300\nwait 40\nup 0\nwait 100\ndown 0 400 300\nwait 30\nswipe 360 0 12 16\nup 0\nhold 800\n" | /bin/touchinject; echo replay=$?' > "$out/tapdrag.txt"
grep -q '^replay=0$' "$out/tapdrag.txt" || { echo "tap-drag replay: FAILED"; status=1; }
if guest "grep -E 'KWL INPUT device=/dev/input/event[0-9]+ kind=touchpad abs=0 resolution=12,12' /tmp/zdesktop.log" | grep -q kind=touchpad; then
	echo "touch pad taken as a touch pad: ok"
else
	echo "touch pad taken as a touch pad: FAILED"
	status=1
fi
after=$(moved_x "$surface")
echo "tap-drag: x $before -> ${after:-?}"
if [ -n "$after" ] && [ "$after" -gt $((before + 50)) ]; then
	echo "tap-drag moves the window: ok"
else
	echo "tap-drag moves the window: FAILED"
	status=1
fi

# 3. The pad pressed and moved 30 mm left on the title bar (the pointer is still on it after the drag).
before=${after:-$wx}
guest 'printf "pad 1336 760 5 scan\nwait 2600\ndown 0 800 600\nwait 30\npress; move 0 799 600\nwait 30\nswipe -360 0 12 16\nrelease; move 0 439 600\nwait 30\nup 0\nhold 800\n" | /bin/touchinject; echo replay=$?' > "$out/pressdrag.txt"
grep -q '^replay=0$' "$out/pressdrag.txt" || { echo "press-drag replay: FAILED"; status=1; }
after=$(moved_x "$surface")
echo "press-drag: x $before -> ${after:-?}"
if [ -n "$after" ] && [ "$after" -lt $((before - 50)) ]; then
	echo "press-drag moves the window: ok"
else
	echo "press-drag moves the window: FAILED"
	status=1
fi

# 4. The compositor is up, without errors.
running=$(guest 'ps -A -o args | grep -cE "[w]ayland( |$)"' | tail -1)
guest 'grep -E "KWL INPUT|GLASS moved|ERROR" /tmp/zdesktop.log' > "$out/log.txt"
if [ "$running" = "1" ] && ! grep -q ERROR "$out/log.txt"; then
	echo "compositor up, no ERROR: ok"
else
	echo "compositor up, no ERROR: FAILED"
	status=1
fi
guest "$stop_all" >/dev/null

[ $status -eq 0 ] && echo "ws159-p004: PASS" || echo "ws159-p004: FAIL"
exit $status
