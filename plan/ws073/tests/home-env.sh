#!/bin/sh
# BUG-079: an application started from zdesktop's App Home gets zdesktop's environment.
# zdesktop --glass is started from SSH with HOME, FOO and XDG_RUNTIME_DIR set (XDG_RUNTIME_DIR first, which zdesktop's
# launch replaces), with LIBDIR's libc.so when LIBDIR is given (LD_LIBRARY_PATH, so the image's libc.so stays as it is).
# Home's list has two entries: "Env" writes `env` to /tmp/home-env.txt, and "Terminal".  Env is started from its icon and
# the file is checked for HOME, FOO, XDG_RUNTIME_DIR and WAYLAND_DISPLAY; then Terminal is started, `env` is typed into it
# and the screen is taken (OUTDIR/terminal.png).
#
#   GUEST_RUNTIME=build/ws073-run plan/ws035/tests/zdesktop-guest.sh start IMAGE    (the Venus guest must be up)
#   plan/ws073/tests/home-env.sh OUTDIR [LIBDIR]      LIBDIR: a directory in the guest holding a libc.so
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
GUEST_RUNTIME="${GUEST_RUNTIME:-$(pwd)/build/ws073-run}"
export GUEST_RUNTIME
out=${1:-build/ws073-shots}
libdir=${2:-}
mkdir -p "$out"
guest() { timeout 90 python3 plan/tools/guest/guest.py run "$1" 2>&1 </dev/null; }
check() { python3 plan/ws035/tests/zdesktop-check.py "$@" --runtime "$GUEST_RUNTIME"; }
pointer() { python3 plan/ws035/tests/qmp-pointer.py "$GUEST_RUNTIME/qmp.sock" "$@"; }
keys() { python3 plan/ws035/tests/qmp-keys.py "$GUEST_RUNTIME/qmp.sock" "$@"; }
stop_all='ps -A -o pid,comm | awk "{ n = \$2; sub(\".*/\", \"\", n) } n ~ /^(zdesktop|zdesktop-.*)$/ {print \$1}" | while read p; do kill $p; done; sleep 1'
status=0

# The centre of an icon, from zdesktop's log.
icon() {
	guest "grep 'ZWL HOME icon name=\"$1\"' /tmp/zdesktop.log | tail -1" | sed -n 's/.* x=\([0-9]*\) y=\([0-9]*\).*/\1 \2/p'
}

# Opens Home from the launcher and starts an application from its icon.
launch() {
	pointer move 23 17 sleep 300 down sleep 60 up sleep 1500
	set -- $(icon "$1")
	pointer move "${1:-0}" "${2:-0}" sleep 400 down sleep 60 up sleep 6000
}

guest "$stop_all; rm -f /tmp/home-env.txt; mkdir -p /etc/keiland /tmp/dhome" >/dev/null
guest "printf '%s\n' 'Env|/bin/sh -c \"env > /tmp/home-env.txt\"|env|3a8fd8' 'Terminal|/bin/terminal|term|323a4e' > /etc/keiland/apps.conf" >/dev/null
library=
[ -n "$libdir" ] && library="LD_LIBRARY_PATH=$libdir"
guest "rm -f /tmp/wayland-0; env -i XDG_RUNTIME_DIR=/tmp PATH=/bin:/usr/bin HOME=/tmp/dhome FOO=bar $library /bin/wayland --timeout=600 --width=1280 --height=800 --glass --wallpaper=/usr/share/keiland/wallpaper.ppm > /tmp/zdesktop.log 2>&1 </dev/null & sleep 4; echo started" >/dev/null

# Env from its icon.
launch Env
env=$(guest 'cat /tmp/home-env.txt')
echo "$env" > "$out/home-env.txt"
for want in HOME=/tmp/dhome FOO=bar XDG_RUNTIME_DIR=/tmp WAYLAND_DISPLAY=wayland-0; do
	if echo "$env" | grep -qx "$want"; then
		echo "env: $want ok"
	else
		echo "env: $want MISSING"
		status=1
	fi
done

# Terminal from its icon, and env typed into it.
launch Terminal
keys 'env | sort' '<ret>'
sleep 2
pointer move 1270 790 sleep 400
check "$out/terminal.png" >/dev/null
guest "$stop_all; rm -f /etc/keiland/apps.conf" >/dev/null
[ $status = 0 ] && echo "home-env: PASS" || echo "home-env: FAIL"
exit $status
