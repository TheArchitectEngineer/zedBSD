#!/bin/sh
# ws081-p015: writing with a finger in Notes on the pen test guest (main's pen image, with the injector and
# touchinject).  Notes, libpdf (with libz-compat and libjpeg-compat) and libkeiland under test are copied into the
# running guest; the image's compositor runs at 1280x800 with Notes fullscreen, and the touch screen and the pen are
# declared 0..1279 by 0..799, so their numbers are the output's pixels.  Notes' own log (/tmp/notes.log, read
# through SSH) is judged; nothing reads the console.
#
#  1. Off (the default), one finger's drag writes nothing.
#  2. A tap on the toolbar's Finger (action 14 in the NOTES BUTTONS line) turns writing with a finger on
#     (NOTES FINGER write=1).
#  3. One finger's drag writes a line (NOTES TOUCH write end, NOTES STROKE with the reports as its points).
#  4. Two fingers put down together and parted take the line just begun back (NOTES TOUCH write abort
#     reason=fingers, NOTES ABORT stroke) and zoom (NOTES TOUCH pinch end, about 1.9 times); no stroke is added.
#  5. Two fingers flick the zoomed page up: it scrolls and glides (NOTES TOUCH drag fingers=2, release, rest); no
#     stroke is added.
#  6. One finger writes on the zoomed page (a stroke is added).
#  7. A tap on Finger again turns it off (NOTES FINGER write=0); one finger's drag then writes nothing.
# Each script waits 2.6 s after declaring its device before it touches: the compositor finds a new evdev node by
# looking now and then.  Pictures go to OUTDIR (and to PREFIX* when given).
#
#   sh plan/tools/guest/test-image.sh plan/ws079/tests/config-amd64-pen.mk build/ws081-main-pen   (ws136-p003)
#   cp build/ws081-main-pen/hdd-image.img build/ws081-main-pen.img
#   VENUS_RENDERER=$PWD/build/ws035-sq-venus/install \
#   GUEST_RUNTIME=$PWD/build/ws081-run plan/ws079/tests/pen-guest.sh start build/ws081-main-pen.img
#   GUEST_RUNTIME=... plan/ws081/tests/p015-guest.sh BUILD [OUTDIR [PREFIX]]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
export GUEST_RUNTIME="${GUEST_RUNTIME:-$PWD/build/ws081-run}"
build=${1:?usage: p015-guest.sh BUILD [OUTDIR [PREFIX]]}
out=${2:-build/ws081-p015-guest}
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
stop_all='for p in $(ps -A -o pid,args | grep -E "[w]ayland( |$)|/bin/[n]otes|[t]ouchinject|[p]eninject" | awk "{print \$1}"); do kill $p; done; i=0; while ps -A -o args | grep -qE "[w]ayland( |$)|/bin/[n]otes" && [ $i -lt 50 ]; do sleep 0.2; i=$((i+1)); done'
status=0

# Counts the lines of Notes' log that match a pattern.
count() {
	guest "grep -cE '$1' /tmp/notes.log" | tail -1
}

# Fails the run unless Notes' log has a line matching a pattern (within a few seconds).
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

# Runs an injector (touchinject or peninject) on a script in the guest.  Without a replay=0 line the step fails, and the
# SSH command's status and whole output are kept in OUTDIR/SCRIPT.failed.log (BUG-099: tell a failed replay from
# a lost output; the injector says on standard error how it ended).
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

# Replays a touch script (a file in OUTDIR) in the guest.
touches() {
	inject touchinject "$1"
}

# Replays a pen script (a file in OUTDIR) in the guest.
pen() {
	inject peninject "$1"
}

# The programs under test, the compositor of the image, and Notes fullscreen on a new notebook.
guest "$stop_all" >/dev/null
put "$build/bin/notes" /bin/notes
put "$build/bin/touchinject" /bin/touchinject
put "$build/dynamic/libkeiland.so" /lib/libkeiland.so
put "$build/dynamic/libpdf.so" /lib/libpdf.so
put "$build/dynamic/libz-compat.so" /lib/libz-compat.so
put "$build/dynamic/libjpeg-compat.so" /lib/libjpeg-compat.so
# A new notebook each run: the test's folder and every journal (a journal of the same path would be recovered) go.
guest 'chmod 755 /bin/notes /bin/touchinject; rm -rf /tmp/notes-p015 "${XDG_DATA_HOME:-$HOME/.local/share}/keiland/notes"; mkdir -p /tmp/notes-p015' >/dev/null
guest 'export XDG_RUNTIME_DIR=/tmp; rm -f /tmp/wayland-0 /tmp/zdesktop.log
/bin/wayland --testing --timeout=900 --width=1280 --height=800 --glass > /tmp/zdesktop.log 2>&1 </dev/null &
i=0; while ! grep -q "ZWL MODE" /tmp/zdesktop.log && [ $i -lt 60 ]; do sleep 0.5; i=$((i+1)); done; sleep 2; echo started' >/dev/null
guest 'export XDG_RUNTIME_DIR=/tmp WAYLAND_DISPLAY=wayland-0; rm -f /tmp/notes.log; /bin/notes --fullscreen /tmp/notes-p015/touch.pdf > /tmp/notes.log 2>&1 </dev/null & i=0; while ! grep -q "NOTES FRAME first" /tmp/notes.log && [ $i -lt 60 ]; do sleep 0.5; i=$((i+1)); done; sleep 2; echo started' >/dev/null
expect 'NOTES LAYOUT window=1280x800' "Notes is fullscreen"
shot whole.png

# The toolbar's Finger button (action 14 in the NOTES BUTTONS line): its middle.
set -- $(guest "grep 'NOTES BUTTONS' /tmp/notes.log | tail -1" | tr ' ' '\n' | sed -n 's/^14:\([0-9]*\),\([0-9]*\),\([0-9]*\),\([0-9]*\)$/\1 \2 \3 \4/p')
if [ $# -eq 4 ]; then
	fx=$(($1 + $3 / 2)); fy=$(($2 + $4 / 2))
	echo "ok: the toolbar has Finger at $fx,$fy"
else
	echo "FAILED: no Finger button in NOTES BUTTONS"
	status=1
	fx=0; fy=0
fi

# Fails the run unless the number of NOTES STROKE lines is the one given.
strokes_are() {
	now=$(count 'NOTES STROKE')
	if [ "$now" -eq "$1" ] 2>/dev/null; then echo "ok: $2 ($now strokes)"; else echo "FAILED: $2 (strokes $now, not $1)"; status=1; fi
}

# 1. Off: one finger's drag writes nothing.
printf 'size 1279 799 2\nwait 2600\ndown 1 500 400\nwait 100\nswipe 200 80 12 16.667\nup 1\nhold 1000\n' > "$out/off.script"
touches off.script
sleep 1
strokes_are 0 "off, one finger writes nothing"

# 2. A tap on Finger turns writing with a finger on.
printf 'size 1279 799 2\nwait 2600\ndown 1 %d %d\nwait 70\nup 1\nhold 1000\n' "$fx" "$fy" > "$out/finger-on.script"
touches finger-on.script
expect 'NOTES FINGER write=1' "a tap on Finger turns it on"
shot finger-on.png

# 3. One finger's drag writes a line: a touch and 17 moves over about 300 ms.
printf 'size 1279 799 2\nwait 2600\ndown 1 450 350\nwait 17\nswipe 300 120 17 16.667\nup 1\nhold 1000\n' > "$out/write.script"
touches write.script
sleep 1
expect 'NOTES TOUCH write end' "the finger's line ends at its lift"
strokes_are 1 "one finger wrote a line"
guest "grep 'NOTES STROKE' /tmp/notes.log | tail -1" > "$out/write-stroke.txt"
points=$(sed -n 's/.* points=\([0-9]*\) .*/\1/p' "$out/write-stroke.txt")
if [ "${points:-0}" -ge 12 ] 2>/dev/null; then echo "ok: the line has $points points (from 18 reports)"; else echo "FAILED: the line has ${points:-no} points"; status=1; fi
shot written.png

# 4. Two fingers down together, parted: the line just begun is taken back, and they zoom.
{
	printf 'size 1279 799 2\nwait 2600\ndown 1 540 434; down 2 740 434\nwait 100\n'
	k=1
	while [ $k -le 20 ]; do
		printf 'move 1 %d 434; move 2 %d 434\nwait 16.667\n' $((540 - 5 * k)) $((740 + 5 * k))
		k=$((k + 1))
	done
	printf 'hold 150\nup 1; up 2\nhold 1500\n'
} > "$out/pinch.script"
touches pinch.script
sleep 2
expect 'NOTES TOUCH write abort reason=fingers' "the second finger takes the line just begun back"
expect 'NOTES ABORT stroke' "Notes drops that line"
expect 'NOTES TOUCH pinch end' "two fingers zoomed"
guest "grep -E 'NOTES TOUCH pinch end' /tmp/notes.log | tail -1" > "$out/pinch-log.txt"
python3 - "$out/pinch-log.txt" <<'PY' || status=1
import sys
lines = open(sys.argv[1]).read().splitlines()
end = [float(l.split("zoom=")[1]) for l in lines if "pinch end" in l]
ok = len(end) == 1 and 1.7 < end[0] < 2.0
print(("ok: " if ok else "FAILED: ") + "the zoom is about twice (distance 200 to 400 px): %s" % end)
sys.exit(0 if ok else 1)
PY
strokes_are 1 "two fingers wrote nothing"
shot zoomed.png

# 5. Two fingers flick the zoomed page up.
printf 'size 1279 799 2\nwait 2600\ndown 1 560 600; down 2 720 600\nwait 100\nswipe 0 -100 6 16.667\nup 1; up 2\nhold 3000\n' > "$out/flick.script"
touches flick.script
sleep 2
expect 'NOTES TOUCH drag fingers=2' "two fingers drag the page"
guest "grep -E 'NOTES TOUCH (release|rest)' /tmp/notes.log | tail -2" > "$out/flick-log.txt"
python3 - "$out/flick-log.txt" <<'PY' || status=1
import sys
lines = open(sys.argv[1]).read().splitlines()
fields = [dict(f.split("=", 1) for f in l.split() if "=" in f) for l in lines]
release = [f for l, f in zip(lines, fields) if "release" in l]
rest = [f for l, f in zip(lines, fields) if "rest" in l]
ok = len(release) == 1 and len(rest) == 1
if ok:
    vy = -float(release[0]["vy"])
    glided = float(rest[0]["y"]) - float(release[0]["y"])
    ok = vy > 500.0 and glided > 50.0
    print(("ok: " if ok else "FAILED: ") + "two fingers' flick %.0f px/s, the page glided %.0f px after the lift" % (vy, glided))
else:
    print("FAILED: one lift and one rest: %s" % lines)
sys.exit(0 if ok else 1)
PY
strokes_are 1 "two fingers' flick wrote nothing"
shot flicked.png

# 6. One finger writes on the zoomed page.
printf 'size 1279 799 2\nwait 2600\ndown 1 500 450\nwait 17\nswipe 250 -60 15 16.667\nup 1\nhold 1000\n' > "$out/write2.script"
touches write2.script
sleep 1
strokes_are 2 "one finger wrote on the zoomed page"
shot written-zoomed.png

# 7. Finger off again: one finger's drag writes nothing.
printf 'size 1279 799 2\nwait 2600\ndown 1 %d %d\nwait 70\nup 1\nhold 1000\n' "$fx" "$fy" > "$out/finger-off.script"
touches finger-off.script
expect 'NOTES FINGER write=0' "a tap on Finger turns it off"
printf 'size 1279 799 2\nwait 2600\ndown 1 500 450\nwait 100\nswipe 200 80 12 16.667\nup 1\nhold 1500\n' > "$out/off2.script"
touches off2.script
sleep 1
strokes_are 2 "off again, one finger writes nothing"
shot finger-off.png

# Notes' log, the compositor's errors, and everything stops.
guest "grep -E 'NOTES (TOUCH|STROKE|ABORT|FINGER|PAGE|LAYOUT)' /tmp/notes.log" > "$out/notes-log.txt"
guest "grep -cE 'ERROR|FAILED' /tmp/zdesktop.log" | tail -1 > "$out/errors.txt"
guest "$stop_all" >/dev/null
[ "$(cat "$out/errors.txt")" = 0 ] || { echo "compositor errors: $(cat "$out/errors.txt")"; status=1; }

[ $status -eq 0 ] && echo "p015: PASS" || echo "p015: FAIL"
exit $status
