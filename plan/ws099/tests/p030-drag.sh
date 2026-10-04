#!/bin/sh
# ws099-p030: a title bar's menu items and its search field move the window when dragged (2026-10-04 user), on the
# pen test guest (plan/ws079/tests/build-pen-image.sh: the lean Venus image with the test touch screen, the terminal
# and titlebar-probe).  zdesktop --glass at 1280x800.  A press waits: one that goes 2 pixels (a mouse) or 8 (a finger)
# moves the window with the pressed point under the pointer; one released before that is a click.
#  A. The terminal's menus (Shell, Edit, View, Session, Help):
#     1. mouse: a press on Edit opens nothing until the release; the release opens it (menu-click.png).  Esc.
#     2. mouse: a press that wanders 1 pixel still opens Edit on its release.  Esc.
#     3. mouse: Edit dragged by (60, 80): the window moves exactly that far, no menu opens (menu-drag.png).
#     4. touch: a tap on Edit opens it; a tap that wanders 3 pixels too.  Esc each time.
#     5. touch: Edit dragged by (80, 60) moves the window that far, no menu opens.
#     6. mouse: docked (a double click on the title), Edit in the system bar dragged down pulls the window out.
#  B. titlebar-probe's controls (a file manager's: back, forward, home, a breadcrumb, the search field id=5, ...):
#     1. mouse: a click on the search field gives it the keyboard (focus, caret); "abcdef" typed.
#     2. mouse: on the field with the keyboard a drag selects (select anchor=0 cursor=6) and the window stays;
#        "Z" replaces the selection (text=Z); a click puts the caret (anchor=1 cursor=1) (search-select.png).  Esc.
#     3. mouse: the field without the keyboard dragged by (50, 40) moves the window that far, no focus.
#     4. touch: a tap on the field gives it the keyboard; a finger's drag in it selects.  Esc.
#     5. touch: the field without the keyboard dragged by (70, 50) moves the window that far.
#     6. mouse: the close button acts on the press (GLASS close before the release).
#  No ERROR line in zdesktop's log.
#
#   plan/ws079/tests/build-pen-image.sh BUILD
#   plan/ws079/tests/pen-guest.sh start BUILD/hdd-image.img
#   plan/ws099/tests/p030-drag.sh [OUTDIR]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
export GUEST_RUNTIME="${GUEST_RUNTIME:-$PWD/build/ws079-run}"
out=${1:-build/ws099-p030}
mkdir -p "$out"
guest() { timeout 120 python3 plan/tools/guest/guest.py run "$1" 2>&1; }
put() { timeout 90 python3 plan/tools/guest/guest.py put "$1" "$2" >/dev/null 2>&1; }
. plan/tools/guest/zwl-clients.sh
check() { python3 plan/ws035/tests/zdesktop-check.py "$@" --runtime "$GUEST_RUNTIME"; }
pointer() { python3 plan/ws035/tests/qmp-pointer.py "$GUEST_RUNTIME/qmp.sock" "$@"; }
keys() { python3 plan/ws035/tests/qmp-keys.py "$GUEST_RUNTIME/qmp.sock" "$@"; sleep 0.7; }
stop_all='for p in $(ps -A -o pid,args | grep -E "[w]ayland( |$)|[t]erminal|[t]itlebar-probe|[t]ouchinject" | awk "{print \$1}"); do kill $p; done; i=0; while ps -A -o args | grep -qE "[w]ayland( |$)|[t]erminal|[t]itlebar-probe" && [ $i -lt 50 ]; do sleep 0.2; i=$((i+1)); done'
status=0

# Counts the lines of a guest log matching a pattern (zdesktop's by default).
count() {
	guest "grep -cE '$1' ${2:-/tmp/zdesktop.log}" | tail -1
}

# Fails the run unless a guest log (zdesktop's by default) has a line matching a pattern (within a few seconds).
expect_log() {
	tries=0
	while [ $tries -lt 5 ]; do
		if guest "grep -E '$1' ${2:-/tmp/zdesktop.log}" | grep -q .; then
			echo "log: $1 ok"
			return
		fi
		tries=$((tries + 1))
		sleep 1
	done
	echo "log: $1 MISSING"
	status=1
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

# Replays a touch script (its frames separated by '|') on the output's pixels.
touches() {
	printf 'size 1279 799 2\nwait 2600\n%s\nhold 300\n' "$1" | tr '|' '\n' > "$out/touch.script"
	put "$out/touch.script" /tmp/touch.script
	guest '/bin/touchinject /tmp/touch.script; echo replay=$?' | grep -q '^replay=0$' || { echo "touchinject: FAILED"; status=1; }
	sleep 0.8
}

# Presses a point, moves with the button held by (dx, dy) in steps, and lets go.
drag() {
	pointer move "$1" "$2" sleep 300 down sleep 120 \
	    move $(($1 + $3 / 4)) $(($2 + $4 / 4)) sleep 80 \
	    move $(($1 + $3 / 2)) $(($2 + $4 / 2)) sleep 80 \
	    move $(($1 + $3)) $(($2 + $4)) sleep 300 up sleep 900
}
click() {
	pointer move $(($1 - 2)) "$2" sleep 150 move "$1" "$2" sleep 300 down sleep 60 up sleep "${3:-800}"
}
double() {
	pointer move "$1" "$2" sleep 300 down sleep 50 up sleep 80 down sleep 50 up sleep "${3:-1200}"
}
shot() {
	pointer move 1270 790 sleep 400
	check "$out/$1" >/dev/null
}

# Starts zdesktop alone (a fresh log) and waits for it.
start_zdesktop() {
	guest "$stop_all" >/dev/null
	guest 'export XDG_RUNTIME_DIR=/tmp; rm -f /tmp/wayland-0; picture=; [ -f /usr/share/keiland/wallpaper.ppm ] && picture=--wallpaper=/usr/share/keiland/wallpaper.ppm
/bin/wayland --timeout=900 --width=1280 --height=800 --glass $picture > /tmp/zdesktop.log 2>&1 </dev/null & for w in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do grep -q ZWL.READY /tmp/zdesktop.log 2>/dev/null && break; sleep 0.5; done; sleep 1; echo started' >/dev/null
}

# The latest map of an application client: "surface x y".
mapped() {
	guest "grep 'ZWL MAP client=$(zwl_app_client $1) ' /tmp/zdesktop.log | tail -1" | sed -n 's/.* surface=\([0-9]*\) x=\([-0-9]*\) y=\([-0-9]*\).*/\1 \2 \3/p'
}

# The centre (x) of a menu item as last logged in a place, from the window's x (0 for the system bar).
item_x() {
	guest "grep 'MENU bar client=$(zwl_app_client $1) .* where=$2 item=$3 ' /tmp/zdesktop.log | tail -1" |
	    sed -n 's/.* offset=\([-0-9]*\) top=[-0-9]* width=\([0-9]*\).*/\1 \2/p' | { read offset width; echo $(( ${4:-0} + ${offset:-0} + ${width:-0} / 2 )); }
}

# A titlebar control's rectangle as last logged: "x y width height".
control() {
	guest "grep 'ZWL TITLEBAR control client=$(zwl_app_client $1) .* where=$2 id=$3 ' /tmp/zdesktop.log | tail -1" |
	    sed -n 's/.* x=\([-0-9]*\) y=\([-0-9]*\) width=\([0-9]*\) height=\([0-9]*\).*/\1 \2 \3 \4/p'
}

# A. The terminal's menus.
guest 'rm -f $HOME/.config/keiland/terminal.conf' >/dev/null
start_zdesktop
guest 'export XDG_RUNTIME_DIR=/tmp; /bin/terminal --token=t1 --timeout-s=800 > /tmp/t.log 2>&1 </dev/null & sleep 6; echo started' >/dev/null
zwl_app_clients
set -- $(mapped 1)
surface=${1:-0}; wx=${2:-0}; wy=${3:-0}
echo "terminal: client $zc1 surface $surface at $wx,$wy"
expect_log "MENU bar client=$zc1 surface=$surface where=floating item=5 "

# 1. A press on Edit opens nothing until its release.
ex=$(item_x 1 floating 2 "$wx"); bar=$((wy - 30))
opens=$(count 'MENU open ')
pointer move "$ex" "$bar" sleep 300 down sleep 600
expect_count "menus open while Edit is held" "$(count 'MENU open ')" "$opens"
pointer up sleep 800
expect_log "MENU open client=$zc1 surface=$surface item=2 "
check "$out/menu-click.png" >/dev/null
keys '<esc>'

# 2. A press that wanders 1 pixel is still a click.
opens=$(count 'MENU open ')
pointer move "$ex" "$bar" sleep 300 down sleep 100 move $((ex + 1)) "$bar" sleep 200 up sleep 800
expect_count "menus open after a 1-pixel press" "$(count 'MENU open ')" $((opens + 1))
keys '<esc>'

# 3. Edit dragged by the mouse moves the window.
opens=$(count 'MENU open ')
drag "$ex" "$bar" 60 80
expect_log "ZWL MENU press moves client=$zc1 surface=$surface item=2 docked=0"
expect_log "GLASS press move surface=$surface "
expect_log "GLASS moved surface=$surface x=$((wx + 60)) y=$((wy + 80))"
expect_count "menus open after the drag" "$(count 'MENU open ')" "$opens"
wx=$((wx + 60)); wy=$((wy + 80))
shot menu-drag.png

# 4. Taps on Edit open it; a tap may wander a little.
ex=$(item_x 1 floating 2 "$wx"); bar=$((wy - 30))
opens=$(count 'MENU open ')
touches "down 1 $ex $bar|hold 60|up 1"
expect_log "TOUCH title tap contact=0 x=$ex y=$bar"
expect_count "menus open after the tap" "$(count 'MENU open ')" $((opens + 1))
keys '<esc>'
touches "down 1 $ex $bar|hold 60|move 1 $((ex + 3)) $((bar + 2))|hold 40|up 1"
expect_count "menus open after the wandering tap" "$(count 'MENU open ')" $((opens + 2))
keys '<esc>'

# 5. Edit dragged by a finger moves the window.
opens=$(count 'MENU open ')
moves=$(count "MENU press moves ")
touches "down 1 $ex $bar|swipe 80 60 8 40|up 1"
expect_count "menu presses that moved after the finger's drag" "$(count 'MENU press moves ')" $((moves + 1))
expect_log "GLASS moved surface=$surface x=$((wx + 80)) y=$((wy + 60))"
expect_count "menus open after the finger's drag" "$(count 'MENU open ')" "$opens"
wx=$((wx + 80)); wy=$((wy + 60))

# 6. Docked, Edit in the system bar pulled down brings the window out.
double $((wx + 80)) $((wy - 30))
expect_log "GLASS dock surface=$surface "
expect_log "MENU bar client=$zc1 surface=$surface where=docked item=2 "
dx=$(item_x 1 docked 2 0)
pointer move "$dx" 17 sleep 300 down sleep 120 move "$dx" 60 sleep 100 move "$dx" 120 sleep 100 move "$dx" 200 sleep 100 move "$dx" 260 sleep 300 up sleep 1000
expect_log "ZWL MENU press moves client=$zc1 surface=$surface item=2 docked=1"
expect_log "GLASS press pull surface=$surface"
expect_log "GLASS undock surface=$surface via=pull "
shot menu-pull.png
guest 'grep -E "ZWL (MENU|GLASS|TOUCH)" /tmp/zdesktop.log' > "$out/menu-log.txt"
errors_a=$(count 'ERROR')

# B. The search field of titlebar-probe's controls.
start_zdesktop
guest 'export XDG_RUNTIME_DIR=/tmp; /bin/titlebar-probe --show=Files --mode=controls --seconds=600 > /tmp/probe.log 2>&1 </dev/null & sleep 5; echo started' >/dev/null
zwl_app_clients
set -- $(mapped 1)
surface=${1:-0}; wx=${2:-0}; wy=${3:-0}
echo "probe: client $zc1 surface $surface at $wx,$wy"
expect_log 'TITLEBARPROBE show ready mode=controls' /tmp/probe.log
expect_log "ZWL TITLEBAR control client=$zc1 surface=$surface where=floating id=5 .* shown=1"
set -- $(control 1 floating 5)
fx=${1:-0}; fy=${2:-0}; fw=${3:-0}; fh=${4:-0}
cy=$((fy + fh / 2)); tx=$((fx + 30))
echo "search field: $fx,$fy ${fw}x$fh"

# 1. A click gives it the keyboard with the caret.
click $((fx + fw / 2)) "$cy"
expect_log "ZWL TITLEBAR focus client=$zc1 surface=$surface id=5 edit=0"
expect_log "ZWL TITLEBAR caret client=$zc1 surface=$surface id=5 cursor=0"
keys 'abcdef'
expect_log 'TITLEBARPROBE event=text id=5 text=abcdef' /tmp/probe.log

# 2. On the field with the keyboard a drag selects and the window stays.
moves=$(count 'GLASS moved ')
drag $((tx + 1)) "$cy" $((fw - 44)) 0
expect_log "ZWL TITLEBAR select client=$zc1 surface=$surface id=5 anchor=0 cursor=6"
expect_count "windows moved by the selecting drag" "$(count 'GLASS moved ')" "$moves"
expect_count "presses that moved" "$(count 'TITLEBAR press moves ')" 0
keys 'Z'
expect_log 'TITLEBARPROBE event=text id=5 text=Z$' /tmp/probe.log
click $((fx + fw - 14)) "$cy"
expect_log "ZWL TITLEBAR select client=$zc1 surface=$surface id=5 anchor=1 cursor=1"
shot search-select.png
keys '<esc>'
expect_log 'TITLEBARPROBE event=done id=5 how=1 ' /tmp/probe.log

# 3. The field without the keyboard dragged by the mouse moves the window.
focuses=$(count 'TITLEBAR focus ')
drag $((fx + fw / 2)) "$cy" 50 40
expect_log "ZWL TITLEBAR press moves client=$zc1 surface=$surface id=5 docked=0"
expect_log "GLASS moved surface=$surface x=$((wx + 50)) y=$((wy + 40))"
expect_count "focuses after the drag" "$(count 'TITLEBAR focus ')" "$focuses"
wx=$((wx + 50)); wy=$((wy + 40)); fx=$((fx + 50)); fy=$((fy + 40)); cy=$((cy + 40)); tx=$((tx + 50))
shot search-drag.png

# 4. A tap gives it the keyboard; a finger's drag in it selects.
touches "down 1 $((fx + fw / 2)) $cy|hold 60|up 1"
expect_count "focuses after the tap" "$(count 'TITLEBAR focus ')" $((focuses + 1))
keys 'hello'
selects=$(count 'TITLEBAR select ')
touches "down 1 $((tx + 1)) $cy|swipe $((fw - 44)) 0 6 40|up 1"
expect_count "selections by the finger" "$(count 'TITLEBAR select .* anchor=0 cursor=[1-9]')" $((selects + 1))
keys '<esc>'

# 5. The field without the keyboard dragged by a finger moves the window.
moved=$(count 'TITLEBAR press moves ')
touches "down 1 $((fx + fw / 2)) $cy|swipe 70 50 7 40|up 1"
expect_count "presses that moved after the finger's drag" "$(count 'TITLEBAR press moves ')" $((moved + 1))
expect_log "GLASS moved surface=$surface x=$((wx + 70)) y=$((wy + 50))"
expect_count "focuses after the finger's drag" "$(count 'TITLEBAR focus ')" $((focuses + 1))
wx=$((wx + 70)); wy=$((wy + 50))

# 6. The close button acts on the press.
pointer move $((wx + 800 - 26)) $((wy - 30)) sleep 300 down sleep 600
expect_count "closes while the close button is held" "$(count "GLASS close surface=$surface")" 1
pointer up sleep 800

guest 'grep -E "ZWL (MENU|GLASS|TOUCH|TITLEBAR)" /tmp/zdesktop.log' > "$out/search-log.txt"
guest 'cat /tmp/probe.log' > "$out/probe.log"
errors_b=$(count 'ERROR')
[ "${errors_a:-1}" = 0 ] && [ "${errors_b:-1}" = 0 ] && echo "zdesktop: no ERROR" || { echo "zdesktop: ERROR lines"; status=1; }
guest "$stop_all" >/dev/null
[ $status = 0 ] && echo "p030-drag: PASS" || echo "p030-drag: FAIL"
exit $status
