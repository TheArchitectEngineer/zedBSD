#!/bin/sh
# ws081-p011: the terminal's fingers on the pen test guest (main's pen image, with the injector and touchinject).
# The terminal, libkeiland and touchinject under test are copied into the running guest; the image's compositor runs
# at 1280x800 and the touch screen is declared 0..1279 by 0..799, so a finger's numbers are the output's pixels.  The
# terminal runs a command that writes 400 numbered lines (a scrollback) and waits.  Its own log (/tmp/zterm.log,
# read through SSH) is judged; nothing reads the console.
#
#  1. One finger flicks down over the text: it scrolls back into the scrollback (ZTERM TOUCH drag, release) and the
#     view glides on after the lift and comes to rest further back.
#  2. A tap is a click (ZTERM TOUCH tap).
#  3. A long press on a word selects it (ZTERM TOUCH select, ZTERM SELECT how=word).
#  4. A flick up goes back toward the live screen.
# Each script waits 2.6 s after declaring its screen before it touches (the compositor finds a new evdev node by
# looking now and then).  Pictures go to OUTDIR (and to PREFIX* when given).
#
#   sh plan/tools/guest/test-image.sh plan/ws079/tests/config-amd64-pen.mk build/ws081-main-pen   (ws136-p003)
#   cp build/ws081-main-pen/hdd-image.img build/ws081-main-pen.img
#   VENUS_RENDERER=$PWD/build/ws035-sq-venus/install \
#   GUEST_RUNTIME=$PWD/build/ws081-run plan/ws079/tests/pen-guest.sh start build/ws081-main-pen.img
#   GUEST_RUNTIME=... plan/ws081/tests/p011-guest.sh BUILD [OUTDIR [PREFIX]]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
export GUEST_RUNTIME="${GUEST_RUNTIME:-$PWD/build/ws081-run}"
build=${1:?usage: p011-guest.sh BUILD [OUTDIR [PREFIX]]}
out=${2:-build/ws081-p011-guest}
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
stop_all='for p in $(ps -A -o pid,args | grep -E "[w]ayland( |$)|/bin/[t]erminal|[t]ouchinject" | awk "{print \$1}"); do kill $p; done; i=0; while ps -A -o args | grep -qE "[w]ayland( |$)|/bin/[t]erminal" && [ $i -lt 50 ]; do sleep 0.2; i=$((i+1)); done'
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

# Replays a touch script (a file in OUTDIR) in the guest.
touches() {
	inject touchinject "$1"
}

# Fails the run unless the terminal's log has a line matching a pattern (within a few seconds).
expect() {
	tries=0
	found=0
	while [ $tries -lt 10 ]; do
		found=$(guest "grep -cE '$1' /tmp/zterm.log" | tail -1)
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

# The programs under test, the compositor of the image, and the terminal with a scrollback.
guest "$stop_all" >/dev/null
put "$build/bin/terminal" /bin/terminal
put "$build/bin/touchinject" /bin/touchinject
put "$build/dynamic/libkeiland.so" /lib/libkeiland.so
guest 'chmod 755 /bin/terminal /bin/touchinject' >/dev/null
guest 'export XDG_RUNTIME_DIR=/tmp; rm -f /tmp/wayland-0 /tmp/zdesktop.log
/bin/wayland --testing --timeout=900 --width=1280 --height=800 --glass --log-frames > /tmp/zdesktop.log 2>&1 </dev/null &
i=0; while ! grep -q "ZWL MODE" /tmp/zdesktop.log && [ $i -lt 60 ]; do sleep 0.5; i=$((i+1)); done; sleep 2; echo started' >/dev/null
printf '%s\n' 'i=0; while [ $i -lt 400 ]; do echo "line $i of the scrollback test"; i=$((i+1)); done; sleep 600' > "$out/lines.sh"
put "$out/lines.sh" /tmp/lines.sh
guest 'export XDG_RUNTIME_DIR=/tmp WAYLAND_DISPLAY=wayland-0; rm -f /tmp/zterm.log; /bin/terminal --columns=80 --rows=30 --token=p011 "--command=sh /tmp/lines.sh" > /tmp/zterm.log 2>&1 </dev/null & i=0; while ! grep -q "ZTERM START" /tmp/zterm.log && [ $i -lt 60 ]; do sleep 0.5; i=$((i+1)); done; sleep 4; echo started' >/dev/null
expect 'ZTERM START run=p011' "the terminal started"
set -- $(guest "grep 'ZWL MAP client=' /tmp/zdesktop.log | tail -1" | sed -n 's/.* x=\([-0-9]*\) y=\([-0-9]*\).*/\1 \2/p')
wx=${1:-0}; wy=${2:-0}
echo "window at $wx,$wy"
shot live.png

# 1. A flick down over the text.
x=$((wx + 300)); y=$((wy + 100))
printf 'size 1279 799 2\nwait 2600\ndown 1 %d %d\nwait 100\nswipe 0 200 6 16.667\nup 1\nhold 3000\n' "$x" "$y" > "$out/flick.script"
touches flick.script
sleep 2
guest "grep -E 'ZTERM TOUCH (drag|release|rest)' /tmp/zterm.log | tail -3" > "$out/flick-log.txt"
python3 - "$out/flick-log.txt" <<'EOF' || status=1
import sys
lines = open(sys.argv[1]).read().splitlines()
fields = [dict(f.split("=", 1) for f in l.split() if "=" in f) for l in lines]
release = [f for l, f in zip(lines, fields) if "release" in l]
rest = [f for l, f in zip(lines, fields) if "rest" in l]
ok = len(release) == 1 and len(rest) == 1
if ok:
    vy = float(release[0]["vy"])
    back = int(rest[0]["view"]) - int(release[0]["view"])
    ok = vy > 1000.0 and back > 5 and int(rest[0]["view"]) > 0
    print(("ok: " if ok else "FAILED: ") + "the flick's velocity %.0f px/s, the view glided %d lines further back to %s" % (vy, back, rest[0]["view"]))
else:
    print("FAILED: one lift and one rest: %s" % lines)
sys.exit(0 if ok else 1)
EOF
shot scrolled.png

# 2. A tap.
printf 'size 1279 799 2\nwait 2600\ndown 1 %d %d\nwait 70\nup 1\nhold 1000\n' $((wx + 200)) $((wy + 300)) > "$out/tap.script"
touches tap.script
expect 'ZTERM TOUCH tap x=200 y=300 caught=0' "a tap is a click"

# 3. A long press on a word selects it.
printf 'size 1279 799 2\nwait 2600\ndown 1 %d %d\nwait 900\nup 1\nhold 1000\n' $((wx + 60)) $((wy + 200)) > "$out/long.script"
touches long.script
expect 'ZTERM TOUCH select' "a long press selects"
expect 'ZTERM SELECT how=word' "the word under the finger is selected"
shot selected.png

# 4. A flick up goes back toward the live screen.
before=$(guest "grep -E 'ZTERM TOUCH rest' /tmp/zterm.log | tail -1" | sed -n 's/.*view=\([0-9]*\).*/\1/p')
printf 'size 1279 799 2\nwait 2600\ndown 1 %d %d\nwait 100\nswipe 0 -200 6 16.667\nup 1\nhold 3000\n' $((wx + 300)) $((wy + 400)) > "$out/back.script"
touches back.script
sleep 2
after=$(guest "grep -E 'ZTERM TOUCH rest' /tmp/zterm.log | tail -1" | sed -n 's/.*view=\([0-9]*\).*/\1/p')
if [ -n "$before" ] && [ -n "$after" ] && [ "$after" -lt "$before" ] 2>/dev/null; then
	echo "ok: a flick up went back toward the live screen ($before -> $after lines back)"
else
	echo "FAILED: a flick up ($before -> $after)"
	status=1
fi
shot back.png

# The terminal's log, the compositor's errors, and everything stops.
guest "grep -E 'ZTERM (TOUCH|SELECT|VIEW|FAILED)' /tmp/zterm.log" > "$out/zterm-log.txt"
guest "grep -cE 'ERROR|FAILED' /tmp/zdesktop.log" | tail -1 > "$out/errors.txt"
guest "$stop_all" >/dev/null
[ "$(cat "$out/errors.txt")" = 0 ] || { echo "compositor errors: $(cat "$out/errors.txt")"; status=1; }
if grep -q 'ZTERM FAILED' "$out/zterm-log.txt"; then echo "FAILED: the terminal failed"; status=1; fi

[ $status -eq 0 ] && echo "p011: PASS" || echo "p011: FAIL"
exit $status
