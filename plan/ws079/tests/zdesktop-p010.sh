#!/bin/sh
# ws079-p010: the top-right corner's swipe to Notes and the edges' gestures,
# checked on the Venus guest (the pattern of plan/ws035/tests/zdesktop-p062.sh).
#
# The compositor under test and wltest (with --app-id) are copied into the
# running guest; /bin/notes is the stand-in plan/ws079/tests/notes-standin.sh
# (a wltest window, app_id "notes", colour fdf6e3) while Notes is not built.
# The output is 1280x800; the pointer is driven through QMP (qmp-pointer.py).
#
#  0. The corner (the top-right 28x28) does not reach the network icon.
#  1. Negative: a short, a wrong-direction, a slow, a late (timed out) and an
#     outside-the-corner swipe bring nothing.
#  2. A swipe (the hint held first, hint.png) starts the stand-in fullscreen.
#  3. A flick while it is on top and fullscreen does nothing (no second one).
#  4. A windowed stand-in under another window: a swipe raises it and makes it
#     fullscreen (the compositor's configure).
#  5. App Home opened over fullscreen Notes; the top-right swipe closes Home
#     and leaves Notes (already on top and fullscreen).
#  6. Over fullscreen Notes the bottom edge's swipe takes it back to a window (ws099-p015), not Wiseview.
#  7. App Home open without Notes: the top-right swipe closes Home and starts it.
#  8. On App Home the bottom edge's swipe closes Home and does not open Wiseview.
#  9. On the desktop the bottom edge's swipe opens Wiseview; a stroke that
#     starts above the edge does not.
# 10. App Home by its corner drag and by a click on the desktop's corner, and
#     docking by a double click on a title bar, still work.
#
#   GUEST_RUNTIME=... plan/ws035/tests/zdesktop-guest.sh start IMAGE
#   GUEST_RUNTIME=... plan/ws079/tests/zdesktop-p010.sh BUILD [OUTDIR [PREFIX]]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
export GUEST_RUNTIME="${GUEST_RUNTIME:-$PWD/build/ws035-sq-run}"
build=${1:?usage: zdesktop-p010.sh BUILD [OUTDIR [PREFIX]]}
out=${2:-build/ws079-p010-shots}
prefix=${3:-ws079-p010-20260928-}
mkdir -p "$out"
guest() { timeout 90 python3 plan/tools/guest/guest.py run "$1" 2>&1; }
put() { timeout 90 python3 plan/tools/guest/guest.py put "$1" "$2" >/dev/null 2>&1; }
shot() { name=$1; shift; python3 plan/ws035/tests/zdesktop-check.py "$out/$prefix$name.png" --runtime "$GUEST_RUNTIME" "$@"; }
pointer() { python3 plan/ws035/tests/qmp-pointer.py "$GUEST_RUNTIME/qmp.sock" "$@"; }
stop_all='for p in $(ps -A -o pid,args | grep -E "[w]ayland( |$)|[w]ltest|[m]view" | awk "{print \$1}"); do kill $p; done; i=0; while ps -A -o args | grep -qE "[w]ayland( |$)|[w]ltest|[m]view" && [ $i -lt 50 ]; do sleep 0.2; i=$((i+1)); done'
stop_notes='for p in $(cat /tmp/notes.pids 2>/dev/null); do kill $p 2>/dev/null; done; rm -f /tmp/notes.pids; sleep 1'
env='export XDG_RUNTIME_DIR=/tmp WAYLAND_DISPLAY=wayland-0;'
status=0

# Counts the compositor's log lines matching a pattern.
count() {
	guest "grep -cE '$1' /tmp/zdesktop.log" | tail -1
}

# Counts the stand-ins running.
notes_running() {
	guest 'n=0; for p in $(cat /tmp/notes.pids 2>/dev/null); do kill -0 $p 2>/dev/null && n=$((n+1)); done; echo $n' | tail -1
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

# Prints the pointer steps of a stroke: a press at (x0, y0), n moves step_ms apart to (x1, y1); no release.
stroke() {
	x0=$1; y0=$2; x1=$3; y1=$4; n=$5; ms=$6
	steps="move $x0 $y0 sleep 200 down sleep 30"
	i=1
	while [ $i -le $n ]; do
		steps="$steps move $((x0 + (x1 - x0) * i / n)) $((y0 + (y1 - y0) * i / n)) sleep $ms"
		i=$((i + 1))
	done
	echo "$steps"
}

# Swipes from the top-left corner to open App Home and waits for it to settle.
open_home() {
	pointer $(stroke 4 4 204 204 10 30) up sleep 1200
}

# The compositor and the clients under test, and the stand-in.
guest "$stop_all" >/dev/null
put "$build/bin/wayland" /bin/wayland
put "$build/bin/wltest" /bin/wltest
put plan/ws079/tests/notes-standin.sh /bin/notes
guest 'chmod 755 /bin/wayland /bin/wltest /bin/notes; rm -f /tmp/notes-args /tmp/notes.log /tmp/notes.pids' >/dev/null
guest 'export XDG_RUNTIME_DIR=/tmp; rm -f /tmp/wayland-0; picture=; [ -f /usr/share/keiland/wallpaper.png ] && picture=--wallpaper=/usr/share/keiland/wallpaper.png
/bin/wayland --testing --timeout=900 --width=1280 --height=800 --glass $picture > /tmp/zdesktop.log 2>&1 </dev/null & sleep 5
/bin/wltest --windowed --size=420x300 --color=f4f7fc --frames=3600 --delay-ms=250 --token=a > /tmp/a.log 2>&1 </dev/null & sleep 4; echo started' >/dev/null
pointer move 640 500 sleep 800

# 0. The corner against the network icon.
set -- $(guest "grep 'KWL CORNER zone' /tmp/zdesktop.log | head -1" | sed -n 's/.* x=\([0-9]*\) .*/\1/p')
zone_x=${1:-0}
set -- $(guest "grep 'KWL NETWORK icon' /tmp/zdesktop.log | head -1" | sed -n 's/.* x=\([-0-9]*\) y=[0-9]* width=\([0-9]*\) .*/\1 \2/p')
icon_x=${1:-0}; icon_width=${2:-0}
echo "corner from x=$zone_x; network icon x=$icon_x..$((icon_x + icon_width - 1))"
if [ "$zone_x" -ge 1200 ] && [ $((icon_x + icon_width)) -le "$zone_x" ]; then
	echo "zone: clear of the network icon ok"
else
	echo "zone: overlaps the network icon FAILED"
	status=1
fi
shot desktop >/dev/null

# 1. Negative swipes: nothing is started.
pointer $(stroke 1272 6 1242 36 6 40) up sleep 500
pointer $(stroke 1272 6 1072 26 10 30) up sleep 500
pointer $(stroke 1272 6 1182 96 30 60) sleep 500 up sleep 500
pointer move 1272 6 sleep 200 down sleep 1800 $(stroke 1272 6 1072 206 10 30 | sed 's/^move 1272 6 sleep 200 down sleep 30//') up sleep 500
before=$(count 'CORNER press')
pointer $(stroke 1240 6 1040 206 10 30) up sleep 500
expect_count "cancel short" "$(count 'CORNER cancel reason=short')" 2
expect_count "cancel direction" "$(count 'CORNER cancel reason=direction')" 1
expect_count "cancel timeout" "$(count 'CORNER cancel reason=timeout')" 1
expect_count "press outside the corner" "$(count 'CORNER press')" "$before"
expect_count "commits after the negative swipes" "$(count 'CORNER commit')" 0
expect_count "stand-ins after the negative swipes" "$(notes_running)" 0

# 2. A swipe starts the stand-in fullscreen; the hint shows while it is held.
pointer $(stroke 1272 6 1112 166 8 40) sleep 700
shot hint >/dev/null
pointer up sleep 8000
expect_log 'CORNER commit via=distance'
expect_log 'CORNER notes launch pid='
expect_log 'CONFIGURE client=[0-9]+ surface=[0-9]+ serial=[0-9]+ width=1280 height=800 fullscreen=1'
expect_count "stand-ins after the swipe" "$(notes_running)" 1
shot notes-fullscreen --expect 640,400,fdf6e3 --expect 1275,4,fdf6e3 --expect 4,796,fdf6e3 || status=1

# 3. A flick while it is on top and fullscreen: nothing more.
pointer move 1272 6 sleep 200 down sleep 10 move 1248 30 sleep 15 move 1224 54 sleep 15 move 1200 78 sleep 15 move 1176 102 up sleep 2500
expect_log 'CORNER commit via=flick'
expect_log 'CORNER notes surface=[0-9]* already'
expect_count "stand-ins after the flick" "$(notes_running)" 1
shot notes-after-flick --expect 640,400,fdf6e3 || status=1

# 4. A windowed stand-in under another window: raised and made fullscreen.
guest "$stop_notes" >/dev/null
guest "$env /bin/notes --windowed & sleep 4; /bin/wltest --windowed --size=500x360 --color=c8d8ec --frames=3600 --delay-ms=250 --token=b > /tmp/b.log 2>&1 </dev/null & sleep 4; echo started" >/dev/null
set -- $(guest "grep 'KWL MAP client=' /tmp/zdesktop.log | tail -1" | sed -n 's/.* surface=\([0-9]*\) x=\([-0-9]*\) y=\([-0-9]*\).*/\1 \2 \3/p')
b_surface=${1:-0}; bx=${2:-0}; by=${3:-0}
echo "b: surface $b_surface at $bx,$by"
pointer move 640 500 sleep 800
shot notes-behind >/dev/null
pointer $(stroke 1272 6 1112 166 8 40) up sleep 3000
expect_log 'CORNER notes surface=[0-9]* raise fullscreen'
set -- $(guest "grep 'CORNER notes surface=[0-9]* raise fullscreen' /tmp/zdesktop.log | tail -1" | sed -n 's/.*surface=\([0-9]*\).*/\1/p')
notes_surface=${1:-0}
expect_log "CONFIGURE client=[0-9]* surface=$notes_surface serial=[0-9]* width=1280 height=800 fullscreen=1"
shot notes-raised --expect 640,400,fdf6e3 --expect 1275,4,fdf6e3 --expect 4,796,fdf6e3 || status=1

# 5. App Home over fullscreen Notes; the top-right swipe closes Home, Notes stays.
already=$(count 'CORNER notes surface=[0-9]* already')
open_home
expect_log 'HOME open via=drag'
shot home-over-notes >/dev/null
pointer $(stroke 1272 6 1112 166 8 40) up sleep 2500
expect_log 'HOME close via=notes'
expect_count "already, after the swipe on Home" "$(count 'CORNER notes surface=[0-9]* already')" $((already + 1))
shot home-to-notes --expect 640,400,fdf6e3 || status=1

# 6. Over fullscreen Notes the bottom edge's swipe takes it back to a window (ws099-p015), and Wiseview does not open.
opening=$(count 'WISEVIEW opening')
backs=$(count 'GLASS unfullscreen surface=[0-9]+ via=swipe errno=0')
pointer $(stroke 640 796 640 476 10 30) up sleep 1500
expect_count "Wiseview over fullscreen Notes" "$(count 'WISEVIEW opening')" "$opening"
expect_count "fullscreen Notes back to a window" "$(count 'GLASS unfullscreen surface=[0-9]+ via=swipe errno=0')" $((backs + 1))
shot notes-swiped-back >/dev/null

# 7. App Home open without Notes: the top-right swipe closes Home and starts Notes.
guest "$stop_notes" >/dev/null
launches=$(count 'CORNER notes launch pid=')
closes=$(count 'HOME close via=notes')
open_home
pointer $(stroke 1272 6 1112 166 8 40) up sleep 8000
expect_count "closes of Home for Notes" "$(count 'HOME close via=notes')" $((closes + 1))
expect_count "launches from Home" "$(count 'CORNER notes launch pid=')" $((launches + 1))
shot home-launch-notes --expect 640,400,fdf6e3 || status=1
guest "$stop_notes" >/dev/null

# 8. On App Home the bottom edge closes Home, and Wiseview does not open.
pointer move 640 500 sleep 500
open_home
opening=$(count 'WISEVIEW opening')
pointer $(stroke 640 796 640 556 8 40) up sleep 1500
expect_log 'HOME bottom swipe'
expect_log 'HOME close via=bottom'
expect_count "Wiseview from the bottom edge on Home" "$(count 'WISEVIEW opening')" "$opening"
shot home-bottom-closed >/dev/null

# 9. On the desktop the bottom edge opens Wiseview; a stroke from above the edge does not.
pointer $(stroke 640 796 640 476 10 30) up sleep 1500
expect_count "Wiseview from the bottom edge" "$(count 'WISEVIEW opening')" $((opening + 1))
shot wiseview >/dev/null
pointer move 20 700 sleep 200 down sleep 60 up sleep 1500
pointer $(stroke 640 770 640 450 10 30) up sleep 1500
expect_count "Wiseview from above the edge" "$(count 'WISEVIEW opening')" $((opening + 1))

# 10. App Home by its drag and its desktop corner, and docking, still work.
opens=$(count 'HOME open via=drag')
open_home
expect_count "Home opened by the drag" "$(count 'HOME open via=drag')" $((opens + 1))
pointer move 1270 790 sleep 300 down sleep 60 up sleep 1500
expect_log 'HOME close via=corner'
surface=$b_surface
pointer move $((bx + 150)) $((by - 30)) sleep 400 down sleep 60 up sleep 60 down sleep 60 up sleep 2500
expect_log "GLASS dock surface=$surface via=double-click"
shot docked >/dev/null
set -- $(guest "grep 'GLASS dock surface=$surface via=double-click' /tmp/zdesktop.log | tail -1" | sed -n 's/.* title=\([0-9]*\).*/\1/p')
title_x=${1:-0}
pointer move $((title_x + 60)) 17 sleep 400 down sleep 60 up sleep 60 down sleep 60 up sleep 2500
expect_log "GLASS undock surface=$surface via=double-click"

# Nothing failed.
guest 'grep -E "ERROR|FAILED" /tmp/zdesktop.log /tmp/a.log /tmp/b.log /tmp/notes.log' | tee "$out/${prefix}errors.txt"
[ -s "$out/${prefix}errors.txt" ] && status=1
guest 'grep -E "CORNER|HOME (open|close|bottom)|WISEVIEW (opening|cancel|close)|MODE|GLASS (dock|undock)|CONFIGURE" /tmp/zdesktop.log' > "$out/${prefix}log.txt"
guest "$stop_all" >/dev/null
[ $status -eq 0 ] && echo "p010: PASS" || echo "p010: FAIL"
exit $status
