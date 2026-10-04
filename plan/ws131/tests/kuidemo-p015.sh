#!/bin/sh
# ws131-p015: the application API (kl_app, KL_VERSION 26) on the Venus guest of plan/ws131/tests/config-amd64-p015.mk
# (FILES_CONFIG=... plan/tools/files/build-files-image.sh). zdesktop --glass at 1280x800, kuidemo at 900x640.
#  1. kuidemo opens with kl_app_open and kl_app_window_create: KUIDEMO READY, its menu and its titlebar's controls
#     given as tables (KUIDEMO MENU error=0, CONTROLS error=0), its glass panels (ZWL GLASS ... panels=2); zdesktop
#     shows the two controls in the floating titlebar (ZWL TITLEBAR control ... id=1, id=2 shown=1) (start.png).
#  2. The control "Page after" (id 2): KUIDEMO ACTION action=21 id=2, PAGE List (the list page, list.png).
#  3. Ctrl+3, the menu's shortcut of Page > Dialogs (zdesktop chooses the item): ACTION action=12 id=6, PAGE Dialogs.
#  4. A right press on the window's body: KUIDEMO CONTEXT error=0 and zdesktop's context menu opens (context.png);
#     Esc closes it.
#  5. Ctrl+Q, the shortcut of File > Quit: ACTION action=1 id=2 and KUIDEMO DONE reason=close.
# PASS: every "ok" line and the last line kuidemo-p015: PASS.
#
#   plan/tools/files/files-guest.sh start           (the guest must be up, this image)
#   plan/ws131/tests/kuidemo-p015.sh [OUTDIR]       (default build/ws131-p015)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
GUEST_RUNTIME="${GUEST_RUNTIME:-$(pwd)/build/ws071-run}"
export GUEST_RUNTIME
out=${1:-build/ws131-p015}
mkdir -p "$out"
guest() { timeout 90 python3 plan/tools/guest/guest.py run "$1" 2>&1; }
. plan/tools/guest/zwl-clients.sh
check() { python3 plan/ws035/tests/zdesktop-check.py "$@" --runtime "$GUEST_RUNTIME"; }
pointer() { python3 plan/ws035/tests/qmp-pointer.py "$GUEST_RUNTIME/qmp.sock" "$@"; }
keys() { python3 plan/ws035/tests/qmp-keys.py "$GUEST_RUNTIME/qmp.sock" "$@"; sleep 0.7; }
stop_all='service stop greeter >/dev/null 2>&1; for p in $(ps -A -o pid,args | grep -E "[w]ayland( |$)|[k]uidemo" | awk "{print \$1}"); do kill $p; done; i=0; while ps -A -o args | grep -qE "[w]ayland( |$)|[k]uidemo" && [ $i -lt 50 ]; do sleep 0.2; i=$((i+1)); done'
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

# A control's place (client, ID) as zdesktop last logged it: "x y width height".
place() {
	guest "grep 'ZWL TITLEBAR control client=$(zwl_app_client $1) .* where=floating id=$2 ' /tmp/zdesktop.log | tail -1" |
	    sed -n 's/.* x=\([-0-9]*\) y=\([-0-9]*\) width=\([0-9]*\) height=\([0-9]*\).*/\1 \2 \3 \4/p'
}

# Clicks a screen point with a button (the left; "right" for the right one).
click() {
	press=down; release=up
	[ "${3:-}" = right ] && { press=right-down; release=right-up; }
	pointer move $(($1 - 2)) "$2" sleep 150 move "$1" "$2" sleep 300 $press sleep 60 $release sleep 900
}

# A picture of the screen with the pointer out of the way.
shot() {
	pointer move 1270 790 sleep 500
	check "$out/$1" >/dev/null
	echo "shot: $out/$1"
}

guest "$stop_all" >/dev/null
guest 'export XDG_RUNTIME_DIR=/tmp
picture=; [ -f /usr/share/keiland/wallpaper.ppm ] && picture=--wallpaper=/usr/share/keiland/wallpaper.ppm
rm -f /tmp/wayland-0; /bin/wayland --timeout=900 --width=1280 --height=800 --glass $picture > /tmp/zdesktop.log 2>&1 </dev/null & sleep 4
/bin/kuidemo --timeout-s=600 > /tmp/k.log 2>&1 </dev/null & sleep 5; echo started' >/dev/null
zwl_app_clients

# 1. Opened with kl_app: the menu, the controls and the glass.
expect_log /tmp/k.log 'KUIDEMO READY width='
expect_log /tmp/k.log 'KUIDEMO MENU error=0'
expect_log /tmp/k.log 'KUIDEMO CONTROLS error=0'
expect_log /tmp/zdesktop.log "ZWL GLASS client=$zc1 surface=[0-9]+ panels=2 "
expect_log /tmp/zdesktop.log "ZWL TITLEBAR control client=$zc1 .* where=floating id=1 .* shown=1"
expect_log /tmp/zdesktop.log "ZWL TITLEBAR control client=$zc1 .* where=floating id=2 .* shown=1"
shot start.png

# 2. The page after.
set -- $(place 1 2)
click $((${1:-0} + ${3:-0} / 2)) $((${2:-0} + ${4:-0} / 2))
expect_log /tmp/k.log 'KUIDEMO ACTION action=21 id=2'
expect_log /tmp/k.log 'KUIDEMO PAGE List'
shot list.png

# 3. The menu's shortcut of Page > Dialogs.
keys '<ctrl-3>'
expect_log /tmp/k.log 'KUIDEMO ACTION action=12 id=6'
expect_log /tmp/k.log 'KUIDEMO PAGE Dialogs'

# 4. A right press on the body (below the controls): the context menu of the pages.
set -- $(place 1 2)
click $((${1:-0} + 40)) $((${2:-0} + 200)) right
expect_log /tmp/k.log 'KUIDEMO CONTEXT error=0'
shot context.png
keys '<esc>'

# 5. Quit through the menu's shortcut.
keys '<ctrl-q>'
expect_log /tmp/k.log 'KUIDEMO ACTION action=1 id=2'
expect_log /tmp/k.log 'KUIDEMO DONE reason=close'

errors=$(guest "grep -c ERROR /tmp/zdesktop.log" | tail -1)
[ "${errors:-1}" = 0 ] && echo "zdesktop: no ERROR" || { echo "zdesktop: ERROR lines"; status=1; }
guest "$stop_all" >/dev/null
guest 'cat /tmp/k.log' > "$out/k.log"
guest 'grep -E "ZWL (TITLEBAR|GLASS|MENU|CONTEXT)" /tmp/zdesktop.log' > "$out/zdesktop.log"
[ $status = 0 ] && echo "kuidemo-p015: PASS" || echo "kuidemo-p015: FAIL"
exit $status
