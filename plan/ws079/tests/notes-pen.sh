#!/bin/sh
# ws079-p005: Notes with the pen, through the tablet protocol, on the Venus guest (the lean image with Notes and
# the test pen, plan/ws079/tests/build-notes-image.sh).  zdesktop --glass at 1280x800 and
# /bin/notes --fullscreen /tmp/notes-pen/pen.pdf; the pen is replayed by peninject (/dev/input-inject):
#  1. Notes binds the tablet seat and hears the pen (NOTES TABLET seat, NOTES TABLET tool type=0x140).
#  2. pressure.png: a stroke across the page with the pressure rising from 0 to 4095 and falling back, the pen
#     tilted: one stroke whose samples carry the pressure (NOTES STROKE ... pressure=0..6xxxx tilt=1) and whose
#     width follows it (thin ends, a thick middle).
#  3. A second stroke at a constant light pressure (600 of 4095: pressure=...9602), thinner.
#  The bold width is chosen from the toolbar first, so that the width's change shows.
#  4. eraser.png: the pen's eraser end (tool rubber) dragged across the first stroke removes it
#     (NOTES ERASE removed=1); Ctrl+Z brings it back.
#  5. Ctrl+S saves; pen.pdf is copied out, checked with qpdf and drawn with pdftoppm (OUTDIR/pen-1.png).
#  6. (ws079-p011) hover-pen.png: the pen held over the page without touching shows its mark, a dot of the pen's
#     colour and width (NOTES HOVER source=1); hover-eraser.png: the eraser end held over it shows the eraser's
#     ring (NOTES HOVER source=2); the pen taken away removes the mark (NOTES HOVER gone).
#
#   GUEST_RUNTIME=build/ws079-p005-run plan/ws035/tests/zdesktop-guest.sh start build/amd64/hdd-image.img
#   plan/ws079/tests/notes-pen.sh [OUTDIR] [SHOTS PREFIX]
# NOTES_BINARY=build/amd64/bin/notes copies a newer build into the running guest first.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
GUEST_RUNTIME="${GUEST_RUNTIME:-$(pwd)/build/ws079-p005-run}"
export GUEST_RUNTIME
out=${1:-build/ws079-p005-pen}
prefix=${2:-}
mkdir -p "$out"
guest() { timeout 90 python3 plan/tools/guest/guest.py run "$1" 2>&1 </dev/null; }
check() { python3 plan/ws035/tests/zdesktop-check.py "$@" --runtime "$GUEST_RUNTIME"; }
pointer() { python3 plan/ws035/tests/qmp-pointer.py "$GUEST_RUNTIME/qmp.sock" "$@"; }
keys() { python3 plan/ws035/tests/qmp-keys.py "$GUEST_RUNTIME/qmp.sock" "$@"; sleep 0.8; }
stop_all='for p in $(ps -A -o pid,args | grep -E "[w]ayland( |$)|/bin/[n]otes|[p]eninject" | awk "{print \$1}"); do kill $p; done; i=0; while ps -A -o args | grep -qE "[w]ayland( |$)|/bin/[n]otes" && [ $i -lt 50 ]; do sleep 0.2; i=$((i+1)); done'
status=0

# Fails the run unless a log has a line matching a pattern (within a few seconds).
expect_log() {
	tries=0
	found=0
	while [ $tries -lt 10 ]; do
		found=$(guest "grep -cE '$2' $1" | tail -1)
		[ "${found:-0}" -gt 0 ] 2>/dev/null && break
		tries=$((tries + 1))
		sleep 1
	done
	if [ "${found:-0}" -gt 0 ] 2>/dev/null; then
		echo "log: $2 ok"
	else
		echo "log: $2 MISSING"
		status=1
	fi
}

shot() {
	sleep 0.6
	check "$out/$1" >/dev/null
	[ -n "$prefix" ] && cp "$out/$1" "$prefix$1"
}

# The pen script: the pen is declared 21600 x 13500 over the 1280 x 800 output (the compositor maps the whole
# tablet to the whole output), and a screen pixel (x, y) is (x * 21600 / 1279, y * 13500 / 799).
python3 - "$out/pen.pen" <<'EOF'
import math, sys
def raw(x, y):
    return round(x * 21600 / 1279), round(y * 13500 / 799)
lines = ["size 21600 13500", "wait 3000", "tool pen"]
# 2. A stroke from (450, 300) to (830, 300): the pressure 0 -> 4095 -> 0, the pen tilted.
x, y = raw(450, 300)
lines += ["hover %d %d 20 -15" % (x, y), "wait 200", "down %d %d 0 20 -15" % (x, y)]
steps = 60
for step in range(1, steps + 1):
    x, y = raw(450 + 380 * step / steps, 300 + 30 * math.sin(step / steps * 2 * math.pi))
    pressure = round(4095 * math.sin(step / steps * math.pi))
    lines += ["move %d %d %d 20 -15" % (x, y, pressure), "wait 15"]
lines += ["up", "wait 500"]
# 3. A light stroke at a constant pressure, from (450, 420) to (830, 420).
x, y = raw(450, 420)
lines += ["hover %d %d" % (x, y), "wait 200", "down %d %d 600" % (x, y)]
for step in range(1, 31):
    x, y = raw(450 + 380 * step / 30, 420)
    lines += ["move %d %d 600" % (x, y), "wait 15"]
lines += ["up", "wait 800"]
open(sys.argv[1], "w").write("\n".join(lines) + "\n")
EOF
python3 - "$out/rubber.pen" <<'EOF'
import sys
def raw(x, y):
    return round(x * 21600 / 1279), round(y * 13500 / 799)
lines = ["size 21600 13500", "wait 3000", "tool rubber"]
# 4. The eraser end across the first stroke, from (640, 240) down to (640, 360).
x, y = raw(640, 240)
lines += ["hover %d %d" % (x, y), "wait 200", "down %d %d 2048" % (x, y)]
for step in range(1, 13):
    x, y = raw(640, 240 + 10 * step)
    lines += ["move %d %d 2048" % (x, y), "wait 20"]
lines += ["up", "wait 800"]
open(sys.argv[1], "w").write("\n".join(lines) + "\n")
EOF

python3 - "$out/hover.pen" <<'EOF'
import sys
def raw(x, y):
    return round(x * 21600 / 1279), round(y * 13500 / 799)
lines = ["size 21600 13500", "wait 3000", "tool pen"]
# 6. The pen over (700, 560) without touching, held; then the eraser end over (560, 620), held.
for step in range(6):
    x, y = raw(660 + 8 * step, 540 + 4 * step)
    lines += ["hover %d %d" % (x, y), "wait 30"]
lines += ["hold 8000", "up", "wait 500", "tool rubber"]
for step in range(6):
    x, y = raw(520 + 8 * step, 600 + 4 * step)
    lines += ["hover %d %d" % (x, y), "wait 30"]
lines += ["hold 8000", "up", "wait 500"]
open(sys.argv[1], "w").write("\n".join(lines) + "\n")
EOF

guest "$stop_all" >/dev/null
[ -n "${NOTES_BINARY:-}" ] && timeout 60 python3 plan/tools/guest/guest.py put "$NOTES_BINARY" /bin/notes >/dev/null 2>&1 </dev/null
timeout 60 python3 plan/tools/guest/guest.py put "$out/hover.pen" /tmp/hover.pen >/dev/null 2>&1 </dev/null
timeout 60 python3 plan/tools/guest/guest.py put "$out/pen.pen" /tmp/pen.pen >/dev/null 2>&1 </dev/null
timeout 60 python3 plan/tools/guest/guest.py put "$out/rubber.pen" /tmp/rubber.pen >/dev/null 2>&1 </dev/null
guest 'rm -rf /tmp/notes-pen /tmp/notes-pen.log /root/.local/share/keiland/notes; mkdir -p /tmp/notes-pen' >/dev/null
guest 'export XDG_RUNTIME_DIR=/tmp; rm -f /tmp/wayland-0; picture=; [ -f /usr/share/keiland/wallpaper.png ] && picture=--wallpaper=/usr/share/keiland/wallpaper.png
/bin/wayland --testing --timeout=600 --width=1280 --height=800 --glass $picture > /tmp/zdesktop.log 2>&1 </dev/null & sleep 4; echo started' >/dev/null
guest 'export XDG_RUNTIME_DIR=/tmp; /bin/notes --fullscreen /tmp/notes-pen/pen.pdf > /tmp/notes-pen.log 2>&1 </dev/null & sleep 4; echo started' >/dev/null
expect_log /tmp/notes-pen.log 'NOTES TABLET seat'

# The bold width from the toolbar (its button's place from the NOTES BUTTONS line), which shows the pressure best.
set -- $(guest "grep 'NOTES BUTTONS' /tmp/notes-pen.log | tail -1" | tr ' ' '\n' | sed -n 's/^32:\([0-9]*\),\([0-9]*\),\([0-9]*\),\([0-9]*\)$/\1 \2 \3 \4/p' |
    awk '{print int($1 + $3 / 2), int($2 + $4 / 2)}')
pointer move "${1:-519}" "${2:-26}" sleep 100 down sleep 60 up sleep 300

# 1-3. The pen's strokes.
guest 'timeout 60 /bin/peninject /tmp/pen.pen; echo peninject=$?' | tail -1
expect_log /tmp/notes-pen.log 'NOTES TABLET tool type=0x140 pressure=1 tilt=1'
expect_log /tmp/notes-pen.log 'NOTES STROKE page=0 id=1 tool=0 points=[0-9]+ strokes=1 pressure=[0-9]+\.\.6[0-9]{4} tilt=1'
expect_log /tmp/notes-pen.log 'NOTES STROKE page=0 id=2 tool=0 points=[0-9]+ strokes=2 pressure=[0-9]+\.\.9[0-9]{3} tilt=1'
pointer move 1270 790 sleep 300
shot pressure.png

# 4. The eraser end, and undo.
guest 'timeout 60 /bin/peninject /tmp/rubber.pen; echo peninject=$?' | tail -1
expect_log /tmp/notes-pen.log 'NOTES ERASE page=0 removed=1 strokes=1'
shot eraser.png
keys '<ctrl-z>'
expect_log /tmp/notes-pen.log 'NOTES UNDO page=0 strokes=2'

# 6. The pen's mark while it hovers: the pen's dot, then the eraser's ring, then none.
guest 'timeout 60 /bin/peninject /tmp/hover.pen > /tmp/hover.out 2>&1 </dev/null & echo started' >/dev/null
sleep 5
expect_log /tmp/notes-pen.log 'NOTES HOVER source=1'
shot hover-pen.png
sleep 6
expect_log /tmp/notes-pen.log 'NOTES HOVER source=2'
shot hover-eraser.png
sleep 5
expect_log /tmp/notes-pen.log 'NOTES HOVER gone'

# 5. Save, and the PDF on the host.
keys '<ctrl-s>'
expect_log /tmp/notes-pen.log 'NOTES SAVE reason=request pages=1 strokes=2'
timeout 60 python3 plan/tools/guest/guest.py get /tmp/notes-pen/pen.pdf "$out/pen.pdf" >/dev/null 2>&1 </dev/null
if qpdf --check "$out/pen.pdf" > "$out/qpdf.txt" 2>&1; then echo "qpdf: ok"; else echo "qpdf: FAIL"; status=1; fi
pdftoppm -r 90 -png -f 1 -l 1 -singlefile "$out/pen.pdf" "$out/pen-1"
[ -n "$prefix" ] && cp "$out/pen-1.png" "${prefix}pdf.png"

errors=$(guest "grep -c ERROR /tmp/zdesktop.log" | tail -1)
[ "${errors:-1}" = 0 ] && echo "zdesktop: no ERROR" || { echo "zdesktop: ERROR lines"; status=1; }
guest 'cat /tmp/notes-pen.log' > "$out/notes.log"
guest 'grep -E "TABLET|tablet" /tmp/zdesktop.log | head -40' > "$out/zdesktop-tablet.log"
guest "$stop_all" >/dev/null
[ $status = 0 ] && echo "notes-pen: PASS" || echo "notes-pen: FAIL"
exit $status
