#!/bin/sh
# ws074-p052: CSS background images on the Venus guest (build-browser-image.sh, with the pictures of
# make-test-images.py at /usr/share/browser-images).  zdesktop runs at 1280x800 with --glass and the
# wallpaper; the browser opens backgrounds.html at 900x640.  Checks:
#  1. window.png: the page with its tiled, placed and sized backgrounds and the canvas's tile is shown
#     (READY), and neither log has an ERROR line.
#  2. The GPU renderer (Venus) agrees with the CPU renderer on backgrounds.html: --render-gpu and --render,
#     compared on the host by gpu-compare.py --pictures (channel <= 2, at most 0.1% over).
#
#   plan/ws074/tests/browser-guest.sh start      (the Venus guest must be up)
#   plan/ws074/tests/browser-p052.sh [OUTDIR]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
GUEST_RUNTIME="${GUEST_RUNTIME:-$(pwd)/build/ws074-run}"
export GUEST_RUNTIME
out=${1:-build/ws074-p052}
mkdir -p "$out"
guest() { timeout 120 python3 plan/tools/guest/guest.py run "$1" 2>&1; }
check() { python3 plan/ws035/tests/zdesktop-check.py "$@" --runtime "$GUEST_RUNTIME"; }
pointer() { python3 plan/ws035/tests/qmp-pointer.py "$GUEST_RUNTIME/qmp.sock" "$@"; }
pages=/usr/share/browser-images
stop_all='for p in $(ps -A -o pid,args | grep -E "[w]ayland( |$)|[b]rowser" | awk "{print \$1}"); do kill $p; done; i=0; while ps -A -o args | grep -qE "[w]ayland( |$)|[b]rowser" && [ $i -lt 50 ]; do sleep 0.2; i=$((i+1)); done'
status=0

# 1. The page in the window.
guest "$stop_all" >/dev/null
guest 'export XDG_RUNTIME_DIR=/tmp; rm -f /tmp/wayland-0
picture=; [ -f /usr/share/keiland/wallpaper.png ] && picture=--wallpaper=/usr/share/keiland/wallpaper.png
/bin/wayland --timeout=900 --width=1280 --height=800 --glass $picture > /tmp/zdesktop.log 2>&1 </dev/null & sleep 4; echo started' >/dev/null
guest "export XDG_RUNTIME_DIR=/tmp; /bin/browser --width=900 --height=640 $pages/backgrounds.html > /tmp/b.log 2>&1 </dev/null & sleep 6; echo started" >/dev/null
ready=$(guest "grep -c 'ZBROWSER READY width=900 height=640' /tmp/b.log" | tail -1)
if [ "${ready:-0}" -gt 0 ] 2>/dev/null; then echo "ready: ok"; else echo "ready: MISSING"; status=1; fi
pointer move 1270 790 sleep 400
check "$out/window.png" >/dev/null

# Neither log has an error line; then everything ends.
errors=$(guest "grep -c 'ERROR' /tmp/zdesktop.log" | tail -1)
if [ "${errors:-1}" = 0 ]; then echo "zdesktop: no ERROR"; else echo "zdesktop: ERROR lines"; status=1; fi
browser_errors=$(guest "grep -c 'ZBROWSER ERROR' /tmp/b.log" | tail -1)
if [ "${browser_errors:-1}" = 0 ]; then echo "browser: no ERROR"; else echo "browser: ERROR lines"; status=1; fi
guest "$stop_all" >/dev/null

# 2. The GPU and CPU renderers on the backgrounds.
guest "/bin/browser --render-gpu --output=/tmp/backgrounds-gpu.ppm --width=900 --height=640 $pages/backgrounds.html; /bin/browser --render --output=/tmp/backgrounds-cpu.ppm --width=900 --height=640 $pages/backgrounds.html; echo drawn" >/dev/null
python3 plan/tools/guest/guest.py get /tmp/backgrounds-gpu.ppm "$out/backgrounds-gpu.ppm" >/dev/null
python3 plan/tools/guest/guest.py get /tmp/backgrounds-cpu.ppm "$out/backgrounds-cpu.ppm" >/dev/null
if ! python3 plan/ws074/tests/gpu-compare.py --pictures "$out/backgrounds-gpu.ppm" "$out/backgrounds-cpu.ppm" --out "$out"; then
	status=1
fi
guest 'rm -f /tmp/backgrounds-gpu.ppm /tmp/backgrounds-cpu.ppm' >/dev/null
echo "browser-p052: status $status (pictures in $out)"
exit $status
