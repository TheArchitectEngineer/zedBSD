#!/bin/sh
# ws035-p108: the Kei look on the Venus guest of the graphical login image
# (plan/ws035/tests/build-login-image.sh BUILD graphical-network; root's password is empty).
#  1. The picture wallpaper the test image carries is moved aside and the greeter started again, so the
#     login screen shows the drawn wallpaper in the Kei tone and the Kei mark and word at the bottom left
#     (greeter.png).
#  2. root logs in; the desktop shows the drawn wallpaper (desktop.png).
#  3. Super+L: the lock screen with the mark and word (locked.png); Enter unlocks.
#  4. files on its Home: the hero card with the mark and word (files-home.png); files on an empty folder:
#     the faint mark over "Nothing here" (files-empty.png).
# The picture wallpaper is put back at the end.
#
#   GUEST_RUNTIME=... plan/ws035/tests/zdesktop-guest.sh start build/<x>/hdd-image.img
#   plan/ws035/tests/zdesktop-p108.sh [OUTDIR] [SHOTS PREFIX]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
GUEST_RUNTIME="${GUEST_RUNTIME:-$(pwd)/build/ws071-run}"
export GUEST_RUNTIME
out=${1:-build/ws035-p108}
prefix=${2:-}
mkdir -p "$out"
guest() { timeout 120 python3 plan/tools/guest/guest.py run "$1" 2>&1 </dev/null; }
check() { python3 plan/ws035/tests/zdesktop-check.py "$@" --runtime "$GUEST_RUNTIME"; }
keys() { python3 plan/ws035/tests/qmp-keys.py "$GUEST_RUNTIME/qmp.sock" "$@"; sleep 0.8; }
pointer() { python3 plan/ws035/tests/qmp-pointer.py --width 1280 --height 800 "$GUEST_RUNTIME/qmp.sock" "$@"; }
session=/run/user/0/session.log
status=0

# Fails the run unless a guest file has at least N lines matching a pattern (within some seconds).
expect_log() {
	tries=0
	found=0
	while [ $tries -lt "${3:-10}" ]; do
		found=$(guest "grep -cE '$2' $1" | tail -1)
		[ "${found:-0}" -ge "${4:-1}" ] 2>/dev/null && break
		tries=$((tries + 1))
		sleep 1
	done
	if [ "${found:-0}" -ge "${4:-1}" ] 2>/dev/null; then
		echo "log: $2 ok"
	else
		echo "log: $2 MISSING"
		status=1
	fi
}
shot() {
	pointer move 1270 790 sleep 400
	check "$out/$1" >/dev/null
	[ -n "$prefix" ] && cp "$out/$1" "$prefix$1"
}

# 1. The drawn wallpaper: the picture aside, the greeter again.
expect_log /var/log/greeter.log 'ZWL GREETER open' 60
guest 'mv /usr/share/keiland/wallpaper.ppm /tmp/wallpaper.ppm.saved; echo moved' >/dev/null
sleep 21
guest 'kill $(sed -n "s/.*SESSIOND GREETER start pid=\([0-9]*\).*/\1/p" /var/log/sessiond.log | tail -1); echo killed' >/dev/null
expect_log /var/log/greeter.log 'ZWL GREETER open' 30 2
sleep 3
shot greeter.png

# 2. The session.
keys '\n'
expect_log $session 'ZWL HANDOFF go=1' 30
sleep 3
shot desktop.png

# 3. The lock screen.
keys '<super-l>'
expect_log $session 'ZWL LOCK locked reason=key' 5
sleep 1
shot locked.png
keys '\n'
expect_log $session 'ZWL LOCK unlocked' 8

# 4. files on its Home, then on an empty folder.
guest 'export XDG_RUNTIME_DIR=/run/user/0 WAYLAND_DISPLAY=wayland-0; /bin/files --token=h --timeout-s=120 > /tmp/fh.log 2>&1 </dev/null & sleep 6; echo started' >/dev/null
expect_log /tmp/fh.log 'ZFILES LOCATION kind=home'
sleep 1
shot files-home.png
guest 'for p in $(ps -A -o pid,args | grep -E "[f]iles" | awk "{print \$1}"); do kill $p; done; mkdir -p /tmp/empty-folder; export XDG_RUNTIME_DIR=/run/user/0 WAYLAND_DISPLAY=wayland-0; /bin/files --token=e --timeout-s=120 /tmp/empty-folder > /tmp/fe.log 2>&1 </dev/null & sleep 6; echo started' >/dev/null
expect_log /tmp/fe.log 'ZFILES LOCATION kind=folder path=/tmp/empty-folder items=0'
sleep 1
shot files-empty.png

errors=$(guest "grep -c ERROR $session" | tail -1)
[ "${errors:-1}" = 0 ] && echo "zdesktop: no ERROR" || { echo "zdesktop: ERROR lines"; status=1; }
guest "cat $session" > "$out/session.log"

# The picture wallpaper back.
guest 'for p in $(ps -A -o pid,args | grep -E "[f]iles" | awk "{print \$1}"); do kill $p; done; mv /tmp/wallpaper.ppm.saved /usr/share/keiland/wallpaper.ppm; echo back' >/dev/null
[ $status = 0 ] && echo "zdesktop-p108: PASS" || echo "zdesktop-p108: FAIL"
exit $status
