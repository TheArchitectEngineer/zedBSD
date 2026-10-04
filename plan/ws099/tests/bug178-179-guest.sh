#!/bin/sh
# BUG-178 and BUG-179 on the pen test guest (plan/ws079/tests/config-amd64-pen.mk, the injector's touch pad of the
# Latitude 5330's size; plan/ws079/tests/pen-guest.sh start IMAGE), with the native touch pad layer (WS159).  The
# compositor under test (BUILD/bin/wayland) is copied in, 1280x800, with one wltest window drawing every 16 ms.
#  1. BUG-178: a tap on the window's title bar, then within the tap's drag time a touch at another place of the pad
#     (500 units right and 250 down of the tap) that moves 30 mm right: the window moves right ("GLASS moved" with a
#     larger x), as when the second touch lands where the tap did.
#  2. The same with the second touch where the tap was (the case that worked): the window moves right again.
#  3. BUG-179: a double click (QMP's tablet) on the title bar docks the window at the second press ("GLASS dock
#     ... via=double-click"; the outline reaches the docked size in the dock's 120 ms), and the compositor takes the
#     client's image of the docked size as soon as it is committed ("GLASS resized ... after_ms=A committed_ms=C",
#     A - C <= 20).  How long the client takes to draw again is reported, not judged here: in QEMU it is Venus's
#     swapchain and WSI (T1-141, T1-146); the hardware's is judged in the UAT.  docked.png for the eye.
#  4. The compositor stays up, with no ERROR in its log.
#   plan/ws099/tests/bug178-179-guest.sh BUILD [OUTDIR]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
export GUEST_RUNTIME="${GUEST_RUNTIME:-$PWD/build/ws079-run}"
build=${1:?usage: bug178-179-guest.sh BUILD [OUTDIR]}
out=${2:-build/ws099-bug178-179}
mkdir -p "$out"
qmp="$GUEST_RUNTIME/qmp.sock"
guest() { timeout 120 python3 plan/tools/guest/guest.py run "$1" 2>&1; }
put() { timeout 90 python3 plan/tools/guest/guest.py put "$1" "$2" >/dev/null 2>&1; }
pointer() { python3 plan/ws035/tests/qmp-pointer.py "$qmp" "$@" >/dev/null; }
# A picture for the eye, read from the Venus head through QEMU's VNC.
shot() { timeout 60 python3 plan/ws035/tests/zdesktop-check.py "$out/$1.png" --runtime "$GUEST_RUNTIME" >> "$out/shots.txt" 2>&1; }
count() { guest "grep -c -- '$1' /tmp/zdesktop.log" | tail -1; }
stop_all='for p in $(ps -A -o pid,args | grep -E "[w]ayland( |$)|[w]ltest|[t]ouchinject" | awk "{print \$1}"); do kill $p; done; i=0; while ps -A -o args | grep -qE "[w]ayland( |$)|[w]ltest" && [ $i -lt 50 ]; do sleep 0.2; i=$((i+1)); done'
env='export XDG_RUNTIME_DIR=/tmp WAYLAND_DISPLAY=wayland-0;'
status=0
pass() { echo "$1: ok"; }
fail() { echo "$1: FAILED"; status=1; }
# The x of the window's last move.
moved_x() { guest "grep 'GLASS moved surface=$1 ' /tmp/zdesktop.log | tail -1" | sed -n 's/.* x=\([-0-9]*\) .*/\1/p' | tail -1; }
# A pad script after the declaration and the 2.6 s wait (the compositor looks for new nodes every 2 s).
pad() { name=$1; shift; script="pad 1336 760 5 scan\nwait 2600"; for line in "$@"; do script="$script\n$line"; done
	guest "printf '$script\nhold 800\n' | /bin/touchinject; echo replay=\$?" > "$out/$name.txt"
	grep -q '^replay=0$' "$out/$name.txt" || fail "$name replay"; sleep 0.5; }
: > "$out/shots.txt"

# The compositor under test, and one window that draws often.
guest "$stop_all" >/dev/null
put "$build/bin/wayland" /bin/wayland
guest 'chmod 755 /bin/wayland; export XDG_RUNTIME_DIR=/tmp; rm -f /tmp/wayland-0
/bin/wayland --timeout=600 --width=1280 --height=800 --glass > /tmp/zdesktop.log 2>&1 </dev/null & for w in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do grep -q ZWL.READY /tmp/zdesktop.log 2>/dev/null && break; sleep 0.5; done; sleep 2; echo started' >/dev/null
guest "$env /bin/wltest --windowed --size=420x300 --color=f4f7fc --frames=3600 --delay-ms=16 > /tmp/w.log 2>&1 </dev/null & sleep 4; echo started" >/dev/null
set -- $(guest "grep 'ZWL MAP client=' /tmp/zdesktop.log | tail -1" | sed -n 's/.* surface=\([0-9]*\) x=\([-0-9]*\) y=\([-0-9]*\).*/\1 \2 \3/p')
surface=${1:-0}; wx=${2:-0}; wy=${3:-0}
echo "window: surface=$surface at $wx,$wy"

# 1. The tap on the title bar (the mouse puts the pointer there), then a touch elsewhere on the pad that moves right.
pointer move $((wx + 150)) $((wy - 30)) sleep 300
before=$wx
pad displaced "down 0 300 300" "wait 40" "up 0" "wait 100" "down 0 800 550" "wait 30" "swipe 360 0 12 16" "up 0"
after=$(moved_x "$surface")
echo "displaced tap-drag: x $before -> ${after:-?}"
if [ -n "$after" ] && [ "$after" -gt $((before + 50)) ]; then pass bug178-displaced-moves; else fail bug178-displaced-moves; fi

# 2. The second touch where the tap was: back left this time (the window may stop at the right).
before=${after:-$wx}
pad same-place "down 0 900 300" "wait 40" "up 0" "wait 100" "down 0 900 300" "wait 30" "swipe -360 0 12 16" "up 0"
after=$(moved_x "$surface")
echo "same-place tap-drag: x $before -> ${after:-?}"
if [ -n "$after" ] && [ "$after" -lt $((before - 50)) ]; then pass bug178-same-place-moves; else fail bug178-same-place-moves; fi

# 3. A double click on the title bar where the window is now.
set -- $(guest "grep 'GLASS moved surface=$surface ' /tmp/zdesktop.log | tail -1" | sed -n 's/.* x=\([-0-9]*\) y=\([-0-9]*\).*/\1 \2/p')
tx=${1:-$wx}; ty=${2:-$wy}
docks=$(count "GLASS dock surface=$surface via=double-click")
pointer move $((tx + 150)) $((ty - 30)) sleep 400 down sleep 60 up sleep 60 down sleep 60 up sleep 1500
n=$(count "GLASS dock surface=$surface via=double-click")
[ "${n:-0}" -gt "${docks:-0}" ] 2>/dev/null && pass bug179-docks || fail bug179-docks
resized=$(guest "grep 'GLASS resized surface=$surface docked=1 ' /tmp/zdesktop.log | tail -1" | sed -n 's/.*after_ms=\([0-9]*\).*/\1/p')
echo "the client drew the docked size ${resized:-?} ms after the dock"
# The wait split (T1-138: 1.3 s): the configure's acknowledgment, the client's new swapchain, and the rest (its first
# frame of the new size and the compositor's import), from both logs (one monotonic clock).
guest "grep 'GLASS resized surface=$surface docked=1 ' /tmp/zdesktop.log | tail -1; grep 'WLTEST RESIZE' /tmp/w.log | tail -1; grep -A2 'WLTEST RESIZE' /tmp/w.log | grep 'WLTEST FRAME' | head -2" > "$out/bug179-split.txt"
acked=$(sed -n 's/.*acked_ms=\([0-9]*\).*/\1/p' "$out/bug179-split.txt" | head -1)
sent=$(sed -n 's/.*sent_at_ms=\([0-9]*\).*/\1/p' "$out/bug179-split.txt" | head -1)
took=$(sed -n 's/.*took_ms=\([0-9]*\).*/\1/p' "$out/bug179-split.txt" | head -1)
began=$(sed -n 's/.*RESIZE.* at_ms=\([0-9]*\).*/\1/p' "$out/bug179-split.txt" | head -1)
committed=$(sed -n 's/.*committed_ms=\([0-9]*\).*/\1/p' "$out/bug179-split.txt" | head -1)
first=$(sed -n 's/^WLTEST FRAME.* at_ms=\([0-9]*\).*/\1/p' "$out/bug179-split.txt" | head -1)
# Judged in QEMU: the compositor takes the client's image as soon as it is committed (T1-141, T1-146: the rest of the
# wait is Venus's swapchain and WSI, 0.8 s and 0.3 s; how fast the content is drawn again is judged on the hardware).
if [ -n "$resized" ] && [ -n "$committed" ] && [ $((resized - committed)) -le 20 ]; then pass bug179-compositor-takes-at-once; else fail bug179-compositor-takes-at-once; fi
echo "split: acknowledged after ${acked:-?} ms; the client began its swapchain $(( ${began:-0} - ${sent:-0} )) ms after the configure and took ${took:-?} ms; its first frame of the new size was presented at $(( ${first:-0} - ${sent:-0} )) ms, its last commit came at ${committed:-?} ms, the compositor took the image at ${resized:-?} ms (after its acquire fence)"
shot docked

# 4. Up, without errors.
running=$(guest 'ps -A -o args | grep -cE "[w]ayland( |$)"' | tail -1)
[ "$running" = "1" ] && pass alive || fail alive
guest 'grep -E "ZWL INPUT|GLASS (moved|dock|undock|resized)|ERROR" /tmp/zdesktop.log' > "$out/log.txt"
grep -q ERROR "$out/log.txt" && fail no-error || pass no-error
guest "$stop_all" >/dev/null

echo "bug178-179-guest: status $status (outputs in $out; docked.png for the eye)"
exit $status
