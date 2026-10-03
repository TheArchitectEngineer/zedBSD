#!/bin/sh
# ws074-p014: browser's window and GPU renderer on the Venus guest (build-browser-image.sh).
# zdesktop runs at 1280x800 with --glass and the wallpaper (the desktop tests' look, where it draws every window's titlebar); the browser opens a test page at 900x640.  Checks:
#  1. first.png: the window shows first.html (ZBROWSER READY, then a frame).
#  2. The GPU renderer (Venus) agrees with the CPU renderer: --render-gpu and --render of each test
#     page, compared on the host by gpu-compare.py --pictures (channel <= 2, at most 0.1% over).
#  3. blocks.html scrolls: End (scroll to the bottom), then the wheel up (scroll less), then Home;
#     scrolled.png is taken at the bottom.
#  4. Ctrl+Q closes the window; zdesktop draws the titlebar: a drag moves the window (moved.png) and the
#     close button ends the browser.
#  5. zdesktop's log has no ERROR line.
#
#   plan/ws074/tests/browser-guest.sh start      (the Venus guest must be up)
#   plan/ws074/tests/browser-p014.sh [OUTDIR]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
GUEST_RUNTIME="${GUEST_RUNTIME:-$(pwd)/build/ws074-run}"
export GUEST_RUNTIME
out=${1:-build/ws074-p014}
mkdir -p "$out"
guest() { timeout 120 python3 plan/tools/guest/guest.py run "$1" 2>&1; }
check() { python3 plan/ws035/tests/zdesktop-check.py "$@" --runtime "$GUEST_RUNTIME"; }
pointer() { python3 plan/ws035/tests/qmp-pointer.py "$GUEST_RUNTIME/qmp.sock" "$@"; }
keys() { python3 plan/ws035/tests/qmp-keys.py "$GUEST_RUNTIME/qmp.sock" "$@"; }
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

# Starts the browser on a page (zdesktop must be running) and waits for its first frame.
open_page() {
	guest "export XDG_RUNTIME_DIR=/tmp; /bin/browser --width=900 --height=640 $pages/$1 > /tmp/b.log 2>&1 </dev/null & sleep 5; echo started" >/dev/null
	expect_log /tmp/b.log 'ZBROWSER READY width=900 height=640'
	expect_log /tmp/b.log 'ZBROWSER FRAME scroll=0 '
}

# Waits up to ten seconds for the browser to end (its Vulkan teardown takes a moment) and reports it as NAME.
wait_gone() {
	left=1
	for wait in 1 2 3 4 5 6 7 8 9 10; do
		sleep 1
		left=$(guest "ps -A -o args | grep -c '[b]rowser'" | tail -1)
		[ "${left:-1}" = 0 ] && break
	done
	if [ "${left:-1}" = 0 ]; then
		echo "$1: ok"
	else
		echo "$1: the browser is still running"
		status=1
		guest 'for p in $(ps -A -o pid,args | grep "[b]rowser" | awk "{print \$1}"); do kill $p; done' >/dev/null
	fi
}

# Ends the browser with Ctrl+Q (the pointer over the window, which gives it the keyboard) and checks that it went.
close_page() {
	pointer move 600 400 sleep 300
	keys '<ctrl-q>'
	wait_gone close
}

guest "$stop_all" >/dev/null
guest 'export XDG_RUNTIME_DIR=/tmp; rm -f /tmp/wayland-0
picture=; [ -f /usr/share/keiland/wallpaper.ppm ] && picture=--wallpaper=/usr/share/keiland/wallpaper.ppm
/bin/wayland --timeout=900 --width=1280 --height=800 --glass $picture > /tmp/zdesktop.log 2>&1 </dev/null & sleep 4; echo started' >/dev/null

# 1. The window on first.html.
open_page first.html
pointer move 1270 790 sleep 400
check "$out/first.png" >/dev/null
close_page

# 2. The GPU renderer against the CPU renderer, in the guest.
for page in first blocks; do
	guest "/bin/browser --render-gpu --output=/tmp/$page-gpu.ppm --width=800 --height=600 $pages/$page.html; /bin/browser --render --output=/tmp/$page-cpu.ppm --width=800 --height=600 $pages/$page.html; echo drawn" >/dev/null
	python3 plan/tools/guest/guest.py get "/tmp/$page-gpu.ppm" "$out/$page-gpu.ppm" >/dev/null 2>&1
	python3 plan/tools/guest/guest.py get "/tmp/$page-cpu.ppm" "$out/$page-cpu.ppm" >/dev/null 2>&1
	if ! python3 plan/ws074/tests/gpu-compare.py --pictures "$out/$page-gpu.ppm" "$out/$page-cpu.ppm" --out "$out"; then
		status=1
	fi
done

# 3. Scrolling blocks.html: to the bottom, a notch of the wheel up, then back to the top.
open_page blocks.html
pointer move 600 400 sleep 300
keys '<end>'
sleep 1
bottom=$(guest "grep 'ZBROWSER FRAME' /tmp/b.log | tail -1" | sed -n 's/.*scroll=\([0-9]*\).*/\1/p' | tail -1)
echo "scroll: End went to ${bottom:-?}"
if [ "${bottom:-0}" -gt 0 ] 2>/dev/null; then echo "scroll: End ok"; else echo "scroll: End did not scroll"; status=1; fi
pointer move 1270 790 sleep 400
check "$out/scrolled.png" >/dev/null
pointer move 600 400 sleep 300 wheel-up sleep 800
after=$(guest "grep 'ZBROWSER FRAME' /tmp/b.log | tail -1" | sed -n 's/.*scroll=\([0-9]*\).*/\1/p' | tail -1)
echo "scroll: the wheel went to ${after:-?}"
if [ "${after:-0}" -lt "${bottom:-0}" ] 2>/dev/null; then echo "scroll: wheel ok"; else echo "scroll: the wheel did not scroll"; status=1; fi
keys '<home>'
sleep 1
top=$(guest "grep 'ZBROWSER FRAME' /tmp/b.log | tail -1" | sed -n 's/.*scroll=\([0-9]*\).*/\1/p' | tail -1)
if [ "${top:-1}" = 0 ]; then echo "scroll: Home ok"; else echo "scroll: Home went to ${top:-?}"; status=1; fi
close_page

# 4. zdesktop's titlebar: a drag moves the window by (100, 60), and its close button ends the browser.
open_page first.html
set -- $(guest "grep 'ZWL MAP client=' /tmp/zdesktop.log | tail -1" | sed -n 's/.* x=\([-0-9]*\) y=\([-0-9]*\).*/\1 \2/p')
wx=${1:-0}; wy=${2:-0}
# (The drag starts on the title: since ws074-p045 the middle of the titlebar holds the location's control.)
pointer move $((wx + 150)) $((wy - 30)) sleep 300 down sleep 200 move $((wx + 200)) $((wy)) sleep 200 move $((wx + 250)) $((wy + 30)) sleep 300 up sleep 800
expect_log /tmp/zdesktop.log "ZWL GLASS moved surface=[0-9]* x=$((wx + 100)) y=$((wy + 60))"
pointer move 1270 790 sleep 400
check "$out/moved.png" >/dev/null
pointer move $((wx + 100 + 871)) $((wy + 60 - 30)) sleep 300 move $((wx + 100 + 873)) $((wy + 60 - 30)) sleep 300 down sleep 80 up sleep 1500
expect_log /tmp/zdesktop.log 'ZWL GLASS close surface='
wait_gone "titlebar close"

# 5. No error in zdesktop's log.
errors=$(guest "grep -c 'ERROR' /tmp/zdesktop.log" | tail -1)
if [ "${errors:-1}" = 0 ]; then echo "zdesktop: no ERROR"; else echo "zdesktop: ERROR lines"; status=1; fi
guest "$stop_all" >/dev/null
echo "browser-p014: status $status (pictures in $out)"
exit $status
