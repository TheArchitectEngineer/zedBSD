#!/bin/sh
# ws138-p002: the compositor reads the wallpaper as a PNG and as a JPEG, refuses a PPM, and how long the reading takes.
#
#   plan/ws089/tests/settings-guest.sh start BUILD/hdd-image.img     (the Settings image: /bin/wayland, keiland-settings)
#   plan/ws138/tests/wallpaper-time.sh [OUTDIR]                      (default build/ws138-p002/time)
#
# The host makes the pictures from the tree's Birch-Lake.png: the PNG itself, a JPEG of it (ImageMagick, quality 92)
# and a PPM of the same pixels (python3, ppm-to-png.py's read_png).  All three go to the guest's /tmp, so the disk
# cache treats them alike.
#  1. The start: zdesktop --testing --glass at 1280x800 with --wallpaper=, PNG and JPEG in turn, four times each; the first of
#     each is dropped (the cache warms).  The medians of "ZWL STARTUP step=wallpaper ms=" and
#     "step=wallpaper-picture ms=" are printed.  A "ZWL GLASS no wallpaper" line fails the run.
#  2. The PPM is refused: "ZWL GLASS no wallpaper: path=/tmp/w.ppm errno=22", and the landscape is drawn instead.
#  3. A wallpaper chosen during the session (glass.c's thread, ws138 U7): zdesktop without a picture, keiland-settings
#     sets /tmp/w.png, then /tmp/w.jpg, then resets: "ZWL GLASS wallpaper path=... ms=" for each (the reset is the
#     landscape, path=-).
# Only "both read, the PPM refused" is judged; the times are recorded, not judged (the QEMU host has no GPU for the
# guest and its CPU time varies with other load).  Prints "wallpaper-time: PASS" or FAIL.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
GUEST_RUNTIME="${GUEST_RUNTIME:-$(pwd)/build/ws089-run}"
export GUEST_RUNTIME
out=${1:-build/ws138-p002/time}
mkdir -p "$out"
status=0
guest() { timeout 90 python3 plan/tools/guest/guest.py run "$1" 2>&1 </dev/null; }
put() { timeout 120 python3 plan/tools/guest/guest.py put "$1" "$2" >/dev/null 2>&1 </dev/null || { echo "put $1: FAILED"; status=1; }; }
stop_all='service stop greeter >/dev/null 2>&1; for p in $(ps -A -o pid,args | grep -E "[w]ayland( |$)|[s]ettings" | awk "{print \$1}"); do kill $p; done; i=0; while ps -A -o args | grep -qE "[w]ayland( |$)|[s]ettings" && [ $i -lt 50 ]; do sleep 0.2; i=$((i+1)); done'

# The pictures.
tree=userland/desktop/keiland/wallpapers/Birch-Lake.png
cp "$tree" "$out/w.png"
convert "$tree" -quality 92 "$out/w.jpg" || { echo "wallpaper-time: FAIL: no JPEG (ImageMagick)"; exit 1; }
python3 - "$tree" "$out/w.ppm" <<'EOF'
import importlib.util, sys
spec = importlib.util.spec_from_file_location('p2p', 'userland/desktop/wallpapers/ppm-to-png.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
width, height, rgb = module.read_png(sys.argv[1])
with open(sys.argv[2], 'wb') as stream:
    stream.write(b'P6\n%d %d\n255\n' % (width, height) + rgb)
EOF
for kind in png jpg ppm; do
	put "$out/w.$kind" "/tmp/w.$kind"
done

# Starts zdesktop for a few seconds with a wallpaper (or none) and prints its startup lines.
run_start() {
	guest "$stop_all" >/dev/null
	guest "export XDG_RUNTIME_DIR=/tmp HOME=/root; rm -f /tmp/wayland-0 /root/.config/keiland/desktop.conf; /bin/wayland --testing --timeout=8 --width=1280 --height=800 --glass $1 > /tmp/wt.log 2>&1 </dev/null; grep -E 'ZWL STARTUP step=wallpaper|ZWL GLASS no wallpaper' /tmp/wt.log"
}

# The median of the numbers on standard input.
median() { sort -n | awk '{ v[NR] = $1 } END { if (NR == 0) print "-"; else if (NR % 2) print v[(NR + 1) / 2]; else print (v[NR / 2] + v[NR / 2 + 1]) / 2 }'; }

# 1. The start, PNG and JPEG in turn.
: > "$out/start.txt"
for round in 1 2 3 4; do
	for kind in png jpg; do
		lines=$(run_start "--wallpaper=/tmp/w.$kind")
		echo "$lines" | sed "s/^/$kind round=$round /" >> "$out/start.txt"
		if echo "$lines" | grep -q 'ZWL GLASS no wallpaper'; then
			echo "start $kind round $round: no wallpaper"
			status=1
		fi
	done
done
echo "start (median of rounds 2-4, ms):"
for kind in png jpg; do
	whole=$(grep "^$kind round=[234] ZWL STARTUP step=wallpaper ms=" "$out/start.txt" | sed 's/.*ms=//' | median)
	picture=$(grep "^$kind round=[234] ZWL STARTUP step=wallpaper-picture ms=" "$out/start.txt" | sed 's/.*ms=//' | median)
	echo "  $kind step=wallpaper $whole step=wallpaper-picture $picture"
done

# 2. The PPM is refused.
lines=$(run_start "--wallpaper=/tmp/w.ppm")
echo "$lines" > "$out/ppm.txt"
if echo "$lines" | grep -q 'ZWL GLASS no wallpaper: path=/tmp/w.ppm errno=22'; then
	echo "ppm: refused (errno 22) ok"
else
	echo "ppm: NOT refused"
	status=1
fi

# 3. Chosen during the session, then reset.
guest "$stop_all" >/dev/null
guest "export XDG_RUNTIME_DIR=/tmp HOME=/root; rm -f /tmp/wayland-0 /root/.config/keiland/desktop.conf /tmp/probe.log; /bin/wayland --testing --timeout=120 --width=1280 --height=800 --glass > /tmp/wt.log 2>&1 </dev/null & sleep 6; echo started" >/dev/null
for value in "set wallpaper /tmp/w.png" "set wallpaper /tmp/w.jpg" "reset wallpaper"; do
	guest "export XDG_RUNTIME_DIR=/tmp HOME=/root; /bin/keiland-settings $value >> /tmp/probe.log 2>&1; echo done" >/dev/null
	sleep 2
done
guest "grep -E 'ZWL GLASS wallpaper path=|ZWL GLASS no wallpaper' /tmp/wt.log" > "$out/session.txt"
cat "$out/session.txt"
for expected in 'path=/tmp/w.png ms=' 'path=/tmp/w.jpg ms=' 'path=- ms='; do
	if ! grep -q "ZWL GLASS wallpaper $expected" "$out/session.txt"; then
		echo "session: ZWL GLASS wallpaper $expected MISSING"
		status=1
	fi
done
guest "$stop_all" >/dev/null

if [ $status = 0 ]; then
	echo "wallpaper-time: PASS"
else
	echo "wallpaper-time: FAIL"
fi
exit $status
