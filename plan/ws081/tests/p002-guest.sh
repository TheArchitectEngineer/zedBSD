#!/bin/sh
# ws081-p002: the touch screen's Scan Time and one time per report, on the
# pen test guest (plan/ws079/tests/config-amd64-pen.mk, the WS079 injector
# and touchinject), started with
#   GUEST_RUNTIME=$PWD/build/ws081-run plan/ws079/tests/pen-guest.sh start build/amd64/hdd-image.img
#
#  1. touchinject -c: the refusals as before (14 cases; the reserved case now
#     sets an unknown bit, since bit 0 declares a Scan Time).
#  2. touchinject -s: a screen with a Scan Time refuses a pen with one, an
#     unknown bit and a Scan Time past 65535, declares MSC_TIMESTAMP (EVIOCGBIT
#     EV_MSC), and reads back down, move, still and lift as MSC_TIMESTAMP 0,
#     8300, 16600, 24900 right before SYN_REPORT, the still frame alone, one
#     time for every event of a frame.
#  3. A 90 Hz swipe with +-3 ms of jitter on a screen with a Scan Time, read
#     with touchinject -t: MSC_TIMESTAMP steps are the script's intervals
#     (8.1 to 14.1 ms) whatever the guest's sleeps did, and every frame's
#     events share one evdev time.
#
#   plan/ws081/tests/p002-guest.sh [OUTDIR]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
export GUEST_RUNTIME="${GUEST_RUNTIME:-$PWD/build/ws081-run}"
out=${1:-build/ws081-p002-guest}
mkdir -p "$out"
guest() { timeout 120 python3 plan/tools/guest/guest.py run "$1" 2>&1; }
status=0

# 1. The refusals, as before.
guest '/bin/touchinject -c' > "$out/touchcheck.txt"
if grep -q 'TOUCHCHECK result=ok passed=14 failed=0' "$out/touchcheck.txt"; then
	echo "touchcheck: ok"
else
	echo "touchcheck: FAILED"
	status=1
fi

# 2. The Scan Time check.
guest '/bin/touchinject -s' > "$out/scancheck.txt"
if grep -q 'SCANCHECK result=ok passed=15 failed=0' "$out/scancheck.txt"; then
	echo "scancheck: ok"
else
	echo "scancheck: FAILED"
	cat "$out/scancheck.txt"
	status=1
fi

# 3. A jittered 90 Hz swipe, read with the times.
guest '/bin/touchinject -t 4000 > /tmp/scandump.txt 2>&1 & sleep 0.3; printf "size 1000 1000 2 scan\ndown 1 100 500\nswipe 400 0 40 11.111 3\nup 1\n" | /bin/touchinject; echo replay=$?; sleep 4; cat /tmp/scandump.txt' > "$out/scandump.txt"
grep -q '^replay=0$' "$out/scandump.txt" || { echo "jittered replay: FAILED"; status=1; }
python3 - "$out/scandump.txt" <<'EOF' || status=1
import sys
stamps = []
frame_times = set()
mixed = 0
for line in open(sys.argv[1]):
    parts = line.split()
    if len(parts) < 5 or parts[0] != "TOUCHDUMP" or parts[1] != "event":
        continue
    time = parts[4]
    frame_times.add(time)
    if parts[2] == "MSC_TIMESTAMP":
        stamps.append(int(parts[3]))
    if parts[2] == "SYN_REPORT":
        if len(frame_times) != 1:
            mixed += 1
        frame_times = set()
steps = [b - a for a, b in zip(stamps, stamps[1:])]
inside = [s for s in steps if 8100 <= s <= 14200]
print("jittered swipe: %d frames, %d steps, %d within 8.1..14.2 ms, %d frames with mixed times, steps %s" %
      (len(stamps), len(steps), len(inside), mixed, steps[:12]))
ok = len(stamps) >= 38 and len(inside) >= len(steps) - 1 and mixed == 0
print("jittered swipe: " + ("ok" if ok else "FAILED"))
sys.exit(0 if ok else 1)
EOF

[ $status -eq 0 ] && echo "p002: PASS" || echo "p002: FAIL"
exit $status
