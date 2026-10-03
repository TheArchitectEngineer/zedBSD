#!/bin/sh
# ws102-p023: Text Editor tells its real editing state (kui_window_edit_state), on the Venus guest of the inset image
# (plan/ws102/tests/build-inset-image.sh).  zdesktop --glass at 1280x800, an empty clipboard, Text Editor on "abc".
# Each step's state (ZWL EDIT state ... flags=) and, where noted, what the buttons would show (Super+Alt+Q:
# ZWL EDIT enabled=):
#  1. opened: flags=0x0 (no selection, nothing to paste, undo or redo); enabled=0x60 (select all, select begin).
#  2. select all (Super+Alt+A): flags=0x1.
#  3. copy (Super+Alt+C): flags=0x3 (and something to paste).
#  4. x typed over the selection: flags=0x6 (to paste, to undo); enabled=0x6c.
#  5. undo (Super+Alt+Z): something to redo (flags with 0x8).
# Prints "edit-state-guest: PASS" or "edit-state-guest: FAIL".
#
#   plan/ws102/tests/edit-state-guest.sh IMAGE OUTDIR
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
image=$1
out=$2
mkdir -p "$out"
GUEST_RUNTIME="${GUEST_RUNTIME:-$(pwd)/build/ws102-p023-run}"
export GUEST_RUNTIME
status=0
guest() { timeout 120 python3 plan/tools/guest/guest.py run "$1" 2>&1 </dev/null; }
pointer() { python3 plan/ws035/tests/qmp-pointer.py "$GUEST_RUNTIME/qmp.sock" "$@" >/dev/null; }
keys() { python3 plan/ws035/tests/qmp-keys.py "$GUEST_RUNTIME/qmp.sock" "$@" >/dev/null; sleep 0.8; }
shot() { pointer move 1275 400 sleep 300; python3 plan/ws035/tests/zdesktop-check.py "$out/$1" --runtime "$GUEST_RUNTIME" >/dev/null; echo "shot $out/$1"; }

# The last state line's flags, compared with a pattern.
state_is() {
	tries=0
	while [ $tries -lt 8 ]; do
		got=$(guest "grep 'ZWL EDIT state' /tmp/zdesktop.log | tail -1" | sed -n 's/.* flags=\(0x[0-9a-f]*\).*/\1/p')
		echo "$got" | grep -qE "^$1\$" && { echo "state $2: $got ok"; return 0; }
		tries=$((tries + 1))
		sleep 1
	done
	echo "state $2: got $got, want $1 FAIL"
	status=1
}

# What the buttons would show (Super+Alt+Q).
enabled_is() {
	keys '<super-alt-q>'
	got=$(guest "grep 'ZWL EDIT enabled=' /tmp/zdesktop.log | tail -1" | sed -n 's/.*enabled=\(0x[0-9a-f]*\).*/\1/p')
	[ "$got" = "$1" ] && echo "enabled $2: $got ok" || { echo "enabled $2: got $got, want $1 FAIL"; status=1; }
}

# The guest, zdesktop and Text Editor.
sh plan/ws035/tests/zdesktop-guest.sh stop >/dev/null 2>&1
timeout 180 sh plan/ws035/tests/zdesktop-guest.sh start "$image" >/dev/null 2>&1
tries=0
until guest 'echo up' | grep -q '^up$' || [ $tries -ge 30 ]; do tries=$((tries + 1)); sleep 5; done
guest 'service stop greeter >/dev/null 2>&1; printf "abc" > /root/s.txt; export XDG_RUNTIME_DIR=/tmp HOME=/root; rm -f /tmp/wayland-0; picture=; [ -f /usr/share/keiland/wallpaper.ppm ] && picture=--wallpaper=/usr/share/keiland/wallpaper.ppm
/bin/wayland --timeout=900 --width=1280 --height=800 --glass $picture > /tmp/zdesktop.log 2>&1 </dev/null &
i=0; while ! grep -q "ZWL OSK zone" /tmp/zdesktop.log && [ $i -lt 60 ]; do sleep 0.5; i=$((i+1)); done; sleep 1
/bin/textedit --timeout-s=600 /root/s.txt > /tmp/te.log 2>&1 </dev/null & sleep 6; echo started' >/dev/null

state_is 0x0 opened
enabled_is 0x60 opened
keys '<super-alt-a>'
state_is 0x1 "select all"
shot selected.png
keys '<super-alt-c>'
state_is 0x3 copy
keys 'x'
state_is 0x6 typed
enabled_is 0x6c typed
shot typed.png
keys '<super-alt-z>'
state_is '0x[0-9a-f]*[89a-f]' undo
shot undone.png

guest 'grep -E "ZWL EDIT" /tmp/zdesktop.log' > "$out/zdesktop.log"
sh plan/ws035/tests/zdesktop-guest.sh stop >/dev/null 2>&1
[ $status -eq 0 ] && echo "edit-state-guest: PASS" || echo "edit-state-guest: FAIL"
exit $status
