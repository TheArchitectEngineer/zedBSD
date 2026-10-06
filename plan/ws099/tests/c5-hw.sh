#!/bin/sh
# ws099-p002 (C5): App Home's and Wiseview's openings and closings on the 5330's i915 passthrough (WS075's harness,
# plan/ws075/tests/hdmi-h4-hw.sh, which takes the machine under flock /tmp/i915-hw.lock), with the ten applications
# of App Home open (hdmi/apps8.sh's tiles, as resize-hw.sh).  ROUNDS times: Wiseview opened with Super+Tab and closed
# with Esc, App Home opened with the launcher and closed with Esc.  The compositor logs each request's first frame
# (KWL FIRST_FRAME what=... ms=..., the request to the frame's submission, ws099-p002) in the session's log, which
# Terminal copies to /home/kei/c5-hw.log (/run is not on the disk) and is read from the image afterwards.
# Prints the FIRST_FRAME lines, "C5-HW RESULT count=N max_ms=M over=K" and "c5-hw: PASS" (every one within LIMIT_MS,
# default 100) or "c5-hw: FAIL".
#
#   plan/ws099/tests/c5-hw.sh IMAGE OUTDIR [ROUNDS]     (IMAGE: plan/ws075/demo/build-demo-image.sh BUILD passthrough)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
image=$1
out=$2
rounds=${3:-3}
limit=${LIMIT_MS:-100}
host=${I915_HOST:-solaris10-man}
h4=plan/ws075/tests/hdmi-h4-hw.sh
ctl() { timeout 120 "$h4" ctl "$@"; }
mkdir -p "$out"

# The machine, the session and the ten applications.
H4_MINUTES=${H4_MINUTES:-20} "$h4" start "$image" "$out" || exit 1
sleep 75
for tile in files:600:386 notes:743:386 settings:887:386 terminal:1031:386 pdf:1175:386 images:1319:386 browser:600:538 mview:743:538 gears:887:538 xterm:1031:538; do
	set -- $(echo "$tile" | tr : ' ')
	ctl pointer move 22 16 sleep 100 down up sleep 1500 move "$2" "$3" sleep 150 down up sleep 8000 > /dev/null
done
ctl shot opened > /dev/null

# The openings and closings.
n=1
while [ $n -le "$rounds" ]; do
	ctl hmp "sendkey meta_l-tab" > /dev/null
	sleep 2
	[ $n -eq 1 ] && ctl shot wiseview > /dev/null
	ctl hmp "sendkey esc" > /dev/null
	sleep 2
	ctl pointer move 22 16 sleep 100 down up sleep 2000 > /dev/null
	[ $n -eq 1 ] && ctl shot home > /dev/null
	ctl hmp "sendkey esc" > /dev/null
	sleep 2
	n=$((n + 1))
done

# The session's log onto the disk: Terminal (App Home), a command, and the log read from the image.
ctl pointer move 22 16 sleep 100 down up sleep 1500 move 1031 386 sleep 150 down up sleep 6000 > /dev/null
ctl keys "'cp /run/user/1000/session.log /home/kei/c5-hw.log; sync\\n'" > /dev/null
sleep 4
ctl shot saved > /dev/null
ssh "$host" "sudo -n python3 bigbang/h4/h4-ctl.py quit" > /dev/null 2>&1
sleep 3
scp -q plan/ws031/tests/ufs-cat.py "$host:bigbang/"
ssh "$host" "python3 bigbang/ufs-cat.py bigbang/h4/guest.img /home/kei/c5-hw.log" > "$out/session.log" 2>&1
"$h4" stop "$out" > /dev/null

# The verdict from the log (the Terminal's own Home opening at the end counts too).
grep 'KWL FIRST_FRAME' "$out/session.log" | tee "$out/first-frames.txt"
python3 - "$out/first-frames.txt" "$limit" <<'PY'
import re, sys
values = [int(m.group(1)) for m in (re.search(r' ms=(\d+)', l) for l in open(sys.argv[1])) if m]
limit = int(sys.argv[2])
over = sum(v > limit for v in values)
print("C5-HW RESULT count=%d max_ms=%s over=%d" % (len(values), max(values) if values else '?', over))
sys.exit(0 if values and over == 0 else 1)
PY
[ $? -eq 0 ] && { echo "c5-hw: PASS"; exit 0; }
echo "c5-hw: FAIL"
exit 1
