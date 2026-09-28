#!/bin/sh
# ws079-p011: Notes' finish on the Venus guest (the image of plan/ws079/tests/build-notes-image.sh): the Kei look,
# the page's picture, the pen's mark and the eraser of parts.  zdesktop --glass at 1280x800 and
# /bin/notes --fullscreen /tmp/notes-p011/demo.pdf; the pen is replayed by peninject (/dev/input-inject):
#  1. The word "Kei" written with the pen in the bold width (pressure rising and falling in each stroke), a blue wave under it,
#     a yellow marker over it: each finished stroke is added on top of the page's picture
#     (NOTES PICTURE add strokes=N..N+1).
#  2. demo.png: the fullscreen notebook in the Kei look with the pen held over the page (its mark, NOTES HOVER).
#  3. The eraser chosen twice becomes the eraser of parts (NOTES TOOL 3 parts=1); dragged down across the blue
#     wave it cuts it in two (NOTES ERASE page=0 cut=1), and the page's picture is drawn again from the start
#     (NOTES PICTURE full); erase-parts.png.  Ctrl+Z puts the wave back whole, Ctrl+Y cuts it again.
#  4. Ctrl+S saves; the PDF is checked on the host: qpdf --check, Producer "Kei Notes", the attachment
#     kei-notes.bin (ZNOT), and pdftoppm draws it (OUTDIR/demo-1.png).
#
#   GUEST_RUNTIME=build/ws079-p011-run plan/ws035/tests/zdesktop-guest.sh start build/amd64/hdd-image.img
#   plan/ws079/tests/notes-p011.sh [OUTDIR] [SHOTS PREFIX]
# NOTES_BINARY=build/amd64/bin/notes copies a newer build into the running guest first.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
GUEST_RUNTIME="${GUEST_RUNTIME:-$(pwd)/build/ws079-p011-run}"
export GUEST_RUNTIME
out=${1:-build/ws079-p011}
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

# The middle of a toolbar button of an action, from the NOTES BUTTONS line.
button() {
	guest "grep 'NOTES BUTTONS' /tmp/notes-p011.log | tail -1" | tr ' ' '\n' | sed -n "s/^$1:\([0-9]*\),\([0-9]*\),\([0-9]*\),\([0-9]*\)$/\1 \2 \3 \4/p" | head -1 |
	    awk '{print int($1 + $3 / 2), int($2 + $4 / 2)}'
}

# A click on a toolbar button of an action.
press() {
	set -- $(button "$1")
	pointer move "$1" "$2" sleep 100 down sleep 60 up sleep 300
}

# The pen scripts (the pen is declared 21600 x 13500 over the 1280 x 800 output, as in notes-pen.sh).
python3 - "$out" <<'EOF'
import math, sys
out = sys.argv[1]
def raw(x, y):
    return round(x * 21600 / 1279), round(y * 13500 / 799)
def stroke(points, lines, light=900, heavy=3600):
    # A stroke through the points, a sample every 3 pixels, the pressure rising and falling along it.
    samples = []
    for (x0, y0), (x1, y1) in zip(points, points[1:]):
        steps = max(1, int(math.hypot(x1 - x0, y1 - y0) / 3))
        for step in range(steps):
            samples.append((x0 + (x1 - x0) * step / steps, y0 + (y1 - y0) * step / steps))
    samples.append(points[-1])
    x, y = raw(*samples[0])
    lines += ["hover %d %d 10 -20" % (x, y), "wait 60", "down %d %d %d 10 -20" % (x, y, light)]
    for index, (px, py) in enumerate(samples[1:], 1):
        share = index / (len(samples) - 1)
        x, y = raw(px, py)
        lines += ["move %d %d %d 10 -20" % (x, y, round(light + (heavy - light) * math.sin(share * math.pi))), "wait 8"]
    lines += ["up", "wait 250"]
def curve(cx, cy, rx, ry, start, end, count=24):
    return [(cx + rx * math.cos(start + (end - start) * i / count), cy + ry * math.sin(start + (end - start) * i / count)) for i in range(count + 1)]
def write(name, strokes):
    lines = ["size 21600 13500", "wait 2500", "tool pen"]
    for points in strokes:
        stroke(points, lines)
    open("%s/%s.pen" % (out, name), "w").write("\n".join(lines) + "\n")
# "Kei": the K's stem and its two arms, the e's bar and bowl, the i's stem and dot.
k = [[(470, 190), (470, 330)], [(545, 190), (474, 262)], [(492, 245), (550, 330)]]
e = [[(585, 280), (650, 280)] + curve(618, 282, 33, 40, 0.0, -2.0 * math.pi * 0.85, 30)]
i = [[(690, 245), (688, 330)], [(690, 205), (692, 212)]]
write("kei", k + e + i)
write("wave", [[(460 + 300 * s / 40.0, 370 + 12 * math.sin(s / 40.0 * 4 * math.pi)) for s in range(41)]])
write("marker", [[(455, 300), (560, 300), (720, 300)]])
write("note", [[(470 + 8 * s, 470 + 6 * math.sin(s * 0.9)) for s in range(36)], [(470 + 7 * s, 520 + 5 * math.sin(s * 1.1 + 1)) for s in range(40)],
               [(470 + 6 * s, 570 + 6 * math.sin(s * 0.7 + 2)) for s in range(34)]])
hover = ["size 21600 13500", "wait 2500", "tool pen"]
for step in range(6):
    x, y = raw(760 + 6 * step, 610 + 3 * step)
    hover += ["hover %d %d" % (x, y), "wait 30"]
hover += ["hold 9000", "up", "wait 300"]
open("%s/hover.pen" % out, "w").write("\n".join(hover) + "\n")
EOF

guest "$stop_all" >/dev/null
[ -n "${NOTES_BINARY:-}" ] && timeout 60 python3 plan/tools/guest/guest.py put "$NOTES_BINARY" /bin/notes >/dev/null 2>&1 </dev/null
for script in kei wave marker note hover; do
	timeout 60 python3 plan/tools/guest/guest.py put "$out/$script.pen" "/tmp/$script.pen" >/dev/null 2>&1 </dev/null
done
guest 'rm -rf /tmp/notes-p011 /tmp/notes-p011.log /root/.local/share/keiland/notes; mkdir -p /tmp/notes-p011' >/dev/null
guest 'export XDG_RUNTIME_DIR=/tmp; rm -f /tmp/wayland-0; picture=; [ -f /usr/share/keiland/wallpaper.ppm ] && picture=--wallpaper=/usr/share/keiland/wallpaper.ppm
/bin/wayland --timeout=900 --width=1280 --height=800 --glass $picture > /tmp/zdesktop.log 2>&1 </dev/null & sleep 4; echo started' >/dev/null
guest 'export XDG_RUNTIME_DIR=/tmp; /bin/notes --fullscreen /tmp/notes-p011/demo.pdf > /tmp/notes-p011.log 2>&1 </dev/null & sleep 4; echo started' >/dev/null
expect_log /tmp/notes-p011.log 'NOTES START width=1280 height=800 fullscreen=1'
expect_log /tmp/notes-p011.log 'NOTES TABLET seat'
expect_log /tmp/notes-p011.log 'NOTES PICTURE full strokes=0..0'

# 1. "Kei" in black with the bold width, a blue wave under it, a yellow marker over it, and a few lines of notes.
press 32
guest 'timeout 60 /bin/peninject /tmp/kei.pen; echo peninject=$?' | tail -1
expect_log /tmp/notes-p011.log 'NOTES STROKE page=0 id=6 tool=0'
expect_log /tmp/notes-p011.log 'NOTES PICTURE add strokes=5..6'
press 31
press 21
guest 'timeout 60 /bin/peninject /tmp/wave.pen; echo peninject=$?' | tail -1
expect_log /tmp/notes-p011.log 'NOTES STROKE page=0 id=7 tool=0'
keys m
press 20
guest 'timeout 60 /bin/peninject /tmp/marker.pen; echo peninject=$?' | tail -1
expect_log /tmp/notes-p011.log 'NOTES STROKE page=0 id=8 tool=1'
keys p
press 20
guest 'timeout 60 /bin/peninject /tmp/note.pen; echo peninject=$?' | tail -1
expect_log /tmp/notes-p011.log 'NOTES STROKE page=0 id=11 tool=0'
expect_log /tmp/notes-p011.log 'NOTES PICTURE add strokes=10..11'

# 2. The pen over the page, and the fullscreen notebook.
pointer move 1270 790 sleep 300
guest 'timeout 60 /bin/peninject /tmp/hover.pen > /tmp/hover.out 2>&1 </dev/null & echo started' >/dev/null
sleep 5
expect_log /tmp/notes-p011.log 'NOTES HOVER source=1'
shot demo.png
sleep 6

# 3. The eraser of parts across the wave, undo and redo.
keys e
keys e
expect_log /tmp/notes-p011.log 'NOTES TOOL 3 parts=1'
pointer move 600 340 sleep 60 down sleep 40 move 600 360 sleep 30 move 601 375 sleep 30 move 601 390 sleep 30 move 602 405 sleep 30 up sleep 400
expect_log /tmp/notes-p011.log 'NOTES ERASE page=0 cut=1 strokes=12'
expect_log /tmp/notes-p011.log 'NOTES PICTURE full strokes=0..12'
pointer move 1270 790 sleep 300
shot erase-parts.png
keys '<ctrl-z>'
expect_log /tmp/notes-p011.log 'NOTES UNDO page=0 strokes=11'
keys '<ctrl-y>'
expect_log /tmp/notes-p011.log 'NOTES REDO page=0 strokes=12'

# 4. Save, and the PDF on the host.
keys '<ctrl-s>'
expect_log /tmp/notes-p011.log 'NOTES SAVE reason=request pages=1 strokes=12'
timeout 60 python3 plan/tools/guest/guest.py get /tmp/notes-p011/demo.pdf "$out/demo.pdf" >/dev/null 2>&1 </dev/null
if qpdf --check "$out/demo.pdf" > "$out/qpdf.txt" 2>&1; then echo "qpdf: ok"; else echo "qpdf: FAIL"; status=1; fi
producer=$(pdfinfo "$out/demo.pdf" | sed -n 's/^Producer: *//p')
[ "$producer" = "Kei Notes" ] && echo "producer: $producer" || { echo "producer: '$producer' WRONG"; status=1; }
qpdf --show-attachment=kei-notes.bin "$out/demo.pdf" > "$out/edit.bin" 2>/dev/null
[ "$(head -c 4 "$out/edit.bin")" = ZNOT ] && echo "attachment: kei-notes.bin ZNOT" || { echo "attachment: MISSING"; status=1; }
pdftoppm -r 60 -png -f 1 -l 1 -singlefile "$out/demo.pdf" "$out/demo-1"
[ -n "$prefix" ] && cp "$out/demo-1.png" "${prefix}pdf.png"

errors=$(guest "grep -c ERROR /tmp/zdesktop.log" | tail -1)
[ "${errors:-1}" = 0 ] && echo "zdesktop: no ERROR" || { echo "zdesktop: ERROR lines"; status=1; }
guest 'cat /tmp/notes-p011.log' > "$out/notes.log"
guest "$stop_all" >/dev/null
[ $status = 0 ] && echo "notes-p011: PASS" || echo "notes-p011: FAIL"
exit $status
