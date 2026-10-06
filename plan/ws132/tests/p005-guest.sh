#!/bin/sh
# ws132-p005: the removable media on the desktop, on the Venus guest of plan/ws132/tests/config-amd64-p004.mk (started
# with plan/tools/files/files-guest.sh start IMAGE): volumed, zdesktop --glass at 1280x800 (as root, the seat's user here).
#  1. A FAT stick (label USBSTICK) plugged in: zdesktop's bar shows the media icon and it blinks
#     (KWL MEDIA new ..., volumes=1 icon=1; bar.png); nothing is mounted.
#  2. A click on the icon starts Files on its devices (KWL MEDIA files pid=, files --devices): Files lists the stick
#     not mounted, new and blinking (ZFILES DEVICE ... mounted=0 new=1 ... blink=1), under Devices and on Today
#     (ZFILES DEVICE row / card; files.png).
#  3. A double click on Today's card asks whether to mount it (ws132-p009: DEVICE mount confirm ... fs=fat
#     bytes=16777216; confirm.png); Esc leaves it (DEVICE mount answer ... confirmed=0, nothing asked of the desktop);
#     a second double click and Enter (Mount) mount it and open it (DEVICE ask ... mount=1 error=0, DEVICE result ...
#     errno=0, DEVICE open ... path=/media/USBSTICK); the bar's icon goes (icon=0); /media/USBSTICK holds HELLO.TXT
#     (mounted.png).
#  4. The eject button of its row in the sidebar ejects it (DEVICE result ... mount=0 errno=0, MESSAGE The device can be
#     taken out safely.), and /media/USBSTICK is gone.
#  5. Pulled out (not mounted): the list empties (KWL MEDIA volumes=0, ZFILES DEVICES count=0).
# PASS: every "ok" line and the last line p005: PASS.
#   plan/ws132/tests/p005-guest.sh [OUTDIR]     (default build/ws132-p005)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
GUEST_RUNTIME="${GUEST_RUNTIME:-$(pwd)/build/ws071-run}"
export GUEST_RUNTIME
qmp="$GUEST_RUNTIME/qmp.sock"
out=${1:-build/ws132-p005}
mkdir -p "$out"
guest() { timeout 60 python3 plan/tools/guest/guest.py run "$1" 2>&1 | tr -d '\r'; }
send() { timeout 40 python3 plan/ws049/tests/qmp-send.py "$qmp" "$@" >> "$out/qmp.txt" 2>&1; }
check() { python3 plan/ws035/tests/zdesktop-check.py "$@" --runtime "$GUEST_RUNTIME"; }
pointer() { python3 plan/ws035/tests/qmp-pointer.py "$GUEST_RUNTIME/qmp.sock" "$@"; }
keys() { python3 plan/ws035/tests/qmp-keys.py "$GUEST_RUNTIME/qmp.sock" "$@"; sleep 1; }
stop_all='service stop greeter >/dev/null 2>&1; for p in $(ps -A -o pid,args | grep -E "[w]ayland( |$)|[f]iles" | awk "{print \$1}"); do kill $p; done; sleep 1'
status=0

# Fails the run unless zdesktop's log (Files' lines are in it: zdesktop started Files) has a line (within TRIES s).
expect_log() {
	tries=0
	found=0
	while [ $tries -lt "${2:-6}" ]; do
		found=$(guest "grep -cE '$1' /tmp/zdesktop.log" | tail -1)
		[ "${found:-0}" -gt 0 ] 2>/dev/null && break
		tries=$((tries + 1))
		sleep 1
	done
	if [ "${found:-0}" -gt 0 ] 2>/dev/null; then echo "ok: $1"; else echo "FAIL: $1"; status=1; fi
}

# The four numbers x y width height of the latest log line matching a pattern.
rect_of() {
	guest "grep -E '$1' /tmp/zdesktop.log | tail -1" | sed -n 's/.* x=\([-0-9]*\) y=\([-0-9]*\) width=\([0-9]*\) height=\([0-9]*\).*/\1 \2 \3 \4/p'
}

# Clicks (or double-clicks with "double") a screen point.
click() {
	if [ "${3:-}" = double ]; then
		pointer move $(($1 - 2)) "$2" sleep 150 move "$1" "$2" sleep 300 down sleep 50 up sleep 80 down sleep 50 up sleep 900
	else
		pointer move $(($1 - 2)) "$2" sleep 150 move "$1" "$2" sleep 300 down sleep 60 up sleep 900
	fi
}

# A picture of the screen with the pointer out of the way.
shot() {
	pointer move 1270 790 sleep 300
	check "$out/$1" >/dev/null
	echo "shot: $out/$1"
}

# The stick.
stick="$out/stick.img"
rm -f "$stick"
truncate -s 16M "$stick"
mformat -i "$stick" -T 32768 -h 2 -s 32 -v USBSTICK ::
printf 'hi\n' > "$out/hello.txt"
mcopy -i "$stick" "$out/hello.txt" ::HELLO.TXT
: > "$out/qmp.txt"

# The desktop, with volumed.
guest "$stop_all" >/dev/null
guest 'service start volumed >/dev/null 2>&1; chown root /dev/gpu0; export XDG_RUNTIME_DIR=/tmp HOME=/root
picture=; [ -f /usr/share/keiland/wallpaper.png ] && picture=--wallpaper=/usr/share/keiland/wallpaper.png
rm -f /tmp/wayland-0; /bin/wayland --testing --timeout=600 --width=1280 --height=800 --glass $picture > /tmp/zdesktop.log 2>&1 </dev/null & sleep 5; echo started' >/dev/null

# 1.
send blockdev-add "{\"driver\":\"raw\",\"node-name\":\"stick0\",\"file\":{\"driver\":\"file\",\"filename\":\"$(realpath "$stick")\"}}"
send device_add '{"driver":"usb-storage","bus":"xhci.0","drive":"stick0","id":"stick"}'
expect_log 'KWL MEDIA new id=sd[a-z] label=USBSTICK' 8
expect_log 'KWL MEDIA volumes=1 icon=1'
expect_log 'KWL MEDIA icon x='
shot bar.png

# 2.
set -- $(rect_of 'KWL MEDIA icon x=')
click $((${1:-0} + ${3:-0} / 2)) $((${2:-0} + ${4:-0} / 2))
expect_log 'KWL MEDIA files pid=[1-9]'
expect_log 'ZFILES DEVICE id=sd[a-z] name=USBSTICK mounted=0 new=1 path= blink=1' 8
expect_log 'ZFILES DEVICE row id=sd[a-z] '
expect_log 'ZFILES DEVICE card id=sd[a-z] '
shot files.png

# 3.
set -- $(guest "grep 'KWL MAP client=' /tmp/zdesktop.log | tail -1" | sed -n 's/.* x=\([-0-9]*\) y=\([-0-9]*\).*/\1 \2/p')
wx=${1:-0}; wy=${2:-0}
set -- $(rect_of 'ZFILES DEVICE card id=')
card_x=$((wx + ${1:-0} + ${3:-0} / 2)); card_y=$((wy + ${2:-0} + ${4:-0} / 2))
click "$card_x" "$card_y" double
expect_log 'ZFILES DEVICE mount confirm id=sd[a-z] fs=fat bytes=16777216'
shot confirm.png
keys "<esc>"
expect_log 'ZFILES DEVICE mount answer id=sd[a-z] confirmed=0'
if guest "grep -c 'ZFILES DEVICE ask id=' /tmp/zdesktop.log" | tail -1 | grep -qx 0; then echo "ok: Esc asked nothing of the desktop"; else echo "FAIL: a mount was asked after Esc"; status=1; fi
click "$card_x" "$card_y" double
keys "<ret>"
expect_log 'ZFILES DEVICE mount answer id=sd[a-z] confirmed=1'
expect_log 'ZFILES DEVICE ask id=sd[a-z] mount=1 error=0'
expect_log 'ZFILES DEVICE result id=sd[a-z] mount=1 errno=0'
expect_log 'ZFILES DEVICE open id=sd[a-z] path=/media/USBSTICK'
expect_log 'KWL MEDIA volumes=1 icon=0'
guest 'ls /media/USBSTICK' > "$out/media.txt"
if grep -qi '^hello.txt$' "$out/media.txt"; then echo "ok: /media/USBSTICK holds HELLO.TXT"; else echo "FAIL: /media/USBSTICK: $(cat "$out/media.txt")"; status=1; fi
shot mounted.png

# 4.
set -- $(rect_of 'ZFILES DEVICE row id=')
click $((wx + ${1:-0} + ${3:-0} - 16)) $((wy + ${2:-0} + 15))
expect_log 'ZFILES DEVICE result id=sd[a-z] mount=0 errno=0'
expect_log 'ZFILES MESSAGE The device can be taken out safely.'
guest 'ls /media; echo media-end' > "$out/ejected.txt"
if grep -q '^USBSTICK$' "$out/ejected.txt"; then echo "FAIL: /media/USBSTICK stayed"; status=1; else echo "ok: /media/USBSTICK is gone"; fi
shot ejected.png

# 5.
send device_del '{"id":"stick"}'
sleep 4
send blockdev-del '{"node-name":"stick0"}'
expect_log 'KWL MEDIA volumes=0 icon=0'
expect_log 'ZFILES DEVICES count=0'

guest "$stop_all" >/dev/null
guest 'grep -E "KWL MEDIA|KWL SYSTEM devices|ZFILES DEVICE|ZFILES MESSAGE" /tmp/zdesktop.log' > "$out/log.txt"
[ $status = 0 ] && echo "p005: PASS" || echo "p005: FAIL"
exit $status
