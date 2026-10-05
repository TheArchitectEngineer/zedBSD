#!/bin/sh
# ws035-p122: the Notes toolbar's tools in both eraser modes, on the Venus guest (the demo image,
# plan/ws035/tests/build-demo-venus-image.sh, with the greeter service stopped): the compositor at 1280x800 and
# /bin/notes --fullscreen; pen.png (Pen chosen), eraser.png (E: the eraser of whole strokes) and parts.png
# (E again: "Part Eraser", NOTES TOOL 3 parts=1).  The labels Pen, Marker and Eraser are evenly spaced and the
# buttons after the tools do not move between the modes; judged by eye, and the right edge of the eraser's
# group (the first colour's place) is compared between eraser.png and parts.png by the reader.
#
#   plan/tools/files/files-guest.sh start build/amd64/hdd-image.img
#   plan/ws035/tests/p122-notes-eraser.sh [OUTDIR]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
GUEST_RUNTIME="${GUEST_RUNTIME:-$(pwd)/build/ws071-run}"
export GUEST_RUNTIME
out=${1:-build/ws035-p122}
mkdir -p "$out"
guest() { timeout 90 python3 plan/tools/guest/guest.py run "$1" 2>&1 </dev/null; }
check() { python3 plan/ws035/tests/zdesktop-check.py "$@" --runtime "$GUEST_RUNTIME"; }
keys() { python3 plan/ws035/tests/qmp-keys.py "$GUEST_RUNTIME/qmp.sock" "$@"; sleep 1; }
stop_all='for p in $(ps -A -o pid,args | grep -E "[w]ayland( |$)|/bin/[n]otes" | awk "{print \$1}"); do kill $p; done; i=0; while ps -A -o args | grep -qE "[w]ayland( |$)|/bin/[n]otes" && [ $i -lt 50 ]; do sleep 0.2; i=$((i+1)); done'
status=0

guest "service stop greeter >/dev/null 2>&1; $stop_all" >/dev/null
guest 'export XDG_RUNTIME_DIR=/tmp HOME=/root; rm -f /tmp/wayland-0; mkdir -p /tmp/p122
/bin/wayland --testing --timeout=300 --width=1280 --height=800 --glass > /tmp/p122/wayland.log 2>&1 </dev/null & i=0; while [ ! -S /tmp/wayland-0 ] && [ $i -lt 60 ]; do sleep 0.5; i=$((i+1)); done; sleep 2
WAYLAND_DISPLAY=wayland-0 /bin/notes --fullscreen --timeout-s=200 /tmp/p122/p122.pdf > /tmp/p122/notes.log 2>&1 </dev/null & sleep 6; echo started' >/dev/null
check "$out/pen.png" >/dev/null || status=1
keys e
check "$out/eraser.png" >/dev/null || status=1
keys e
check "$out/parts.png" >/dev/null || status=1
tools=$(guest 'grep -a "NOTES TOOL" /tmp/p122/notes.log' | tr '\n' '|')
echo "tools: $tools"
case "$tools" in
*"NOTES TOOL 3 parts=1"*) ;;
*) status=1 ;;
esac
guest "$stop_all" >/dev/null
if [ $status -eq 0 ]; then
	echo "p122-notes-eraser: PASS (and judge the pictures)"
else
	echo "p122-notes-eraser: FAIL"
fi
exit $status
