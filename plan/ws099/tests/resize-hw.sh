#!/bin/sh
# ws099-p011 (BUG-121): the corner drags of resize-stress.sh on the 5330's i915 passthrough (WS075's harness,
# plan/ws075/tests/hdmi-h4-hw.sh, which takes the machine under flock /tmp/i915-hw.lock), in the case BUG-121 was
# seen in (ws075-p025): the ten applications of App Home open (Model viewer last, so it is on top), then COUNT drags
# of Model viewer's corners, in pairs (inwards, then back by the same distance, so its outline comes back and the
# drags need no log to aim).  Its body is found on the first shot (the viewer's grey, 0x333333).  Every drag is
# photographed; a shot without the grey body is a vanished window.  At the end Terminal copies the session's log
# (/run is not on the disk) to /home/kei/resize-hw.log, which is read from the image before the machine is given back.
# Prints "RESIZE-HW RESULT drags=N vanished=V" and "resize-hw: PASS" or "resize-hw: FAIL".
#
#   plan/ws099/tests/resize-hw.sh IMAGE OUTDIR [COUNT]      (IMAGE: plan/ws075/demo/build-demo-image.sh BUILD passthrough)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
image=$1
out=$2
total=${3:-20}
host=${I915_HOST:-solaris10-man}
h4=plan/ws075/tests/hdmi-h4-hw.sh
ctl() { timeout 120 "$h4" ctl "$@"; }
mkdir -p "$out"

# The grey body's outline in a PNG (left top right bottom), or nothing.
body() {
	python3 - "$1" <<'EOF'
import sys
from PIL import Image
p = Image.open(sys.argv[1]).convert("RGB")
w, h = p.size
data = p.load()
rows = []
cols = {}
for y in range(0, h, 2):
	n = 0
	first = last = None
	for x in range(0, w, 2):
		r, g, b = data[x, y]
		if abs(r - 51) <= 3 and abs(g - 51) <= 3 and abs(b - 51) <= 3:
			n += 1
			first = x if first is None else first
			last = x
	if n > 100:
		rows.append((y, first, last))
if len(rows) > 50:
	print(min(r[1] for r in rows), rows[0][0], max(r[2] for r in rows) + 2, rows[-1][0] + 2)
EOF
}

# Drags a point by (DX, DY) in 12 steps over half a second (h4-ctl.py's drag).
drag() {
	ctl pointer drag "$1" "$2" $(($1 + $3)) $(($2 + $4)) sleep 400 > /dev/null
}

# The machine, the session and the applications (App Home on the 1920x1080 panel, hdmi/apps8.sh's tiles).
"$h4" start "$image" "$out" || exit 1
sleep 75
for tile in files:600:386 notes:743:386 settings:887:386 terminal:1031:386 pdf:1175:386 images:1319:386 \
    browser:600:538 gears:887:538 xterm:1031:538 mview:743:538; do
	set -- $(echo "$tile" | tr : ' ')
	ctl pointer move 22 16 sleep 100 down up sleep 1500 move "$2" "$3" sleep 150 down up sleep 8000 > /dev/null
done
ctl shot opened > /dev/null
"$h4" fetch "$out" > /dev/null
set -- $(body "$out/shots/opened-live.png")
if [ -z "${1:-}" ]; then
	echo "no Model viewer body on the first shot"
	"$h4" stop "$out" > /dev/null
	echo "RESIZE-HW RESULT drags=0 vanished=?"
	echo "resize-hw: FAIL"
	exit 1
fi
left=$1 top=$(($2 - 52)) right=$3 bottom=$4
echo "Model viewer: body $1,$2 to $3,$4"

# The drags: corner (N / 2) % 4, inwards on even N and back outwards on odd N.
n=0
while [ $n -lt "$total" ]; do
	distance=$((60 + (n / 2 * 37) % 137))
	sign=1
	[ $((n % 2)) -eq 1 ] && sign=-1
	# The corner: inwards from the outline (the screen's edges cannot cut it), then back from where that drag left it.
	case $(((n / 2) % 4)) in
	0) x=$((right + 3)) y=$((bottom + 3)) dx=1 dy=1 ;;
	1) x=$((left - 4)) y=$((top - 4)) dx=-1 dy=-1 ;;
	2) x=$((right + 3)) y=$((top - 4)) dx=1 dy=-1 ;;
	*) x=$((left - 4)) y=$((bottom + 3)) dx=-1 dy=1 ;;
	esac
	if [ $sign -eq 1 ]; then
		drag "$x" "$y" $((-dx * distance)) $((-dy * distance))
	else
		drag $((x - dx * distance)) $((y - dy * distance)) $((dx * distance)) $((dy * distance))
	fi
	ctl shot "drag-$n" > /dev/null
	n=$((n + 1))
done

# The session's log onto the disk: Terminal (App Home), a command, and the log read from the image.
ctl pointer move 22 16 sleep 100 down up sleep 1500 move 1031 386 sleep 150 down up sleep 6000 > /dev/null
ctl keys 'cp /run/user/1000/session.log /home/kei/resize-hw.log; sync\n' > /dev/null
sleep 4
ctl shot saved > /dev/null
ssh "$host" "sudo -n python3 bigbang/h4/h4-ctl.py quit" > /dev/null 2>&1
sleep 3
scp -q plan/ws031/tests/ufs-cat.py "$host:bigbang/"
ssh "$host" "python3 bigbang/ufs-cat.py bigbang/h4/guest.img /home/kei/resize-hw.log" > "$out/session.log" 2>&1
"$h4" stop "$out" > /dev/null

# The verdict from the shots.
vanished=0
n=0
while [ $n -lt "$total" ]; do
	found=$(body "$out/shots/drag-$n-live.png")
	if [ -z "$found" ]; then
		vanished=$((vanished + 1))
		echo "drag $n: VANISHED"
	else
		echo "drag $n: body $found"
	fi
	n=$((n + 1))
done
grep -E 'MVIEW (DONE|FAILED)|ZWL (ERROR|GPU_ERROR)|ZWL CLIENT gone|ZWL GLASS (close|minimize)|ZWL UNMAP' "$out/session.log" | tee "$out/events.txt"
echo "RESIZE-HW RESULT drags=$total vanished=$vanished"
[ "$vanished" -eq 0 ] && { echo "resize-hw: PASS"; exit 0; }
echo "resize-hw: FAIL"
exit 1
