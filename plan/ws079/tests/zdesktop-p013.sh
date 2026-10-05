#!/bin/sh
# ws079-p013 (mouse part): a triple click on a floating title bar sends the
# window to the back and brings the next one forward; a double click docks at
# its second press (BUG-179), and a third press takes that dock back before
# the window goes to the back.  Checked on the Venus guest (the pattern of
# plan/ws035/tests/zdesktop-p062.sh).
#
# The compositor under test is copied into the running guest.  Three wltest
# windows a (420x300, f4f7fc), b (500x360, c8d8ec) and c (460x320, e8c8b0)
# are dragged, each while it is the newest and on top, to a staircase:
#   a body (100,150)  b body (300,250)  c body (560,350)
# so every title bar shows while a < b < c.  P_BC (700,450) is inside b and c
# only, P_AB (450,350) inside a and b only.  A window is named client:surface
# (surface numbers are per client; the move and dock lines carry only the
# surface, so the pixels decide which window is where).  The output is 1280x800; the
# pointer is driven through QMP (qmp-pointer.py).
#
#  0. The three drags land where the pointer took them (a drag is unchanged).
#  1. A triple click on c's title bar: c goes to the back, b comes forward and
#     has the keyboard; P_BC shows b; the dock of the second press is taken
#     back (undock via=triple-click) and c floats where it was.
#  2. A triple click on b's title bar: b goes to the back, a comes forward;
#     P_AB shows a and P_BC shows c (c is above b now).
#  3. A single click on c's title bar raises it and does nothing else.
#  4. A double click on c's title bar docks it at the second press (the
#     picture right after it already shows c in the docked space, BUG-179);
#     a double click on the title in the bar brings it back.
#  5. A drag of c's title bar moves it by the pointer's way (unchanged).
#
#   GUEST_RUNTIME=... plan/ws079/tests/pen-guest.sh start IMAGE
#   GUEST_RUNTIME=... plan/ws079/tests/zdesktop-p013.sh BUILD [OUTDIR [PREFIX]]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
export GUEST_RUNTIME="${GUEST_RUNTIME:-$PWD/build/ws079-run}"
build=${1:?usage: zdesktop-p013.sh BUILD [OUTDIR [PREFIX]]}
out=${2:-build/ws079-p013-shots}
prefix=${3:-ws079-p013-20260928-}
mkdir -p "$out"
guest() { timeout 90 python3 plan/tools/guest/guest.py run "$1" 2>&1; }
put() { timeout 90 python3 plan/tools/guest/guest.py put "$1" "$2" >/dev/null 2>&1; }
shot() { name=$1; shift; python3 plan/ws035/tests/zdesktop-check.py "$out/$prefix$name.png" --runtime "$GUEST_RUNTIME" "$@"; }
pointer() { python3 plan/ws035/tests/qmp-pointer.py "$GUEST_RUNTIME/qmp.sock" "$@"; }
stop_all='for p in $(ps -A -o pid,args | grep -E "[w]ayland( |$)|[w]ltest|[m]view" | awk "{print \$1}"); do kill $p; done; i=0; while ps -A -o args | grep -qE "[w]ayland( |$)|[w]ltest|[m]view" && [ $i -lt 50 ]; do sleep 0.2; i=$((i+1)); done'
env='export XDG_RUNTIME_DIR=/tmp WAYLAND_DISPLAY=wayland-0;'
status=0
A=f4f7fc
B=c8d8ec
C=e8c8b0

# Counts the compositor's log lines matching a pattern.
count() {
	guest "grep -cE '$1' /tmp/zdesktop.log" | tail -1
}

# Fails the run unless a count is what it should be.
expect_count() {
	if [ "$2" = "$3" ]; then
		echo "count: $1 = $2 ok"
	else
		echo "count: $1 = $2, expected $3 FAILED"
		status=1
	fi
}

# Fails the run unless the compositor's log has a line matching a pattern.
expect_log() {
	if guest "grep -E '$1' /tmp/zdesktop.log" | grep -q .; then
		echo "log: $1 ok"
	else
		echo "log: $1 MISSING"
		status=1
	fi
}

# Starts a wltest window and drags it by its title bar to a body place; prints it as client:surface.
open_at() {
	token=$1; size=$2; color=$3; x=$4; y=$5
	guest "$env /bin/wltest --windowed --size=$size --color=$color --frames=3600 --delay-ms=250 --token=$token > /tmp/$token.log 2>&1 </dev/null & sleep 4; echo started" >/dev/null
	set -- $(guest "grep 'ZWL MAP client=' /tmp/zdesktop.log | tail -1" | sed -n 's/.* client=\([0-9]*\) surface=\([0-9]*\) x=\([-0-9]*\) y=\([-0-9]*\).*/\1:\2 \3 \4/p')
	s=${1:-0:0}; sx=${2:-0}; sy=${3:-0}
	pointer move $((sx + 150)) $((sy - 30)) sleep 300 down sleep 80 \
	    move $(((sx + x) / 2 + 150)) $(((sy + y) / 2 - 30)) sleep 80 \
	    move $((x + 150)) $((y - 30)) sleep 150 up sleep 600 >/dev/null
	echo "$s"
}

# Presses the left button n times at a point, 60 ms apart (a double or triple click).
clicks() {
	x=$1; y=$2; n=$3
	steps="move $x $y sleep 400 down sleep 60 up"
	i=1
	while [ $i -lt $n ]; do
		steps="$steps sleep 60 down sleep 60 up"
		i=$((i + 1))
	done
	pointer $steps
}

# The compositor under test.
guest "$stop_all" >/dev/null
put "$build/bin/wayland" /bin/wayland
guest 'chmod 755 /bin/wayland; rm -f /tmp/a.log /tmp/b.log /tmp/c.log' >/dev/null
guest 'export XDG_RUNTIME_DIR=/tmp; rm -f /tmp/wayland-0; picture=; [ -f /usr/share/keiland/wallpaper.png ] && picture=--wallpaper=/usr/share/keiland/wallpaper.png
/bin/wayland --timeout=900 --width=1280 --height=800 --glass $picture > /tmp/zdesktop.log 2>&1 </dev/null & sleep 5; echo started' >/dev/null

# 0. The staircase, each window dragged while it is on top.
a=$(open_at a 420x300 $A 100 150)
b=$(open_at b 500x360 $B 300 250)
c=$(open_at c 460x320 $C 560 350)
echo "surfaces: a=$a b=$b c=$c"
pointer move 1200 780 sleep 500 >/dev/null
expect_log "GLASS moved surface=${a#*:} x=100 y=150"
expect_log "GLASS moved surface=${b#*:} x=300 y=250"
expect_log "GLASS moved surface=${c#*:} x=560 y=350"
expect_count "docks or lowers while dragging" "$(count 'GLASS (dock|lower) ')" 0
shot stack --expect 700,450,$C --expect 450,350,$B --expect 200,200,$A || status=1

# 1. A triple click on c's title bar: c to the back, b forward with the keyboard.
clicks 710 320 3
sleep 1.5
expect_log "GLASS lower client=${c%:*} surface=${c#*:} via=triple-click next=$b focus=$b"
expect_log "GLASS undock surface=${c#*:} via=triple-click x=560 y=350"
pointer move 1200 780 sleep 500 >/dev/null
shot c-lowered --expect 700,450,$B --expect 450,350,$B --expect 900,640,$C || status=1

# 2. A triple click on b's title bar: b to the back, a forward.
clicks 450 220 3
sleep 1.5
expect_log "GLASS lower client=${b%:*} surface=${b#*:} via=triple-click next=$a focus=$a"
expect_log "GLASS undock surface=${b#*:} via=triple-click x=300 y=250"
pointer move 1200 780 sleep 500 >/dev/null
shot b-lowered --expect 450,350,$A --expect 700,450,$C || status=1

# 3. A single click on c's title bar raises it, and nothing else.
lowers=$(count 'GLASS lower ')
docks=$(count 'GLASS dock surface=')
clicks 710 320 1
sleep 1.5
pointer move 1200 780 sleep 500 >/dev/null
expect_count "lowers after a single click" "$(count 'GLASS lower ')" "$lowers"
expect_count "docks after a single click" "$(count 'GLASS dock surface=')" "$docks"
shot c-raised --expect 700,450,$C --expect 450,350,$A || status=1

# 4. A double click on c's title bar docks it at once, and no third press takes it back.
cdocks=$(count "GLASS dock surface=${c#*:} via=double-click")
undocks=$(count "GLASS undock surface=${c#*:} ")
clicks 710 320 2
shot c-docking --expect 500,700,$C || status=1
sleep 2.5
expect_count "docks of c by the double click" "$(count "GLASS dock surface=${c#*:} via=double-click")" $((cdocks + 1))
expect_count "undocks of c after the double click" "$(count "GLASS undock surface=${c#*:} ")" "$undocks"
resized=$(guest "grep 'GLASS resized surface=${c#*:} docked=1 ' /tmp/zdesktop.log | tail -1" | sed -n 's/.*after_ms=\([0-9]*\).*/\1/p')
echo "the client drew the docked size ${resized:-?} ms after the dock (informational: wltest draws every 250 ms)"
expect_count "lowers from the double click" "$(count 'GLASS lower ')" "$lowers"
pointer move 1200 780 sleep 500 >/dev/null
shot c-docked --expect 20,780,$C --expect 1260,60,$C || status=1
set -- $(guest "grep 'GLASS dock surface=${c#*:} via=double-click' /tmp/zdesktop.log | tail -1" | sed -n 's/.* title=\([0-9]*\).*/\1/p')
title_x=${1:-0}
clicks $((title_x + 60)) 17 2
sleep 2
expect_log "GLASS undock surface=${c#*:} via=double-click x=560 y=350"

# 5. A drag of c's title bar moves it as the pointer went.
pointer move 710 320 sleep 600 down sleep 80 move 680 340 sleep 80 move 650 360 sleep 150 up sleep 800 >/dev/null
expect_log "GLASS moved surface=${c#*:} x=500 y=390"
expect_count "lowers after the drag" "$(count 'GLASS lower ')" "$lowers"
pointer move 1200 780 sleep 500 >/dev/null
shot c-moved --expect 520,660,$C || status=1

# Nothing failed.
guest 'grep -E "ERROR|FAILED" /tmp/zdesktop.log /tmp/a.log /tmp/b.log /tmp/c.log' | tee "$out/${prefix}errors.txt"
[ -s "$out/${prefix}errors.txt" ] && status=1
guest 'grep -E "GLASS (dock|undock|moved|lower|resized)|CONFIGURE" /tmp/zdesktop.log' > "$out/${prefix}log.txt"
guest "$stop_all" >/dev/null
[ $status -eq 0 ] && echo "p013: PASS" || echo "p013: FAIL"
exit $status
