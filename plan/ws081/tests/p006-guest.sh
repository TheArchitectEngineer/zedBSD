#!/bin/sh
# ws081-p006: the browser's fingers on the pen test guest (main's pen image, with the injector and touchinject).
# The compositor, the browser (with libbrowser, libgif-compat and libkeiland) and touchinject under test are copied
# into the running guest (1280x800, the touch screen declared 0..1279 by 0..799, so a finger's numbers are the
# output's pixels).  The browser shows plan/ws081/tests/pages/touch.html (a red block at the top, 60 rows, a blue
# block at the end, on a green canvas; its listeners write mousedown, click and wheel to the console) at 900x640.
# The browser's own log (/tmp/b.log, its ZBROWSER lines) and the compositor's are read through SSH; nothing reads
# the console.
#
#  1. A tap on the page is a click of the primary button (ZBROWSER TOUCH tap, CONSOLE mousedown button=0, click).
#  2. A long press lifted is a click of the secondary button (ZBROWSER TOUCH context, CONSOLE mousedown button=2).
#  3. A flick up scrolls the page and it glides on (TOUCH drag, release, rest further down); the page gets no wheel
#     event from the fingers.
#  4. At the top, a finger pulls the page down and holds: the content stretches past the top (the picture shows the
#     green canvas above the red block, stretched.png), and springs back after the lift (rest scroll=0, the red
#     block at the top again).
# Each script waits 2.6 s after declaring its screen before it touches.  Pictures go to OUTDIR (and PREFIX*).
#
#   GUEST_RUNTIME=... plan/ws081/tests/p006-guest.sh BUILD [OUTDIR [PREFIX]]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
export GUEST_RUNTIME="${GUEST_RUNTIME:-$PWD/build/ws081-run}"
build=${1:?usage: p006-guest.sh BUILD [OUTDIR [PREFIX]]}
out=${2:-build/ws081-p006-guest}
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
stop_all='for p in $(ps -A -o pid,args | grep -E "[w]ayland( |$)|/bin/[b]rowser|[t]ouchinject" | awk "{print \$1}"); do kill $p; done; i=0; while ps -A -o args | grep -qE "[w]ayland( |$)|/bin/[b]rowser" && [ $i -lt 50 ]; do sleep 0.2; i=$((i+1)); done'
status=0

# Runs an injector on a script in the guest.  Without a replay=0 line the step fails, and the SSH command's status
# and whole output are kept in OUTDIR/SCRIPT.failed.log (BUG-099).
inject() {
	put "$out/$2" "/tmp/$2"
	result=$(guest "/bin/$1 /tmp/$2 2>&1; echo replay=\$?")
	rc=$?
	if ! printf '%s\n' "$result" | grep -q '^replay=0$'; then
		{ echo "ssh status: $rc"; printf '%s\n' "$result"; } > "$out/$2.failed.log"
		echo "$1 $2: FAILED (ssh status $rc, output in $out/$2.failed.log)"
		printf '%s\n' "$result" | tail -5 | sed 's/^/  | /'
		status=1
	fi
}

# Replays a touch script made from a body (the lines after the screen's declaration and its wait).
touches() {
	printf 'size 1279 799 2\nwait 2600\n%s\n' "$2" > "$out/$1"
	inject touchinject "$1"
}

# Counts the lines of the browser's log that match a pattern.
count() {
	guest "grep -cE '$1' /tmp/b.log" | tail -1
}

# Fails the run unless the browser's log has a line matching a pattern (within a few seconds).
expect() {
	tries=0
	found=0
	while [ $tries -lt 10 ]; do
		found=$(count "$1")
		[ "${found:-0}" -gt 0 ] 2>/dev/null && break
		tries=$((tries + 1))
		sleep 1
	done
	if [ "${found:-0}" -gt 0 ] 2>/dev/null; then
		echo "ok: $2"
	else
		echo "FAILED: $2 (no line: $1)"
		status=1
	fi
}

# Tells the color of a picture's pixel as "R G B".
pixel() {
	python3 - "$1" "$2" "$3" <<'PY'
import sys
from PIL import Image
image = Image.open(sys.argv[1]).convert("RGB")
print("%d %d %d" % image.getpixel((int(sys.argv[2]), int(sys.argv[3]))))
PY
}

# Fails the run unless a pixel is near a color ("R G B").
color_is() {
	got=$(pixel "$1" "$2" "$3")
	python3 - "$got" "$4" "$5" <<'PY' || status=1
import sys
got = [int(v) for v in sys.argv[1].split()]
want = [int(v) for v in sys.argv[2].split()]
ok = max(abs(a - b) for a, b in zip(got, want)) <= 24
print(("ok: " if ok else "FAILED: ") + "%s (pixel %s, wanted %s)" % (sys.argv[3], sys.argv[1], sys.argv[2]))
sys.exit(0 if ok else 1)
PY
}

# The programs under test, the page, the compositor and the browser.
guest "$stop_all" >/dev/null
put "$build/bin/wayland" /bin/wayland
put "$build/bin/browser" /bin/browser
put "$build/bin/touchinject" /bin/touchinject
put "$build/dynamic/libbrowser.so" /lib/libbrowser.so
put "$build/dynamic/libgif-compat.so" /lib/libgif-compat.so
put "$build/dynamic/libkeiland.so" /lib/libkeiland.so
put "$build/dynamic/libz-compat.so" /lib/libz-compat.so
put "$build/dynamic/libpng-compat.so" /lib/libpng-compat.so
put "$build/dynamic/libjpeg-compat.so" /lib/libjpeg-compat.so
put plan/ws081/tests/pages/touch.html /tmp/touch.html
guest 'chmod 755 /bin/wayland /bin/browser /bin/touchinject; echo made' >/dev/null
guest 'export XDG_RUNTIME_DIR=/tmp; rm -f /tmp/wayland-0 /tmp/zdesktop.log
/bin/wayland --testing --timeout=900 --width=1280 --height=800 --glass > /tmp/zdesktop.log 2>&1 </dev/null &
i=0; while ! grep -q "KWL MODE" /tmp/zdesktop.log && [ $i -lt 60 ]; do sleep 0.5; i=$((i+1)); done; sleep 2; echo started' >/dev/null
guest 'export XDG_RUNTIME_DIR=/tmp WAYLAND_DISPLAY=wayland-0; /bin/browser --width=900 --height=640 --fallback-font=/usr/share/fonts/keiland.ttf /tmp/touch.html > /tmp/b.log 2>&1 </dev/null & i=0; while ! grep -q "ZBROWSER READY" /tmp/b.log && [ $i -lt 60 ]; do sleep 0.5; i=$((i+1)); done; sleep 3; echo started' >/dev/null
expect 'ZBROWSER READY width=900 height=640 document=3960' "the browser shows the page"
set -- $(guest "grep 'KWL MAP client=' /tmp/zdesktop.log | tail -1" | sed -n 's/.* client=\([0-9]*\) surface=[0-9]* x=\([-0-9]*\) y=\([-0-9]*\).*/\1 \2 \3/p')
client=${1:-0}; wx=${2:-0}; wy=${3:-0}
echo "browser client $client at $wx,$wy"
shot start.png
color_is "$out/start.png" $((wx + 450)) $((wy + 20)) "220 20 20" "the red block is at the top"

# 1. A tap on a row.
touches tap.script "$(printf 'down 1 %d %d\nwait 70\nup 1\nhold 800' $((wx + 300)) $((wy + 300)))"
expect 'ZBROWSER TOUCH tap' "a tap on the page"
expect 'ZBROWSER CONSOLE level=0 mousedown button=0' "the page gets the primary button"
expect 'ZBROWSER CONSOLE level=0 click button=0' "and a click"

# 2. A long press, lifted.
touches context.script "$(printf 'down 1 %d %d\nwait 800\nup 1\nhold 800' $((wx + 300)) $((wy + 400)))"
expect 'ZBROWSER TOUCH context' "a long press lifted is the secondary button's click"
expect 'ZBROWSER CONSOLE level=0 mousedown button=2' "the page gets the secondary button"

# 3. A flick up.
touches flick.script "$(printf 'down 1 %d %d\nwait 100\nswipe 0 -150 6 16.667\nup 1\nhold 3000' $((wx + 450)) $((wy + 500)))"
sleep 2
guest "grep -E 'ZBROWSER TOUCH (release|rest)' /tmp/b.log | tail -2" > "$out/flick-log.txt"
python3 - "$out/flick-log.txt" <<'EOF' || status=1
import sys
lines = open(sys.argv[1]).read().splitlines()
fields = [dict(f.split("=", 1) for f in l.split() if "=" in f) for l in lines]
release = [f for l, f in zip(lines, fields) if "release" in l]
rest = [f for l, f in zip(lines, fields) if "rest" in l]
ok = len(release) == 1 and len(rest) == 1
if ok:
    vy = -float(release[0]["vy"])
    glided = float(rest[0]["scroll"]) - float(release[0]["scroll"])
    ok = vy > 1000.0 and glided > 100.0
    print(("ok: " if ok else "FAILED: ") + "the flick's velocity %.0f px/s, the page glided %.0f px after the lift" % (vy, glided))
else:
    print("FAILED: one lift and one rest: %s" % lines)
sys.exit(0 if ok else 1)
EOF
wheels=$(count 'CONSOLE level=0 wheel')
if [ "${wheels:-1}" -eq 0 ] 2>/dev/null; then echo "ok: the fingers gave the page no wheel event"; else echo "FAILED: the page got $wheels wheel events"; status=1; fi
shot flicked.png

# 4. Back to the top by a flick down, then a pull down held past the top: the canvas shows above the red block.
touches top.script "$(printf 'down 1 %d %d\nwait 100\nswipe 0 300 6 16.667\nup 1\nhold 3000' $((wx + 450)) $((wy + 200)))"
touches top2.script "$(printf 'down 1 %d %d\nwait 100\nswipe 0 300 6 16.667\nup 1\nhold 3000' $((wx + 450)) $((wy + 200)))"
touches top3.script "$(printf 'down 1 %d %d\nwait 100\nswipe 0 300 6 16.667\nup 1\nhold 3000' $((wx + 450)) $((wy + 200)))"
expect 'ZBROWSER TOUCH rest scroll=0' "flicks down bring the page to its top"
{
	printf 'size 1279 799 2\nwait 2600\ndown 1 %d %d\nwait 100\nswipe 0 200 20 16.667\nhold 6000\nup 1\nhold 2500\n' $((wx + 450)) $((wy + 150))
} > "$out/stretch.script"
put "$out/stretch.script" /tmp/stretch.script
guest "/bin/touchinject /tmp/stretch.script > /tmp/stretch.out 2>&1 &" >/dev/null
sleep 6
shot stretched.png
color_is "$out/stretched.png" $((wx + 450)) $((wy + 20)) "10 200 30" "held past the top, the canvas shows above the content"
sleep 7
result=$(guest "cat /tmp/stretch.out; echo")
case "$result" in *"done"*) ;; *) echo "FAILED: the stretch's replay: $result"; status=1 ;; esac
expect 'ZBROWSER TOUCH rest scroll=0' "after the lift it rests at the top"
shot sprung.png
color_is "$out/sprung.png" $((wx + 450)) $((wy + 20)) "220 20 20" "and springs back: the red block at the top again"

# The logs, the errors, and everything stops.
guest "grep -E 'ZBROWSER (TOUCH|CONSOLE|ERROR|READY)' /tmp/b.log" > "$out/browser-log.txt"
guest "grep -cE 'ERROR|FAILED' /tmp/zdesktop.log" | tail -1 > "$out/errors.txt"
guest "$stop_all" >/dev/null
[ "$(cat "$out/errors.txt")" = 0 ] || { echo "compositor errors: $(cat "$out/errors.txt")"; status=1; }
if grep -q 'ZBROWSER ERROR' "$out/browser-log.txt"; then echo "FAILED: the browser reported an error"; status=1; fi

[ $status -eq 0 ] && echo "p006: PASS" || echo "p006: FAIL"
exit $status
