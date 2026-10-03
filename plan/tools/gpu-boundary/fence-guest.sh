#!/bin/sh
# ws103-p005: on the Venus guest of the forgery image (plan/tools/gpu-boundary/build-forge-image.sh), a Vulkan Wayland client
# (wltest) sends each present's own new fence: every acquire fence zdesktop receives is at its first generation
# ("ZWL ACQUIRE_FENCE ... generation=1"; before ws103-p005 the reused slot fence advanced 1, 2, 3, ...).  Also times
# FRAMES presents of wltest without delay, to compare with an image before the change.
# Prints "fence-guest RESULT fences=N first_generation=M max_generation=G frames=F seconds=S" and "fence-guest: PASS",
# or "fence-guest: FAIL ..." (with EXPECT=advancing the check is the old behaviour's, for the image before the change).
#
#   plan/ws035/tests/zdesktop-guest.sh start IMAGE      (the guest must be up)
#   plan/tools/gpu-boundary/fence-guest.sh [OUTDIR] [FRAMES]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
out=${1:-build/ws103/p005-fence}
frames=${2:-600}
expect=${EXPECT:-first}
mkdir -p "$out"
GUEST_RUNTIME=${GUEST_RUNTIME:-$(pwd)/build/ws035-sq-run}
export GUEST_RUNTIME
guest() { timeout 300 python3 plan/tools/guest/guest.py run "$1" 2>&1; }
stop_all='service stop greeter >/dev/null 2>&1; for p in $(ps -A -o pid,args | grep -E "[w]ayland( |$)|[w]ltest" | awk "{print \$1}"); do kill $p; done; i=0; while ps -A -o args | grep -qE "[w]ayland( |$)|[w]ltest" && [ $i -lt 50 ]; do sleep 0.2; i=$((i+1)); done'
fail() { echo "fence-guest: FAIL $*"; exit 1; }

# zdesktop alone, with the per-frame lines.
guest "$stop_all" >/dev/null
guest 'export XDG_RUNTIME_DIR=/tmp; rm -f /tmp/wayland-0; /bin/wayland --timeout=600 --width=1280 --height=800 --log-frames > /tmp/zdesktop.log 2>&1 </dev/null & for w in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30; do grep -q ZWL.READY /tmp/zdesktop.log 2>/dev/null && break; sleep 0.5; done; sleep 1' >/dev/null

# FRAMES presents without delay, timed.
guest "export XDG_RUNTIME_DIR=/tmp; start=\$(date +%s); /bin/wltest --windowed --size=400x300 --color=ff0000 --frames=$frames --delay-ms=0 --token=f > /tmp/f.log 2>&1 </dev/null; end=\$(date +%s); echo seconds=\$((end - start))" | tee "$out/timing.txt"
seconds=$(sed -n 's/.*seconds=\([0-9]*\).*/\1/p' "$out/timing.txt" | tail -1)
drawn=$(guest 'grep -c "WLTEST FRAME run=f" /tmp/f.log' | tail -1)

# The fences zdesktop received.
guest "grep -a 'ZWL ACQUIRE_FENCE' /tmp/zdesktop.log" > "$out/fences.txt"
count=$(grep -c 'ZWL ACQUIRE_FENCE' "$out/fences.txt")
first=$(grep -c 'generation=1$' "$out/fences.txt")
max=$(sed -n 's/.*generation=\([0-9]*\).*/\1/p' "$out/fences.txt" | sort -n | tail -1)
echo "fence-guest RESULT fences=$count first_generation=$first max_generation=${max:-0} frames=$drawn seconds=${seconds:-?}" | tee "$out/summary.txt"
guest 'grep -aE "ZWL (FAILED|GPU_ERROR|VULKAN_ERROR|IMPORT_ERROR)" /tmp/zdesktop.log' | tee "$out/errors.txt"
guest "$stop_all" >/dev/null

# The verdict.
[ "$count" -gt 0 ] || fail "no acquire fence was received"
[ -s "$out/errors.txt" ] && fail "zdesktop reported errors"
[ "$drawn" -gt 0 ] || fail "wltest drew no frame"
if [ "$expect" = advancing ]; then
	[ "${max:-0}" -gt 1 ] || fail "the reused fence did not advance"
else
	[ "$first" = "$count" ] || fail "a fence was not a new one ($first of $count at generation 1)"
fi
echo "fence-guest: PASS"
