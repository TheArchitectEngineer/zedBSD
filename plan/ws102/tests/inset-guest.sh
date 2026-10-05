#!/bin/sh
# ws102-p015: the keyboard inset (keiland_keyboard_inset_v1, libkeiland's default) on the Venus guest of the inset image
# (plan/ws102/tests/build-inset-image.sh: the WS079 demo image with Text Editor).  zdesktop --glass at 1280x800.
#  1. Text Editor on a document of 200 lines ("L001" ...; line 150 is a row of M's), the caret put on line 150 with the
#     keys (Ctrl+End to the empty line 201, then Up 51 times).  before.png.
#  2. The bottom-left corner's swipe opens the QWERTY row: zdesktop tells Text Editor how much of it the row covers
#     (ZWL INSET ... bottom>0 reason=2), and Text Editor, which does nothing of its own, has the caret's line (the M's)
#     in the middle of the part of its text the row leaves: the M row's centre within one line of the middle between
#     the first and the last text row seen above the keyboard.  after.png.
#  3. The row closes (ZWL INSET ... right=0 bottom=0 reason=0).
#  4. wlshm (no libkeiland, no inset) under the keyboard: it keeps drawing, gets no inset, and nothing fails.
# Prints "INSET RESULT ..." and "inset-guest: PASS" or "inset-guest: FAIL".
#
#   plan/ws102/tests/inset-guest.sh IMAGE OUTDIR
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
image=$1
out=$2
mkdir -p "$out"
GUEST_RUNTIME="${GUEST_RUNTIME:-$(pwd)/build/ws102-p015-run}"
export GUEST_RUNTIME
status=0
guest() { timeout 120 python3 plan/tools/guest/guest.py run "$1" 2>&1 </dev/null; }
put() { timeout 120 python3 plan/tools/guest/guest.py put "$1" "$2" >/dev/null 2>&1 </dev/null; }
pointer() { python3 plan/ws035/tests/qmp-pointer.py "$GUEST_RUNTIME/qmp.sock" "$@"; }
keys() { python3 plan/ws035/tests/qmp-keys.py "$GUEST_RUNTIME/qmp.sock" "$@" >/dev/null; }
shot() { pointer move 1275 400 sleep 400 >/dev/null; python3 plan/ws035/tests/zdesktop-check.py "$out/$1" --runtime "$GUEST_RUNTIME" >/dev/null; echo "shot $out/$1"; }
count() { guest "grep -cE '$1' /tmp/zdesktop.log" | tail -1; }

# Waits (up to 15 s) for more than N lines of zdesktop's log matching a pattern.
expect_more() {
	tries=0
	while [ $tries -lt 15 ]; do
		found=$(count "$1")
		[ "${found:-0}" -gt "$2" ] 2>/dev/null && { echo "log: $1 ok"; return 0; }
		tries=$((tries + 1))
		sleep 1
	done
	echo "log: $1 MISSING"
	status=1
	return 1
}

# The guest, the document, zdesktop.
sh plan/ws035/tests/zdesktop-guest.sh stop >/dev/null 2>&1
timeout 180 sh plan/ws035/tests/zdesktop-guest.sh start "$image" >/dev/null 2>&1
tries=0
until guest 'echo up' | grep -q '^up$' || [ $tries -ge 30 ]; do tries=$((tries + 1)); sleep 5; done
python3 - "$out/inset.txt" <<'PY'
import sys
lines = ["M" * 60 if n == 150 else "L%03d" % n for n in range(1, 201)]
open(sys.argv[1], "w").write("\n".join(lines) + "\n")
PY
put "$out/inset.txt" /root/inset.txt
guest 'service stop greeter >/dev/null 2>&1; export XDG_RUNTIME_DIR=/tmp; rm -f /tmp/wayland-0; picture=; [ -f /usr/share/keiland/wallpaper.png ] && picture=--wallpaper=/usr/share/keiland/wallpaper.png
/bin/wayland --testing --timeout=900 --width=1280 --height=800 --glass $picture > /tmp/zdesktop.log 2>&1 </dev/null &
i=0; while ! grep -q "ZWL OSK zone" /tmp/zdesktop.log && [ $i -lt 60 ]; do sleep 0.5; i=$((i+1)); done; sleep 1; echo started' >/dev/null

# 1. Text Editor, the caret on line 150.
guest "export XDG_RUNTIME_DIR=/tmp HOME=/root; /bin/textedit --timeout-s=800 /root/inset.txt > /tmp/te.log 2>&1 </dev/null & sleep 6; echo started" >/dev/null
expect_more 'ZWL INSET create client=' 0
expect_more 'ZWL MAP client=' 0
pointer move 640 400 sleep 200 down sleep 60 up sleep 500 >/dev/null
keys '<ctrl-end>'
n=0
while [ $n -lt 51 ]; do keys '<up>'; n=$((n + 1)); done
sleep 1.5
shot before.png

# 2. The QWERTY row.
insets=$(count 'ZWL INSET client=')
pointer move 6 792 sleep 200 down sleep 80 move 78 721 sleep 60 move 150 650 sleep 120 up sleep 1500 >/dev/null
expect_more 'ZWL OSK open kind=qwerty' 0
expect_more 'ZWL INSET client=[0-9]+ surface=[0-9]+ right=0 bottom=[1-9][0-9]* reason=2' 0
panel_y=$(guest "grep 'ZWL OSK open kind=qwerty' /tmp/zdesktop.log | tail -1" | sed -n 's/.* y=\([0-9]*\) .*/\1/p')
body_y=$(guest "grep 'ZWL MAP client=' /tmp/zdesktop.log | tail -1" | sed -n 's/.* y=\([0-9]*\).*/\1/p')
guest "grep 'ZWL INSET' /tmp/zdesktop.log" | tail -3
sleep 1
shot after.png

# The M row against the text rows seen above the keyboard.
python3 - "$out/after.png" "${panel_y:-800}" "${body_y:-40}" > "$out/verdict.txt" <<'PY'
import sys
from PIL import Image
image = Image.open(sys.argv[1]).convert("L")
panel = int(sys.argv[2])
top = int(sys.argv[3])
w, h = image.size
data = image.load()
# Rows with dark text (the line numbers and the text) between the system bar and the keyboard.
dark = []
for y in range(top + 4, min(panel, h)):
    xs = [x for x in range(0, w, 2) if data[x, y] < 90]
    dark.append((y, len(xs)))
bands = []
start = None
for y, n in dark:
    if n > 0 and start is None:
        start = y
    if n == 0 and start is not None:
        bands.append((start, y - 1))
        start = None
if start is not None:
    bands.append((start, dark[-1][0]))
# Each band's width of dark pixels at its densest row; the M's line is the widest by far.
def width(band):
    return max(n for y, n in dark if band[0] <= y <= band[1])
text = [b for b in bands if b[1] - b[0] >= 4]
if len(text) < 3:
    print("INSET RESULT bands=%d FAIL" % len(text)); sys.exit(1)
m = max(text, key=width)
pitch = sorted(b2[0] - b1[0] for b1, b2 in zip(text, text[1:]))[len(text) // 2 - 1]
first, last = text[0], [b for b in text if b[1] < panel - 2][-1]
middle = ((first[0] + first[1]) / 2 + (last[0] + last[1]) / 2) / 2
centre = (m[0] + m[1]) / 2
ok = abs(centre - middle) <= pitch
print("INSET RESULT m_row=%.0f middle=%.0f first=%d last=%d pitch=%d panel_y=%d off_lines=%.2f %s" % (centre, middle, first[0], last[1], pitch, panel, (centre - middle) / pitch, "ok" if ok else "FAIL"))
sys.exit(0 if ok else 1)
PY
[ $? -eq 0 ] || status=1
cat "$out/verdict.txt"

# 3. The row closes.
pointer move 6 792 sleep 200 down sleep 80 move 78 721 sleep 60 move 150 650 sleep 120 up sleep 1500 >/dev/null
expect_more 'ZWL OSK close kind=qwerty' 0
expect_more 'ZWL INSET client=[0-9]+ surface=[0-9]+ right=0 bottom=0 reason=0' 0

# 4. wlshm under the keyboard: no inset, still drawing.
guest "export XDG_RUNTIME_DIR=/tmp; /bin/wlshm --size=480x320 --frames=100000 > /tmp/wlshm.log 2>&1 </dev/null & sleep 4; echo started" >/dev/null
creates=$(count 'ZWL INSET create')
pointer move 6 792 sleep 200 down sleep 80 move 78 721 sleep 60 move 150 650 sleep 120 up sleep 1500 >/dev/null
expect_more 'ZWL OSK open kind=qwerty' 1
shot wlshm.png
commits1=$(guest 'grep -c . /tmp/wlshm.log' | tail -1)
sleep 2
commits2=$(guest 'grep -c . /tmp/wlshm.log' | tail -1)
alive=$(guest 'ps -A -o args | grep -c "[w]lshm"' | tail -1)
[ "$(count 'ZWL INSET create')" = "$creates" ] && echo "wlshm: no inset ok" || { echo "wlshm: an inset was made FAIL"; status=1; }
[ "${alive:-0}" -ge 1 ] && echo "wlshm: running ok" || { echo "wlshm: gone FAIL"; status=1; }
echo "wlshm: log lines $commits1 -> $commits2"
pointer move 6 792 sleep 200 down sleep 80 move 78 721 sleep 60 move 150 650 sleep 120 up sleep 1000 >/dev/null

# Nothing failed.
errors=$(count 'ZWL ERROR|protocol error|FAILED')
[ "${errors:-1}" = 0 ] && echo "no errors ok" || { echo "errors: $errors FAIL"; status=1; }
guest 'grep -E "ZWL (INSET|OSK (open|close)|MAP|ERROR)" /tmp/zdesktop.log' > "$out/zdesktop.log"
guest 'cat /tmp/te.log' > "$out/te.log"
sh plan/ws035/tests/zdesktop-guest.sh stop >/dev/null 2>&1
[ $status -eq 0 ] && echo "inset-guest: PASS" || echo "inset-guest: FAIL"
exit $status
