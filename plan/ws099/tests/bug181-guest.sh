#!/bin/sh
# BUG-181: the selection of a long text in a title bar field stays within the field.  On the Venus guest of
# plan/tools/titlebar/config-amd64-menu.mk built from the commit under test (plan/tools/files/files-guest.sh start
# IMAGE), zdesktop --glass at 1280x800 shows /bin/titlebar-probe --show --mode=controls --width=1100 (a file manager's
# controls: the search field is control 4; narrower, the search goes into the title bar's "...").
#  1. A click on the search field gives it the keyboard ("TITLEBAR focus ... id=4"); a text far longer than the field
#     is typed, and Ctrl+A selects all of it; select-all.png: the blue tint ends at the field's right end (before,
#     it ran past the field and out of the title bar).  The eye judges the picture; the colour right of the field is
#     printed for the record.
#  2. A drag from the field's left end to past its right end selects by the pointer ("TITLEBAR select ... anchor=0");
#     drag-select.png, the same.
#  3. No ERROR in zdesktop's log.
# PASS: the last line "bug181: status 0" and the two pictures as above.
#   plan/ws099/tests/bug181-guest.sh [OUTDIR]          (default build/ws099-bug181)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
GUEST_RUNTIME="${GUEST_RUNTIME:-$(pwd)/build/ws071-run}"
export GUEST_RUNTIME
out=${1:-build/ws099-bug181}
mkdir -p "$out"
guest() { timeout 90 python3 plan/tools/guest/guest.py run "$1" 2>&1; }
check() { python3 plan/ws035/tests/zdesktop-check.py "$@" --runtime "$GUEST_RUNTIME"; }
pointer() { python3 plan/ws035/tests/qmp-pointer.py "$GUEST_RUNTIME/qmp.sock" "$@"; }
keys() { python3 plan/ws035/tests/qmp-keys.py "$GUEST_RUNTIME/qmp.sock" "$@"; sleep 1; }
stop_all='service stop greeter >/dev/null 2>&1; for p in $(ps -A -o pid,args | grep -E "[w]ayland( |$)|[t]itlebar-probe" | awk "{print \$1}"); do kill $p; done; i=0; while ps -A -o args | grep -qE "[w]ayland( |$)|[t]itlebar-probe" && [ $i -lt 50 ]; do sleep 0.2; i=$((i+1)); done'
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

guest "$stop_all" >/dev/null
guest 'export XDG_RUNTIME_DIR=/tmp; rm -f /tmp/wayland-0
/bin/wayland --testing --timeout=600 --width=1280 --height=800 --glass > /tmp/zdesktop.log 2>&1 </dev/null & for w in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do grep -q ZWL.READY /tmp/zdesktop.log 2>/dev/null && break; sleep 0.5; done; sleep 1
export WAYLAND_DISPLAY=wayland-0; /bin/titlebar-probe --show=Probe --mode=controls --width=1100 --seconds=300 > /tmp/probe.log 2>&1 </dev/null & sleep 5; echo started' >/dev/null

# The search field's place (the latest layout of control 4, floating and shown).
set -- $(guest "grep -E 'ZWL TITLEBAR control client=[0-9]+ surface=[0-9]+ where=floating id=4 .* shown=1' /tmp/zdesktop.log | tail -1" | sed -n 's/.* x=\([-0-9]*\) y=\([-0-9]*\) width=\([0-9]*\) height=\([0-9]*\).*/\1 \2 \3 \4/p')
fx=${1:-0}; fy=${2:-0}; fw=${3:-0}; fh=${4:-0}
echo "search field: x=$fx y=$fy width=$fw height=$fh"
[ "$fw" -gt 0 ] || { echo "field: MISSING"; status=1; }

# 1. Focus, a long text, Ctrl+A.
pointer move $((fx + fw / 2)) $((fy + fh / 2)) sleep 300 down sleep 60 up sleep 600 >/dev/null
expect_log "TITLEBAR focus client=[0-9]+ surface=[0-9]+ id=4 "
keys "file:///usr/share/doc/a-very-long-folder-name/another-long-folder-name/and-one-more-folder/index.html"
keys "<ctrl-a>"
pointer move 1270 790 sleep 500 >/dev/null
check "$out/select-all.png" >/dev/null
echo "select-all.png: the tint should end at x=$((fx + fw)) (the field's right end)"

# 2. A drag selection from the left end to well past the right end.
pointer move $((fx + 40)) $((fy + fh / 2)) sleep 300 down sleep 100 move $((fx + fw / 2)) $((fy + fh / 2)) sleep 100 \
    move $((fx + fw + 200)) $((fy + fh / 2)) sleep 300 up sleep 600 >/dev/null
expect_log "TITLEBAR select client=[0-9]+ surface=[0-9]+ id=4 anchor="
check "$out/drag-select.png" >/dev/null

# 3. No ERROR.
if guest 'grep -c ERROR /tmp/zdesktop.log' | tail -1 | grep -qx 0; then echo "no-error: ok"; else echo "no-error: FAILED"; status=1; fi

echo "bug181: status $status"
exit $status
