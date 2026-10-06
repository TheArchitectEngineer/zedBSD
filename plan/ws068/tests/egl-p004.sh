#!/bin/sh
# ws068-p004: OpenGL ES 2.0's built-ins in GL's directions, gl_DepthRange and an array of samplers on the Venus guest
# (the lean image, plan/ws068/tests/build-glsl-image.sh; run by plan/ws035/tests/zdesktop-guest.sh start).
# egltest --scene=es2 draws gl_FragCoord.y / height as red (dark at the bottom, bright at the top, as GL counts rows)
# and its first frame's check reads back gl_FragCoord and gl_PointCoord on framebuffer 0 and in a framebuffer
# object, gl_DepthRange after glDepthRangef(0.25, 0.75), and the sampler array given units by glUniform1iv and
# glUniform1i (EGLTEST PIXEL lines, EGLTEST CHECK); the start checks what the API reports (EGLTEST ES2 check lines).
#  1. display.png: without zdesktop, the whole screen: the gradient's rows near the top, the middle and the bottom.
#  2. wayland.png: in a zdesktop --glass window of 640x400: the same rows of the window.
#  3. pbuffer: the readings only (EGLTEST CHECK), framebuffer 0 being the pbuffer.
#
#   plan/ws068/tests/egl-p004.sh [OUTDIR]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
export GUEST_RUNTIME="${GUEST_RUNTIME:-$PWD/build/ws068-run}"
out=${1:-build/ws068-p004}
mkdir -p "$out"
guest() { timeout 150 python3 plan/tools/guest/guest.py run "$1" 2>&1; }
. plan/tools/guest/zwl-clients.sh
check() { python3 plan/ws035/tests/zdesktop-check.py "$@" --runtime "$GUEST_RUNTIME"; }
pointer() { python3 plan/ws035/tests/qmp-pointer.py "$GUEST_RUNTIME/qmp.sock" "$@"; }
stop_all='for p in $(ps -A -o pid,args | grep -E "[w]ayland( |$)|[e]gltest" | awk "{print \$1}"); do kill $p; done; i=0; while ps -A -o args | grep -qE "[w]ayland( |$)|[e]gltest" && [ $i -lt 50 ]; do sleep 0.2; i=$((i+1)); done'
status=0

# Fails the run unless a log has a line matching a pattern.
expect_log() {
	found=$(guest "grep -cE '$2' $1" | tail -1)
	if [ "${found:-0}" -gt 0 ] 2>/dev/null; then
		echo "log: $2 ok"
	else
		echo "log: $2 MISSING"
		status=1
	fi
}

# The gradient's red on the screen for a window at X,Y of W x H (screen rows from the top): row r is GL's row H-1-r,
# red (H - r - 0.5) / H.  The top row read is 16, not nearer the edge: in zdesktop the title bar's shadow falls on
# the window's first rows (about 10, darker and bluish: T1-182 read f40203 at row 4 where the gradient is fc0000).
es2_expect() {
	x=$1; y=$2; w=$3; h=$4
	for r in 16 $((h / 2)) $((h - 5)); do
		red=$(( ((h - r) * 2 - 1) * 255 / (h * 2) ))
		printf ' --expect %d,%d,%02x0000' $((x + w / 4)) $((y + r)) $red
	done
}

# 1. The whole screen, without a compositor.
guest "$stop_all" >/dev/null
guest '/bin/egltest --platform=display --scene=es2 --frames=120 --delay-ms=30 --token=d > /tmp/egl-d.log 2>&1 </dev/null & i=0; while ! grep -q "EGLTEST CHECK" /tmp/egl-d.log && [ $i -lt 60 ]; do sleep 1; i=$((i+1)); done; sleep 1; echo started' >/dev/null
check "$out/display.png" $(es2_expect 0 0 1280 800) || status=1
guest 'i=0; while ! grep -q EGLTEST.DONE /tmp/egl-d.log && [ $i -lt 110 ]; do sleep 1; i=$((i+1)); done; cat /tmp/egl-d.log' > "$out/display.txt"
cat "$out/display.txt"
expect_log /tmp/egl-d.log 'EGLTEST ES2 ready point-largest=[0-9]+ failures=0'
expect_log /tmp/egl-d.log 'EGLTEST CHECK run=d failures=0 glerror=0x0'
expect_log /tmp/egl-d.log 'EGLTEST DONE run=d frames=120 glerror=0x0 failures=0'

# 2. In a zdesktop window.
guest "$stop_all" >/dev/null
guest 'export XDG_RUNTIME_DIR=/tmp; rm -f /tmp/wayland-0; /bin/wayland --testing --timeout=600 --width=1280 --height=800 --glass --log-frames > /tmp/zdesktop.log 2>&1 </dev/null & sleep 4
/bin/egltest --display=/tmp/wayland-0 --size=640x400 --scene=es2 --frames=300 --delay-ms=30 --token=w > /tmp/egl-w.log 2>&1 </dev/null & i=0; while ! grep -q "EGLTEST CHECK" /tmp/egl-w.log && [ $i -lt 60 ]; do sleep 1; i=$((i+1)); done; sleep 1; echo started' >/dev/null
zwl_app_clients
set -- $(guest "grep 'KWL MAP client=$zc1 ' /tmp/zdesktop.log" | sed -n 's/.* x=\([-0-9]*\) y=\([-0-9]*\).*/\1 \2/p')
wx=${1:-0}; wy=${2:-0}
pointer move 1250 780 sleep 400
check "$out/wayland.png" $(es2_expect "$wx" "$wy" 640 400) || status=1
guest 'cat /tmp/egl-w.log' > "$out/wayland.txt"
cat "$out/wayland.txt"
expect_log /tmp/egl-w.log 'EGLTEST ES2 ready point-largest=[0-9]+ failures=0'
expect_log /tmp/egl-w.log 'EGLTEST CHECK run=w failures=0 glerror=0x0'

# 3. A pbuffer.
guest "$stop_all" >/dev/null
guest '/bin/egltest --platform=pbuffer --scene=es2 --frames=30 --delay-ms=0 --token=p > /tmp/egl-p.log 2>&1 </dev/null; echo exit=$? >> /tmp/egl-p.log; cat /tmp/egl-p.log' > "$out/pbuffer.txt"
cat "$out/pbuffer.txt"
expect_log /tmp/egl-p.log 'EGLTEST CHECK run=p failures=0 glerror=0x0'
expect_log /tmp/egl-p.log 'EGLTEST DONE run=p frames=30 glerror=0x0 failures=0'
guest "$stop_all" >/dev/null
[ $status -eq 0 ] && echo "egl-p004: PASS" || echo "egl-p004: FAIL"
exit $status
