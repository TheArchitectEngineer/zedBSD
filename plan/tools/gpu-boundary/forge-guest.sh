#!/bin/sh
# ws103-p004 (V3): on the Venus guest of the forgery image (plan/tools/gpu-boundary/build-forge-image.sh), zdesktop must refuse a
# GPU buffer whose fd is an allocation capability sent as an image (/bin/gpu-forge-test: "gpu-forge: PASS", the
# connection ended with a protocol error, "KWL IMPORT_ERROR" in zdesktop's log), keep running, and then import a real
# client's buffers (wltest through the Vulkan WSI: "KWL IMPORT client" lines after the refusal, frames drawn).
# Prints "forge-guest: PASS" or "forge-guest: FAIL ...".
#
#   plan/ws035/tests/zdesktop-guest.sh start build/ws103/p004-forge.img      (the guest must be up)
#   plan/tools/gpu-boundary/forge-guest.sh [OUTDIR]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
out=${1:-build/ws103/p004-forge}
mkdir -p "$out"
GUEST_RUNTIME=${GUEST_RUNTIME:-$(pwd)/build/ws035-sq-run}
export GUEST_RUNTIME
guest() { timeout 90 python3 plan/tools/guest/guest.py run "$1" 2>&1; }
stop_all='service stop greeter >/dev/null 2>&1; for p in $(ps -A -o pid,args | grep -E "[w]ayland( |$)|[w]ltest|[g]pu-forge" | awk "{print \$1}"); do kill $p; done; i=0; while ps -A -o args | grep -qE "[w]ayland( |$)|[w]ltest|[g]pu-forge" && [ $i -lt 50 ]; do sleep 0.2; i=$((i+1)); done'
fail() { echo "forge-guest: FAIL $*"; exit 1; }

# zdesktop alone, with the per-frame lines.
guest "$stop_all" >/dev/null
guest 'export XDG_RUNTIME_DIR=/tmp; rm -f /tmp/wayland-0; /bin/wayland --testing --timeout=180 --width=1280 --height=800 --log-frames > /tmp/zdesktop.log 2>&1 </dev/null & for w in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30; do grep -q KWL.READY /tmp/zdesktop.log 2>/dev/null && break; sleep 0.5; done; sleep 1' >/dev/null

# The forgery.
guest 'export XDG_RUNTIME_DIR=/tmp; /bin/gpu-forge-test > /tmp/forge.log 2>&1 </dev/null; echo exit=$?' > "$out/forge-run.txt"
guest 'cat /tmp/forge.log' | tee "$out/forge.log"
grep -q 'gpu-forge: PASS' "$out/forge.log" || fail "the forgery was not refused (or the test could not run)"

# zdesktop is still running and logged the refusal.
alive=$(guest 'ps -A -o args | grep -cE "^/bin/wayland( |$)"' | tail -1)
[ "$alive" = 1 ] || fail "zdesktop is not running after the refusal"
guest 'grep -aE "IMPORT_ERROR|VULKAN_IMPORT" /tmp/zdesktop.log' | tee "$out/refusal.txt"
grep -q 'KWL IMPORT_ERROR' "$out/refusal.txt" || fail "no KWL IMPORT_ERROR line"
before=$(guest "grep -ac 'KWL IMPORT client' /tmp/zdesktop.log" | tail -1)

# A real client afterwards.
guest 'export XDG_RUNTIME_DIR=/tmp; /bin/wltest --windowed --size=300x200 --color=00ff00 --frames=120 --token=r > /tmp/r.log 2>&1 </dev/null; echo exit=$?' > "$out/wltest-run.txt"
after=$(guest "grep -ac 'KWL IMPORT client' /tmp/zdesktop.log" | tail -1)
frames=$(guest 'grep -c "WLTEST FRAME run=r" /tmp/r.log' | tail -1)
guest "grep -a 'KWL IMPORT client' /tmp/zdesktop.log | tail -3" | tee "$out/imports.txt"
echo "imports before=$before after=$after wltest_frames=$frames $(cat "$out/wltest-run.txt" | tail -1)" | tee "$out/summary.txt"
[ "$after" -gt "$before" ] || fail "no import of the real client's buffers"
[ "$frames" -gt 0 ] || fail "the real client drew no frame"
guest 'grep -aE "KWL (FAILED|GPU_ERROR|VULKAN_ERROR)" /tmp/zdesktop.log' | tee "$out/errors.txt"
[ -s "$out/errors.txt" ] && fail "zdesktop reported errors"

guest "$stop_all" >/dev/null
echo "forge-guest: PASS"
