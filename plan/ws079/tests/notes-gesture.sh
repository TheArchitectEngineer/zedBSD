#!/bin/sh
# ws079-p005: the real Notes behind ws079-p010's top-right swipe, on the Venus guest (the lean image with Notes).
# zdesktop --glass at 1280x800, the pointer driven through QMP:
#  1. swipe.png: a swipe from the top-right corner down to the left starts /bin/notes --fullscreen
#     (KWL CORNER commit, NOTES START ... fullscreen=1); a stroke in it (NOTES STROKE); Ctrl+W closes it.
#  2. raised.png: Notes started as a window (NOTES START ... fullscreen=0) is made fullscreen by the swipe
#     (NOTES LAYOUT window=1280x800 after the compositor's configure), and no second Notes starts.
#
#   GUEST_RUNTIME=build/ws079-p005-run plan/ws035/tests/zdesktop-guest.sh start build/amd64/hdd-image.img
#   plan/ws079/tests/notes-gesture.sh [OUTDIR] [SHOTS PREFIX]
# NOTES_BINARY=build/amd64/bin/notes copies a newer build into the running guest first.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
GUEST_RUNTIME="${GUEST_RUNTIME:-$(pwd)/build/ws079-p005-run}"
export GUEST_RUNTIME
out=${1:-build/ws079-p005-gesture}
prefix=${2:-}
mkdir -p "$out"
guest() { timeout 90 python3 plan/tools/guest/guest.py run "$1" 2>&1 </dev/null; }
check() { python3 plan/ws035/tests/zdesktop-check.py "$@" --runtime "$GUEST_RUNTIME"; }
pointer() { python3 plan/ws035/tests/qmp-pointer.py "$GUEST_RUNTIME/qmp.sock" "$@"; }
keys() { python3 plan/ws035/tests/qmp-keys.py "$GUEST_RUNTIME/qmp.sock" "$@"; sleep 0.8; }
stop_all='for p in $(ps -A -o pid,args | grep -E "[w]ayland( |$)|/bin/[n]otes" | awk "{print \$1}"); do kill $p; done; i=0; while ps -A -o args | grep -qE "[w]ayland( |$)|/bin/[n]otes" && [ $i -lt 50 ]; do sleep 0.2; i=$((i+1)); done'
status=0

# Fails the run unless a log has a line matching a pattern (within a few seconds).
expect_log() {
	tries=0
	found=0
	while [ $tries -lt 8 ]; do
		found=$(guest "grep -cE '$2' $1" | tail -1)
		[ "${found:-0}" -gt 0 ] 2>/dev/null && break
		tries=$((tries + 1))
		sleep 1
	done
	if [ "${found:-0}" -gt 0 ] 2>/dev/null; then
		echo "log: $2 ok"
	else
		echo "log: $2 MISSING"
		status=1
	fi
}

shot() {
	sleep 0.6
	check "$out/$1" >/dev/null
	[ -n "$prefix" ] && cp "$out/$1" "$prefix$1"
}

# A drag from one point to another in n steps of ms milliseconds.
stroke() {
	x0=$1; y0=$2; x1=$3; y1=$4; n=$5; ms=$6
	steps="move $x0 $y0 sleep 200 down sleep 30"
	i=1
	while [ $i -le $n ]; do
		steps="$steps move $((x0 + (x1 - x0) * i / n)) $((y0 + (y1 - y0) * i / n)) sleep $ms"
		i=$((i + 1))
	done
	echo "$steps"
}

guest "$stop_all" >/dev/null
[ -n "${NOTES_BINARY:-}" ] && timeout 60 python3 plan/tools/guest/guest.py put "$NOTES_BINARY" /bin/notes >/dev/null 2>&1 </dev/null
guest 'rm -rf /root/Documents/Notes /root/.local/share/keiland/notes /tmp/notes-gesture; mkdir -p /tmp/notes-gesture' >/dev/null
guest 'export XDG_RUNTIME_DIR=/tmp; rm -f /tmp/wayland-0; picture=; [ -f /usr/share/keiland/wallpaper.png ] && picture=--wallpaper=/usr/share/keiland/wallpaper.png
/bin/wayland --testing --timeout=600 --width=1280 --height=800 --glass $picture > /tmp/zdesktop.log 2>&1 </dev/null & sleep 4; echo started' >/dev/null

# 1. The swipe starts Notes fullscreen.
pointer $(stroke 1272 6 1072 206 10 30) up sleep 5000
expect_log /tmp/zdesktop.log 'KWL CORNER commit'
guest 'ps -A -o pid,args | grep "[n]otes"' | tee "$out/ps-swipe.txt"
running=$(grep -c "/bin/notes" "$out/ps-swipe.txt")
expect_log /tmp/zdesktop.log 'NOTES START width=1280 height=800 fullscreen=1'
echo "notes started by the swipe: $running"
[ "$running" = 1 ] || status=1
pointer $(stroke 500 300 800 400 12 20) up sleep 400
pointer move 1270 790 sleep 300
shot swipe.png
keys '<ctrl-w>'
sleep 2

# 2. A windowed Notes made fullscreen by the swipe.
guest 'export XDG_RUNTIME_DIR=/tmp; /bin/notes /tmp/notes-gesture/raise.pdf > /tmp/notes-gesture/notes.log 2>&1 </dev/null & sleep 4; echo started' >/dev/null
expect_log /tmp/notes-gesture/notes.log 'NOTES START .*fullscreen=0'
pointer $(stroke 1272 6 1072 206 10 30) up sleep 3000
expect_log /tmp/notes-gesture/notes.log 'NOTES LAYOUT window=1280x800'
running=$(guest 'ps -A -o args | grep -c "/bin/[n]otes"' | tail -1)
echo "notes running after the swipe: $running"
[ "$running" = 1 ] || status=1
pointer move 1270 790 sleep 300
shot raised.png
keys '<ctrl-w>'
sleep 1

errors=$(guest "grep -c ERROR /tmp/zdesktop.log" | tail -1)
[ "${errors:-1}" = 0 ] && echo "zdesktop: no ERROR" || { echo "zdesktop: ERROR lines"; status=1; }
guest 'grep -E "KWL CORNER" /tmp/zdesktop.log' > "$out/zdesktop-corner.log"
guest "$stop_all" >/dev/null
[ $status = 0 ] && echo "notes-gesture: PASS" || echo "notes-gesture: FAIL"
exit $status
