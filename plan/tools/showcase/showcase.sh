#!/bin/sh
# 2026-10-04 user "現状できている機能のスクショを取りまくってくれるqemuでのテスト": a tour of the desktop on the
# Venus guest at 1920x1080 that photographs each application, App Home and a desktop of several windows, for posts.
# Nothing is judged; each step prints the picture it took.  The guest is the demo image (config-amd64-demo.mk with
# the guest harness's files, e.g. build/tq-1/demo/hdd-image.img); the compositor is started as in demo-s8-s9.sh.
#
#   plan/tools/showcase/showcase.sh IMAGE [OUTDIR]        (OUTDIR default build/showcase)
#
# Host set-up: as plan/ws035/tests/zdesktop-guest.sh (the Venus renderer, vgem).  One QEMU on the host at a time.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
image=$1
out=${2:-build/showcase}
mkdir -p "$out"
export GUEST_RUNTIME="$PWD/build/showcase-run"
export VENUS_SIZE=1920x1080
W=1920
H=1080
guest() { timeout 90 python3 plan/tools/guest/guest.py run "$1" 2>&1 </dev/null; }
put() { timeout 60 python3 plan/tools/guest/guest.py put "$1" "$2" >/dev/null 2>&1 </dev/null; }
pointer() { python3 plan/ws035/tests/qmp-pointer.py "$GUEST_RUNTIME/qmp.sock" --width $W --height $H "$@" >/dev/null 2>&1; }
keys() { python3 plan/ws035/tests/qmp-keys.py "$GUEST_RUNTIME/qmp.sock" "$@" >/dev/null 2>&1; sleep 0.8; }
shot() {
	pointer move $((W - 8)) $((H - 8)) sleep 300
	sleep "${2:-2}"
	python3 plan/ws035/tests/zdesktop-check.py "$out/$1" --runtime "$GUEST_RUNTIME" >/dev/null 2>&1
	if [ -s "$out/$1" ]; then echo "shot: $out/$1"; else echo "shot: $1 MISSING"; fi
}
# Starts an application in the compositor's session (root, as the demo tests do) and waits for its window.
app() {
	name=$1
	shift
	guest "cd /root; XDG_RUNTIME_DIR=/tmp WAYLAND_DISPLAY=wayland-0 HOME=/root $* > /tmp/app-$name.log 2>&1 < /dev/null & echo launched" >/dev/null
	sleep "${WAIT:-6}"
}
# Stops every application (not the compositor).
close_all() {
	guest 'for p in files terminal textedit settings imageview pdfviewer notes browser mview zgears monitor sh; do
		ps -A -o pid,args | awk -v p="/bin/$p" '"'"'$2 == p && $0 !~ /wayland/ {print $1}'"'"' | while read pid; do kill $pid 2>/dev/null; done
	done; echo closed' >/dev/null
	sleep 2
}

# 0. The guest, the sample files and the compositor.
sh plan/ws035/tests/zdesktop-guest.sh stop >/dev/null 2>&1
timeout 180 sh plan/ws035/tests/zdesktop-guest.sh start "$image" >/dev/null 2>&1
timeout 200 python3 plan/tools/guest/guest.py wait --timeout 180 >/dev/null 2>&1 || { echo "guest: no answer"; exit 1; }
[ -s "$out/a4.pdf" ] || sh plan/ws079/tests/make-a4-document.sh "$out/a4.pdf" >/dev/null 2>&1
mkdir -p "$out/pictures"
python3 plan/tools/imageview/make-images.py "$out/pictures" >/dev/null 2>&1
guest 'mkdir -p /root/Documents /root/Pictures; echo ok' >/dev/null
put "$out/a4.pdf" /root/Documents/manual.pdf
for f in "$out"/pictures/*; do [ -f "$f" ] && put "$f" "/root/Pictures/$(basename "$f")"; done
for f in userland/desktop/keiland/wallpapers/*.ppm; do put "$f" "/root/Pictures/$(basename "$f")"; done
printf 'zedBSD / Kei 1.0.0 Beta 1\n\nA UNIX-like OS under a permissive license,\nwith its own kernel, desktop and GPU drivers.\n\n- Keiland desktop (Wayland)\n- Vulkan on the i915 driver and Venus\n- Files, Notes, Terminal, PDF Viewer, Image Viewer\n' > "$out/hello.txt"
put "$out/hello.txt" /root/Documents/hello.txt
guest 'service stop greeter >/dev/null 2>&1; export XDG_RUNTIME_DIR=/tmp; rm -f /tmp/wayland-0; picture=; [ -f /usr/share/keiland/wallpaper.ppm ] && picture=--wallpaper=/usr/share/keiland/wallpaper.ppm
/bin/wayland --timeout=3000 --width='$W' --height='$H' --glass $picture > /tmp/zdesktop.log 2>&1 </dev/null & sleep 8; echo started' >/dev/null
sleep 10
shot 01-desktop.png 3

# 1. App Home (the launcher at the top left).
pointer move 23 17 sleep 300 down sleep 60 up sleep 1500
shot 02-app-home.png 2
keys '<esc>'
sleep 2

# 2. Each application alone.
app files /bin/files; shot 03-files.png
close_all
app terminal /bin/terminal; guest "echo ok" >/dev/null
keys 'uname -a
'; sleep 1; keys 'ls -l /
'; sleep 1
shot 04-terminal.png
close_all
app textedit /bin/textedit /root/Documents/hello.txt; shot 05-text-editor.png
close_all
app settings /bin/settings; shot 06-settings.png
close_all
WAIT=8 app imageview /bin/imageview /root/Pictures/Birch-Lake.ppm; shot 07-image-viewer.png
close_all
WAIT=10 app pdfviewer /bin/pdfviewer /root/Documents/manual.pdf; shot 08-pdf-viewer.png 3
close_all
app notes /bin/notes; shot 09-notes.png
close_all
WAIT=15 app browser /bin/browser /usr/share/browser/start.html; shot 10-browser.png 3
close_all
WAIT=10 app mview /bin/mview --windowed --size=1200x800; shot 11-model-viewer.png 3
close_all
WAIT=8 app zgears /bin/zgears; shot 12-gears.png 2
close_all
WAIT=8 app monitor /bin/monitor; shot 13-system-monitor.png 3
close_all

# 3. A desktop of several windows.
WAIT=3 app files /bin/files
WAIT=3 app terminal /bin/terminal
WAIT=3 app textedit /bin/textedit /root/Documents/hello.txt
WAIT=8 app pdfviewer /bin/pdfviewer /root/Documents/manual.pdf
sleep 4
shot 14-many-windows.png 3
pointer move 23 17 sleep 300 down sleep 60 up sleep 1500
shot 15-app-home-over-windows.png 2
keys '<esc>'
close_all

sh plan/ws035/tests/zdesktop-guest.sh stop >/dev/null 2>&1
ls "$out"/*.png | wc -l | sed 's/^/pictures: /'
