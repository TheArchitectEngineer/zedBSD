#!/bin/sh
# ws102-p018: the lock screen empties the clipboard's history, on the Venus guest of the criteria image (the graphical
# login, plan/ws099/tests/build-criteria-image.sh; kei is logged in at boot, kei's password is kei).  data-probe sets its text
# as the selection (KWL CLIP add, Super+Alt+H: count=1); Super+L locks (KWL CLIP clear reason=lock count=1); Enter
# and kei's password unlock; the history is empty (count=0).  lock.png.  A selection again, then App Home's Log Out:
# KWL CLIP clear reason=logout count=1.
# Prints "clip-lock: PASS" or "clip-lock: FAIL".
#
#   plan/ws102/tests/clip-lock.sh IMAGE OUTDIR
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
image=$1
out=$2
mkdir -p "$out"
GUEST_RUNTIME="${GUEST_RUNTIME:-$(pwd)/build/ws102-p018-run}"
export GUEST_RUNTIME
status=0
log=/run/user/1000/session.log
guest() { timeout 120 python3 plan/tools/guest/guest.py run "$1" 2>&1 </dev/null; }
keys() { python3 plan/ws035/tests/qmp-keys.py "$GUEST_RUNTIME/qmp.sock" "$@" >/dev/null; sleep 0.8; }

# Waits (up to N s) for a guest file to have a line matching a pattern.
expect_log() {
	tries=0
	while [ $tries -lt "$3" ]; do
		guest "grep -qE '$2' $1 && echo found" | grep -q '^found$' && { echo "log: $2 ok"; return 0; }
		tries=$((tries + 1))
		sleep 1
	done
	echo "log: $2 MISSING"
	status=1
	return 1
}

# The guest and kei's session, logged in at boot.
sh plan/ws035/tests/zdesktop-guest.sh stop >/dev/null 2>&1
timeout 180 sh plan/ws035/tests/zdesktop-guest.sh start "$image" >/dev/null 2>&1
tries=0
until guest 'echo up' | grep -q '^up$' || [ $tries -ge 30 ]; do tries=$((tries + 1)); sleep 5; done
expect_log $log 'KWL HANDOFF go=1' 90
sleep 3

# A selection in the history.
guest 'export XDG_RUNTIME_DIR=/run/user/1000 WAYLAND_DISPLAY=wayland-0; /bin/data-probe --token=l --text="before lock" --timeout-s=300 > /tmp/pl.log 2>&1 </dev/null & sleep 5; echo started' >/dev/null
guest 'grep -E "DATAPROBE (ready|FAILED)" /tmp/pl.log' 
keys 's'
expect_log $log 'KWL CLIP add length=11 ' 10
keys '<super-alt-h>'
expect_log $log 'KWL CLIP history count=1 ' 5

# The lock empties it; Enter unlocks; the history is empty.
keys '<super-l>'
expect_log $log 'KWL LOCK locked reason=key' 5
expect_log $log 'KWL CLIP clear reason=lock count=1' 5
python3 plan/ws035/tests/zdesktop-check.py "$out/lock.png" --runtime "$GUEST_RUNTIME" >/dev/null
keys 'kei\n'
expect_log $log 'KWL LOCK unlocked' 10
keys '<super-alt-h>'
expect_log $log 'KWL CLIP history count=0$' 5
# Log Out empties it too: a selection again, then App Home's Log Out.
keys 's'
expect_log $log 'KWL CLIP history count=0$' 1 >/dev/null
tries=0
until [ "$(guest "grep -c 'KWL CLIP add' $log" | tail -1)" -ge 2 ] 2>/dev/null || [ $tries -ge 10 ]; do tries=$((tries + 1)); sleep 1; done
python3 plan/ws035/tests/qmp-pointer.py "$GUEST_RUNTIME/qmp.sock" move 22 16 sleep 200 down sleep 60 up sleep 2000 >/dev/null
set -- $(guest "grep 'KWL HOME icon name=\"Log Out\"' $log | tail -1" | sed -n 's/.* x=\([0-9]*\) y=\([0-9]*\).*/\1 \2/p')
python3 plan/ws035/tests/qmp-pointer.py "$GUEST_RUNTIME/qmp.sock" move "${1:-0}" "${2:-0}" sleep 200 down sleep 60 up sleep 3000 >/dev/null
expect_log $log 'KWL CLIP clear reason=logout count=1' 10
guest "grep -E 'KWL (CLIP|LOCK|HANDOFF)' $log" > "$out/session.log"
sh plan/ws035/tests/zdesktop-guest.sh stop >/dev/null 2>&1
[ $status -eq 0 ] && echo "clip-lock: PASS" || echo "clip-lock: FAIL"
exit $status
