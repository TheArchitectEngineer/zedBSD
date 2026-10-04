#!/bin/sh
# ws100-p013 (BUG-170): a quick, long drag of the volume's slider (as a touchpad gives: a motion every 10 ms) neither
# holds the desktop up nor plays the feedback sound for its steps; the sound plays once, when the slider is let go.
# On the Venus guest of the volume image (build-volume-image.sh) with QEMU's HD Audio (volume-guest.sh), kei's session
# at boot.
#  1. The system bar: the popup's slider pressed at a quarter, dragged in 160 steps of 10 ms to the right end and back
#     to the middle, and let go.  Exactly one new "ZWL VOLUME feedback ... via=slider" (the release's); the release's
#     "set ... final=1" comes within the drag's time and 2 s of its first step (zdesktop kept up); audiod has the final
#     volume; a click on the icon right after closes the popup within 3 s (the desktop answers).
#  2. Settings' Sound page: its slider dragged the same way (120 steps of 10 ms) and let go.  Exactly one new
#     "SOUND feedback error=0" (the release's); "SOUND set ... final=1"; audiod has the final volume.
#  3. No ZWL ERROR; Settings still runs.
#   plan/ws100/tests/volume-bug170.sh IMAGE [OUTDIR]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
image=${1:?usage: volume-bug170.sh IMAGE [OUTDIR]}
out=${2:-build/ws100-shots/bug170}
mkdir -p "$out"
GUEST_RUNTIME=$(pwd)/build/ws100-run
export GUEST_RUNTIME
log=/run/user/1000/session.log
slog=/tmp/s.log
guest() { timeout 90 python3 plan/tools/guest/guest.py run "$1" 2>&1 </dev/null; }
pointer() { python3 plan/ws035/tests/qmp-pointer.py "$GUEST_RUNTIME/qmp.sock" "$@"; }
shot() { python3 plan/ws035/tests/zdesktop-check.py "$out/$1" --runtime "$GUEST_RUNTIME" >/dev/null 2>&1; echo "shot: $out/$1"; }
status=0

# Records a verdict.
verdict() {
	if [ "$1" = ok ]; then
		echo "$2 ok"
	else
		echo "$2 FAIL"
		status=1
	fi
}

# The number of lines of a guest file matching a pattern.
count() {
	guest "grep -cE '$2' $1" | tail -1
}

# Waits until a guest file has more than N lines matching a pattern (within some seconds).
expect_more() {
	tries=0
	found=0
	while [ $tries -lt "$4" ]; do
		found=$(count "$1" "$2")
		[ "${found:-0}" -gt "$3" ] 2>/dev/null && break
		tries=$((tries + 1))
		sleep 1
	done
	if [ "${found:-0}" -gt "$3" ] 2>/dev/null; then
		echo "log: $2 ok"
		return 0
	fi
	echo "log: $2 MISSING"
	status=1
	return 1
}

# audiod's volume as "left muted".
audiod_volume() {
	guest 'audiod-feedback get' | sed -n 's/.*volume left=\([0-9]*\) right=[0-9]* muted=\([0-9]\).*/\1 \2/p' | tail -1
}

# The last line of a guest file matching a pattern.
last() {
	guest "grep -E '$2' $1 | tail -1"
}

# Prints the pointer steps of a quick drag on a row y: from x0 to x1 and back to x2, n steps each way of 10 ms.
drag_steps() {
	python3 - "$@" <<'EOF'
import sys
x0, x1, x2, y, n = (int(v) for v in sys.argv[1:6])
steps = ["move", str(x0), str(y), "sleep", "200", "down", "sleep", "100"]
for i in range(1, n + 1):
	steps += ["move", str(x0 + (x1 - x0) * i // n), str(y), "sleep", "10"]
for i in range(1, n + 1):
	steps += ["move", str(x1 + (x2 - x1) * i // n), str(y), "sleep", "10"]
steps += ["sleep", "100", "up"]
print(" ".join(steps))
EOF
}

# 0. The guest with HD Audio, kei's session, audiod at 60.
sh plan/ws100/tests/volume-guest.sh stop >/dev/null 2>&1
VOLUME_AUDIO=duplex timeout 180 sh plan/ws100/tests/volume-guest.sh start "$image" >/dev/null 2>&1
sleep 35
expect_more $log 'ZWL HANDOFF go=1' 0 60
expect_more $log 'ZWL VOLUME reachable=1 device=1' 0 20
expect_more $log 'ZWL VOLUME icon x=' 0 10
guest 'audiod-feedback volume 60' >/dev/null
sleep 2

# 1. The system bar's slider: the popup, then the quick drag.
set -- $(last $log 'ZWL VOLUME icon x=' | sed -n 's/.* x=\([0-9]*\) y=\([0-9]*\) width=\([0-9]*\) height=\([0-9]*\).*/\1 \2 \3 \4/p')
ix=$((${1:-900} + ${3:-30} / 2)); iy=$((${2:-3} + ${4:-28} / 2))
opens=$(count $log 'ZWL VOLUME popup open')
pointer move $((ix - 2)) $iy sleep 200 move $ix $iy sleep 300 down sleep 60 up sleep 800
expect_more $log 'ZWL VOLUME popup open' "$opens" 5
set -- $(last $log 'ZWL VOLUME popup open' | sed -n 's/.* x=\([0-9]*\) y=\([0-9]*\) width=\([0-9]*\) height=\([0-9]*\) slider=\([0-9]*\) mute=\([0-9]*\).*/\1 \2 \3 \4 \5 \6/p')
px=${1:-900} pw=${3:-260} slider=${5:-100}
track_left=$((px + 23)) track_width=$((pw - 46))
sy=$((slider + 17))
q1=$((track_left + track_width / 4)) right=$((track_left + track_width)) middle=$((track_left + track_width / 2))
lines=$(guest "wc -l < $log" | tail -1)
feedbacks=$(count $log 'ZWL VOLUME feedback at_ms=[0-9]+ via=slider')
pointer $(drag_steps $q1 $right $middle $sy 80)
expect_more $log 'ZWL VOLUME set value=(49|50|51) muted=0 via=slider final=1' 0 10
guest "tail -n +$((${lines:-0} + 1)) $log | grep -E 'ZWL VOLUME (set|feedback)'" > "$out/bar-drag.log"
after=$(count $log 'ZWL VOLUME feedback at_ms=[0-9]+ via=slider')
echo "bar: feedback via=slider $feedbacks -> $after"
[ "${after:-0}" -eq $((${feedbacks:-0} + 1)) ] 2>/dev/null && verdict ok "bar drag: one feedback sound, at the release" || verdict no "bar drag: one feedback sound, at the release ($feedbacks -> $after)"
first=$(grep -m1 'via=slider final=0' "$out/bar-drag.log" | sed -n 's/.* at_ms=\([0-9]*\).*/\1/p')
final=$(grep 'via=slider final=1' "$out/bar-drag.log" | tail -1 | sed -n 's/.* at_ms=\([0-9]*\).*/\1/p')
span=$((${final:-0} - ${first:-0}))
echo "bar: first step at ${first:-?} ms, release at ${final:-?} ms (span $span ms; the drag itself is 1600 ms of steps)"
[ -n "$first" ] && [ -n "$final" ] && [ "$span" -le 3600 ] && verdict ok "bar drag: zdesktop kept up ($span ms)" || verdict no "bar drag: zdesktop kept up ($span ms)"
held=$(grep -c 'via=held' "$out/bar-drag.log")
[ "${held:-1}" = 0 ] && verdict ok "bar drag: no held sound during the drag" || verdict no "bar drag: no held sound during the drag ($held)"
sleep 1
set -- $(audiod_volume)
[ "${1:-0}" -ge 49 ] && [ "${1:-0}" -le 51 ] && verdict ok "bar drag: audiod at ${1:-?}" || verdict no "bar drag: audiod at ${1:-?}"
shot bar-after.png
closes=$(count $log 'ZWL VOLUME popup close via=icon')
pointer move $ix $iy sleep 100 down sleep 60 up
expect_more $log 'ZWL VOLUME popup close via=icon' "$closes" 3

# 2. Settings' Sound page, run as kei (the compositor serves its system extension only to its own user).
maps=$(count $log 'ZWL MAP client=')
guest "export XDG_RUNTIME_DIR=/run/user/1000 HOME=/home/kei; /bin/runas kei /bin/settings --timeout-s=600 sound > $slog 2>&1 </dev/null & sleep 6; echo started" >/dev/null
expect_more $log 'ZWL MAP client=' "$maps" 20
set -- $(last $log 'ZWL MAP client=' | sed -n 's/.* x=\([-0-9]*\) y=\([-0-9]*\).*/\1 \2/p')
wx=${1:-0}; wy=${2:-0}
expect_more $slog 'SOUND report reachable=1 device=1' 0 10
expect_more $slog 'ZSETTINGS CONTROL index=6 ' 0 10
set -- $(last $slog 'ZSETTINGS CONTROL index=6 ' | sed -n 's/.* x=\([-0-9]*\) y=\([-0-9]*\) width=\([0-9]*\) height=\([0-9]*\).*/\1 \2 \3 \4/p')
cx0=${1:-0}; cy0=${2:-0}; cw=${3:-0}; ch=${4:-0}
cy=$((wy + cy0 + ch / 2))
s20=$((wx + cx0 + 12 + (cw - 24) * 20 / 100)) s90=$((wx + cx0 + 12 + (cw - 24) * 90 / 100)) s30=$((wx + cx0 + 12 + (cw - 24) * 30 / 100))
feedbacks=$(count $slog 'SOUND feedback error=0')
pointer $(drag_steps $s20 $s90 $s30 $cy 60)
expect_more $slog 'SOUND set value=(29|30|31) muted=0 final=1' 0 10
sleep 1
after=$(count $slog 'SOUND feedback error=0')
echo "settings: feedback $feedbacks -> $after"
[ "${after:-0}" -eq $((${feedbacks:-0} + 1)) ] 2>/dev/null && verdict ok "settings drag: one feedback sound, at the release" || verdict no "settings drag: one feedback sound, at the release ($feedbacks -> $after)"
set -- $(audiod_volume)
[ "${1:-0}" -ge 29 ] && [ "${1:-0}" -le 31 ] && verdict ok "settings drag: audiod at ${1:-?}" || verdict no "settings drag: audiod at ${1:-?}"
shot settings-after.png

# 3. The logs, and no errors.
guest "grep -E 'ZSETTINGS SOUND' $slog" > "$out/settings-sound.log"
guest "grep -E 'ZWL (VOLUME|ERROR|PERF)' $log" > "$out/session-volume.log"
grep -q 'ZWL ERROR' "$out/session-volume.log" && { echo "ZWL ERROR in the session"; status=1; }
alive=$(guest "ps -A -o args | grep -c '[s]ettings'" | tail -1)
[ "${alive:-0}" -ge 1 ] && verdict ok "settings runs" || verdict no "settings runs"
sh plan/ws100/tests/volume-guest.sh stop >/dev/null 2>&1
[ $status -eq 0 ] && echo "volume-bug170: PASS" || echo "volume-bug170: FAIL"
exit $status
