#!/bin/sh
# ws081-p010: Files' fingers on the pen test guest (main's pen image, with the injector and touchinject).  The
# compositor (with the finger's drag and drop of ws081-p014), Files, libkeiland, libz-compat, libpng-compat and
# touchinject under test are copied into the running guest (1280x800, the touch screen declared 0..1279 by 0..799,
# so a finger's numbers are the output's pixels).  Files shows /tmp/ftest (the folders alpha and beta and 150 text
# files) in a 900x620 window; the places below are measured from the window's body (its ZWL MAP line) in the grid
# view at that size.  Files' log (/tmp/files.log) and the compositor's are read through SSH; nothing reads the
# console.
#
#  1. A tap on file0 selects it (ZFILES TOUCH tap, ZFILES SELECT count=1).
#  2. A long press on file1, lifted, opens its context menu with the finger's serial (ZFILES TOUCH context,
#     ZFILES CONTEXT-MENU open, ZWL MENU context client=); Esc closes it.
#  3. A long press on file2 carried onto the folder beta moves it there (ZFILES TOUCH hold, DRAG target
#     kind=folder, DRAG drop operation=move; the file is in beta).
#  4. A long press on another file carried out of the window becomes zdesktop's drag and drop, driven by the
#     finger (ZFILES DRAG out, DND start serial=, ZWL TOUCH drag start, the lift over the desktop cancels it).
#  5. A double tap on the folder alpha opens it (LOCATION path=/tmp/ftest/alpha); the titlebar's Back returns.
#  6. A flick up over the items scrolls them and they glide on (ZFILES TOUCH drag, release, rest further down).
# Each script waits 2.6 s after declaring its screen before it touches.  Pictures go to OUTDIR (and PREFIX*).
#
#   GUEST_RUNTIME=... plan/ws081/tests/p010-guest.sh BUILD [OUTDIR [PREFIX]]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
export GUEST_RUNTIME="${GUEST_RUNTIME:-$PWD/build/ws081-run}"
build=${1:?usage: p010-guest.sh BUILD [OUTDIR [PREFIX]]}
out=${2:-build/ws081-p010-guest}
prefix=${3:-}
mkdir -p "$out"
guest() { timeout 120 python3 plan/tools/guest/guest.py run "$1" 2>&1; }
put() { timeout 90 python3 plan/tools/guest/guest.py put "$1" "$2" >/dev/null 2>&1 || { echo "put $1: FAILED"; status=1; }; }
keys() { python3 plan/ws035/tests/qmp-keys.py "$GUEST_RUNTIME/qmp.sock" "$@" >/dev/null; sleep 1; }
shot() {
	python3 plan/ws035/tests/zdesktop-check.py "$out/$1" --runtime "$GUEST_RUNTIME" >/dev/null
	if [ -n "$prefix" ]; then
		cp "$out/$1" "$prefix$1"
	fi
	echo "shot $1"
}
stop_all='for p in $(ps -A -o pid,args | grep -E "[w]ayland( |$)|/bin/[f]iles|[t]ouchinject" | awk "{print \$1}"); do kill $p; done; i=0; while ps -A -o args | grep -qE "[w]ayland( |$)|/bin/[f]iles" && [ $i -lt 50 ]; do sleep 0.2; i=$((i+1)); done'
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
	printf 'size 1279 799 2\nwait 2600\n%s\nhold 1500\n' "$2" > "$out/$1"
	inject touchinject "$1"
}

# Fails the run unless a log has a line matching a pattern (within a few seconds).
expect() {
	tries=0
	found=0
	while [ $tries -lt 10 ]; do
		found=$(guest "grep -cE '$2' $1" | tail -1)
		[ "${found:-0}" -gt 0 ] 2>/dev/null && break
		tries=$((tries + 1))
		sleep 1
	done
	if [ "${found:-0}" -gt 0 ] 2>/dev/null; then
		echo "ok: $3"
	else
		echo "FAILED: $3 (no line in $1: $2)"
		status=1
	fi
}

# The programs under test, the test folder, the compositor and Files.
guest "$stop_all" >/dev/null
put "$build/bin/wayland" /bin/wayland
put "$build/bin/files" /bin/files
put "$build/bin/touchinject" /bin/touchinject
put "$build/dynamic/libkeiland.so" /lib/libkeiland.so
put "$build/dynamic/libz-compat.so" /lib/libz-compat.so
put "$build/dynamic/libpng-compat.so" /lib/libpng-compat.so
guest 'chmod 755 /bin/wayland /bin/files /bin/touchinject; rm -rf /tmp/ftest; mkdir -p /tmp/ftest/alpha /tmp/ftest/beta; i=0; while [ $i -lt 150 ]; do echo x > /tmp/ftest/file$i.txt; i=$((i+1)); done; echo made' >/dev/null
guest 'export XDG_RUNTIME_DIR=/tmp; rm -f /tmp/wayland-0 /tmp/zdesktop.log
/bin/wayland --testing --timeout=900 --width=1280 --height=800 --glass --log-frames > /tmp/zdesktop.log 2>&1 </dev/null &
i=0; while ! grep -q "ZWL MODE" /tmp/zdesktop.log && [ $i -lt 60 ]; do sleep 0.5; i=$((i+1)); done; sleep 2; echo started' >/dev/null
guest 'export XDG_RUNTIME_DIR=/tmp WAYLAND_DISPLAY=wayland-0; /bin/files --width=900 --height=620 --token=p010 /tmp/ftest > /tmp/files.log 2>&1 </dev/null & i=0; while ! grep -q "ZFILES READY" /tmp/files.log && [ $i -lt 60 ]; do sleep 0.5; i=$((i+1)); done; sleep 3; echo started' >/dev/null
expect /tmp/files.log 'ZFILES READY' "Files started"
set -- $(guest "grep 'ZWL MAP client=' /tmp/zdesktop.log | tail -1" | sed -n 's/.* client=\([0-9]*\) surface=[0-9]* x=\([-0-9]*\) y=\([-0-9]*\).*/\1 \2 \3/p')
client=${1:-0}; wx=${2:-0}; wy=${3:-0}
echo "Files client $client at $wx,$wy"
shot start.png

# 1. A tap on file0 (the third cell of the first row).
touches tap.script "$(printf 'down 1 %d %d\nwait 70\nup 1' $((wx + 560)) $((wy + 107)))"
expect /tmp/files.log 'ZFILES TOUCH tap' "a tap on the items"
expect /tmp/files.log 'ZFILES SELECT count=1' "it selects file0"

# 2. A long press on file1, lifted: its context menu, then Esc.
touches context.script "$(printf 'down 1 %d %d\nwait 800\nup 1' $((wx + 672)) $((wy + 107)))"
expect /tmp/files.log 'ZFILES TOUCH context' "a long press lifted is the right button's click"
expect /tmp/files.log 'ZFILES CONTEXT-MENU open' "Files asks for its context menu"
expect /tmp/zdesktop.log "ZWL MENU context client=$client" "zdesktop shows it (the finger's serial is the press's)"
shot context.png
keys '<esc>'

# 3. A long press on file2, carried onto the folder beta: file2 moves there.
touches move.script "$(printf 'down 1 %d %d\nwait 700\nswipe %d %d 20 16.667\nhold 400\nup 1' $((wx + 785)) $((wy + 107)) -338 0)"
expect /tmp/files.log 'ZFILES TOUCH hold' "a long press then a move holds the left button"
expect /tmp/files.log 'ZFILES DRAG target kind=folder path=/tmp/ftest/beta' "the drag's target is beta"
expect /tmp/files.log 'ZFILES DRAG drop operation=move' "the lift moves the file"
moved=$(guest 'ls /tmp/ftest/beta' | tr '\n' ' ')
case "$moved" in
*file2.txt*) echo "ok: file2.txt is in beta ($moved)" ;;
*) echo "FAILED: beta has $moved"; status=1 ;;
esac

# 4. A long press on another file, carried out of the window over the desktop: zdesktop's drag, driven by the finger.
touches out.script "$(printf 'down 1 %d %d\nwait 700\nswipe %d %d 30 16.667\nhold 400\nup 1' $((wx + 785)) $((wy + 107)) $((100 - wx - 785)) 250)"
expect /tmp/files.log 'ZFILES DRAG out items=1' "the drag leaves the window"
expect /tmp/files.log 'ZFILES DND start items=1' "Files starts a drag and drop with the finger's serial"
expect /tmp/zdesktop.log "ZWL TOUCH drag start client=$client" "zdesktop gives the finger to the drag"
expect /tmp/zdesktop.log "ZWL DATA drag cancel client=$client reason=release" "the lift over the desktop gives it up"
expect /tmp/files.log 'ZFILES (DRAG out done|DND end) dropped=0' "Files hears it was not dropped"
shot out.png

# 5. A double tap on the folder alpha opens it; the titlebar's Back returns.
touches open.script "$(printf 'down 1 %d %d\nwait 60\nup 1\nwait 120\ndown 2 %d %d\nwait 60\nup 2' $((wx + 335)) $((wy + 102)) $((wx + 336)) $((wy + 103)))"
expect /tmp/files.log 'ZFILES LOCATION kind=folder path=/tmp/ftest/alpha' "a double tap opens alpha"
shot alpha.png
touches back.script "$(printf 'down 1 %d %d\nwait 70\nup 1' $((wx + 110)) $((wy - 30)))"
expect /tmp/files.log 'ZFILES LOCATION kind=folder path=/tmp/ftest items=' "Back returns to /tmp/ftest"

# 6. A flick up over the items.
touches flick.script "$(printf 'down 1 %d %d\nwait 100\nswipe 0 -150 6 16.667\nup 1\nhold 2500' $((wx + 560)) $((wy + 440)))"
sleep 2
guest "grep -E 'ZFILES TOUCH (drag|release|rest)' /tmp/files.log | tail -3" > "$out/flick-log.txt"
python3 - "$out/flick-log.txt" <<'EOF' || status=1
import sys
lines = open(sys.argv[1]).read().splitlines()
fields = [dict(f.split("=", 1) for f in l.split() if "=" in f) for l in lines]
release = [f for l, f in zip(lines, fields) if "release" in l]
rest = [f for l, f in zip(lines, fields) if "rest" in l]
ok = len(release) == 1 and len(rest) == 1
if ok:
    vy = -float(release[0]["vy"])
    glided = int(rest[0]["scroll"]) - int(release[0]["scroll"])
    ok = vy > 1000.0 and glided > 100
    print(("ok: " if ok else "FAILED: ") + "the flick's velocity %.0f px/s, the items glided %d px after the lift" % (vy, glided))
else:
    print("FAILED: one lift and one rest: %s" % lines)
sys.exit(0 if ok else 1)
EOF
shot flicked.png

# The logs, the compositor's errors, and everything stops.
guest "grep -E 'ZFILES (TOUCH|SELECT|CONTEXT|DRAG|DND|LOCATION|FAILED)' /tmp/files.log" > "$out/files-log.txt"
guest "grep -E 'ZWL (TOUCH|DATA|MENU)' /tmp/zdesktop.log" > "$out/zdesktop-log.txt"
guest "grep -cE 'ERROR|FAILED' /tmp/zdesktop.log" | tail -1 > "$out/errors.txt"
guest "$stop_all" >/dev/null
[ "$(cat "$out/errors.txt")" = 0 ] || { echo "compositor errors: $(cat "$out/errors.txt")"; status=1; }
if grep -q 'ZFILES FAILED' "$out/files-log.txt"; then echo "FAILED: Files failed"; status=1; fi

[ $status -eq 0 ] && echo "p010: PASS" || echo "p010: FAIL"
exit $status
