#!/bin/sh
# ws139-p001: the direct typing latency of Text Editor and Terminal (P-02 of the ledger), on the guest of the
# performance image (plan/ws139/tests/build-perf-image.sh).  Steps 0 to 2 of plan/ws095/tests/latency-bug143.sh copied
# with the same commands: zdesktop --glass 1280x800 with keiland-ime (direct input), then textedit-direct (threshold
# 300, 10 trials) and terminal-direct (threshold 20, 10 trials) by type-latency.py (QMP keys, the VNC picture's change).
# The results go to OUTDIR/latency.txt (LATENCY name= trials= median_ms= max_ms= missed=).  Text Editor and Terminal
# are stopped at the end; the compositor is left (the next step of perf-run.sh stops it).  Measured, not judged.
#
#   plan/ws089/tests/settings-guest.sh start IMAGE
#   plan/ws139/tests/type-only.sh [OUTDIR]          (default build/ws139-perf/latency)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
GUEST_RUNTIME="${GUEST_RUNTIME:-$(pwd)/build/ws089-run}"
export GUEST_RUNTIME
out=${1:-build/ws139-perf/latency}
mkdir -p "$out"
guest() { timeout 90 python3 plan/tools/guest/guest.py run "$1" 2>&1 </dev/null; }
keys() { python3 plan/ws035/tests/qmp-keys.py "$GUEST_RUNTIME/qmp.sock" "$@"; sleep 0.5; }
latency() { python3 plan/ws095/tests/type-latency.py --runtime "$GUEST_RUNTIME" "$@" | tee -a "$out/latency.txt"; }
status=0
. plan/ws089/tests/settings-wait.sh

# Fails the run unless a log has a line matching a pattern (within a few seconds).
expect_log() {
	tries=0
	found=0
	while [ $tries -lt 10 ]; do
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
: > "$out/latency.txt"

# 0. zdesktop with the input method; a file with a long first line.
guest 'service stop greeter >/dev/null 2>&1; for p in $(ps -A -o pid,args | grep -E "[w]ayland( |$)|[k]eiland-ime|[t]extedit|[t]erminal" | awk "{print \$1}"); do kill $p; done; sleep 1; echo stopped' >/dev/null
guest 'export XDG_RUNTIME_DIR=/tmp HOME=/root; rm -f /tmp/wayland-0 /root/.config/kei/ime/ja-user.dict; picture=; [ -f /usr/share/keiland/wallpaper.png ] && picture=--wallpaper=/usr/share/keiland/wallpaper.png
/bin/wayland --testing --timeout=900 --width=1280 --height=800 --glass $picture > /tmp/zdesktop.log 2>&1 </dev/null & sleep 4; echo started' >/dev/null
expect_log /tmp/zdesktop.log 'KEI-IME READY'
guest 'i=0; : > /root/lat.txt; while [ $i -lt 30 ]; do printf "the quick brown fox jumps over the lazy dog %d\n" $i >> /root/lat.txt; i=$((i+1)); done; echo made' >/dev/null

# 1. Text Editor, direct input: the first line's region (the window's text area, its first rows).
guest 'export XDG_RUNTIME_DIR=/tmp HOME=/root; /bin/textedit /root/lat.txt > /tmp/te.log 2>&1 </dev/null & sleep 5; echo started' >/dev/null
expect_log /tmp/te.log 'TEXTEDIT READY'
find_window
echo "textedit at $wx,$wy"
latency --name textedit-direct --key x --region "$((wx + 20)),$((wy + 10)),700,60" --threshold 300 --trials 10

# 2. Terminal, direct input (the baseline): Text Editor closed first.
guest "pid=\$(ps -A -o pid,args | grep '[t]extedit' | awk '{print \$1}'); kill \$pid" >/dev/null
sleep 1
guest 'export XDG_RUNTIME_DIR=/tmp HOME=/root; cd /root; /bin/terminal --token=lat --timeout-s=600 > /tmp/t.log 2>&1 </dev/null & sleep 6; echo started' >/dev/null
expect_log /tmp/t.log 'ZTERM START run=lat'
find_window
echo "terminal at $wx,$wy"
latency --name terminal-direct --key x --region "$((wx)),$((wy)),700,120" --threshold 20 --trials 10
guest "pid=\$(ps -A -o pid,args | grep '[t]erminal' | awk '{print \$1}'); kill \$pid" >/dev/null
sleep 1

exit $status
