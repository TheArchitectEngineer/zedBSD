#!/bin/sh
# ws079-p003: the pen through the compositor, on the Venus guest with the pen image
# (plan/ws079/tests/build-pen-image.sh: /dev/input-inject, peninject, tablet-probe).
#
#   plan/ws079/tests/pen-guest.sh start && plan/ws079/tests/pen-guest.sh wait
#   plan/ws079/tests/p003-guest.sh [OUTDIR] [STEP...]
#
# Steps (default all but select): tablet (tablet-probe binds zwp_tablet_manager_v2; the
# pressure ramp, tilt, a barrel button, the eraser, the implicit grab, proximity), pointer
# (tablet-probe --pointer: the pen as BTN_LEFT, BTN_RIGHT, BTN_MIDDLE), home (a tap on
# the launcher opens App Home), terminal (starts /bin/terminal and photographs it), corner
# (a pen swipe from the top-right corner reaches the ws079-p010 recogniser),
# select X0 X1 Y (a pen drag over the terminal's row, then the second barrel button).
# The pictures go to build/ws035-shots/ws079-p003-20260928-*.png, the logs to OUTDIR.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
GUEST_RUNTIME="${GUEST_RUNTIME:-$(pwd)/build/ws079-run}"
export GUEST_RUNTIME
out=${1:-build/ws079-p003}
[ $# -gt 0 ] && shift
steps=${*:-compositor tablet pointer home terminal}
shots=build/ws035-shots
mkdir -p "$out" "$shots"
guest() { timeout 120 python3 plan/tools/guest/guest.py run "$1" 2>&1; }
shot() { python3 plan/ws035/tests/zdesktop-check.py "$shots/ws079-p003-20260928-$1.png" --runtime "$GUEST_RUNTIME" >/dev/null; echo "shot: $shots/ws079-p003-20260928-$1.png"; }
status=0

# Fails the run unless a log has as many lines matching a pattern as asked (default 1).
expect_log() {
	found=$(guest "grep -cE '$2' $1" | tail -1)
	if [ "${found:-0}" -ge "${3:-1}" ] 2>/dev/null; then
		echo "log: $2 ok ($found)"
	else
		echo "log: $2 MISSING (found ${found:-0})"
		status=1
	fi
}

# Puts the pen scripts on the guest.
python3 plan/ws079/tests/p003-scripts.py "$out/pen" ${SELECT_ARGS:-} >/dev/null
for name in tablet pointer home select corner; do
	timeout 60 python3 plan/tools/guest/guest.py put "$out/pen/$name.pen" "/tmp/$name.pen" >/dev/null 2>&1
done

for step in $steps; do
	case "$step" in
	compositor)
		# The compositor, glass look, 1280x800.
		guest 'for p in $(ps -A -o pid,args | grep -E "[w]ayland( |$)|[t]ablet-probe|[t]erminal" | awk "{print \$1}"); do kill $p; done; sleep 1
export XDG_RUNTIME_DIR=/tmp; rm -f /tmp/wayland-0; picture=; [ -f /usr/share/keiland/wallpaper.ppm ] && picture=--wallpaper=/usr/share/keiland/wallpaper.ppm
/bin/wayland --timeout=900 --width=1280 --height=800 --glass $picture > /tmp/zdesktop.log 2>&1 </dev/null & sleep 5; echo started' >/dev/null
		;;
	tablet)
		# The tablet client, then the pen script; the injector's node is found by the scan.
		guest 'export XDG_RUNTIME_DIR=/tmp; /bin/tablet-probe --timeout-s=60 --token=t > /tmp/t.log 2>&1 </dev/null & sleep 3; peninject /tmp/tablet.pen; echo replay=$?; sleep 1' | tail -1
		shot tablet
		guest 'kill $(ps -A -o pid,args | grep "[t]ablet-probe" | awk "{print \$1}") 2>/dev/null; sleep 1; cat /tmp/t.log' > "$out/tablet.log"
		expect_log /tmp/t.log 'TABLETPROBE ready run=t mode=tablet'
		expect_log /tmp/t.log 'TABLETPROBE tablet name="Test pen \(input-inject\)"'
		expect_log /tmp/t.log 'TABLETPROBE tablet path=/dev/input/event'
		expect_log /tmp/t.log 'TABLETPROBE tablet done'
		expect_log /tmp/t.log 'TABLETPROBE tool type=0x140'
		expect_log /tmp/t.log 'TABLETPROBE tool type=0x141'
		expect_log /tmp/t.log 'TABLETPROBE tool capability=2' 2
		expect_log /tmp/t.log 'TABLETPROBE tool capability=1' 2
		expect_log /tmp/t.log 'TABLETPROBE proximity_in tool=0x140 tablet=1 surface=1' 2
		expect_log /tmp/t.log 'TABLETPROBE proximity_in tool=0x141 tablet=1 surface=1'
		expect_log /tmp/t.log 'TABLETPROBE down' 3
		expect_log /tmp/t.log 'TABLETPROBE up' 3
		expect_log /tmp/t.log 'TABLETPROBE pressure=65535'
		expect_log /tmp/t.log 'TABLETPROBE pressure=0$'
		expect_log /tmp/t.log 'TABLETPROBE tilt x=-30\.'
		expect_log /tmp/t.log 'TABLETPROBE tilt x=30\.'
		expect_log /tmp/t.log 'TABLETPROBE button=0x14b state=1'
		expect_log /tmp/t.log 'TABLETPROBE button=0x14b state=0'
		expect_log /tmp/t.log 'TABLETPROBE proximity_out' 3
		expect_log /tmp/t.log 'TABLETPROBE frame' 40
		;;
	pointer)
		# A client without the tablet: the pen as its pointer.
		guest 'export XDG_RUNTIME_DIR=/tmp; /bin/tablet-probe --pointer --timeout-s=60 --token=p > /tmp/p.log 2>&1 </dev/null & sleep 3; peninject /tmp/pointer.pen; echo replay=$?; sleep 1' | tail -1
		shot pointer
		guest 'kill $(ps -A -o pid,args | grep "[t]ablet-probe" | awk "{print \$1}") 2>/dev/null; sleep 1; cat /tmp/p.log' > "$out/pointer.log"
		expect_log /tmp/p.log 'TABLETPROBE ready run=p mode=pointer'
		expect_log /tmp/p.log 'TABLETPROBE pointer motion' 10
		expect_log /tmp/p.log 'TABLETPROBE pointer button=0x110 state=1'
		expect_log /tmp/p.log 'TABLETPROBE pointer button=0x110 state=0'
		expect_log /tmp/p.log 'TABLETPROBE pointer button=0x111 state=1'
		expect_log /tmp/p.log 'TABLETPROBE pointer button=0x111 state=0'
		expect_log /tmp/p.log 'TABLETPROBE pointer button=0x112 state=1'
		expect_log /tmp/p.log 'TABLETPROBE pointer button=0x112 state=0'
		;;
	home)
		# zdesktop's own UI: a tap on the launcher opens App Home; Esc closes it.
		guest 'peninject /tmp/home.pen; echo replay=$?' | tail -1
		shot home
		python3 plan/ws035/tests/qmp-keys.py "$GUEST_RUNTIME/qmp.sock" '<esc>' >/dev/null 2>&1
		sleep 1
		;;
	terminal)
		# A terminal to click on with the pen.
		guest 'export XDG_RUNTIME_DIR=/tmp; cd /; /bin/terminal > /tmp/term.log 2>&1 </dev/null & sleep 4; echo started' >/dev/null
		python3 plan/ws035/tests/qmp-keys.py "$GUEST_RUNTIME/qmp.sock" e c h o '<spc>' k e i '<spc>' p e n '<ret>' >/dev/null 2>&1
		sleep 1
		shot terminal
		;;
	corner)
		# The top-right swipe (ws079-p010) with the pen: it reaches the recogniser as the pointer.
		guest 'grep -a -c "CORNER commit" /tmp/zdesktop.log' | tail -1 > "$out/corner-before.txt"
		guest 'peninject /tmp/corner.pen; echo replay=$?' | tail -1
		shot corner
		expect_log /tmp/zdesktop.log 'CORNER commit via=' $(( $(cat "$out/corner-before.txt" 2>/dev/null || echo 0) + 1 ))
		;;
	select)
		# A drag with the pen tip over a row, then the second barrel button pastes it (BTN_MIDDLE).
		guest 'peninject /tmp/select.pen; echo replay=$?' | tail -1
		sleep 1
		shot select
		;;
	esac
done

# Protocol errors in the compositor or the clients.
guest 'grep -a -E "ERROR|FAILED|protocol error" /tmp/zdesktop.log /tmp/t.log /tmp/p.log 2>/dev/null' | tee "$out/errors.txt"
[ -s "$out/errors.txt" ] && status=1
guest 'grep -a -E "ZWL (INPUT|TABLET)" /tmp/zdesktop.log' > "$out/zdesktop-input.txt"
echo "p003-guest: status=$status"
exit $status
