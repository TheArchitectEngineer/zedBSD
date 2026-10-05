#!/bin/sh
# ws089-p027: About shows the system's version name (PRETTY_NAME of /etc/os-release) as its operating system.  On the
# Venus guest of the Settings image (build-settings-image.sh, built from main after ws129-p003: the image has
# /etc/os-release), zdesktop --glass at 1280x800.  The Settings under test (BUILD/bin/settings) is copied in.
#  1. Settings started on About: its log line "ZSETTINGS ABOUT system=<PRETTY_NAME of the guest's /etc/os-release>"
#     (the name is read from the guest's file and compared), and about.png (Operating system: Kei/zedBSD ...).
#  2. /etc/os-release moved aside: "ZSETTINGS ABOUT system= kernel=" (empty) and about-none.png (Operating system:
#     Kei); the file is put back.
#  3. No ERROR line in zdesktop's log.
#
#   plan/ws089/tests/settings-guest.sh start     (the guest must be up)
#   plan/ws089/tests/settings-p027.sh BUILD [OUTDIR]   (default build/ws089-shots/p027)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
GUEST_RUNTIME="${GUEST_RUNTIME:-$(pwd)/build/ws089-run}"
export GUEST_RUNTIME
build=${1:?usage: settings-p027.sh BUILD [OUTDIR]}
out=${2:-build/ws089-shots/p027}
mkdir -p "$out"
guest() { timeout 90 python3 plan/tools/guest/guest.py run "$1" 2>&1 </dev/null; }
put() { timeout 90 python3 plan/tools/guest/guest.py put "$1" "$2" >/dev/null 2>&1 </dev/null; }
check() { python3 plan/ws035/tests/zdesktop-check.py "$@" --runtime "$GUEST_RUNTIME"; }
pointer() { python3 plan/ws035/tests/qmp-pointer.py "$GUEST_RUNTIME/qmp.sock" "$@"; }
stop_all='service stop greeter >/dev/null 2>&1; for p in $(ps -A -o pid,args | grep -E "[w]ayland( |$)|[s]ettings" | awk "{print \$1}"); do kill $p; done; i=0; while ps -A -o args | grep -qE "[w]ayland( |$)|[s]ettings" && [ $i -lt 50 ]; do sleep 0.2; i=$((i+1)); done'
start_desktop='export XDG_RUNTIME_DIR=/tmp HOME=/root; rm -f /tmp/wayland-0
/bin/wayland --testing --timeout=900 --width=1280 --height=800 --glass --wallpaper=/usr/share/keiland/wallpaper.png > /tmp/zdesktop.log 2>&1 </dev/null & sleep 4; echo started'
status=0
. plan/ws089/tests/settings-wait.sh
shot() {
	pointer move 1270 790 sleep 500 >/dev/null
	check "$out/$1" >/dev/null
	echo "shot: $out/$1"
}
about() {
	guest 'pkill -x settings 2>/dev/null; sleep 0.5; export XDG_RUNTIME_DIR=/tmp HOME=/root; /bin/settings --timeout-s=120 about > /tmp/s.log 2>&1 </dev/null & sleep 5; echo started' >/dev/null
	guest "grep 'ZSETTINGS ABOUT ' /tmp/s.log | tail -1" | tail -1
}

# The desktop and the Settings under test.
wait_guest
guest "$stop_all" >/dev/null
put "$build/bin/settings" /bin/settings
guest 'chmod 755 /bin/settings' >/dev/null
guest "$start_desktop" >/dev/null
wait_desktop

# 1. The guest's own name.
expected=$(guest "sed -n 's/^PRETTY_NAME=\"\\(.*\\)\"\$/\\1/p' /etc/os-release" | tail -1)
line=$(about)
echo "expected: $expected"
echo "about: $line"
case "$line" in
	*"ABOUT system=$expected kernel="*) [ -n "$expected" ] && echo "about-system: ok" || { echo "about-system: FAILED (no PRETTY_NAME in the guest)"; status=1; } ;;
	*) echo "about-system: FAILED"; status=1 ;;
esac
shot about.png

# 2. Without the file.
guest 'mv /etc/os-release /etc/os-release.p027' >/dev/null
line=$(about)
echo "about (no file): $line"
case "$line" in
	*"ABOUT system= kernel="*) echo "about-no-file: ok" ;;
	*) echo "about-no-file: FAILED"; status=1 ;;
esac
shot about-none.png
guest 'mv /etc/os-release.p027 /etc/os-release; pkill -x settings' >/dev/null

# 3. No ERROR.
if guest 'grep -c ERROR /tmp/zdesktop.log' | tail -1 | grep -qx 0; then echo "no-error: ok"; else echo "no-error: FAILED"; status=1; fi

echo "settings-p027: status $status (pictures in $out)"
exit $status
