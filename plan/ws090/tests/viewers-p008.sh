#!/bin/sh
# ws090-p008: PDF Viewer and Image Viewer on libkeiui's window, on the Venus guest of the WS079 demo image
# (plan/ws079/tests/config-amd64-demo.mk built from this tree: zdesktop, the viewers, libkeiui).  zdesktop --glass at
# 1280x800.  Pictures and the programs' own logs (read over SSH); nothing reads the console.
#  PDF Viewer
#   1. The encrypted document (run-pdfviewer-host.sh's password.pdf, user password "secret"): the password card
#      (pdf-card.png).
#   2. The bottom-left corner's swipe opens the QWERTY row: the viewer hears the inset (PDFVIEWER KEYBOARD inset
#      bottom>0) and the card stands in the part of the window the row leaves (pdf-card-keyboard.png); the row
#      closes again (inset 0).
#   3. "secret" and Enter open the document (PASSWORD accepted, OPEN ... pages=3).
#   4. Ctrl+O shows libkeiui's chooser at /root (CHOOSER open folder=/root; pdf-chooser.png); "a" selects a4.pdf,
#      Enter opens it (CHOOSER chose path=/root/a4.pdf, OPEN ... pages=10; pdf-chosen.png).
#   5. Ctrl+O and Escape: CHOOSER cancelled, the document stays.
#  Image Viewer
#   6. The picture folder: Ctrl+O shows the chooser (CHOOSER open folder=/tmp/pics; iv-chooser.png), "0" (the first
#      picture), Down, Down and Enter pick 03-portrait.jpg (CHOOSER chose, SHOW path=/tmp/pics/03-portrait.jpg; iv-chosen.png); Ctrl+O and Escape
#      cancel.
#   7. F: the full screen as the compositor configured it (FULLSCREEN state=1; iv-fullscreen.png), Esc back (state=0).
# Prints "viewers-p008: PASS" or "viewers-p008: FAIL".
#
#   plan/ws090/tests/viewers-p008.sh IMAGE OUTDIR [BIN]   (BIN: where imageview and the pictures' libraries come from)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
image=$1
out=$2
bin=${3:-build/amd64}
mkdir -p "$out"
GUEST_RUNTIME="${GUEST_RUNTIME:-$(pwd)/build/ws090/viewers-run}"
export GUEST_RUNTIME
status=0
guest() { timeout 120 python3 plan/tools/guest/guest.py run "$1" 2>&1 </dev/null; }
put() { timeout 120 python3 plan/tools/guest/guest.py put "$1" "$2" >/dev/null 2>&1 </dev/null || { echo "put $1: FAILED"; status=1; }; }
pointer() { python3 plan/ws035/tests/qmp-pointer.py "$GUEST_RUNTIME/qmp.sock" "$@" >/dev/null; }
keys() { python3 plan/ws035/tests/qmp-keys.py "$GUEST_RUNTIME/qmp.sock" "$@" >/dev/null; sleep 0.6; }
shot() { pointer move 1275 400 sleep 300; python3 plan/ws035/tests/zdesktop-check.py "$out/$1" --runtime "$GUEST_RUNTIME" >/dev/null; echo "shot $out/$1"; }

# Waits (up to 15 s) for more than N lines of a guest log matching a pattern.
expect_more() {
	tries=0
	while [ $tries -lt 15 ]; do
		found=$(guest "grep -cE '$2' $1" | tail -1)
		[ "${found:-0}" -gt "$3" ] 2>/dev/null && { echo "log: $2 ok"; return 0; }
		tries=$((tries + 1))
		sleep 1
	done
	echo "log: $2 MISSING"
	status=1
	return 1
}
count() { guest "grep -cE '$2' $1" | tail -1; }

# The guest, the documents, the pictures, zdesktop.
sh plan/ws035/tests/zdesktop-guest.sh stop >/dev/null 2>&1
timeout 180 sh plan/ws035/tests/zdesktop-guest.sh start "$image" >/dev/null 2>&1
tries=0
until guest 'echo up' | grep -q '^up$' || [ $tries -ge 30 ]; do tries=$((tries + 1)); sleep 5; done
sh plan/ws079/tests/make-a4-document.sh "$out/a4.pdf" >/dev/null 2>&1 || { echo "a4.pdf: FAILED"; status=1; }
put "$out/a4.pdf" /root/a4.pdf
put build/ws079-p006-host/password.pdf /root/password.pdf
python3 plan/tools/imageview/make-images.py build/ws091-images >/dev/null
guest 'mkdir -p /tmp/pics' >/dev/null
for picture in build/ws091-images/*; do
	put "$picture" "/tmp/pics/$(basename "$picture")"
done
put "$bin/bin/imageview" /bin/imageview
for library in libpng-compat libjpeg-compat libgif-compat libz-compat; do
	put "$bin/dynamic/$library.so" "/lib/$library.so"
done
guest 'chmod 755 /bin/imageview' >/dev/null
guest 'service stop greeter >/dev/null 2>&1; export XDG_RUNTIME_DIR=/tmp; rm -f /tmp/wayland-0; picture=; [ -f /usr/share/keiland/wallpaper.ppm ] && picture=--wallpaper=/usr/share/keiland/wallpaper.ppm
/bin/wayland --timeout=1200 --width=1280 --height=800 --glass $picture > /tmp/zdesktop.log 2>&1 </dev/null &
i=0; while ! grep -q "ZWL READY" /tmp/zdesktop.log && [ $i -lt 60 ]; do sleep 0.5; i=$((i+1)); done; sleep 2; echo started' >/dev/null
expect_more /tmp/zdesktop.log 'ZWL READY' 0

# 1. The password card.
guest 'export XDG_RUNTIME_DIR=/tmp HOME=/root; /bin/pdfviewer --timeout-s=900 /root/password.pdf > /tmp/pv.log 2>&1 </dev/null & sleep 5; echo started' >/dev/null
expect_more /tmp/pv.log 'PDFVIEWER READY' 0
expect_more /tmp/pv.log 'PASSWORD asked' 0
pointer move 640 400 sleep 200 down sleep 60 up sleep 400
shot pdf-card.png

# 2. The QWERTY row and the inset.
pointer move 6 792 sleep 200 down sleep 80 move 78 721 sleep 60 move 150 650 sleep 120 up sleep 1500
expect_more /tmp/zdesktop.log 'ZWL OSK open kind=qwerty' 0
expect_more /tmp/pv.log 'KEYBOARD inset right=0 bottom=[1-9][0-9]*' 0
sleep 1
shot pdf-card-keyboard.png
closed=$(count /tmp/pv.log 'KEYBOARD inset right=0 bottom=0')
pointer move 6 792 sleep 200 down sleep 80 move 78 721 sleep 60 move 150 650 sleep 120 up sleep 1500
expect_more /tmp/pv.log 'KEYBOARD inset right=0 bottom=0' "${closed:-0}"
shot pdf-card-closed.png

# 3. The password.
pointer move 640 400 sleep 200 down sleep 60 up sleep 400
keys secret
keys '<ret>'
expect_more /tmp/pv.log 'PASSWORD accepted' 0
expect_more /tmp/pv.log 'OPEN path=/root/password.pdf pages=3' 0

# 4. The chooser: a4.pdf.
keys '<ctrl-o>'
expect_more /tmp/pv.log 'CHOOSER open folder=/root' 0
sleep 1.5
shot pdf-chooser.png
keys a
keys '<ret>'
expect_more /tmp/pv.log 'CHOOSER chose path=/root/a4.pdf' 0
expect_more /tmp/pv.log 'OPEN path=/root/a4.pdf pages=10' 0
sleep 1
shot pdf-chosen.png

# 5. The chooser cancelled.
keys '<ctrl-o>'
sleep 1.5
keys '<esc>'
expect_more /tmp/pv.log 'CHOOSER cancelled' 0
keys '<ctrl-q>'
sleep 1

# 6. Image Viewer's chooser.
guest 'export XDG_RUNTIME_DIR=/tmp HOME=/tmp; /bin/imageview --width=1180 --height=700 /tmp/pics/01-splash.png > /tmp/iv.log 2>&1 </dev/null & sleep 5; echo started' >/dev/null
expect_more /tmp/iv.log 'IMAGEVIEW READY' 0
pointer move 640 400 sleep 200 down sleep 60 up sleep 400
keys '<ctrl-o>'
expect_more /tmp/iv.log 'CHOOSER open folder=/tmp/pics' 0
sleep 1.5
shot iv-chooser.png
keys 0
keys '<down>'
keys '<down>'
keys '<ret>'
expect_more /tmp/iv.log 'CHOOSER chose path=/tmp/pics/03-portrait.jpg' 0
expect_more /tmp/iv.log 'SHOW path=/tmp/pics/03-portrait.jpg' 0
sleep 1
shot iv-chosen.png
keys '<ctrl-o>'
sleep 1.5
keys '<esc>'
expect_more /tmp/iv.log 'CHOOSER cancelled' 0

# 7. The full screen.
pointer move 640 400 sleep 200 down sleep 60 up sleep 400
keys f
expect_more /tmp/iv.log 'FULLSCREEN state=1' 0
sleep 1
shot iv-fullscreen.png
keys '<esc>'
expect_more /tmp/iv.log 'FULLSCREEN state=0' 0
keys '<ctrl-q>'
sleep 1

# The logs, and no errors.
guest 'cat /tmp/pv.log' > "$out/pv.log"
guest 'cat /tmp/iv.log' > "$out/iv.log"
guest 'grep -E "ZWL (OSK|INSET|ERROR)" /tmp/zdesktop.log' > "$out/zdesktop.log"
errors=$(count /tmp/zdesktop.log 'ZWL ERROR')
if [ "${errors:-1}" = 0 ]; then echo "no ZWL ERROR ok"; else echo "ZWL ERROR ($errors) FAIL"; status=1; fi
failed=$(guest "grep -cE 'FAILED' /tmp/pv.log /tmp/iv.log" | awk -F: '{s += $NF} END {print s + 0}')
if [ "${failed:-1}" = 0 ]; then echo "no FAILED ok"; else echo "FAILED lines ($failed) FAIL"; status=1; fi
sh plan/ws035/tests/zdesktop-guest.sh stop >/dev/null 2>&1
[ $status -eq 0 ] && echo "viewers-p008: PASS" || echo "viewers-p008: FAIL"
exit $status
