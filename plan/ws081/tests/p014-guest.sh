#!/bin/sh
# ws081-p014: a finger's drag and drop on the pen test guest (main's pen image, with the injector and touchinject).
# The compositor, the terminal, libkeiland and touchinject under test are copied into the running guest (1280x800,
# the touch screen declared 0..1279 by 0..799, so a finger's numbers are the output's pixels).  Two terminals: T1
# writes "dragme", T2 is a shell; the compositor cascades them (T2 48 px right of and below T1, on top).
#
#  1. A long press on T1's "dragme" selects the word (ZTERM TOUCH hold on-selection=0, ZTERM SELECT how=word).
#  2. A long press on the selection holds it (on-selection=1); the finger then moves onto T2's uncovered strip:
#     T1 starts a drag with the finger's wl_touch.down serial (ZTERM DRAG start), the compositor gives the finger
#     to the drag (KWL TOUCH drag start; T1 hears wl_touch.cancel), the drag follows the finger, and the lift
#     drops on T2 (KWL DATA drag drop ... target=T2, ZTERM DROP bytes=6 in T2, ZTERM DRAG done dropped=1 in T1).
# The logs are read through SSH; nothing reads the console.  Pictures go to OUTDIR (and PREFIX* when given).
#
#   GUEST_RUNTIME=... plan/ws081/tests/p014-guest.sh BUILD [OUTDIR [PREFIX]]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
export GUEST_RUNTIME="${GUEST_RUNTIME:-$PWD/build/ws081-run}"
build=${1:?usage: p014-guest.sh BUILD [OUTDIR [PREFIX]]}
out=${2:-build/ws081-p014-guest}
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

# The programs under test, the compositor, and the two terminals.
guest "$stop_all" >/dev/null
put "$build/bin/wayland" /bin/wayland
put "$build/bin/terminal" /bin/terminal
put "$build/bin/touchinject" /bin/touchinject
put "$build/dynamic/libkeiland.so" /lib/libkeiland.so
guest 'chmod 755 /bin/wayland /bin/terminal /bin/touchinject' >/dev/null
guest 'export XDG_RUNTIME_DIR=/tmp; rm -f /tmp/wayland-0 /tmp/zdesktop.log
/bin/wayland --testing --timeout=900 --width=1280 --height=800 --glass --log-frames > /tmp/zdesktop.log 2>&1 </dev/null &
i=0; while ! grep -q "KWL MODE" /tmp/zdesktop.log && [ $i -lt 60 ]; do sleep 0.5; i=$((i+1)); done; sleep 2; echo started' >/dev/null
guest 'printf "echo dragme; sleep 600\n" > /tmp/t1.sh; export XDG_RUNTIME_DIR=/tmp WAYLAND_DISPLAY=wayland-0; /bin/terminal --columns=40 --rows=10 --token=t1 "--command=sh /tmp/t1.sh" > /tmp/t1.log 2>&1 </dev/null & sleep 5; /bin/terminal --columns=40 --rows=10 --token=t2 > /tmp/t2.log 2>&1 </dev/null & sleep 5; echo started' >/dev/null
guest "grep 'KWL MAP client=' /tmp/zdesktop.log" > "$out/maps.txt"
set -- $(sed -n 's/.* client=\([0-9]*\) surface=[0-9]* x=\([-0-9]*\) y=\([-0-9]*\).*/\1 \2 \3/p' "$out/maps.txt" | tr '\n' ' ')
c1=${1:-0}; x1=${2:-0}; y1=${3:-0}; c2=${4:-0}; x2=${5:-0}; y2=${6:-0}
echo "T1 client $c1 at $x1,$y1; T2 client $c2 at $x2,$y2"
shot start.png

# 1. A long press on T1's word selects it.
wx=$((x1 + 30)); wy=$((y1 + 17))
printf 'size 1279 799 2\nwait 2600\ndown 1 %d %d\nwait 900\nup 1\nhold 1000\n' "$wx" "$wy" > "$out/select.script"
inject touchinject select.script
expect /tmp/t1.log 'ZTERM TOUCH hold on-selection=0' "a long press away from any selection holds a word selection"
expect /tmp/t1.log 'ZTERM SELECT how=word' "T1's word is selected"
shot selected.png

# 2. A long press on the selection, then the finger carries it onto T2's uncovered strip and lifts.
tx=$((x2 + 400)); ty=$((y2 + 80))
printf 'size 1279 799 2\nwait 2600\ndown 1 %d %d\nwait 700\nswipe %d %d 24 16.667\nhold 300\nup 1\nhold 1500\n' "$wx" "$wy" $((tx - wx)) $((ty - wy)) > "$out/drag.script"
inject touchinject drag.script
expect /tmp/t1.log 'ZTERM TOUCH hold on-selection=1' "a long press on the selection holds it"
expect /tmp/t1.log 'ZTERM DRAG start bytes=6' "T1 starts dragging the selected text"
expect /tmp/zdesktop.log "KWL TOUCH drag start client=$c1" "the compositor gives the finger to the drag"
expect /tmp/zdesktop.log "KWL TOUCH cancel client=$c1 reason=drag" "T1 hears wl_touch.cancel"
expect /tmp/zdesktop.log "KWL DATA drag drop client=$c1 target=$c2" "the lift drops on T2"
expect /tmp/t2.log 'ZTERM DROP bytes=6' "T2 takes the dropped text"
expect /tmp/t1.log 'ZTERM DRAG done dropped=1' "T1 hears the drop was done"
shot dropped.png

# The logs, the compositor's errors, and everything stops.
guest "grep -E 'KWL (TOUCH|DATA)' /tmp/zdesktop.log" > "$out/zdesktop-log.txt"
guest "grep -E 'ZTERM' /tmp/t1.log /tmp/t2.log" > "$out/terminals-log.txt"
guest "grep -cE 'ERROR|FAILED' /tmp/zdesktop.log" | tail -1 > "$out/errors.txt"
guest "$stop_all" >/dev/null
[ "$(cat "$out/errors.txt")" = 0 ] || { echo "compositor errors: $(cat "$out/errors.txt")"; status=1; }

[ $status -eq 0 ] && echo "p014: PASS" || echo "p014: FAIL"
exit $status
