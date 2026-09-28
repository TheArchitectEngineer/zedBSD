#!/bin/sh
# ws081-p012: PDF Viewer's touch screen on the pen test guest (plan/ws079/tests/config-amd64-pen.mk, the injector
# and touchinject).  The compositor, libkeiland, PDF Viewer and libpdf (with libz-compat and libjpeg-compat) under
# test, and the eight-page test document (make-touch-pdf.py), are copied into the running guest; the output is
# 1280x800 and the touch screen is declared 0..1279 by 0..799, so a finger's numbers are the output's pixels.
#
#  1. The scroll mode: one finger flicks up by 300 px in 100 ms (60 Hz).  PDF Viewer's own log: the drag
#     scrolled, the lift's velocity is a fling's, the view glided on after the lift and came to rest where the
#     scroller's fling stops (a function of the time only, so QEMU's slow frames do not change it).
#  2. Two fingers part from 200 px to 400 px: the zoom starts, grows, and ends at the user's zoom.
#  3. The page mode: one finger flicks left: the swipe turns to page 2.
# Each script waits 2.6 s after declaring its screen before a finger touches: the compositor finds a new evdev node
# by looking now and then, and a finger's events before it opens the node never reach it.
# Pictures of each step go to OUTDIR (and to the WS081 shots with a prefix when one is given).  Nothing reads the
# console; the steps read /tmp/pv.log through SSH.
#
#   VENUS_RENDERER=/home/awe/zedBSD-rpi4/build/ws035-sq-venus/install \
#   GUEST_RUNTIME=$PWD/build/ws081-run plan/ws079/tests/pen-guest.sh start build/amd64/hdd-image.img
#   GUEST_RUNTIME=... plan/ws081/tests/p012-guest.sh BUILD [OUTDIR [PREFIX]]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
export GUEST_RUNTIME="${GUEST_RUNTIME:-$PWD/build/ws081-run}"
build=${1:?usage: p012-guest.sh BUILD [OUTDIR [PREFIX]]}
out=${2:-build/ws081-p012-guest}
prefix=${3:-}
mkdir -p "$out"
guest() { timeout 120 python3 plan/tools/guest/guest.py run "$1" 2>&1; }
put() { timeout 90 python3 plan/tools/guest/guest.py put "$1" "$2" >/dev/null 2>&1 || { echo "put $1: FAILED"; status=1; }; }
shot() {
	python3 plan/ws035/tests/zdesktop-check.py "$out/$1" --runtime "$GUEST_RUNTIME" >/dev/null
	if [ -n "$prefix" ]; then
		cp "$out/$1" "$prefix$1"
	fi
	echo "shot $1"
}
stop_all='for p in $(ps -A -o pid,args | grep -E "[w]ayland( |$)|[p]dfviewer|[t]ouchinject" | awk "{print \$1}"); do kill $p; done; i=0; while ps -A -o args | grep -qE "[w]ayland( |$)|[p]dfviewer" && [ $i -lt 50 ]; do sleep 0.2; i=$((i+1)); done'
stop_viewer='for p in $(ps -A -o pid,args | grep -E "[p]dfviewer" | awk "{print \$1}"); do kill $p; done; sleep 1'
env='export XDG_RUNTIME_DIR=/tmp WAYLAND_DISPLAY=wayland-0;'
status=0

# The programs and the document under test.
python3 plan/ws081/tests/make-touch-pdf.py "$out/touch.pdf"
guest "$stop_all" >/dev/null
put "$build/bin/wayland" /bin/wayland
put "$build/bin/pdfviewer" /bin/pdfviewer
put "$build/dynamic/libkeiland.so" /lib/libkeiland.so
put "$build/dynamic/libpdf.so" /lib/libpdf.so
put "$build/dynamic/libz-compat.so" /lib/libz-compat.so
put "$build/dynamic/libjpeg-compat.so" /lib/libjpeg-compat.so
put "$out/touch.pdf" /tmp/touch.pdf
guest 'chmod 755 /bin/wayland /bin/pdfviewer' >/dev/null
guest 'export XDG_RUNTIME_DIR=/tmp; rm -f /tmp/wayland-0 /tmp/zdesktop.log
/bin/wayland --timeout=600 --width=1280 --height=800 --glass --log-frames > /tmp/zdesktop.log 2>&1 </dev/null &
i=0; while ! grep -q "ZWL MODE" /tmp/zdesktop.log && [ $i -lt 60 ]; do sleep 0.5; i=$((i+1)); done; sleep 2; echo started' >/dev/null

# Starts PDF Viewer on the document with extra arguments, and finds where its window is.
viewer() {
	guest "$stop_viewer" >/dev/null
	guest "$env /bin/pdfviewer --width=1000 --height=700 $1 /tmp/touch.pdf > /tmp/pv.log 2>&1 </dev/null & i=0; while ! grep -q 'PDFVIEWER READY' /tmp/pv.log && [ \$i -lt 60 ]; do sleep 0.5; i=\$((i+1)); done; sleep 2; echo started" >/dev/null
	set -- $(guest "grep 'ZWL MAP client=' /tmp/zdesktop.log | tail -1" | sed -n 's/.* x=\([-0-9]*\) y=\([-0-9]*\).*/\1 \2/p')
	wx=${1:-0}; wy=${2:-0}
	echo "window at $wx,$wy"
}

# Replays a touch script (a file in OUTDIR) in the guest.
replay() {
	put "$out/$1" "/tmp/$1"
	guest "/bin/touchinject /tmp/$1; echo replay=\$?" | grep -q '^replay=0$' || { echo "touchinject $1: FAILED"; status=1; }
}

# 1. A flick in the scroll mode.
viewer ""
shot flick-before.png
x=$((wx + 500)); y=$((wy + 560))
printf 'size 1279 799 2\nwait 2600\ndown 1 %d %d\nwait 100\nswipe 0 -300 6 16.667\nup 1\nhold 3500\n' "$x" "$y" > "$out/flick.script"
replay flick.script
sleep 2
shot flick-after.png
guest "grep -E 'PDFVIEWER (TOUCH|FAILED)' /tmp/pv.log" > "$out/flick-log.txt"
python3 - "$out/flick-log.txt" <<'EOF' || status=1
import math, sys
fields = []
for line in open(sys.argv[1]).read().splitlines():
    fields.append((line, dict(f.split("=", 1) for f in line.split() if "=" in f)))
ok = True
def check(condition, text):
    global ok
    print(("ok: " if condition else "FAILED: ") + text)
    ok = ok and condition
check(not any("FAILED" in line for line, _ in fields), "PDF Viewer did not fail")
check(any("TOUCH drag kind=scroll" in line for line, _ in fields), "the finger's drag scrolled")
release = [f for line, f in fields if "TOUCH release" in line]
rest = [f for line, f in fields if "TOUCH rest" in line]
check(len(release) == 1, "one lift")
check(len(rest) >= 1, "the view came to rest")
if release and rest:
    vy = -float(release[0]["vy"])
    y0 = float(release[0]["y"])
    y1 = float(rest[-1]["y"])
    check(1500.0 < vy < 5000.0, "the lift's velocity is a fling's: %.0f px/s (the finger: 3000)" % vy)
    tau, mu = 0.45, 300.0
    expected = tau * vy - mu * tau * tau * math.log(1.0 + vy / (mu * tau))
    check(y1 - y0 > 200.0, "the view glided on after the lift: %.0f px" % (y1 - y0))
    check(abs(y1 - y0 - expected) < 2.0, "it rested where a %.0f px/s fling stops: %.1f px (%.1f)" % (vy, y1 - y0, expected))
sys.exit(0 if ok else 1)
EOF

# 2. Two fingers part: the zoom.
x=$((wx + 500)); y=$((wy + 350))
{
	printf 'size 1279 799 2\nwait 2600\ndown 1 %d %d; down 2 %d %d\nwait 100\n' $((x - 100)) "$y" $((x + 100)) "$y"
	k=1
	while [ $k -le 20 ]; do
		printf 'move 1 %d %d; move 2 %d %d\nwait 16.667\n' $((x - 100 - 5 * k)) "$y" $((x + 100 + 5 * k)) "$y"
		k=$((k + 1))
	done
	printf 'hold 150\nup 1; up 2\nhold 1500\n'
} > "$out/pinch.script"
replay pinch.script
sleep 3
shot pinch-after.png
guest "grep -E 'PDFVIEWER (TOUCH|FAILED)' /tmp/pv.log" > "$out/pinch-log.txt"
python3 - "$out/pinch-log.txt" <<'EOF' || status=1
import sys
lines = open(sys.argv[1]).read().splitlines()
ok = True
def check(condition, text):
    global ok
    print(("ok: " if condition else "FAILED: ") + text)
    ok = ok and condition
start = [l for l in lines if "TOUCH pinch start" in l]
end = [l for l in lines if "TOUCH pinch end" in l]
check(len(start) == 1 and len(end) == 1, "one zoom by two fingers")
if start and end:
    a = float(start[0].split("scale=")[1])
    b = float(end[0].split("scale=")[1])
    check(1.6 < b / a < 2.1, "the fingers' distance doubled, the scale by %.2f (%.3f to %.3f)" % (b / a, a, b))
sys.exit(0 if ok else 1)
EOF

# 3. The page mode: a flick to the left turns the page.
viewer "--mode=page"
x=$((wx + 700)); y=$((wy + 350))
printf 'size 1279 799 2\nwait 2600\ndown 1 %d %d\nwait 50\nswipe -200 0 5 16.667\nup 1\nhold 1500\n' "$x" "$y" > "$out/swipe.script"
replay swipe.script
sleep 2
shot swipe-after.png
guest "grep -E 'PDFVIEWER (TOUCH|SWIPE|PAGE|FAILED)' /tmp/pv.log" > "$out/swipe-log.txt"
python3 - "$out/swipe-log.txt" <<'EOF' || status=1
import sys
lines = open(sys.argv[1]).read().splitlines()
ok = True
def check(condition, text):
    global ok
    print(("ok: " if condition else "FAILED: ") + text)
    ok = ok and condition
check(any("TOUCH drag kind=swipe" in l for l in lines), "the finger swiped")
check(any("SWIPE" in l and "direction=1" in l for l in lines), "the swipe turned forward")
check(any("PAGE shown=1" in l for l in lines), "page 2 is shown")
sys.exit(0 if ok else 1)
EOF

# The compositor's errors, and everything stops.
guest "grep -cE 'ERROR|FAILED' /tmp/zdesktop.log" | tail -1 > "$out/errors.txt"
guest "$stop_all" >/dev/null
[ "$(cat "$out/errors.txt")" = 0 ] || { echo "compositor errors: $(cat "$out/errors.txt")"; status=1; }

[ $status -eq 0 ] && echo "p012: PASS" || echo "p012: FAIL"
exit $status
