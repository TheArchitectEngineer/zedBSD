#!/bin/sh
# Video Player with its own presenter on the Venus guest (QEMU, amd64): the guest's GPU decodes no video, so the clip
# is decoded by libavcodec on the CPU and the presenter draws the pictures the CPU scaled.  The image is
# plan/tools/rtx-video/config-amd64-venus-video.mk's (plan/tools/guest/test-image.sh); the guest is
# plan/ws035/tests/zdesktop-guest.sh's.  Starts the guest, the compositor (zdesktop --testing, 1280x800), plays CLIP
# in a window for 15 s, photographs the screen 5 s after the first picture (OUT/play.png),
# keeps the player's lines (OUT/vp.log, through SSH; nothing reads the console), prints a summary (OUT/summary.txt)
# and stops the guest.
#   GUEST_RUNTIME=... VENUS_RENDERER=... plan/tools/rtx-video/venus-videoplayer.sh OUT IMAGE CLIP   (from the top of the tree)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
out=${1:?usage: venus-videoplayer.sh OUT IMAGE CLIP}
image=${2:?usage: venus-videoplayer.sh OUT IMAGE CLIP}
clip=${3:?usage: venus-videoplayer.sh OUT IMAGE CLIP}
GUEST_RUNTIME="${GUEST_RUNTIME:-$(pwd)/build/ws243-venus-run}"
export GUEST_RUNTIME
mkdir -p "$out"
summary=$out/summary.txt
: > "$summary"
guest() { timeout 90 python3 plan/tools/guest/guest.py run "$1" 2>&1 </dev/null; }

say() {
	echo "$(date +%H:%M:%S) $*" | tee -a "$summary"
}

# The guest, until SSH answers.
timeout 300 sh plan/ws035/tests/zdesktop-guest.sh start "$image" > "$out/start.log" 2>&1 || { say "FAIL start (start.log)"; exit 1; }
timeout 600 python3 plan/tools/guest/guest.py wait > "$out/wait.log" 2>&1 </dev/null || { say "FAIL ssh (wait.log)"; sh plan/ws035/tests/zdesktop-guest.sh stop > /dev/null 2>&1; exit 1; }
say "guest up: $(guest 'uname -v')"

# The clip in; the compositor and then the player in one SSH session held open meanwhile (a session that ends takes
# what it started with it): the player in a window for 15 s once the compositor is ready (its start takes some
# 20 s on Lavapipe), a shot 5 s after its first picture.
timeout 120 python3 plan/tools/guest/guest.py put "$clip" /tmp/clip.mp4 > /dev/null 2>&1 </dev/null || say "FAIL put clip"
timeout 300 python3 plan/tools/guest/guest.py run 'export XDG_RUNTIME_DIR=/tmp HOME=/tmp; rm -f /tmp/wayland-0 /tmp/vp.log
/bin/wayland --testing --timeout=300 --width=1280 --height=800 --glass > /tmp/zdesktop.log 2>&1 </dev/null &
w=0; while ! grep -q "KWL GLASS ready" /tmp/zdesktop.log 2> /dev/null && [ $w -lt 360 ]; do sleep 0.5; w=$((w + 1)); done
sleep 2
/bin/videoplayer --width=960 --height=540 --timeout-s=15 /tmp/clip.mp4 > /tmp/vp.log 2>&1 </dev/null' > "$out/session.log" 2>&1 </dev/null &
session=$!
w=0
while [ $w -lt 60 ] && ! guest 'grep -q "VIDEOPLAYER PRESENTED" /tmp/vp.log && echo shown' | grep -q shown; do
	sleep 4
	w=$((w + 1))
done
sleep 5
python3 plan/ws035/tests/zdesktop-check.py "$out/play.png" --runtime "$GUEST_RUNTIME" > "$out/shot.log" 2>&1
wait "$session"
guest 'cat /tmp/vp.log' > "$out/vp.log"
guest 'cat /tmp/zdesktop.log' > "$out/zdesktop.log"

# The summary.
say "PRESENT: $(grep -m1 'VIDEOPLAYER PRESENT' "$out/vp.log")"
say "MEDIA: $(grep -m1 'MEDIA backend' "$out/vp.log") | $(grep -m1 'VIDEOPLAYER OPEN ' "$out/vp.log" | cut -c1-160)"
say "PRESENTED: $(grep -m1 'VIDEOPLAYER PRESENTED' "$out/vp.log") | $(grep 'VIDEOPLAYER FRAMES' "$out/vp.log" | tail -1) | $(grep 'VIDEOPLAYER DONE' "$out/vp.log" | tail -1)"
say "FAILED lines: $(grep -c 'FAILED' "$out/vp.log") | shot $(wc -c < "$out/play.png" 2> /dev/null || echo 0) bytes"

# The guest goes.
sh plan/ws035/tests/zdesktop-guest.sh stop > "$out/stop.log" 2>&1
say "done: $out"
