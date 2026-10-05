#!/bin/sh
# BUG-194: the way out of fullscreen is the compositor's (F11 and Super+Down, the 2026-10-05 user decision), and
# Terminal's F11 makes it fullscreen.  On the
# Venus guest of plan/ws035/tests/config-amd64-zdesktop.mk built from the commit under test
# (plan/ws035/tests/zdesktop-guest.sh start IMAGE), zdesktop --glass at 1280x800.
#  1. Terminal: F11 makes it fullscreen (its Fullscreen item's shortcut: "MENU ... activated" or "ZTERM FULLSCREEN key
#     on=1", then a configure with fullscreen=1); terminal-full.png.
#  2. F11 again: the compositor takes it ("GLASS fullscreen-leave surface=T via=f11", "WINDOW unfullscreen surface=T")
#     and the window is a window again (a configure with fullscreen=0); terminal-back.png.
#  3. A window that goes fullscreen by itself and never leaves (wltest --fullscreen-at=10): F11 brings it back all the
#     same ("GLASS fullscreen-leave ... via=f11", "WINDOW unfullscreen"); wltest-back.png.
#  4. Terminal fullscreen again by F11, then Super+Down: the compositor takes it ("GLASS fullscreen-leave surface=T
#     via=super-down"); terminal-super-down.png.
#  5. BUG-208: Terminal docked (a double click on its title bar, "GLASS dock surface=T via=double-click"), then F11:
#     a full-screen configure ("width=1280 height=800 fullscreen=1", one more than before); docked-full.png shows no
#     desktop where the bar was.  F11 again: docked again ("WINDOW unfullscreen surface=T x=0 y=48 placed=N docked=1",
#     a configure "width=1280 height=752 fullscreen=0"); docked-back.png.
#  6. No ERROR in zdesktop's log.
# PASS: the last line "bug194: status 0".
#   plan/ws099/tests/bug194-guest.sh [OUTDIR]          (default build/ws099-bug194)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
export GUEST_RUNTIME="${GUEST_RUNTIME:-$PWD/build/ws035-sq-run}"
out=${1:-build/ws099-bug194}
mkdir -p "$out"
guest() { timeout 90 python3 plan/tools/guest/guest.py run "$1" 2>&1; }
check() { python3 plan/ws035/tests/zdesktop-check.py "$@" --runtime "$GUEST_RUNTIME"; }
pointer() { python3 plan/ws035/tests/qmp-pointer.py "$GUEST_RUNTIME/qmp.sock" "$@"; }
keys() { python3 plan/ws035/tests/qmp-keys.py "$GUEST_RUNTIME/qmp.sock" "$@"; sleep 2; }
stop_all='service stop greeter >/dev/null 2>&1; for p in $(ps -A -o pid,args | grep -E "[w]ayland( |$)|[w]ltest|[t]erminal" | awk "{print \$1}"); do kill $p; done; i=0; while ps -A -o args | grep -qE "[w]ayland( |$)|[w]ltest|[t]erminal" && [ $i -lt 50 ]; do sleep 0.2; i=$((i+1)); done'
env='export XDG_RUNTIME_DIR=/tmp WAYLAND_DISPLAY=wayland-0 HOME=/root;'
status=0

# Fails the run unless the compositor's log has a line matching a pattern.
expect_log() {
	if guest "grep -E '$1' /tmp/zdesktop.log" | grep -q .; then
		echo "log: $1 ok"
	else
		echo "log: $1 MISSING"
		status=1
	fi
}

# How many lines of the compositor's log match a pattern.
count() {
	guest "grep -cE '$1' /tmp/zdesktop.log" | tail -1
}

# Fails the run unless the compositor's log has more lines matching a pattern than before.
expect_more() {
	n=$(count "$1")
	if [ "${n:-0}" -gt "$2" ] 2>/dev/null; then
		echo "log: $1 (more than $2) ok"
	else
		echo "log: $1 (more than $2) MISSING"
		status=1
	fi
}

# The surface, x and y of the latest window mapped.
last_map() {
	guest "grep 'ZWL MAP client=' /tmp/zdesktop.log | tail -1" | sed -n 's/.* surface=\([0-9]*\) x=\([-0-9]*\) y=\([-0-9]*\).*/\1 \2 \3/p'
}

guest "$stop_all" >/dev/null
guest 'export XDG_RUNTIME_DIR=/tmp; rm -f /tmp/wayland-0
/bin/wayland --testing --timeout=600 --width=1280 --height=800 --glass > /tmp/zdesktop.log 2>&1 </dev/null & for w in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do grep -q ZWL.READY /tmp/zdesktop.log 2>/dev/null && break; sleep 0.5; done; sleep 1; echo started' >/dev/null

# 1. Terminal, focused by a click, then F11.
guest "$env /bin/terminal > /tmp/t.log 2>&1 </dev/null & sleep 5; echo started" >/dev/null
set -- $(last_map)
t=${1:-0}; tx=${2:-0}; ty=${3:-0}
echo "terminal: surface $t at $tx,$ty"
pointer move $((tx + 100)) $((ty + 100)) sleep 300 down sleep 60 up sleep 500 >/dev/null
keys "<f11>"
expect_log "CONFIGURE client=[0-9]+ surface=$t serial=[0-9]+ width=1280 height=800 fullscreen=1"
check "$out/terminal-full.png" >/dev/null

# 2. F11 again: the compositor's.
keys "<f11>"
expect_log "GLASS fullscreen-leave surface=$t via=f11 error=0"
expect_log "WINDOW unfullscreen surface=$t "
expect_log "CONFIGURE client=[0-9]+ surface=$t serial=[0-9]+ .*fullscreen=0"
check "$out/terminal-back.png" >/dev/null

# 3. A window that goes fullscreen by itself.
guest "$env /bin/wltest --windowed --size=420x300 --color=f4f7fc --frames=3000 --delay-ms=50 --fullscreen-at=10 > /tmp/w.log 2>&1 </dev/null & sleep 5; echo started" >/dev/null
set -- $(last_map)
w=${1:-0}
echo "wltest: surface $w"
expect_log "CONFIGURE client=[0-9]+ surface=$w serial=[0-9]+ width=1280 height=800 fullscreen=1"
keys "<f11>"
expect_log "GLASS fullscreen-leave surface=$w via=f11 error=0"
expect_log "WINDOW unfullscreen surface=$w "
check "$out/wltest-back.png" >/dev/null

# 4. Terminal again: F11 into fullscreen, Super+Down out of it.
pointer move $((tx + 100)) $((ty + 100)) sleep 300 down sleep 60 up sleep 500 >/dev/null
keys "<f11>"
keys "<super-down>"
expect_log "GLASS fullscreen-leave surface=$t via=super-down error=0"
check "$out/terminal-super-down.png" >/dev/null

# 5. BUG-208: Terminal docked by a double click on its title bar, then F11 into fullscreen and out of it again.
full="CONFIGURE client=[0-9]+ surface=$t serial=[0-9]+ width=1280 height=800 fullscreen=1"
docked="CONFIGURE client=[0-9]+ surface=$t serial=[0-9]+ width=1280 height=752 fullscreen=0"
pointer move $((tx + 150)) $((ty - 30)) sleep 400 down sleep 60 up sleep 60 down sleep 60 up sleep 1500 >/dev/null
expect_log "GLASS dock surface=$t via=double-click"
fulls=$(count "$full")
keys "<f11>"
expect_more "$full" "${fulls:-0}"
check "$out/docked-full.png" >/dev/null
dockeds=$(count "$docked")
keys "<f11>"
expect_log "WINDOW unfullscreen surface=$t x=0 y=48 placed=[01] docked=1"
expect_more "$docked" "${dockeds:-0}"
check "$out/docked-back.png" >/dev/null

# 6. No ERROR.
if guest 'grep -c ERROR /tmp/zdesktop.log' | tail -1 | grep -qx 0; then echo "no-error: ok"; else echo "no-error: FAILED"; status=1; fi
guest "grep -E 'fullscreen-leave|unfullscreen|fullscreen=1' /tmp/zdesktop.log; grep FULLSCREEN /tmp/t.log" | tail -12

echo "bug194: status $status"
exit $status
