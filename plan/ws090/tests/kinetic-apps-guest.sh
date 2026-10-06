#!/bin/sh
# ws090-p019: a touch pad's two-finger scroll that flies on in the terminal, the text editor and the file manager,
# through libkeiland's scroller (the one inertia of every program), on the kinetic apps guest
# (plan/ws090/tests/config-amd64-kinetic-apps.mk, started with plan/ws079/tests/pen-guest.sh start IMAGE).
# zdesktop --glass 1280x800, one program at a time at 900x500; the pointer (QMP) on its content; touchinject's pad has
# two fingers move 20 mm up in about 100 ms and lift.  Judged from the logs over SSH and a picture each.
#  1. Terminal (2000 lines of scrollback from seq): "ZTERM KINETIC fling source=finger" and then "ZTERM TOUCH rest"
#     (terminal.png).
#  2. Text Editor (a file of 2000 lines): "TEXTEDIT KINETIC fling source=finger" (textedit.png).
#  3. Files (a folder of 400 files): "ZFILES KINETIC fling source=finger" and then "ZFILES TOUCH rest" (files.png).
#  4. zdesktop has "ZWL AXIS stop" for each and no ERROR line.
#   plan/ws090/tests/kinetic-apps-guest.sh [OUTDIR]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
export GUEST_RUNTIME="${GUEST_RUNTIME:-$PWD/build/ws079-run}"
out=${1:-build/ws090-kinetic-apps}
mkdir -p "$out"
guest() { timeout 120 python3 plan/tools/guest/guest.py run "$1" 2>&1; }
pointer() { python3 plan/ws035/tests/qmp-pointer.py "$GUEST_RUNTIME/qmp.sock" "$@"; }
check() { python3 plan/ws035/tests/zdesktop-check.py "$@" --runtime "$GUEST_RUNTIME"; }
stop_all='for p in $(ps -A -o pid,args | grep -E "[w]ayland( |$)|[t]erminal|[t]extedit|[f]iles|[t]ouchinject" | awk "{print \$1}"); do kill $p; done; i=0; while ps -A -o args | grep -qE "[w]ayland( |$)|[t]erminal|[t]extedit|[f]iles" && [ $i -lt 50 ]; do sleep 0.2; i=$((i+1)); done'
env='export XDG_RUNTIME_DIR=/tmp WAYLAND_DISPLAY=wayland-0 HOME=/root;'
swipe='printf "pad 1336 760 5 scan\nwait 2600\ndown 0 600 500; down 1 760 500\nwait 20\nswipe 0 -240 8 12\nup 0; up 1\nhold 2000\n" | /bin/touchinject; echo replay=$?'
status=0
pass() { echo "$1: ok"; }
fail() { echo "$1: FAILED"; status=1; }

# Starts zdesktop, then a program, and puts the pointer on its content.
start() {
	guest "$stop_all" >/dev/null
	guest 'export XDG_RUNTIME_DIR=/tmp; rm -f /tmp/wayland-0
/bin/wayland --testing --timeout=600 --width=1280 --height=800 --glass > /tmp/zdesktop.log 2>&1 </dev/null & sleep 5; echo started' >/dev/null
	guest "$env $1 > /tmp/app.log 2>&1 </dev/null & sleep 6; echo started" >/dev/null
	set -- $(guest "grep 'ZWL MAP client=' /tmp/zdesktop.log | tail -1" | sed -n 's/.* client=\([0-9]*\) surface=\([0-9]*\) x=\([-0-9]*\) y=\([-0-9]*\).*/\2 \3 \4/p')
	wx=${2:-0}; wy=${3:-0}
	pointer move $((wx + 450)) $((wy + 300)) sleep 300 >/dev/null
}

# Swipes the pad, and checks the program's log for its lines, in order.
judge() {
	name=$1
	shift
	guest "$swipe" > "$out/$name-pad.txt"
	grep -q '^replay=0$' "$out/$name-pad.txt" && pass "$name replay" || fail "$name replay"
	sleep 2
	guest 'cat /tmp/app.log' > "$out/$name.log"
	guest 'grep -E "ZWL AXIS|ERROR" /tmp/zdesktop.log' > "$out/$name-zdesktop.txt"
	for line in "$@"; do
		grep -q "$line" "$out/$name.log" && pass "$name $line" || fail "$name $line"
	done
	grep -q 'ZWL AXIS stop' "$out/$name-zdesktop.txt" && pass "$name axis-stop" || fail "$name axis-stop"
	grep -q ERROR "$out/$name-zdesktop.txt" && fail "$name zdesktop-errors" || pass "$name zdesktop-errors"
	check "$out/$name.png" >/dev/null
}

# 1. Terminal, with a long scrollback.
start "/bin/terminal --columns=80 --rows=24 --command='seq 1 2000; sleep 100' --timeout-s=120"
judge terminal 'ZTERM KINETIC fling source=finger' 'ZTERM TOUCH rest'

# 2. Text Editor, with a long file.
guest 'seq 1 2000 > /tmp/long.txt; echo made' >/dev/null
start "/bin/textedit --timeout-s=120 --width=900 --height=500 /tmp/long.txt"
judge textedit 'TEXTEDIT KINETIC fling source=finger'

# 3. Files, in a folder of many files.
guest 'mkdir -p /tmp/many; i=0; while [ $i -lt 400 ]; do : > /tmp/many/file$i.txt; i=$((i+1)); done; echo made' >/dev/null
start "/bin/files --timeout-s=120 /tmp/many"
judge files 'ZFILES KINETIC fling source=finger' 'ZFILES TOUCH rest'

guest "$stop_all" >/dev/null
[ $status -eq 0 ] && echo "kinetic-apps-guest: PASS" || echo "kinetic-apps-guest: FAIL"
exit $status
