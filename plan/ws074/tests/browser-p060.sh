#!/bin/sh
# ws074-p060: inline blocks in browser's window on the Venus guest (build-browser-image.sh).  zdesktop runs at
# 1280x800 with --glass and the wallpaper.
#  1. inline-block.html at 800x640: the page is shown (inline-block.png), then a click in the middle of the link that
#     is an inline block (its place from the host build's --dump=layout of the same page) follows it: the view goes to
#     second.html (LINK and NAVIGATE lines).
# Neither log may have an ERROR line.
#
#   plan/ws074/tests/browser-guest.sh start      (the Venus guest must be up)
#   plan/ws074/tests/browser-p060.sh [OUTDIR]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
GUEST_RUNTIME="${GUEST_RUNTIME:-$(pwd)/build/ws074-run}"
export GUEST_RUNTIME
out=${1:-build/ws074-p060}
mkdir -p "$out"
guest() { timeout 180 python3 plan/tools/guest/guest.py run "$1" 2>&1; }
check() { python3 plan/ws035/tests/zdesktop-check.py "$@" --runtime "$GUEST_RUNTIME"; }
pointer() { python3 plan/ws035/tests/qmp-pointer.py "$GUEST_RUNTIME/qmp.sock" "$@"; }
pages=/usr/share/browser-tests
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

# The link's middle in the page, from the host build's layout of the same page at the same size.
f=build/ws035-fonts
link=$(build/ws074-host/plain/browser --dump=layout --width=800 --height=640 --font=$f/Inter.ttf \
    --mono-font=$f/JetBrainsMono-Regular.ttf --fallback-font=$f/DroidSansFallbackFull.ttf plan/ws074/tests/pages/inline-block.html 2>/dev/null |
    awk '$1 == "block" && $2 == "<a>" { printf "%d %d\n", $3 + $5 / 2, $4 + $6 / 2; exit }')
echo "inline-block: the link at $link in the page"

# zdesktop and the browser on the page; where the page is on the screen (wx, wy).
guest "$stop_all" >/dev/null
guest 'export XDG_RUNTIME_DIR=/tmp; rm -f /tmp/wayland-0
picture=; [ -f /usr/share/keiland/wallpaper.ppm ] && picture=--wallpaper=/usr/share/keiland/wallpaper.ppm
/bin/wayland --timeout=900 --width=1280 --height=800 --glass $picture > /tmp/zdesktop.log 2>&1 </dev/null & sleep 4; echo started' >/dev/null
guest "export XDG_RUNTIME_DIR=/tmp; /bin/browser --width=800 --height=640 $pages/inline-block.html > /tmp/b.log 2>&1 </dev/null & sleep 5; echo started" >/dev/null
set -- $(guest "grep 'ZWL MAP client=' /tmp/zdesktop.log | tail -1" | sed -n 's/.* x=\([-0-9]*\) y=\([-0-9]*\).*/\1 \2/p')
wx=${1:-0}
wy=${2:-0}
echo "browser: window at $wx,$wy"
expect_log /tmp/b.log 'ZBROWSER READY width=800 height=640'
pointer move $((wx + 10)) $((wy + 10)) sleep 400
check "$out/inline-block.png" >/dev/null

# The click on the link inside the inline block.
set -- $link
pointer move $((wx + $1)) $((wy + $2)) sleep 300 down sleep 60 up sleep 1500
expect_log /tmp/b.log 'ZBROWSER LINK href=second.html'
expect_log /tmp/b.log "ZBROWSER NAVIGATE path=$pages/second.html"
pointer move $((wx + 10)) $((wy + 10)) sleep 400
check "$out/followed.png" >/dev/null

# No error in either log.
errors=$(guest "grep -c 'ERROR' /tmp/zdesktop.log" | tail -1)
if [ "${errors:-1}" = 0 ]; then echo "zdesktop: no ERROR"; else echo "zdesktop: ERROR lines"; status=1; fi
browser_errors=$(guest "grep -c 'ZBROWSER ERROR' /tmp/b.log" | tail -1)
if [ "${browser_errors:-1}" = 0 ]; then echo "browser: no ERROR"; else echo "browser: ERROR lines"; status=1; fi
guest "grep -v CONSOLE /tmp/b.log" > "$out/inline-block.log"
guest "$stop_all" >/dev/null
echo "browser-p060: status $status ($out)"
exit $status
