#!/bin/sh
# ws100-p008: the time from a volume change (a wheel notch over the icon, a release on the slider) to the start of its
# feedback sound, on the Venus guest of the volume image (build-volume-image.sh) with QEMU's HD Audio (volume-guest.sh),
# kei's session at boot.  audiod is started again with AUDIOD_TIMING_LOG (it logs when a feedback sound is asked for,
# the device byte and time it is mixed at, and the time the device takes that byte); zdesktop logs the change and the
# request (ZWL VOLUME set/feedback at_ms).  Both clocks are the guest's CLOCK_MONOTONIC.  For each change: zdesktop's
# request after the change, audiod's receipt, the mix, and the device reaching the sound; the medians against the
# target (FEEDBACK_TARGET_MS, 50 ms).  Then the host's measure (wav-latency.py): from the QMP input to the sound's start
# in QEMU's WAV, which also counts the input's way into the guest and QEMU's codec buffer (up to 8 KiB, which the
# guest's measure cannot see: the DMA position runs ahead of the sound).  The WAV is kept (the guest stopped at the end).
#   plan/ws100/tests/feedback-latency.sh IMAGE [OUTDIR]     (NOTCHES: wheel notches, default 10; RELEASES: 5)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
image=${1:?usage: feedback-latency.sh IMAGE [OUTDIR]}
out=${2:-build/ws100-shots/p008}
mkdir -p "$out"
GUEST_RUNTIME=$(pwd)/build/ws100-run
export GUEST_RUNTIME
log=/run/user/1000/session.log
guest() { timeout 90 python3 plan/tools/guest/guest.py run "$1" 2>&1 </dev/null; }
pointer() { python3 plan/ws035/tests/qmp-pointer.py "$GUEST_RUNTIME/qmp.sock" "$@"; }
count() { guest "grep -cE '$2' $1" | tail -1; }
last() { guest "grep -E '$1' $log | tail -1"; }
notches=${NOTCHES:-10}
releases=${RELEASES:-5}
status=0

# Waits until a guest file has more than N lines matching a pattern (within some seconds).
expect_more() {
	tries=0
	while [ $tries -lt "$4" ]; do
		found=$(count "$1" "$2")
		[ "${found:-0}" -gt "$3" ] 2>/dev/null && return 0
		tries=$((tries + 1))
		sleep 1
	done
	echo "log: $2 MISSING"
	status=1
	return 1
}

# The guest with HD Audio recording, and kei's session.
sh plan/ws100/tests/volume-guest.sh stop >/dev/null 2>&1
VOLUME_AUDIO=duplex timeout 180 sh plan/ws100/tests/volume-guest.sh start "$image" >/dev/null 2>&1
sleep 35
expect_more $log 'ZWL HANDOFF go=1' 0 60
expect_more $log 'ZWL VOLUME reachable=1 device=1' 0 20

# audiod again, timing its feedback sounds; zdesktop reaches it again by itself.
reached=$(count $log 'ZWL VOLUME reachable=1 device=1')
guest 'service stop audiod >/dev/null 2>&1; sleep 1; rm -f /tmp/audiod-timing.log; AUDIOD_TIMING_LOG=/tmp/audiod-timing.log /sbin/audiod > /tmp/audiod.out 2>&1 </dev/null & sleep 1; echo started' >/dev/null
expect_more $log 'ZWL VOLUME reachable=1 device=1' "$reached" 20
sleep 2

# The icon.
set -- $(last 'ZWL VOLUME icon x=' | sed -n 's/.* x=\([0-9]*\) y=\([0-9]*\) width=\([0-9]*\) height=\([0-9]*\).*/\1 \2 \3 \4/p')
ix=$((${1:-900} + ${3:-30} / 2)); iy=$((${2:-3} + ${4:-28} / 2))

# Wheel notches over the icon, down and up in turn, well apart; the host's times of the notches and the WAV's size
# (wav-latency.py record) for the host's measure.
steps="move $ix $iy sleep 500"
i=0
while [ $i -lt "$notches" ]; do
	[ $((i % 2)) -eq 0 ] && steps="$steps wheel-down sleep 800" || steps="$steps wheel-up sleep 800"
	i=$((i + 1))
done
python3 plan/ws100/tests/wav-latency.py record "$GUEST_RUNTIME/qmp.sock" "$GUEST_RUNTIME/out.wav" "$out/wheel-events.json" $steps

# Releases on the slider: the popup, a press and a release at a place, a moment apart.
pointer move $((ix - 2)) $iy sleep 200 move $ix $iy sleep 300 down sleep 60 up sleep 800
set -- $(last 'ZWL VOLUME popup open' | sed -n 's/.* x=\([0-9]*\) y=\([0-9]*\) width=\([0-9]*\) height=\([0-9]*\) slider=\([0-9]*\) mute=\([0-9]*\).*/\1 \2 \3 \4 \5 \6/p')
px=${1:-900} pw=${3:-260} slider=${5:-100}
track_left=$((px + 23)) track_width=$((pw - 46)) sy=$((slider + 17))
steps=""
i=0
while [ $i -lt "$releases" ]; do
	x=$((track_left + track_width * (3 + (i % 2) * 3) / 10))
	steps="$steps move $x $sy sleep 300 down sleep 80 up sleep 900"
	i=$((i + 1))
done
python3 plan/ws100/tests/wav-latency.py record "$GUEST_RUNTIME/qmp.sock" "$GUEST_RUNTIME/out.wav" "$out/release-events.json" $steps

# The lines, and the guest stopped (the WAV complete).
sleep 1
guest "grep -E 'ZWL VOLUME (set|feedback)' $log" > "$out/zdesktop.log"
guest 'cat /tmp/audiod-timing.log' > "$out/audiod.log"
sh plan/ws100/tests/volume-guest.sh stop >/dev/null 2>&1
cp "$GUEST_RUNTIME/out.wav" "$out/out.wav" 2>/dev/null

# Each change's way, and the medians.
python3 - "$out/zdesktop.log" "$out/audiod.log" "${FEEDBACK_TARGET_MS:-50}" <<'PY' | tee "$out/latency.txt"
import re, statistics, sys
sets = [(int(m.group(2)), m.group(1)) for m in (re.search(r'ZWL VOLUME set .*via=(\S+) final=1 at_ms=(\d+)', l) for l in open(sys.argv[1])) if m]
requests = [int(m.group(1)) for m in (re.search(r'ZWL VOLUME feedback at_ms=(\d+)', l) for l in open(sys.argv[1])) if m]
asked, mixed, played = [], [], []
for line in open(sys.argv[2]):
	m = re.search(r'feedback asked at_ms=(\d+)', line)
	if m:
		asked.append(int(m.group(1)))
	m = re.search(r'feedback mixed at_ms=(\d+) .* ahead_ms=(\d+)', line)
	if m:
		mixed.append((int(m.group(1)), int(m.group(2))))
	m = re.search(r'feedback played at_ms=(\d+) seen_ms=(\d+) asked_ms=(\d+)', line)
	if m:
		played.append((int(m.group(1)), int(m.group(3))))
target = int(sys.argv[3])
rows = []
for op, via in sets:
	request = next((r for r in requests if 0 <= r - op < 50), None)
	ask = next((a for a in asked if request is not None and 0 <= a - request < 200), None)
	mix = next((x for x in mixed if ask is not None and 0 <= x[0] - ask < 200), None)
	play = next((p for p in played if ask is not None and p[1] == ask), None)
	if None in (request, ask, mix, play):
		print('change via=%s at_ms=%d: incomplete (request=%s asked=%s mixed=%s played=%s)' % (via, op, request, ask, mix, play))
		continue
	row = {'via': via, 'request': request - op, 'ipc': ask - request, 'to_mix': mix[0] - ask, 'ahead': mix[1], 'to_play': play[0] - mix[0], 'total': play[0] - op}
	rows.append(row)
	print('change via=%s total=%d request=%d ipc=%d to_mix=%d mixed_ahead=%d to_play=%d' % (via, row['total'], row['request'], row['ipc'], row['to_mix'], row['ahead'], row['to_play']))
if rows:
	med = lambda k: statistics.median(r[k] for r in rows)
	print('RESULT changes=%d total_ms=%d (target %d) request=%d ipc=%d to_mix=%d mixed_ahead=%d to_play=%d max=%d %s' % (
		len(rows), med('total'), target, med('request'), med('ipc'), med('to_mix'), med('ahead'), med('to_play'),
		max(r['total'] for r in rows), 'within' if med('total') <= target else 'OVER'))
else:
	print('RESULT no changes measured')
	sys.exit(1)
PY
[ $? -eq 0 ] || status=1

# The host's measure: from the input (QMP) to the sound's start in the WAV.
for kind in wheel release; do
	echo "host: $kind"
	python3 plan/ws100/tests/wav-latency.py analyse "$out/out.wav" "$out/$kind-events.json" "${FEEDBACK_TARGET_MS:-50}" | tee "$out/wav-$kind.txt"
done
exit $status
