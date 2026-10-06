#!/bin/sh
# ws074-p021: browser's images on the Venus guest (build-browser-image.sh, with the pictures of
# make-test-images.py at /usr/share/browser-images).  zdesktop runs at 1280x800 with --glass and the
# wallpaper; the browser opens images.html at 900x640.  Checks:
#  1. window.png: the page with its JPEG (baseline, progressive, CMYK, gray), PNG (alpha, palette) and
#     GIF images is shown (READY), and neither log has an ERROR line.
#  2. A click on the image inside the link opens second.html (LINK, NAVIGATE).
#  3. The GPU renderer (Venus) agrees with the CPU renderer on images.html: --render-gpu and --render,
#     compared on the host by gpu-compare.py --pictures (channel <= 2, at most 0.1% over).
# The linked image's place comes from the host build's --dump=layout of images.html at the same width.
#
#   plan/ws074/tests/browser-guest.sh start      (the Venus guest must be up)
#   plan/ws074/tests/browser-p021.sh [OUTDIR]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
GUEST_RUNTIME="${GUEST_RUNTIME:-$(pwd)/build/ws074-run}"
export GUEST_RUNTIME
out=${1:-build/ws074-p021}
mkdir -p "$out"
guest() { timeout 120 python3 plan/tools/guest/guest.py run "$1" 2>&1; }
check() { python3 plan/ws035/tests/zdesktop-check.py "$@" --runtime "$GUEST_RUNTIME"; }
pointer() { python3 plan/ws035/tests/qmp-pointer.py "$GUEST_RUNTIME/qmp.sock" "$@"; }
pages=/usr/share/browser-images
stop_all='for p in $(ps -A -o pid,args | grep -E "[w]ayland( |$)|[b]rowser" | awk "{print \$1}"); do kill $p; done; i=0; while ps -A -o args | grep -qE "[w]ayland( |$)|[b]rowser" && [ $i -lt 50 ]; do sleep 0.2; i=$((i+1)); done'
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

# Takes a picture with the pointer out of the way.
shot() {
	pointer move 1270 790 sleep 400
	check "$out/$1" >/dev/null
}

# The linked image's middle in the page at 900 wide, from the host's layout: "x y".
f=userland/desktop/fonts
python3 plan/ws074/tests/make-test-images.py >/dev/null
link=$(build/ws074-host/plain/browser --dump=layout --width=900 --height=640 --font=$f/Mahora-Regular.ttf \
    --mono-font=$f/JetBrainsMono-Regular.ttf --fallback-font=$f/DroidSansFallbackFull.ttf "$(pwd)/build/ws074-images/images.html" |
    awk '$1 == "line" { y = $3; h = $5; b = $7 } $1 == "replaced" && $4 == "24.00" { printf "%d %d\n", $2 + $4 / 2, b - $6 / 2; exit }')
echo "linked image: at $link in the page"

# 1. The page in the window.
guest "$stop_all" >/dev/null
guest 'export XDG_RUNTIME_DIR=/tmp; rm -f /tmp/wayland-0
picture=; [ -f /usr/share/keiland/wallpaper.png ] && picture=--wallpaper=/usr/share/keiland/wallpaper.png
/bin/wayland --testing --timeout=900 --width=1280 --height=800 --glass $picture > /tmp/zdesktop.log 2>&1 </dev/null & sleep 4; echo started' >/dev/null
guest "export XDG_RUNTIME_DIR=/tmp; /bin/browser --width=900 --height=640 $pages/images.html > /tmp/b.log 2>&1 </dev/null & sleep 6; echo started" >/dev/null
set -- $(guest "grep 'KWL MAP client=' /tmp/zdesktop.log | tail -1" | sed -n 's/.* x=\([-0-9]*\) y=\([-0-9]*\).*/\1 \2/p')
wx=${1:-0}; wy=${2:-0}
echo "browser: window at $wx,$wy"
expect_log /tmp/b.log 'ZBROWSER READY width=900 height=640'
shot window.png

# 2. The image inside the link.
set -- $link
pointer move $((wx + $1 - 2)) $((wy + $2)) sleep 150 move $((wx + $1)) $((wy + $2)) sleep 300 down sleep 60 up sleep 1500
expect_log /tmp/b.log 'ZBROWSER LINK href=second.html'
expect_log /tmp/b.log "ZBROWSER NAVIGATE path=$pages/second.html"
shot link.png

# Neither log has an error line; then everything ends.
errors=$(guest "grep -c 'ERROR' /tmp/zdesktop.log" | tail -1)
if [ "${errors:-1}" = 0 ]; then echo "zdesktop: no ERROR"; else echo "zdesktop: ERROR lines"; status=1; fi
browser_errors=$(guest "grep -c 'ZBROWSER ERROR' /tmp/b.log" | tail -1)
if [ "${browser_errors:-1}" = 0 ]; then echo "browser: no ERROR"; else echo "browser: ERROR lines"; status=1; fi
guest "$stop_all" >/dev/null

# 3. The GPU and CPU renderers on the images.
guest "/bin/browser --render-gpu --output=/tmp/images-gpu.ppm --width=900 --height=1150 $pages/images.html; /bin/browser --render --output=/tmp/images-cpu.ppm --width=900 --height=1150 $pages/images.html; echo drawn" >/dev/null
python3 plan/tools/guest/guest.py get /tmp/images-gpu.ppm "$out/images-gpu.ppm" >/dev/null
python3 plan/tools/guest/guest.py get /tmp/images-cpu.ppm "$out/images-cpu.ppm" >/dev/null
if ! python3 plan/ws074/tests/gpu-compare.py --pictures "$out/images-gpu.ppm" "$out/images-cpu.ppm" --out "$out"; then
	status=1
fi
guest 'rm -f /tmp/images-gpu.ppm /tmp/images-cpu.ppm' >/dev/null
echo "browser-p021: status $status (pictures in $out)"
exit $status
