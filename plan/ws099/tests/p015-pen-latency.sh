#!/bin/sh
# ws099-p015: the pen's line in fullscreen Notes, before and after the fullscreen window is composed (the direct
# scanout removed), on the Venus guest with the injected pen (plan/ws079/tests/config-amd64-demo.mk's image).
# zdesktop --glass --log-frames at 1280x800 and /bin/notes --fullscreen; peninject draws STROKES strokes of 60
# moves 15 ms apart.  The image must have the per-frame time lines (ZWL LAT pen/adopt/submit/shown, ws099-p015);
# p015-lat.py reads them:
#   latency   from a pen place sent to Notes to the end of the frame that shows Notes' next image (the direct
#             present's return, or the composed frame's fence)
#   interval  between the frames that show a new Notes image during a stroke
# and Notes' own NOTES FRAMES lines (its frame's time: geometry, draw, present and wait) are printed as well.
#
#   plan/ws099/tests/p015-pen-latency.sh IMAGE OUTDIR [STROKES]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
image=$1
out=$2
strokes=${3:-6}
mkdir -p "$out"
GUEST_RUNTIME="${GUEST_RUNTIME:-$(pwd)/build/ws099/p015-run}"
export GUEST_RUNTIME
guest() { timeout 90 python3 plan/tools/guest/guest.py run "$1" 2>&1 </dev/null; }
put() { timeout 60 python3 plan/tools/guest/guest.py put "$1" "$2" >/dev/null 2>&1 </dev/null; }
wait_log() {
	tries=0
	while [ $tries -lt "$3" ]; do
		found=$(guest "grep -cE '$2' $1" | tail -1)
		[ "${found:-0}" -gt 0 ] 2>/dev/null && return 0
		tries=$((tries + 1))
		sleep 1
	done
	echo "log: $2 MISSING"
	return 1
}

# The pen script: STROKES wavy lines across the page, 60 moves each, 15 ms apart.
python3 - "$out/lat.pen" "$strokes" <<'PEN'
import math, sys
def raw(x, y):
    return round(x * 21600 / 1279), round(y * 13500 / 799)
lines = ["size 21600 13500", "wait 2000", "tool pen"]
for stroke in range(int(sys.argv[2])):
    y0 = 200 + 70 * stroke
    x, y = raw(360, y0)
    lines += ["hover %d %d" % (x, y), "wait 300", "down %d %d 1500" % (x, y)]
    for step in range(1, 61):
        x, y = raw(360 + 560 * step / 60, y0 + 25 * math.sin(step / 60 * 2 * math.pi))
        lines += ["move %d %d 2000" % (x, y), "wait 15"]
    lines += ["up", "wait 700"]
open(sys.argv[1], "w").write("\n".join(lines) + "\n")
PEN

sh plan/ws035/tests/zdesktop-guest.sh stop >/dev/null 2>&1
timeout 180 sh plan/ws035/tests/zdesktop-guest.sh start "$image" >/dev/null 2>&1
tries=0
until guest 'echo up' | grep -q '^up$' || [ $tries -ge 24 ]; do
	tries=$((tries + 1))
	sleep 5
done
put "$out/lat.pen" /tmp/lat.pen
guest 'service stop greeter >/dev/null 2>&1; rm -rf /tmp/notes-lat /root/.local/share/keiland/notes; mkdir -p /tmp/notes-lat; export XDG_RUNTIME_DIR=/tmp; rm -f /tmp/wayland-0; picture=; [ -f /usr/share/keiland/wallpaper.ppm ] && picture=--wallpaper=/usr/share/keiland/wallpaper.ppm
/bin/wayland --timeout=600 --width=1280 --height=800 --glass --log-frames $picture > /tmp/zdesktop.log 2>&1 </dev/null & sleep 4; echo started' >/dev/null
wait_log /tmp/zdesktop.log 'ZWL READY' 20
guest 'export XDG_RUNTIME_DIR=/tmp; /bin/notes --fullscreen /tmp/notes-lat/lat.pdf > /tmp/notes-lat.log 2>&1 </dev/null & sleep 4; echo started' >/dev/null
wait_log /tmp/notes-lat.log 'NOTES TABLET seat' 20
sleep 3
guest 'timeout 120 /bin/peninject /tmp/lat.pen; echo peninject=$?' | tail -1
sleep 2
python3 plan/ws035/tests/zdesktop-check.py "$out/strokes.png" --runtime "$GUEST_RUNTIME" >/dev/null 2>&1
guest 'grep -E "^ZWL (LAT|MODE)" /tmp/zdesktop.log' > "$out/lat.log"
guest "cat /tmp/zdesktop.log" > "$out/zdesktop-full.log"
guest 'grep -E "^NOTES (START|STROKE|FRAMES)" /tmp/notes-lat.log' > "$out/notes.log"
guest 'grep -cE "ZWL ERROR" /tmp/zdesktop.log' | tail -1 > "$out/errors.txt"
sh plan/ws035/tests/zdesktop-guest.sh stop >/dev/null 2>&1
grep -E "NOTES (START|FRAMES)" "$out/notes.log"
echo "ZWL ERROR lines: $(cat "$out/errors.txt")"
python3 plan/ws099/tests/p015-lat.py "$out/lat.log"
