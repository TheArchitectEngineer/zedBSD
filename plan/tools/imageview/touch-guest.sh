#!/bin/sh
# ws091-p003: Image Viewer's touch screen on the pen test guest (the image of plan/ws079/tests/config-amd64-pen.mk:
# a kernel with /dev/input-inject and touchinject; main's build/main-pen/hdd-image.img copied, never built here).
# The compositor, libkeiland, Image Viewer and its libraries from BIN and the pictures of make-images.py are copied
# into the running guest; the output is 1280x800 and the touch screen is declared 0..1279 by 0..799, so a finger's
# numbers are the output's pixels.
#  1. Two fingers part from 200 px to 400 px over the fitted 4032x2268 photo: the zoom starts and ends about
#     twice the scale it started at.
#  2. One finger flicks up across the zoomed photo: the drag scrolls, the lift is a fling's, the view glides on
#     after the lift and comes to rest.
#  3. A double tap goes back to the fit; another zooms in again about the tap.
#  4. After the fit (key 0), one finger flicks left across the fitted photo: the swipe turns to the next image.
#  5. A long press opens the context menu.
# Each script waits 2.6 s after declaring its screen before a finger touches (the compositor looks for a new evdev
# node now and then).  The steps read the program's own log (/tmp/iv.log) through SSH; nothing reads the console.
#
#   GUEST_RUNTIME=$PWD/build/ws091-pen-run plan/ws035/tests/zdesktop-guest.sh start build/ws091-pen-run/base.img
#   GUEST_RUNTIME=$PWD/build/ws091-pen-run BIN=build/ws091-amd64 plan/tools/imageview/touch-guest.sh OUTDIR
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
export GUEST_RUNTIME="${GUEST_RUNTIME:-$PWD/build/ws091-pen-run}"
bin=${BIN:-build/ws091-amd64}
out=${1:-build/ws091-shots/touch}
mkdir -p "$out"
status=0
guest() { timeout 120 python3 plan/tools/guest/guest.py run "$1" 2>&1 </dev/null; }
put() { timeout 120 python3 plan/tools/guest/guest.py put "$1" "$2" >/dev/null 2>&1 </dev/null || { echo "put $1: FAILED"; status=1; }; }
keys() { python3 plan/ws035/tests/qmp-keys.py "$GUEST_RUNTIME/qmp.sock" "$@" >/dev/null; }
shot() {
	python3 plan/ws035/tests/zdesktop-check.py "$out/$1" --runtime "$GUEST_RUNTIME" >/dev/null
	echo "shot $1"
}
stop_all='for p in $(ps -A -o pid,args | grep -E "[w]ayland( |$)|[i]mageview|[t]ouchinject" | awk "{print \$1}"); do kill $p; done; i=0; while ps -A -o args | grep -qE "[w]ayland( |$)|[i]mageview" && [ $i -lt 50 ]; do sleep 0.2; i=$((i+1)); done'
stop_viewer='for p in $(ps -A -o pid,args | grep -E "[i]mageview" | awk "{print \$1}"); do kill $p; done; sleep 1'
env='export XDG_RUNTIME_DIR=/tmp WAYLAND_DISPLAY=wayland-0 HOME=/tmp;'

# Checks the log lines of a step (a file in OUTDIR) with a Python snippet reading "lines"; a failure fails the run.
check() {
	python3 - "$out/$1" "$2" <<'EOF' || status=1
import sys
lines = open(sys.argv[1]).read().splitlines()
ok = True
def expect(condition, text):
    global ok
    print(("ok: " if condition else "FAILED: ") + text)
    ok = ok and condition
exec(sys.argv[2])
sys.exit(0 if ok else 1)
EOF
}

# Replays a touch script (a file in OUTDIR) in the guest; without the injector's replay=0 the step fails.
replay() {
	put "$out/$1" "/tmp/$1"
	result=$(guest "/bin/touchinject /tmp/$1 2>&1; echo replay=\$?")
	if ! printf '%s\n' "$result" | grep -q '^replay=0$'; then
		printf '%s\n' "$result" > "$out/$1.failed.log"
		echo "touchinject $1: FAILED (output in $out/$1.failed.log)"
		status=1
	fi
}

# Starts Image Viewer on a picture and finds where its window is.
viewer() {
	guest "$stop_viewer" >/dev/null
	guest "$env /bin/imageview --width=1180 --height=700 $1 > /tmp/iv.log 2>&1 </dev/null & i=0; while ! grep -q 'IMAGEVIEW READY' /tmp/iv.log && [ \$i -lt 60 ]; do sleep 0.5; i=\$((i+1)); done; sleep 2; echo started" >/dev/null
	set -- $(guest "grep 'ZWL MAP client=' /tmp/zdesktop.log | tail -1" | sed -n 's/.* x=\([-0-9]*\) y=\([-0-9]*\).*/\1 \2/p')
	wx=${1:-0}; wy=${2:-0}
	echo "window at $wx,$wy"
}

# The programs, the libraries and the pictures under test, and the compositor.
python3 plan/tools/imageview/make-images.py build/ws091-images >/dev/null
guest "$stop_all" >/dev/null
put "$bin/bin/wayland" /bin/wayland
put "$bin/bin/imageview" /bin/imageview
for library in libkeiland libvulkan libwayland-client libtruetype libpng-compat libjpeg-compat libgif-compat libz-compat; do
	put "$bin/dynamic/$library.so" "/lib/$library.so"
done
guest 'mkdir -p /tmp/pics' >/dev/null
for picture in build/ws091-images/*; do
	put "$picture" "/tmp/pics/$(basename "$picture")"
done
guest 'chmod 755 /bin/wayland /bin/imageview /bin/touchinject' >/dev/null
guest 'export XDG_RUNTIME_DIR=/tmp; rm -f /tmp/wayland-0 /tmp/zdesktop.log
/bin/wayland --timeout=900 --width=1280 --height=800 --glass > /tmp/zdesktop.log 2>&1 </dev/null &
i=0; while ! grep -q "ZWL MODE" /tmp/zdesktop.log && [ $i -lt 60 ]; do sleep 0.5; i=$((i+1)); done; sleep 2; echo started' >/dev/null

# 1. Two fingers part over the fitted photo: the zoom.
viewer /tmp/pics/02-landscape.jpg
shot touch-fit.png
x=$((wx + 590)); y=$((wy + 350))
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
sleep 2
shot touch-pinch.png
guest "grep -E 'IMAGEVIEW (TOUCH|FAILED)' /tmp/iv.log" > "$out/pinch-log.txt"
check pinch-log.txt '
start = [l for l in lines if "TOUCH pinch start" in l]
end = [l for l in lines if "TOUCH pinch end" in l]
expect(not any("FAILED" in l for l in lines), "Image Viewer did not fail")
expect(len(start) == 1 and len(end) == 1, "one zoom by two fingers")
if start and end:
    a = float(start[0].split("scale=")[1])
    b = float(end[0].split("scale=")[1])
    expect(1.6 < b / a < 2.1, "the fingers distance doubled, the scale by %.2f (%.3f to %.3f)" % (b / a, a, b))
'

# 2. One finger flicks up across the zoomed photo: the drag, the fling and the rest.
x=$((wx + 590)); y=$((wy + 520))
printf 'size 1279 799 2\nwait 2600\ndown 1 %d %d\nwait 100\nswipe 0 -300 6 16.667\nup 1\nhold 3500\n' "$x" "$y" > "$out/flick.script"
replay flick.script
sleep 2
shot touch-flick.png
guest "grep -E 'IMAGEVIEW (TOUCH|FAILED)' /tmp/iv.log" > "$out/flick-log.txt"
check flick-log.txt '
release = [dict(f.split("=", 1) for f in l.split() if "=" in f) for l in lines if "TOUCH release" in l]
rest = [dict(f.split("=", 1) for f in l.split() if "=" in f) for l in lines if "TOUCH rest" in l]
expect(any("TOUCH drag kind=scroll" in l for l in lines), "the finger dragged the zoomed image")
expect(len(release) == 1, "one lift")
expect(len(rest) >= 1, "the view came to rest")
if release and rest:
    vy = -float(release[0]["vy"])
    y0 = float(release[0]["y"])
    y1 = float(rest[-1]["y"])
    expect(1500.0 < vy < 5000.0, "the lift velocity is a fling: %.0f px/s (the finger: 3000)" % vy)
    expect(y1 - y0 > 50.0, "the view glided on after the lift: %.0f px" % (y1 - y0))
'

# 3. A double tap back to the fit, and another zooming in about the tap.  The zooms are animated, so the targets are
#    read from the ZOOM lines the taps log after this step's start.
x=$((wx + 590)); y=$((wy + 350))
printf 'size 1279 799 2\nwait 2600\ndown 1 %d %d\nwait 60\nup 1\nwait 120\ndown 1 %d %d\nwait 60\nup 1\nhold 1200\ndown 1 %d %d\nwait 60\nup 1\nwait 120\ndown 1 %d %d\nwait 60\nup 1\nhold 1200\n' \
	"$x" "$y" "$x" "$y" "$x" "$y" "$x" "$y" > "$out/double-tap.script"
before=$(guest "wc -l < /tmp/iv.log" | tail -1 | tr -d ' ')
replay double-tap.script
sleep 1
shot touch-double-tap.png
guest "tail -n +$((before + 1)) /tmp/iv.log | grep -E 'IMAGEVIEW (TOUCH double-tap|ZOOM|FAILED)'" > "$out/double-tap-log.txt"
check double-tap-log.txt '
taps = [l for l in lines if "TOUCH double-tap" in l]
zooms = [float(l.split("scale=")[1].split()[0]) for l in lines if "IMAGEVIEW ZOOM" in l]
expect(len(taps) == 2, "two double taps")
expect(len(zooms) == 2, "two zooms")
if len(zooms) == 2:
    expect(zooms[0] < 0.3, "the first went back to the fit (%.3f)" % zooms[0])
    expect(zooms[1] >= 0.99, "the second zoomed in to 100 %% or more (%.3f)" % zooms[1])
'

# 4. The fit (key 0), then a flick to the left turns to the next image.
keys '0'
sleep 1
x=$((wx + 800)); y=$((wy + 350))
printf 'size 1279 799 2\nwait 2600\ndown 1 %d %d\nwait 50\nswipe -300 0 5 16.667\nup 1\nhold 1500\n' "$x" "$y" > "$out/swipe.script"
replay swipe.script
sleep 2
shot touch-swipe.png
guest "grep -E 'IMAGEVIEW (TOUCH|SWIPE|SHOW|FAILED)' /tmp/iv.log" > "$out/swipe-log.txt"
check swipe-log.txt '
expect(any("TOUCH drag kind=swipe" in l for l in lines), "the finger swiped")
expect(any("SWIPE turn direction=1" in l for l in lines), "the swipe turned forward")
expect(any("SHOW path=/tmp/pics/03-portrait.jpg index=2" in l for l in lines), "the next image is shown")
'

# 5. A long press opens the context menu.
x=$((wx + 590)); y=$((wy + 350))
printf 'size 1279 799 2\nwait 2600\ndown 1 %d %d\nhold 900\nup 1\nhold 800\n' "$x" "$y" > "$out/long-press.script"
replay long-press.script
sleep 1
shot touch-long-press.png
guest "grep -E 'IMAGEVIEW (TOUCH long-press|CONTEXT-MENU|FAILED)' /tmp/iv.log" > "$out/long-press-log.txt"
check long-press-log.txt '
expect(any("TOUCH long-press" in l for l in lines), "the long press was found")
expect(any("CONTEXT-MENU open" in l for l in lines), "the context menu opened")
'
keys '<esc>'

# The compositor's errors, and everything stops.
guest "grep -cE 'ERROR|FAILED' /tmp/zdesktop.log" | tail -1 > "$out/errors.txt"
guest "$stop_all" >/dev/null
[ "$(cat "$out/errors.txt")" = 0 ] || { echo "compositor errors: $(cat "$out/errors.txt")"; status=1; }

[ $status -eq 0 ] && echo "touch-guest: PASS" || echo "touch-guest: FAIL"
exit $status
