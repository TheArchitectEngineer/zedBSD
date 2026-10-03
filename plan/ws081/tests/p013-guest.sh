#!/bin/sh
# ws081-p013: Notes' fingers and pen on the pen test guest (main's pen image, with the injector, peninject and
# touchinject).  Notes, libpdf (with libz-compat and libjpeg-compat) and libkeiland under test are copied into the
# running guest; the image's compositor runs at 1280x800 with Notes fullscreen, and the touch screen and the pen are
# declared 0..1279 by 0..799, so their numbers are the output's pixels.  Notes' own log (/tmp/notes.log, read
# through SSH) is judged; nothing reads the console.
#
#  1. Two fingers part from 200 px to 400 px over the page: it zooms (NOTES TOUCH pinch start/end), about 1.9 times.
#  2. One finger flicks the zoomed page up: it follows, glides on after the lift and comes to rest.
#  3. The pen writes on the zoomed page (NOTES STROKE); a finger's drag writes nothing.
#  4. The pen coming near reaches Notes (NOTES HOVER source=1), which the palm rule rests on.  A finger under the
#     hovering pen cannot be made here: the injector takes one device at a time (a second opener gets EBUSY), so
#     the palm itself is judged by the host test (plan/ws081/tests/host-notestouch.c) and on the device.
#  5. A double tap goes back to the whole page (NOTES TOUCH double-tap zoom=1.000).
#  6. A tap on the toolbar's New Page button presses it (NOTES PAGE current=1 count=2 new).
# Each script waits 2.6 s after declaring its device before it touches: the compositor finds a new evdev node by
# looking now and then.  Pictures go to OUTDIR (and to PREFIX* when given).
#
#   sh plan/tools/guest/test-image.sh plan/ws079/tests/config-amd64-pen.mk build/ws081-main-pen   (ws136-p003)
#   cp build/ws081-main-pen/hdd-image.img build/ws081-main-pen.img
#   VENUS_RENDERER=$PWD/build/ws035-sq-venus/install \
#   GUEST_RUNTIME=$PWD/build/ws081-run plan/ws079/tests/pen-guest.sh start build/ws081-main-pen.img
#   GUEST_RUNTIME=... plan/ws081/tests/p013-guest.sh BUILD [OUTDIR [PREFIX]]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
export GUEST_RUNTIME="${GUEST_RUNTIME:-$PWD/build/ws081-run}"
build=${1:?usage: p013-guest.sh BUILD [OUTDIR [PREFIX]]}
out=${2:-build/ws081-p013-guest}
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
guest 'chmod 755 /bin/notes /bin/touchinject; rm -rf /tmp/notes-p013 "${XDG_DATA_HOME:-$HOME/.local/share}/keiland/notes"; mkdir -p /tmp/notes-p013' >/dev/null
guest 'export XDG_RUNTIME_DIR=/tmp; rm -f /tmp/wayland-0 /tmp/zdesktop.log
/bin/wayland --timeout=900 --width=1280 --height=800 --glass > /tmp/zdesktop.log 2>&1 </dev/null &
i=0; while ! grep -q "ZWL MODE" /tmp/zdesktop.log && [ $i -lt 60 ]; do sleep 0.5; i=$((i+1)); done; sleep 2; echo started' >/dev/null
guest 'export XDG_RUNTIME_DIR=/tmp WAYLAND_DISPLAY=wayland-0; rm -f /tmp/notes.log; /bin/notes --fullscreen /tmp/notes-p013/touch.pdf > /tmp/notes.log 2>&1 </dev/null & i=0; while ! grep -q "NOTES FRAME first" /tmp/notes.log && [ $i -lt 60 ]; do sleep 0.5; i=$((i+1)); done; sleep 2; echo started' >/dev/null
expect 'NOTES LAYOUT window=1280x800' "Notes is fullscreen"
shot whole.png

# 1. Two fingers part over the middle of the page.
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
expect 'NOTES TOUCH pinch end' "two fingers zoomed"
guest "grep -E 'NOTES TOUCH pinch' /tmp/notes.log" > "$out/pinch-log.txt"
python3 - "$out/pinch-log.txt" <<'EOF' || status=1
import sys
lines = open(sys.argv[1]).read().splitlines()
end = [float(l.split("zoom=")[1]) for l in lines if "pinch end" in l]
ok = len(end) == 1 and 1.7 < end[0] < 2.0
print(("ok: " if ok else "FAILED: ") + "the zoom is about twice (distance 200 to 400 px): %s" % end)
sys.exit(0 if ok else 1)
EOF
shot zoomed.png

# 2. A flick up on the zoomed page.
printf 'size 1279 799 2\nwait 2600\ndown 1 640 600\nwait 100\nswipe 0 -100 6 16.667\nup 1\nhold 3000\n' > "$out/flick.script"
touches flick.script
sleep 2
guest "grep -E 'NOTES TOUCH (drag|release|rest)' /tmp/notes.log | tail -3" > "$out/flick-log.txt"
python3 - "$out/flick-log.txt" <<'EOF' || status=1
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
    print(("ok: " if ok else "FAILED: ") + "the flick's velocity %.0f px/s, the page glided %.0f px after the lift" % (vy, glided))
else:
    print("FAILED: one lift and one rest: %s" % lines)
sys.exit(0 if ok else 1)
EOF
shot flicked.png

# 3. The pen writes on the zoomed page; a finger's drag does not.
strokes=$(count 'NOTES STROKE')
printf 'size 1279 799\nwait 2600\ntool pen\nhover 500 300\ndown 500 300 2048\nmove 600 330 2048\nmove 700 380 2048\nmove 800 400 2048\nup\nhold 500\n' > "$out/pen.script"
pen pen.script
sleep 2
after=$(count 'NOTES STROKE')
if [ "$after" -eq $((strokes + 1)) ] 2>/dev/null; then echo "ok: the pen wrote a stroke"; else echo "FAILED: the pen wrote $strokes -> $after"; status=1; fi
printf 'size 1279 799 2\nwait 2600\ndown 1 600 500\nwait 100\nswipe 120 60 8 16.667\nup 1\nhold 1500\n' > "$out/finger.script"
touches finger.script
sleep 1
written=$(count 'NOTES STROKE')
if [ "$written" -eq "$after" ] 2>/dev/null; then echo "ok: a finger's drag wrote nothing"; else echo "FAILED: a finger wrote $after -> $written"; status=1; fi
shot pen.png

# 4. The pen's coming near reached Notes (from step 3).
expect 'NOTES HOVER source=1' "the pen near the window reaches Notes"

# 5. A double tap: the whole page again.
printf 'size 1279 799 2\nwait 2600\ndown 1 640 500\nwait 60\nup 1\nwait 120\ndown 2 642 501\nwait 60\nup 2\nhold 1500\n' > "$out/double.script"
touches double.script
expect 'NOTES TOUCH double-tap zoom=1.000' "a double tap goes back to the whole page"
sleep 2
shot double-tap.png

# 6. A tap on the toolbar's New Page button (action 8 in the NOTES BUTTONS line).
set -- $(guest "grep 'NOTES BUTTONS' /tmp/notes.log | tail -1" | tr ' ' '\n' | sed -n 's/^8:\([0-9]*\),\([0-9]*\),\([0-9]*\),\([0-9]*\)$/\1 \2 \3 \4/p')
if [ $# -eq 4 ]; then
	bx=$(($1 + $3 / 2)); by=$(($2 + $4 / 2))
	printf 'size 1279 799 2\nwait 2600\ndown 1 %d %d\nwait 70\nup 1\nhold 1500\n' "$bx" "$by" > "$out/toolbar.script"
	touches toolbar.script
	expect 'NOTES PAGE current=1 count=2 new' "a tap on the toolbar pressed New Page"
else
	echo "FAILED: no New Page button in NOTES BUTTONS"
	status=1
fi
shot new-page.png

# Notes' log, the compositor's errors, and everything stops.
guest "grep -E 'NOTES (TOUCH|STROKE|PAGE|LAYOUT)' /tmp/notes.log" > "$out/notes-log.txt"
guest "grep -cE 'ERROR|FAILED' /tmp/zdesktop.log" | tail -1 > "$out/errors.txt"
guest "$stop_all" >/dev/null
[ "$(cat "$out/errors.txt")" = 0 ] || { echo "compositor errors: $(cat "$out/errors.txt")"; status=1; }

[ $status -eq 0 ] && echo "p013: PASS" || echo "p013: FAIL"
exit $status
