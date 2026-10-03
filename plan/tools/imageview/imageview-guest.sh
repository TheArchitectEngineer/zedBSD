#!/bin/sh
# ws091-p002: Image Viewer on the Venus guest (the zdesktop image, plan/ws035/tests/zdesktop-guest.sh, with this
# worktree's imageview, compositor and libraries copied in by the install step).  zdesktop --glass at 1280x800.
#   install      copies the program, the compositor, the libraries, the pictures (make-images.py) and the App Home
#                list in, and starts the compositor
#   home         App Home shows Image Viewer (home.png)
#   empty        the viewer without a file: the Kei mark and Open (empty.png)
#   fit          01-splash.png fitted, with its chip (fit.png)
#   next         Right: 02-landscape.jpg (a 4032x2268 photo, from its levels) (next.png)
#   zoom         1 (100 %), the pointer drags it, + and 0 (zoom-100.png, zoom-drag.png, zoom-fit.png)
#   wheel        the wheel zooms about the pointer (wheel.png)
#   portrait     03-portrait.jpg upright by its EXIF orientation (portrait.png)
#   rotate       R turns it right (rotate.png)
#   alpha        04-mark.png over the checkerboard (alpha.png)
#   gif          05-anim.gif at two times (gif-1.png, gif-2.png)
#   pixels       06-pixels.png at 1:1 and much enlarged, sharp (pixels.png, pixels-zoom.png)
#   broken       07-broken.png: the card that says why (broken.png)
#   swipe        a drag across a fitted image goes back to the previous one (swipe.png)
#   fullscreen   F: the full screen, black, the chip dark (fullscreen.png), Esc back
#   chooser      Ctrl+O (chooser.png), Escape
# Every step's pictures go to OUTDIR.  The steps read the program's own log lines (IMAGEVIEW ..., ZWL ...) through
# SSH; nothing reads the console.
#   GUEST_RUNTIME=... BIN=build/ws091-amd64 plan/tools/imageview/imageview-guest.sh OUTDIR STEP...
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
GUEST_RUNTIME="${GUEST_RUNTIME:-$(pwd)/build/ws091-run}"
export GUEST_RUNTIME
bin=${BIN:-build/ws091-amd64}
out=$1
shift
mkdir -p "$out"
status=0
guest() { timeout 90 python3 plan/tools/guest/guest.py run "$1" 2>&1 </dev/null; }
put() { timeout 120 python3 plan/tools/guest/guest.py put "$1" "$2" >/dev/null 2>&1 </dev/null || { echo "put $1: FAILED"; status=1; }; }
pointer() { python3 plan/ws035/tests/qmp-pointer.py "$GUEST_RUNTIME/qmp.sock" "$@"; }
keys() { python3 plan/ws035/tests/qmp-keys.py "$GUEST_RUNTIME/qmp.sock" "$@" >/dev/null; }
shot() {
	python3 plan/ws035/tests/zdesktop-check.py "$out/$1" --runtime "$GUEST_RUNTIME" >/dev/null
	echo "shot $1"
}
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
stop_viewer='for p in $(ps -A -o pid,args | grep -E "[i]mageview" | awk "{print \$1}"); do kill $p; done; sleep 1'
viewer() {
	guest "$stop_viewer" >/dev/null
	guest "export XDG_RUNTIME_DIR=/tmp HOME=/tmp; /bin/imageview --width=1180 --height=700 $1 > /tmp/iv.log 2>&1 </dev/null & sleep 5; echo started" >/dev/null
	expect_log /tmp/iv.log 'IMAGEVIEW READY'
}

for step in "$@"; do
	case "$step" in
	install)
		python3 plan/tools/imageview/make-images.py build/ws091-images >/dev/null
		put "$bin/bin/imageview" /tmp/imageview
		put "$bin/bin/wayland" /tmp/wayland
		for library in libkeiland libvulkan libwayland-client libtruetype libpng-compat libjpeg-compat libgif-compat libz-compat; do
			put "$bin/dynamic/$library.so" "/tmp/$library.so"
		done
		for picture in build/ws091-images/*; do
			put "$picture" "/tmp/$(basename "$picture")"
		done
		put plan/ws035/demo/apps.conf /tmp/apps.conf
		guest 'for p in $(ps -A -o pid,args | grep -E "[w]ayland( |$)|[i]mageview" | awk "{print \$1}"); do kill $p; done; sleep 1
cp /tmp/imageview /bin/imageview && cp /tmp/wayland /bin/wayland && chmod 0755 /bin/imageview /bin/wayland &&
for l in libkeiland libvulkan libwayland-client libtruetype libpng-compat libjpeg-compat libgif-compat libz-compat; do cp /tmp/$l.so /lib/$l.so && chmod 0644 /lib/$l.so; done &&
mkdir -p /etc/keiland /tmp/pics && cp /tmp/apps.conf /etc/keiland/apps.conf && mv /tmp/0*-* /tmp/pics/ && ls /tmp/pics && echo installed'
		guest 'export XDG_RUNTIME_DIR=/tmp; rm -f /tmp/wayland-0; picture=; [ -f /usr/share/keiland/wallpaper.ppm ] && picture=--wallpaper=/usr/share/keiland/wallpaper.ppm
/bin/wayland --timeout=3600 --width=1280 --height=800 --glass $picture > /tmp/zdesktop.log 2>&1 </dev/null & sleep 7; echo started'
		;;
	home)
		pointer move 23 17 sleep 300 down sleep 60 up sleep 1500 move 700 780 sleep 300
		expect_log /tmp/zdesktop.log 'ZWL HOME opened'
		shot home.png
		keys '<esc>'
		sleep 1
		;;
	empty)
		viewer ""
		pointer move 640 420 sleep 300
		shot empty.png
		;;
	fit)
		viewer /tmp/pics/01-splash.png
		expect_log /tmp/iv.log 'IMAGEVIEW SHOW path=/tmp/pics/01-splash.png index=0 count=7'
		pointer move 640 400 sleep 200 move 650 410 sleep 400
		shot fit.png
		;;
	next)
		keys '<right>'
		sleep 2
		expect_log /tmp/iv.log 'IMAGEVIEW SHOW path=/tmp/pics/02-landscape.jpg index=1 count=7 width=4032 height=2268'
		pointer move 640 400 sleep 200 move 650 410 sleep 400
		shot next.png
		;;
	zoom)
		keys '1'
		sleep 1
		shot zoom-100.png
		pointer move 700 420 sleep 200 down sleep 80 move 640 400 sleep 40 move 520 360 sleep 40 move 400 330 sleep 400 up sleep 300
		shot zoom-drag.png
		keys '0'
		sleep 1
		shot zoom-fit.png
		;;
	wheel)
		pointer move 400 300 sleep 200 wheel-up sleep 150 wheel-up sleep 150 wheel-up sleep 600
		expect_log /tmp/iv.log 'IMAGEVIEW ZOOM scale='
		shot wheel.png
		keys '0'
		sleep 1
		;;
	portrait)
		keys '<right>'
		sleep 2
		expect_log /tmp/iv.log 'IMAGEVIEW SHOW path=/tmp/pics/03-portrait.jpg index=2 count=7 width=1060 height=1882'
		shot portrait.png
		;;
	rotate)
		keys 'r'
		sleep 1
		expect_log /tmp/iv.log 'IMAGEVIEW TURN rotation=90'
		shot rotate.png
		;;
	alpha)
		keys '<right>'
		sleep 2
		expect_log /tmp/iv.log 'IMAGEVIEW SHOW path=/tmp/pics/04-mark.png'
		shot alpha.png
		;;
	gif)
		keys '<right>'
		sleep 2
		expect_log /tmp/iv.log 'IMAGEVIEW SHOW path=/tmp/pics/05-anim.gif index=4 count=7 width=320 height=240 frames=4'
		shot gif-1.png
		sleep 0.6
		shot gif-2.png
		;;
	pixels)
		keys '<right>'
		sleep 2
		expect_log /tmp/iv.log 'IMAGEVIEW SHOW path=/tmp/pics/06-pixels.png'
		shot pixels.png
		pointer move 590 350 sleep 200 wheel-up sleep 100 wheel-up sleep 100 wheel-up sleep 100 wheel-up sleep 100 wheel-up sleep 100 wheel-up sleep 100 wheel-up sleep 100 wheel-up sleep 100 wheel-up sleep 100 wheel-up sleep 100 wheel-up sleep 100 wheel-up sleep 100 wheel-up sleep 600
		shot pixels-zoom.png
		keys '0'
		sleep 1
		;;
	broken)
		keys '<right>'
		sleep 2
		expect_log /tmp/iv.log 'IMAGEVIEW SHOW path=/tmp/pics/07-broken.png index=6 count=7 .* error=[1-9]'
		shot broken.png
		;;
	swipe)
		keys '<home>'
		sleep 2
		keys '<right>'
		sleep 2
		pointer move 400 420 sleep 200 down sleep 80 move 480 420 sleep 40 move 600 420 sleep 40 move 760 420 sleep 40 move 900 420 sleep 30 up sleep 900
		expect_log /tmp/iv.log 'IMAGEVIEW SWIPE turn direction=-1'
		expect_log /tmp/iv.log 'IMAGEVIEW SHOW path=/tmp/pics/01-splash.png index=0'
		shot swipe.png
		;;
	fullscreen)
		keys 'f'
		sleep 2
		expect_log /tmp/iv.log 'IMAGEVIEW FULLSCREEN state=1'
		# The screen capture follows the full-screen buffer only after a few more frames (Venus scanout).
		for i in 1 2 3; do
			pointer move 640 400 sleep 200 move 700 450 sleep 500
			sleep 1
		done
		shot fullscreen.png
		keys '<esc>'
		sleep 2
		expect_log /tmp/iv.log 'IMAGEVIEW FULLSCREEN state=0'
		;;
	chooser)
		keys '<ctrl-o>'
		sleep 1
		expect_log /tmp/iv.log 'IMAGEVIEW CHOOSER open'
		shot chooser.png
		keys '<esc>'
		sleep 1
		;;
	log)
		guest 'grep -v "IMAGEVIEW IMAGE" /tmp/iv.log | tail -40'
		;;
	stop)
		guest "$stop_viewer" >/dev/null
		;;
	*)
		echo "unknown step $step"
		status=1
		;;
	esac
done
guest 'grep -c ERROR /tmp/zdesktop.log' | tail -1 | sed 's/^/zdesktop ERROR lines: /'
exit $status
