#!/bin/sh
# ws127-p010: the path's field of files in zdesktop's titlebar, on the Venus guest of the files image
# (plan/tools/files/build-files-image.sh).  zdesktop --glass at 1280x800; files at 1000x640 on the sample home
# (/tmp/fhome), opened on the home folder (its breadcrumb is one part, Home, the last part).
#  1. A click on the last part (Home) makes the path a field (ZFILES TITLEBAR kind=0 id=4 detail=0; ZWL TITLEBAR
#     focus id=4 edit=1).
#  2. /tmp/fhome/P typed: a second later files suggests Pictures/ and Projects/ (ZFILES LOCATION suggest ... count=2,
#     ZWL TITLEBAR suggestions id=4 count=2) and the list drops down under the field (suggestions.png).
#  3. Down and Enter put the first in the field (ZWL TITLEBAR suggestion chosen index=0 text=/tmp/fhome/Pictures/, the
#     field heard changed); Enter goes there (LOCATION kind=folder path=/tmp/fhome/Pictures).
#  4. Ctrl+L, /tmp/fhome/D typed: Desktop/, Documents/, Downloads/; a click on the second row puts Documents/ in the
#     field (index=1) and the field keeps the keyboard; Enter goes there.
#  5. Ctrl+L, /tmp/fhome/D typed, Esc: the list goes (no suggestion chosen) and the field stays; Esc again ends it
#     (text_done cancelled), nothing moves.
# PASS: every "ok" line and the last line files-p010: PASS.
#
#   plan/tools/files/files-guest.sh start     (the guest must be up, the files image)
#   plan/ws127/tests/files-p010.sh [OUTDIR]   (default build/ws127-p010)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
GUEST_RUNTIME="${GUEST_RUNTIME:-$(pwd)/build/ws071-run}"
export GUEST_RUNTIME
out=${1:-build/ws127-p010}
mkdir -p "$out"
guest() { timeout 90 python3 plan/tools/guest/guest.py run "$1" 2>&1; }
. plan/tools/guest/zwl-clients.sh
check() { python3 plan/ws035/tests/zdesktop-check.py "$@" --runtime "$GUEST_RUNTIME"; }
pointer() { python3 plan/ws035/tests/qmp-pointer.py "$GUEST_RUNTIME/qmp.sock" "$@"; }
keys() { python3 plan/ws035/tests/qmp-keys.py "$GUEST_RUNTIME/qmp.sock" "$@"; sleep 0.7; }
stop_all='service stop greeter >/dev/null 2>&1; for p in $(ps -A -o pid,args | grep -E "[w]ayland( |$)|[f]iles" | awk "{print \$1}"); do kill $p; done; i=0; while ps -A -o args | grep -qE "[w]ayland( |$)|[f]iles" && [ $i -lt 50 ]; do sleep 0.2; i=$((i+1)); done'
status=0

# Fails the run unless a log has a line matching a pattern (within a few seconds).
expect_log() {
	tries=0
	found=0
	while [ $tries -lt 5 ]; do
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

# Fails the run when a log has a line matching a pattern.
expect_none() {
	found=$(guest "grep -cE '$2' $1" | tail -1)
	if [ "${found:-0}" = 0 ]; then
		echo "none: $2 ok"
	else
		echo "none: $2 FOUND"
		status=1
	fi
}

# A control's place (client, where, ID) as zdesktop last logged it: "x y width height".
place() {
	guest "grep 'ZWL TITLEBAR control client=$(zwl_app_client $1) .* where=$2 id=$3 ' /tmp/zdesktop.log | tail -1" |
	    sed -n 's/.* x=\([-0-9]*\) y=\([-0-9]*\) width=\([0-9]*\) height=\([0-9]*\).*/\1 \2 \3 \4/p'
}

# Clicks a screen point.
click() {
	pointer move $(($1 - 2)) "$2" sleep 150 move "$1" "$2" sleep 300 down sleep 60 up sleep "${3:-900}"
}

# A picture of the screen (the pointer left where it is).
shot() {
	check "$out/$1" >/dev/null
	echo "shot: $out/$1"
}

guest "$stop_all" >/dev/null
guest 'rm -f /tmp/wayland-0 /tmp/files.clipboard; rm -rf /tmp/fhome; sh /usr/share/files-tests/make-home.sh /tmp/fhome >/dev/null'
guest 'export XDG_RUNTIME_DIR=/tmp
picture=; [ -f /usr/share/keiland/wallpaper.ppm ] && picture=--wallpaper=/usr/share/keiland/wallpaper.ppm
/bin/wayland --timeout=900 --width=1280 --height=800 --glass $picture > /tmp/zdesktop.log 2>&1 </dev/null & sleep 4
HOME=/tmp/fhome /bin/files --token=f1 --timeout-s=800 --width=1000 --height=640 /tmp/fhome > /tmp/f.log 2>&1 </dev/null & sleep 6; echo started' >/dev/null
zwl_app_clients
expect_log /tmp/f.log 'ZFILES TITLEBAR state back=0 forward=0 parts=1 last=Home '
expect_log /tmp/zdesktop.log "ZWL TITLEBAR control client=$zc1 .* where=floating id=4 .* shown=1"

# 1. The last part (the only one) makes the path a field.
set -- $(place 1 floating 4)
click $((${1:-0} + 12)) $((${2:-0} + ${4:-0} / 2))
expect_log /tmp/f.log 'ZFILES TITLEBAR kind=0 id=4 detail=0 '
expect_log /tmp/zdesktop.log 'ZWL TITLEBAR focus client=[0-9]+ surface=[0-9]+ id=4 edit=1'

# 2. A path typed, and the suggestions a second later.
keys '<ctrl-a>' '/tmp/fhome/P'
sleep 2
expect_log /tmp/f.log 'ZFILES LOCATION suggest text=/tmp/fhome/P count=2'
expect_log /tmp/zdesktop.log 'ZWL TITLEBAR suggestions id=4 count=2'
expect_log /tmp/zdesktop.log 'ZWL TITLEBAR suggestions shown x=[0-9]+ y=[0-9]+ '
pointer move 1270 790 sleep 500
shot suggestions.png

# 3. Down and Enter take the first; Enter goes there.
keys '<down>' '<ret>'
expect_log /tmp/zdesktop.log 'ZWL TITLEBAR suggestion chosen index=0 text=/tmp/fhome/Pictures/'
expect_log /tmp/f.log 'ZFILES TITLEBAR kind=1 id=4 detail=0 text=/tmp/fhome/Pictures/'
keys '<ret>'
expect_log /tmp/f.log 'ZFILES LOCATION kind=folder path=/tmp/fhome/Pictures items='

# 4. Ctrl+L, D, and a click on the second row.
keys '<ctrl-l>'
keys '<ctrl-a>' '/tmp/fhome/D'
sleep 2
expect_log /tmp/f.log 'ZFILES LOCATION suggest text=/tmp/fhome/D count=3'
set -- $(guest "grep 'ZWL TITLEBAR suggestions shown ' /tmp/zdesktop.log | tail -1" | sed -n 's/.* x=\([0-9]*\) y=\([0-9]*\) width=\([0-9]*\) row=\([0-9]*\) first=\([0-9]*\) .*/\1 \5 \4/p')
click $((${1:-0} + 30)) $((${2:-0} + ${3:-28} + ${3:-28} / 2)) 900
expect_log /tmp/zdesktop.log 'ZWL TITLEBAR suggestion chosen index=1 text=/tmp/fhome/Documents/'
keys '<ret>'
expect_log /tmp/f.log 'ZFILES LOCATION kind=folder path=/tmp/fhome/Documents items='

# 5. Esc takes the list away first, and then ends the field; nothing moves.
keys '<ctrl-l>'
keys '<ctrl-a>' '/tmp/fhome/M'
sleep 2
expect_log /tmp/f.log 'ZFILES LOCATION suggest text=/tmp/fhome/M count=[1-9]'
keys '<esc>'
expect_none /tmp/zdesktop.log 'ZWL TITLEBAR suggestion chosen index=[0-9]+ text=/tmp/fhome/M'
expect_none /tmp/f.log 'ZFILES TITLEBAR kind=2 id=4 detail=1 text=/tmp/fhome/M'
keys '<esc>'
expect_log /tmp/f.log 'ZFILES TITLEBAR kind=2 id=4 detail=1 text=/tmp/fhome/M'
expect_none /tmp/f.log 'ZFILES LOCATION kind=folder path=/tmp/fhome/M'

errors=$(guest "grep -c ERROR /tmp/zdesktop.log" | tail -1)
[ "${errors:-1}" = 0 ] && echo "zdesktop: no ERROR" || { echo "zdesktop: ERROR lines"; status=1; }
guest "$stop_all" >/dev/null
guest 'cat /tmp/f.log' > "$out/f.log"
guest 'grep -E "ZWL TITLEBAR" /tmp/zdesktop.log' > "$out/zdesktop-titlebar.log"
[ $status = 0 ] && echo "files-p010: PASS" || echo "files-p010: FAIL"
exit $status
