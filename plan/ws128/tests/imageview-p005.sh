#!/bin/sh
# ws128-p005: Image Viewer's Move to Trash, slideshow and Open With on the Venus guest (the image of
# plan/ws128/tests/config-amd64-imageview.mk, built by plan/tools/guest/test-image.sh; start the guest first, e.g.
# plan/tools/files/files-guest.sh start IMAGE).  zdesktop --glass at 1280x800, imageview at 1180x700, HOME=/tmp/ivhome,
# three pictures of plan/tools/imageview/make-images.py in /tmp/ivpics (a.png, b.jpg, c.png).
#  1. Open With: the menu's applications for the picture (MENU openers count=N, N >= 1; Files' ways less Quick Look).
#  2. Right, then Delete: b.jpg goes to ~/.local/share/Trash (TRASH ... errno=0, the file and its record there), and
#     the viewer shows c.png, now the second of two (SHOW ... index=1 count=2): trash.png.
#  3. F5: a slideshow on the full screen (SLIDESHOW start, FULLSCREEN state=1); after its interval the next image
#     (SLIDESHOW next): slideshow.png.  Esc stops it and leaves the full screen (SLIDESHOW stop, FULLSCREEN state=0).
#   sh plan/ws128/tests/imageview-p005.sh [OUTDIR]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
GUEST_RUNTIME="${GUEST_RUNTIME:-$(pwd)/build/ws071-run}"
export GUEST_RUNTIME
out=${1:-build/ws128-p005}
mkdir -p "$out"
status=0
guest() { timeout 90 python3 plan/tools/guest/guest.py run "$1" 2>&1 </dev/null; }
put() { timeout 120 python3 plan/tools/guest/guest.py put "$1" "$2" >/dev/null 2>&1 </dev/null || { echo "put $1: FAILED"; status=1; }; }
pointer() { python3 plan/ws035/tests/qmp-pointer.py "$GUEST_RUNTIME/qmp.sock" "$@"; }
keys() { python3 plan/ws035/tests/qmp-keys.py "$GUEST_RUNTIME/qmp.sock" "$@" >/dev/null; }
shot() {
	pointer move 1270 790 sleep 400
	python3 plan/ws035/tests/zdesktop-check.py "$out/$1" --runtime "$GUEST_RUNTIME" >/dev/null
	echo "shot $1"
}
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
expect_guest() {
	if guest "$1 && echo YES" | grep -q YES; then echo "guest: $2 ok"; else echo "guest: $2 FAILED"; status=1; fi
}
stop_all='service stop greeter >/dev/null 2>&1; for p in $(ps -A -o pid,args | grep -E "[w]ayland( |$)|[i]mageview|[f]iles" | awk "{print \$1}"); do kill $p; done; i=0; while ps -A -o args | grep -qE "[w]ayland( |$)|[i]mageview" && [ $i -lt 50 ]; do sleep 0.2; i=$((i+1)); done'

# The pictures, and a fresh home.
python3 plan/tools/imageview/make-images.py "$out/images" >/dev/null
guest "$stop_all" >/dev/null
guest 'rm -rf /tmp/ivhome /tmp/ivpics; mkdir -p /tmp/ivhome /tmp/ivpics; echo made' >/dev/null
put "$out/images/01-splash.png" /tmp/ivpics/a.png
put "$out/images/02-landscape.jpg" /tmp/ivpics/b.jpg
put "$out/images/04-mark.png" /tmp/ivpics/c.png
guest 'export XDG_RUNTIME_DIR=/tmp; rm -f /tmp/wayland-0; picture=; [ -f /usr/share/keiland/wallpaper.png ] && picture=--wallpaper=/usr/share/keiland/wallpaper.png
/bin/wayland --testing --timeout=600 --width=1280 --height=800 --glass $picture > /tmp/zdesktop.log 2>&1 </dev/null & sleep 5
HOME=/tmp/ivhome /bin/imageview --width=1180 --height=700 /tmp/ivpics/a.png > /tmp/iv.log 2>&1 </dev/null & sleep 5; echo started' >/dev/null
expect_log /tmp/iv.log 'IMAGEVIEW READY'
expect_log /tmp/iv.log 'IMAGEVIEW SHOW path=/tmp/ivpics/a.png index=0 count=3'

# 1. Open With's applications.
expect_log /tmp/iv.log 'IMAGEVIEW MENU openers count=[1-8] first='

# 2. Right, then Delete.
pointer move 640 400 sleep 300
keys '<right>'
sleep 2
expect_log /tmp/iv.log 'IMAGEVIEW SHOW path=/tmp/ivpics/b.jpg index=1 count=3'
keys '<delete>'
sleep 2
expect_log /tmp/iv.log 'IMAGEVIEW TRASH path=/tmp/ivpics/b.jpg trashed=/tmp/ivhome/.local/share/Trash/files/b.jpg errno=0'
expect_log /tmp/iv.log 'IMAGEVIEW SHOW path=/tmp/ivpics/c.png index=1 count=2'
expect_guest '[ -f /tmp/ivhome/.local/share/Trash/files/b.jpg ] && grep -q "Path=/tmp/ivpics/b.jpg" /tmp/ivhome/.local/share/Trash/info/b.jpg.trashinfo && [ ! -e /tmp/ivpics/b.jpg ]' 'b.jpg is in the trash with its record'
shot trash.png

# 3. The slideshow.
keys '<f5>'
sleep 2
expect_log /tmp/iv.log 'IMAGEVIEW SLIDESHOW start count=2'
expect_log /tmp/iv.log 'IMAGEVIEW FULLSCREEN state=1'
sleep 3
expect_log /tmp/iv.log 'IMAGEVIEW SLIDESHOW next index=0'
for i in 1 2; do
	pointer move 640 400 sleep 200 move 700 450 sleep 500
done
shot slideshow.png
keys '<esc>'
sleep 2
expect_log /tmp/iv.log 'IMAGEVIEW SLIDESHOW stop'
expect_log /tmp/iv.log 'IMAGEVIEW FULLSCREEN state=0'

errors=$(guest "grep -c ERROR /tmp/zdesktop.log" | tail -1)
[ "${errors:-1}" = 0 ] && echo "zdesktop: no ERROR" || { echo "zdesktop: ERROR lines"; status=1; }
guest "$stop_all" >/dev/null
[ $status = 0 ] && echo "imageview-p005: PASS" || echo "imageview-p005: FAIL"
exit $status
