#!/bin/sh
# ws079-p005: Notes v1 on the Venus guest (the lean image with Notes, plan/ws079/tests/build-notes-image.sh).
# zdesktop --glass at 1280x800 and /bin/notes --fullscreen /tmp/notes-test/test.pdf, driven by QMP:
#  1. strokes.png: two pen strokes drawn by pointer drags (black, then blue from the toolbar's colour), a
#     highlighter stroke (key M) over the first (NOTES STROKE ... strokes=1..3).
#  2. undo.png: Ctrl+Z takes the highlighter back (NOTES UNDO strokes=2), Ctrl+Y makes it again (NOTES REDO strokes=3).
#  3. erase.png: the eraser (key E) dragged across the blue stroke removes it (NOTES ERASE removed=1); Ctrl+Z
#     brings it back.
#  4. page2.png: Ctrl+N adds a page (NOTES PAGE current=1 count=2 new), a stroke on it; Page Up goes back to page 1.
#  5. Ctrl+S saves (NOTES SAVE reason=request pages=2 strokes=4); the PDF is copied out and checked on the
#     host with qpdf --check, the attachment and pdftoppm (OUTDIR/test-1.png, test-2.png).
#  6. autosave: one more stroke and 7 s of stillness save it on their own (NOTES SAVE reason=autosave strokes=5).
#  7. journal: a stroke, and Notes is killed (SIGKILL) at once; a new Notes on the same file recovers it from
#     the journal (NOTES RECOVER ... strokes=6), recovered.png; the recovered notebook is saved (by the autosave
#     5 s later, or when Ctrl+W closes it) and nothing is left unsaved (NOTES EXIT ... dirty=0), no journal left.
#  8. reopened.png: Notes opens the saved PDF again from its edit data (NOTES OPEN pages=2 strokes=6), a stroke
#     and Ctrl+S save it (strokes=7); final.pdf is copied out.
#
#   GUEST_RUNTIME=build/ws079-p005-run plan/ws035/tests/zdesktop-guest.sh start build/amd64/hdd-image.img
#   plan/ws079/tests/notes-p005.sh [OUTDIR] [SHOTS PREFIX]
# NOTES_BINARY=build/amd64/bin/notes copies a newer build into the running guest first.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
GUEST_RUNTIME="${GUEST_RUNTIME:-$(pwd)/build/ws079-p005-run}"
export GUEST_RUNTIME
out=${1:-build/ws079-p005}
prefix=${2:-}
mkdir -p "$out"
guest() { timeout 90 python3 plan/tools/guest/guest.py run "$1" 2>&1 </dev/null; }
check() { python3 plan/ws035/tests/zdesktop-check.py "$@" --runtime "$GUEST_RUNTIME"; }
pointer() { python3 plan/ws035/tests/qmp-pointer.py "$GUEST_RUNTIME/qmp.sock" "$@"; }
keys() { python3 plan/ws035/tests/qmp-keys.py "$GUEST_RUNTIME/qmp.sock" "$@"; sleep 0.8; }
stop_all='for p in $(ps -A -o pid,args | grep -E "[w]ayland( |$)|/bin/[n]otes" | awk "{print \$1}"); do kill $p; done; i=0; while ps -A -o args | grep -qE "[w]ayland( |$)|/bin/[n]otes" && [ $i -lt 50 ]; do sleep 0.2; i=$((i+1)); done'
start_notes='export XDG_RUNTIME_DIR=/tmp; /bin/notes --fullscreen /tmp/notes-test/test.pdf >> /tmp/notes.log 2>&1 </dev/null & sleep 4; echo started'
status=0

# Fails the run unless a log has a line matching a pattern (within a few seconds).
expect_log() {
	tries=0
	found=0
	while [ $tries -lt 8 ]; do
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

# A drag along a path given as page points "x,y x,y ..." (converted with the page's place and scale).
drag() {
	steps=$(python3 - "$px" "$py" "$scale" "$@" <<'EOF'
import sys
px, py, scale = float(sys.argv[1]), float(sys.argv[2]), float(sys.argv[3])
points = [tuple(float(v) for v in p.split(",")) for p in sys.argv[4:]]
words = []
for index, (x, y) in enumerate(points):
    words += ["move", str(int(px + x * scale)), str(int(py + y * scale)), "sleep", "20"]
    if index == 0:
        words += ["down", "sleep", "40"]
words += ["up", "sleep", "300"]
print(" ".join(words))
EOF
)
	pointer $steps
}

# A wave from (x0, y) to (x1, y), in page points.
wave() {
	python3 -c "
import math, sys
x0, x1, y, amp = $1, $2, $3, $4
print(' '.join('%.1f,%.1f' % (x0 + (x1 - x0) * i / 40.0, y + amp * math.sin(i / 40.0 * 4 * math.pi)) for i in range(41)))"
}

# The middle of a toolbar button of an action, from the NOTES BUTTONS line.
button() {
	guest "grep 'NOTES BUTTONS' /tmp/notes.log | tail -1" | tr ' ' '\n' | sed -n "s/^$1:\([0-9]*\),\([0-9]*\),\([0-9]*\),\([0-9]*\)$/\1 \2 \3 \4/p" | head -1 |
	    awk '{print int($1 + $3 / 2), int($2 + $4 / 2)}'
}

guest "$stop_all" >/dev/null
[ -n "${NOTES_BINARY:-}" ] && timeout 60 python3 plan/tools/guest/guest.py put "$NOTES_BINARY" /bin/notes >/dev/null 2>&1 </dev/null
guest 'rm -rf /tmp/notes-test /tmp/notes.log /root/.local/share/keiland/notes; mkdir -p /tmp/notes-test' >/dev/null
guest 'export XDG_RUNTIME_DIR=/tmp; rm -f /tmp/wayland-0; picture=; [ -f /usr/share/keiland/wallpaper.ppm ] && picture=--wallpaper=/usr/share/keiland/wallpaper.ppm
/bin/wayland --timeout=900 --width=1280 --height=800 --glass $picture > /tmp/zdesktop.log 2>&1 </dev/null & sleep 4; echo started' >/dev/null
guest "$start_notes" >/dev/null
expect_log /tmp/notes.log 'NOTES START width=1280 height=800 fullscreen=1'
set -- $(guest "grep 'NOTES LAYOUT' /tmp/notes.log | tail -1" | sed -n 's/.* page=\([0-9]*\),\([0-9]*\),[0-9]*,[0-9]* scale=\([0-9.]*\).*/\1 \2 \3/p')
px=${1:-387}; py=${2:-68}; scale=${3:-0.85}
echo "page at $px,$py scale $scale"
pointer move 1270 790 sleep 300
shot start.png

# 1. Two pen strokes and a highlighter.
drag $(wave 80 520 150 25)
expect_log /tmp/notes.log 'NOTES STROKE page=0 id=1 tool=0 points=[0-9]+ strokes=1'
set -- $(button 21)
pointer move "$1" "$2" sleep 100 down sleep 60 up sleep 300
drag 80,260 200,300 320,280 440,330 520,300
expect_log /tmp/notes.log 'NOTES STROKE page=0 id=2 tool=0 points=[0-9]+ strokes=2'
keys m
drag 80,150 300,150 520,150
expect_log /tmp/notes.log 'NOTES STROKE page=0 id=3 tool=1 points=[0-9]+ strokes=3'
pointer move 1270 790 sleep 300
shot strokes.png

# 2. Undo and redo.
keys '<ctrl-z>'
expect_log /tmp/notes.log 'NOTES UNDO page=0 strokes=2'
shot undo.png
keys '<ctrl-y>'
expect_log /tmp/notes.log 'NOTES REDO page=0 strokes=3'

# 3. The eraser across the blue stroke, and undo.
keys e
drag 300,230 300,290 300,350
expect_log /tmp/notes.log 'NOTES ERASE page=0 removed=1 strokes=2'
shot erase.png
keys '<ctrl-z>'
expect_log /tmp/notes.log 'NOTES UNDO page=0 strokes=3'
keys p

# 4. A new page with a stroke, and back.
keys '<ctrl-n>'
expect_log /tmp/notes.log 'NOTES PAGE current=1 count=2 new'
drag $(wave 100 500 400 60)
expect_log /tmp/notes.log 'NOTES STROKE page=1 id=4 tool=0'
pointer move 1270 790 sleep 300
shot page2.png
keys '<pgup>'
expect_log /tmp/notes.log 'NOTES PAGE current=0 count=2'

# 5. Save, and the PDF on the host.
keys '<ctrl-s>'
expect_log /tmp/notes.log 'NOTES SAVE reason=request pages=2 strokes=4'
shot saved.png
timeout 60 python3 plan/tools/guest/guest.py get /tmp/notes-test/test.pdf "$out/test.pdf" >/dev/null 2>&1 </dev/null
if qpdf --check "$out/test.pdf" > "$out/qpdf.txt" 2>&1; then echo "qpdf: ok"; else echo "qpdf: FAIL"; status=1; fi
qpdf --list-attachments "$out/test.pdf"
qpdf --show-attachment=kei-notes.bin "$out/test.pdf" > "$out/edit.bin" && od -An -c -N 12 "$out/edit.bin" | head -1
pdfinfo "$out/test.pdf" | grep -E "Pages|Page size|Producer"
pdftoppm -r 60 -png "$out/test.pdf" "$out/test"
[ -n "$prefix" ] && cp "$out/test-1.png" "${prefix}pdf-page1.png" && cp "$out/test-2.png" "${prefix}pdf-page2.png"

# 6. The autosave after the idle time.
drag 100,600 250,650 400,600 500,700
expect_log /tmp/notes.log 'NOTES STROKE page=0 id=5'
sleep 7
expect_log /tmp/notes.log 'NOTES SAVE reason=autosave pages=2 strokes=5'

# 7. A stroke, Notes killed at once, and the journal's recovery.
drag 100,750 300,720 500,780
expect_log /tmp/notes.log 'NOTES STROKE page=0 id=6'
guest 'for p in $(ps -A -o pid,args | grep "/bin/[n]otes" | awk "{print \$1}"); do kill -9 $p; done; sleep 1; ls -l /root/.local/share/keiland/notes/' | tee "$out/journal-ls.txt"
guest "$start_notes" >/dev/null
expect_log /tmp/notes.log 'NOTES RECOVER records=[0-9]+ pages=2 strokes=6 path=/tmp/notes-test/test.pdf'
pointer move 1270 790 sleep 300
shot recovered.png
keys '<ctrl-w>'
expect_log /tmp/notes.log 'NOTES SAVE reason=(autosave|close) pages=2 strokes=6'
expect_log /tmp/notes.log 'NOTES EXIT pages=2 strokes=6 dirty=0'
guest 'ls /root/.local/share/keiland/notes/ 2>/dev/null | wc -l' | tail -1 | sed 's/^/journals left: /'

# 8. The saved PDF opens again from its edit data (no journal is left), and editing goes on.
guest "$start_notes" >/dev/null
expect_log /tmp/notes.log 'NOTES OPEN pages=2 strokes=6 path=/tmp/notes-test/test.pdf'
pointer move 1270 790 sleep 300
shot reopened.png
drag 120,400 250,380 380,420 480,380
expect_log /tmp/notes.log 'NOTES STROKE page=0 id=7'
keys '<ctrl-s>'
expect_log /tmp/notes.log 'NOTES SAVE reason=request pages=2 strokes=7'
keys '<ctrl-w>'
expect_log /tmp/notes.log 'NOTES EXIT pages=2 strokes=7 dirty=0'

errors=$(guest "grep -c ERROR /tmp/zdesktop.log" | tail -1)
[ "${errors:-1}" = 0 ] && echo "zdesktop: no ERROR" || { echo "zdesktop: ERROR lines"; status=1; }
guest 'cat /tmp/notes.log' > "$out/notes.log"
timeout 60 python3 plan/tools/guest/guest.py get /tmp/notes-test/test.pdf "$out/final.pdf" >/dev/null 2>&1 </dev/null
guest "$stop_all" >/dev/null
[ $status = 0 ] && echo "notes-p005: PASS" || echo "notes-p005: FAIL"
exit $status
