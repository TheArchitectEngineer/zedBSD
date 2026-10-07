#!/bin/sh
# ws079-p013 (touch part): the compositor's touch screen on the pen test guest
# (plan/ws079/tests/config-amd64-pen.mk: the injector's test touch screen and
# touchinject, ws079-p012).  The compositor, libwayland-client (wl_touch) and
# tablet-probe (--touch) under test are copied into the running guest.  The
# output is 1280x800 and every touch script declares the screen 0..1279 by
# 0..799, so a finger's numbers are the output's pixels.  Each script waits
# 2.6 s first: the compositor looks for new evdev nodes every 2 s.
#
#  1. wl_touch: a client with wl_touch (tablet-probe --touch) hears two
#     fingers (down, motion, up, frame, with its own surface and local places),
#     and the seat offers touch while the touch screen is there.  A client
#     without wl_touch (tablet-probe --pointer) hears the first finger as the
#     pointer's left button (press, motion, release).
#  2. Cancel: a finger on the client, then a second one swiped down from the
#     top edge's band (WS181): Wiseview opens and the client hears cancel, and
#     nothing more of the first finger.  A tap closes Wiseview.
#  3. The edges by touch: the top-left corner opens App Home; on Home the
#     bottom edge's swipe does nothing and a drag down closes it (WS181); the
#     top-right corner brings Notes (the stand-in,
#     plan/ws079/tests/notes-standin.sh) with the source "touch"; on the
#     desktop the bottom edge opens App Home and the top band's swipe down
#     opens Wiseview (WS181).
#  4. The title bars, with three wltest windows a < b < c on a staircase (as
#     zdesktop-p013.sh places them with the mouse):
#     a. two fingers flicked up quickly on c's title bar: c goes to the back,
#        b comes forward with the keyboard (the same as a triple click);
#     b. two fingers dragged up slowly on b's title bar: nothing happens;
#     c. two fingers flicked down, and sideways, on b's: nothing happens;
#     d. one finger dragged on b's title bar moves b as far as the finger went;
#     e. one finger tapped on c's title bar brings c forward.
#
#   GUEST_RUNTIME=... plan/ws079/tests/pen-guest.sh start IMAGE
#   GUEST_RUNTIME=... plan/ws079/tests/zdesktop-p013-touch.sh BUILD [OUTDIR [PREFIX]]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
export GUEST_RUNTIME="${GUEST_RUNTIME:-$PWD/build/ws079-run}"
build=${1:?usage: zdesktop-p013-touch.sh BUILD [OUTDIR [PREFIX]]}
out=${2:-build/ws079-p013-touch}
prefix=${3:-ws079-p013-20260928-touch-}
mkdir -p "$out"
guest() { timeout 120 python3 plan/tools/guest/guest.py run "$1" 2>&1; }
put() { timeout 90 python3 plan/tools/guest/guest.py put "$1" "$2" >/dev/null 2>&1; }
shot() { name=$1; shift; python3 plan/ws035/tests/zdesktop-check.py "$out/$prefix$name.png" --runtime "$GUEST_RUNTIME" "$@"; }
pointer() { python3 plan/ws035/tests/qmp-pointer.py "$GUEST_RUNTIME/qmp.sock" "$@"; }
stop_all='for p in $(ps -A -o pid,args | grep -E "[w]ayland( |$)|[w]ltest|[t]ablet-probe|[t]ouchinject" | awk "{print \$1}"); do kill $p; done; for p in $(cat /tmp/notes.pids 2>/dev/null); do kill $p 2>/dev/null; done; rm -f /tmp/notes.pids; i=0; while ps -A -o args | grep -qE "[w]ayland( |$)|[w]ltest|[t]ablet-probe" && [ $i -lt 50 ]; do sleep 0.2; i=$((i+1)); done'
env='export XDG_RUNTIME_DIR=/tmp WAYLAND_DISPLAY=wayland-0;'
status=0
A=f4f7fc
B=c8d8ec
C=e8c8b0
PROBE=d8f0d8
DOT=1e8a3a

# Counts the lines of a guest log matching a pattern (the compositor's by default).
count() {
	guest "grep -cE '$1' ${2:-/tmp/zdesktop.log}" | tail -1
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

# Fails the run unless a guest log (the compositor's by default) has a line matching a pattern.
expect_log() {
	if guest "grep -E '$1' ${2:-/tmp/zdesktop.log}" | grep -q .; then
		echo "log: $1 ok"
	else
		echo "log: $1 MISSING"
		status=1
	fi
}

# Replays a touch script (its frames separated by '|') on the output's pixels.
touches() {
	printf 'size 1279 799 2\nwait 2600\n%s\nhold 300\n' "$1" | tr '|' '\n' > "$out/touch.script"
	put "$out/touch.script" /tmp/touch.script
	guest '/bin/touchinject /tmp/touch.script; echo replay=$?' | grep -q '^replay=0$' || { echo "touchinject: FAILED"; status=1; }
	sleep 0.8
}

# Starts a wltest window and drags it with the mouse by its title bar to a body place; prints it as client:surface.
open_at() {
	token=$1; size=$2; color=$3; x=$4; y=$5
	guest "$env /bin/wltest --windowed --size=$size --color=$color --frames=3600 --delay-ms=250 --token=$token > /tmp/$token.log 2>&1 </dev/null & sleep 4; echo started" >/dev/null
	set -- $(guest "grep 'KWL MAP client=' /tmp/zdesktop.log | tail -1" | sed -n 's/.* client=\([0-9]*\) surface=\([0-9]*\) x=\([-0-9]*\) y=\([-0-9]*\).*/\1:\2 \3 \4/p')
	s=${1:-0:0}; sx=${2:-0}; sy=${3:-0}
	pointer move $((sx + 150)) $((sy - 30)) sleep 300 down sleep 80 \
	    move $(((sx + x) / 2 + 150)) $(((sy + y) / 2 - 30)) sleep 80 \
	    move $((x + 150)) $((y - 30)) sleep 150 up sleep 600 >/dev/null
	echo "$s"
}

# The compositor and the clients under test, and the stand-in of Notes.
guest "$stop_all" >/dev/null
put "$build/bin/wayland" /bin/wayland
put "$build/dynamic/libwayland-client.so" /lib/libwayland-client.so
put "$build/bin/tablet-probe" /bin/tablet-probe
put plan/ws079/tests/notes-standin.sh /bin/notes
guest 'chmod 755 /bin/wayland /bin/tablet-probe /bin/notes; rm -f /tmp/notes-args /tmp/notes.log /tmp/notes.pids /tmp/a.log /tmp/b.log /tmp/c.log /tmp/touchprobe.log' >/dev/null
guest 'export XDG_RUNTIME_DIR=/tmp; rm -f /tmp/wayland-0; picture=; [ -f /usr/share/keiland/wallpaper.png ] && picture=--wallpaper=/usr/share/keiland/wallpaper.png
/bin/wayland --testing --timeout=900 --width=1280 --height=800 --glass $picture > /tmp/zdesktop.log 2>&1 </dev/null & sleep 5; echo started' >/dev/null

# 1. wl_touch: two fingers on the probe's window.
guest "$env /bin/tablet-probe --touch --color=$PROBE --token=touch --timeout-s=600 > /tmp/touchprobe.log 2>&1 </dev/null & sleep 4; echo started" >/dev/null
expect_log 'TABLETPROBE ready run=touch mode=touch' /tmp/touchprobe.log
set -- $(guest "grep 'KWL MAP client=' /tmp/zdesktop.log | tail -1" | sed -n 's/.* client=\([0-9]*\) surface=\([0-9]*\) x=\([-0-9]*\) y=\([-0-9]*\).*/\1:\2 \3 \4/p')
probe=${1:-0:0}; px=${2:-0}; py=${3:-0}
echo "probe: $probe at $px,$py"
pointer move 1200 780 sleep 300 >/dev/null
touches "down 1 $((px + 100)) $((py + 100))|swipe 40 30 4 40|down 2 $((px + 400)) $((py + 300))|move 1 $((px + 160)) $((py + 150)); move 2 $((px + 380)) $((py + 280))|up 1|up 2"
expect_log "INPUT device=/dev/input/event[0-9]+ kind=touch abs=1"
expect_log "TOUCH down client=${probe%:*} surface=${probe#*:} contact=0 x=$((px + 100)) y=$((py + 100))"
expect_log "TOUCH down client=${probe%:*} surface=${probe#*:} contact=1 x=$((px + 400)) y=$((py + 300))"
expect_log 'TABLETPROBE seat capabilities=7' /tmp/touchprobe.log
expect_log 'TABLETPROBE touch down id=0 surface=1 x=100.00 y=100.00' /tmp/touchprobe.log
expect_log 'TABLETPROBE touch motion id=0 x=140.00 y=130.00' /tmp/touchprobe.log
expect_log 'TABLETPROBE touch down id=1 surface=1 x=400.00 y=300.00' /tmp/touchprobe.log
expect_log 'TABLETPROBE touch motion id=1 x=380.00 y=280.00' /tmp/touchprobe.log
expect_log 'TABLETPROBE touch up id=0' /tmp/touchprobe.log
expect_log 'TABLETPROBE touch up id=1' /tmp/touchprobe.log
expect_count "probe frames" "$(count 'TABLETPROBE touch frame' /tmp/touchprobe.log)" 9
expect_count "probe cancels" "$(count 'TABLETPROBE touch cancel' /tmp/touchprobe.log)" 0
guest 'grep "TABLETPROBE" /tmp/touchprobe.log' > "$out/${prefix}probe-log.txt"
shot probe --expect $((px + 20)),$((py + 20)),$PROBE --expect $((px + 100)),$((py + 100)),$DOT --expect $((px + 380)),$((py + 280)),$DOT || status=1

# 1b. A client without wl_touch (tablet-probe --pointer) hears the first finger as the pointer's left button.
guest "$env /bin/tablet-probe --pointer --color=f0e0d0 --token=pointer --timeout-s=600 > /tmp/pointerprobe.log 2>&1 </dev/null & echo \$! > /tmp/pointerprobe.pid; sleep 4; echo started" >/dev/null
set -- $(guest "grep 'KWL MAP client=' /tmp/zdesktop.log | tail -1" | sed -n 's/.* client=\([0-9]*\) surface=\([0-9]*\) x=\([-0-9]*\) y=\([-0-9]*\).*/\1:\2 \3 \4/p')
fallback=${1:-0:0}; fx=${2:-0}; fy=${3:-0}
echo "pointer probe: $fallback at $fx,$fy"
touches "down 1 $((fx + 100)) $((fy + 100))|swipe 60 20 3 40|up 1"
expect_log "TOUCH pointer client=${fallback%:*} surface=${fallback#*:} contact=0 x=$((fx + 100)) y=$((fy + 100))"
expect_log 'TABLETPROBE pointer button=0x110 state=1' /tmp/pointerprobe.log
expect_log "TABLETPROBE pointer motion x=160 y=120" /tmp/pointerprobe.log
expect_log 'TABLETPROBE pointer button=0x110 state=0' /tmp/pointerprobe.log
expect_count "wl_touch downs of the pointer probe" "$(count "TOUCH down client=${fallback%:*} ")" 0
guest 'grep "TABLETPROBE" /tmp/pointerprobe.log' > "$out/${prefix}pointer-log.txt"
shot pointer --expect $((fx + 20)),$((fy + 20)),f0e0d0 --expect $((fx + 97)),$((fy + 100)),202020 || status=1
guest 'kill $(cat /tmp/pointerprobe.pid); sleep 1.5' >/dev/null

# 2. Cancel: a finger on the probe, then a swipe down from the top edge's band opens Wiseview (WS181).
touches "down 1 $((px + 200)) $((py + 200))|hold 200|down 2 640 4|swipe 0 200 8 30|hold 400|up 2|move 1 $((px + 220)) $((py + 220))|up 1"
expect_log "TOUCH shell contact=1 x=640 y=4"
expect_log 'WISEVIEW gesture via=top-edge'
expect_log "TOUCH cancel client=${probe%:*} reason=shell"
expect_log 'WISEVIEW opening'
expect_count "probe cancels" "$(count 'TABLETPROBE touch cancel' /tmp/touchprobe.log)" 1
expect_count "probe moves after the cancel" "$(guest "sed -n '/touch cancel/,\$p' /tmp/touchprobe.log | grep -c 'touch motion id=0 x=220'" | tail -1)" 0
expect_count "probe ups after the cancel" "$(guest "sed -n '/touch cancel/,\$p' /tmp/touchprobe.log | grep -c 'touch up'" | tail -1)" 0
sleep 1
shot wiseview >/dev/null
touches "down 1 60 120|hold 60|up 1"
expect_log 'WISEVIEW closed'
guest "$stop_all" >/dev/null
guest 'export XDG_RUNTIME_DIR=/tmp; rm -f /tmp/wayland-0; picture=; [ -f /usr/share/keiland/wallpaper.png ] && picture=--wallpaper=/usr/share/keiland/wallpaper.png
/bin/wayland --testing --timeout=900 --width=1280 --height=800 --glass $picture > /tmp/zdesktop-edges.log 2>&1 </dev/null & sleep 5; ln -sf /tmp/zdesktop-edges.log /tmp/zdesktop.log; echo started' >/dev/null

# 3. The edges by touch: Home from the top-left corner; its bottom edge does nothing, a drag down closes it (WS181).
touches "down 1 8 8|swipe 200 200 8 30|up 1"
sleep 1
expect_log 'TOUCH shell contact=0 x=8 y=8'
expect_log 'HOME open via=drag'
shot home >/dev/null
touches "down 1 640 795|swipe 0 -150 6 30|up 1"
sleep 1
expect_count "Home closed by its bottom edge" "$(count 'HOME close via=')" 0
expect_count "Wiseview from Home's bottom edge" "$(count 'WISEVIEW opening')" 0
touches "down 1 640 300|swipe 0 300 10 30|up 1"
sleep 1
expect_log 'HOME close via=pull-down'

# Notes from the top-right corner.
touches "down 1 1272 6|swipe -160 160 8 30|up 1"
sleep 6
expect_log 'CORNER press source=touch x=1272 y=6'
expect_log 'CORNER commit via=distance'
expect_log 'CORNER notes launch pid='
shot notes --expect 640,400,fdf6e3 || status=1
guest 'for p in $(cat /tmp/notes.pids 2>/dev/null); do kill $p 2>/dev/null; done; rm -f /tmp/notes.pids; sleep 2' >/dev/null

# App Home from the bottom edge of the desktop, closed by a drag down; Wiseview from the top edge's band (WS181).
touches "down 1 640 795|swipe 0 -300 8 30|up 1"
sleep 1
expect_log 'HOME open via=edge'
touches "down 1 640 300|swipe 0 300 10 30|up 1"
sleep 1
expect_count "Home closed by a drag down" "$(count 'HOME close via=pull-down')" 2
touches "down 1 640 4|swipe 0 200 8 30|up 1"
sleep 1
expect_count "Wiseview from the top band" "$(count 'WISEVIEW gesture via=top-edge')" 1
expect_log 'WISEVIEW opening'
touches "down 1 60 120|hold 60|up 1"
expect_log 'WISEVIEW closed'
guest 'grep -E "KWL (TOUCH|EDGE|HOME (open|close|rise)|CORNER|WISEVIEW)" /tmp/zdesktop-edges.log' > "$out/${prefix}edges-log.txt"
guest "$stop_all" >/dev/null
guest 'export XDG_RUNTIME_DIR=/tmp; rm -f /tmp/wayland-0 /tmp/zdesktop.log; picture=; [ -f /usr/share/keiland/wallpaper.png ] && picture=--wallpaper=/usr/share/keiland/wallpaper.png
/bin/wayland --testing --timeout=900 --width=1280 --height=800 --glass $picture > /tmp/zdesktop.log 2>&1 </dev/null & sleep 5; echo started' >/dev/null

# 4. The title bars: the staircase, each window dragged with the mouse while it is on top.
a=$(open_at a 420x300 $A 100 150)
b=$(open_at b 500x360 $B 300 250)
c=$(open_at c 460x320 $C 560 350)
echo "surfaces: a=$a b=$b c=$c"
pointer move 1200 780 sleep 500 >/dev/null
expect_log "GLASS moved surface=${c#*:} x=560 y=350"
moves=$(count 'GLASS moved ')
shot stack --expect 700,450,$C --expect 450,350,$B --expect 200,200,$A || status=1

# a. Two fingers flicked up on c's title bar: c to the back, b forward with the keyboard.
touches "down 1 700 320; down 2 780 320|swipe 0 -40 4 25|up 1; up 2"
expect_log "TOUCH title pair window=$c"
expect_log "TOUCH flick window=$c dx=0,0 dy=-40,-40"
expect_log "GLASS lower client=${c%:*} surface=${c#*:} via=two-finger-flick next=$b focus=$b"
expect_count "c docked by the flick" "$(count "GLASS (dock|double-click) surface=${c#*:} ")" 0
expect_count "moves by the flick" "$(count 'GLASS moved ')" "$moves"
shot flick-lowered --expect 700,450,$B --expect 450,350,$B --expect 900,640,$C || status=1

# b. Two fingers dragged up slowly on b's title bar: nothing.
lowers=$(count 'GLASS lower ')
touches "down 1 450 220; down 2 520 220|swipe 0 -40 10 60|up 1; up 2"
expect_log 'TOUCH flick refused reason=slow'
expect_count "lowers after the slow drag" "$(count 'GLASS lower ')" "$lowers"
expect_count "moves after the slow drag" "$(count 'GLASS moved ')" "$moves"
shot slow --expect 700,450,$B --expect 450,350,$B || status=1

# c. Two fingers flicked down, then sideways, on b's title bar: nothing.
touches "down 1 450 220; down 2 520 220|swipe 0 40 4 25|up 1; up 2"
expect_log 'TOUCH flick refused reason=down'
touches "down 1 450 220; down 2 520 220|swipe 60 -10 4 25|up 1; up 2"
expect_log 'TOUCH flick refused reason=sideways'
expect_count "lowers after the down and sideways flicks" "$(count 'GLASS lower ')" "$lowers"
expect_count "moves after the down and sideways flicks" "$(count 'GLASS moved ')" "$moves"
shot down --expect 700,450,$B --expect 450,350,$B || status=1

# d. One finger dragged on b's title bar moves b the finger's way (it waits 150 ms for a second finger, then catches up).
touches "down 1 450 220|swipe -60 40 6 40|up 1"
expect_log 'TOUCH title held window='"$b"
expect_log 'TOUCH title drag contact=0 x=450 y=220'
expect_log "GLASS moved surface=${b#*:} x=240 y=290"
expect_count "lowers after the drag" "$(count 'GLASS lower ')" "$lowers"
shot drag --expect 300,500,$B --expect 900,400,$C || status=1

# e. One finger tapped on c's title bar (right of b) brings c forward.
touches "down 1 780 320|hold 60|up 1"
expect_log 'TOUCH title tap contact=0 x=780 y=320'
expect_count "lowers after the tap" "$(count 'GLASS lower ')" "$lowers"
shot tap --expect 700,450,$C --expect 300,500,$B || status=1

# Nothing failed.
guest 'grep -E "ERROR|FAILED|protocol error" /tmp/zdesktop.log /tmp/zdesktop-edges.log /tmp/a.log /tmp/b.log /tmp/c.log /tmp/touchprobe.log' | tee "$out/${prefix}errors.txt"
[ -s "$out/${prefix}errors.txt" ] && status=1
guest 'grep -E "KWL (TOUCH|INPUT)|GLASS (dock|undock|moved|lower|double-click)" /tmp/zdesktop.log' > "$out/${prefix}log.txt"
guest "$stop_all" >/dev/null
[ $status -eq 0 ] && echo "p013 touch: PASS" || echo "p013 touch: FAIL"
exit $status
