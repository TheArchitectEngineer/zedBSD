#!/bin/sh
# ws074: shows one test page in browser's window on the Venus guest (build-browser-image.sh) and takes
# its picture: zdesktop runs at 1280x800 with --glass and the wallpaper, the browser opens the page at WIDTH x
# HEIGHT (default 800 x 600).  Checks the READY line and that neither log has an ERROR line.
#
#   plan/ws074/tests/browser-guest.sh start      (the Venus guest must be up)
#   plan/ws074/tests/browser-page.sh PAGE.html OUT.png [WIDTH HEIGHT]
#
# PAGE.html is the name of a page of plan/ws074/tests/pages (in the image under /usr/share/browser-tests).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
GUEST_RUNTIME="${GUEST_RUNTIME:-$(pwd)/build/ws074-run}"
export GUEST_RUNTIME
page=$1
out=$2
width=${3:-800}
height=${4:-600}
mkdir -p "$(dirname -- "$out")"
guest() { timeout 120 python3 plan/tools/guest/guest.py run "$1" 2>&1; }
check() { python3 plan/ws035/tests/zdesktop-check.py "$@" --runtime "$GUEST_RUNTIME"; }
pointer() { python3 plan/ws035/tests/qmp-pointer.py "$GUEST_RUNTIME/qmp.sock" "$@"; }
pages=/usr/share/browser-tests
stop_all='for p in $(ps -A -o pid,args | grep -E "[z]desktop( |$)|[z]desktop-browser" | awk "{print \$1}"); do kill $p; done; i=0; while ps -A -o args | grep -qE "[z]desktop( |$)|[z]desktop-browser" && [ $i -lt 50 ]; do sleep 0.2; i=$((i+1)); done'
status=0

guest "$stop_all" >/dev/null
guest 'export XDG_RUNTIME_DIR=/tmp; rm -f /tmp/wayland-0
picture=; [ -f /usr/share/zdesktop/wallpaper.ppm ] && picture=--wallpaper=/usr/share/zdesktop/wallpaper.ppm
/bin/wayland --timeout=900 --width=1280 --height=800 --glass $picture > /tmp/zdesktop.log 2>&1 </dev/null & sleep 4; echo started' >/dev/null
guest "export XDG_RUNTIME_DIR=/tmp; /bin/browser --width=$width --height=$height $pages/$page > /tmp/b.log 2>&1 </dev/null & sleep 5; echo started" >/dev/null

# The page is shown.
ready=$(guest "grep -c 'ZBROWSER READY width=$width height=$height' /tmp/b.log" | tail -1)
if [ "${ready:-0}" -gt 0 ] 2>/dev/null; then echo "ready: ok"; else echo "ready: MISSING"; status=1; fi
pointer move 1270 790 sleep 400
check "$out" >/dev/null

# No error in either log, then everything ends.
errors=$(guest "grep -c 'ERROR' /tmp/zdesktop.log" | tail -1)
if [ "${errors:-1}" = 0 ]; then echo "zdesktop: no ERROR"; else echo "zdesktop: ERROR lines"; status=1; fi
browser_errors=$(guest "grep -c 'ZBROWSER ERROR' /tmp/b.log" | tail -1)
if [ "${browser_errors:-1}" = 0 ]; then echo "browser: no ERROR"; else echo "browser: ERROR lines"; status=1; fi
guest "$stop_all" >/dev/null
echo "browser-page $page: status $status ($out)"
exit $status
