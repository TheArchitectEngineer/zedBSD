#!/bin/sh
# ws035-p101: the display's hand-over between the greeter and the session, on the Venus guest of the graphical
# login image (plan/ws035/tests/build-login-image.sh BUILD graphical), photographed several times a second
# (frames.py; each picture is black, text (the text console) or picture):
#  1. Login: root logs in with Enter; the greeter stays ("Starting session...") until the session's zdesktop says
#     READY, sessiond ends the greeter and answers GO (SESSIOND HANDOFF session ready=1, ZWL HANDOFF go=1).
#     No picture is the text console (before p101 the console's text showed for about 3 seconds).
#  2. Log Out: the session ends and the greeter comes back; no picture is the text console.
# The pictures are kept in OUTDIR/login and OUTDIR/logout, the lists in OUTDIR/login.txt and OUTDIR/logout.txt.
#
#   plan/tools/files/files-guest.sh start build/amd64/hdd-graphical.img
#   plan/ws035/tests/zdesktop-p101.sh [OUTDIR]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
GUEST_RUNTIME="${GUEST_RUNTIME:-$(pwd)/build/ws071-run}"
export GUEST_RUNTIME
out=${1:-build/ws035-p101}
mkdir -p "$out"
guest() { timeout 120 python3 plan/tools/guest/guest.py run "$1" 2>&1 </dev/null; }
keys() { python3 plan/ws035/tests/qmp-keys.py "$GUEST_RUNTIME/qmp.sock" "$@"; }
pointer() { python3 plan/ws035/tests/qmp-pointer.py --width 1280 --height 800 "$GUEST_RUNTIME/qmp.sock" "$@"; }
frames() { python3 plan/ws035/tests/frames.py "$out/$1" --runtime "$GUEST_RUNTIME" --seconds "$2" > "$out/$1.txt" 2>&1; }
status=0

# Fails the run unless a guest file has a line matching a pattern (within some seconds).
expect_log() {
	tries=0
	found=0
	while [ $tries -lt "${3:-10}" ]; do
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

# Fails the run when any picture of a list was the text console.
no_text() {
	count=$(grep -c ' text$' "$out/$1.txt")
	pictures=$(grep -c 'frame-' "$out/$1.txt")
	if [ "${count:-0}" -eq 0 ] && [ "${pictures:-0}" -gt 0 ]; then
		echo "frames: $1: $pictures pictures, no text console ok"
	else
		echo "frames: $1: $count of $pictures pictures are the text console FAIL"
		status=1
	fi
	grep -c ' black$' "$out/$1.txt" | sed "s/^/frames: $1: black pictures: /"
}

# 1. The greeter, then the login photographed.
expect_log /var/log/greeter.log 'ZWL GREETER open' 60
expect_log /var/log/greeter.log 'ZWL HANDOFF go=1' 30
sleep 3
frames login 16 &
sleep 2
keys '\n'
wait
expect_log /var/log/sessiond.log 'SESSIOND GREETER stays pid=' 5
expect_log /var/log/sessiond.log 'SESSIOND HANDOFF session ready=1' 5
expect_log /var/log/sessiond.log 'SESSIOND HANDOFF greeter released=1' 5
expect_log /var/log/sessiond.log 'SESSIOND HANDOFF go written=3' 5
expect_log /run/user/0/session.log 'ZWL HANDOFF go=1' 5
expect_log /var/log/greeter.log 'ZWL GREETER starting' 5
no_text login

# 2. Log Out photographed.
pointer move 23 17 sleep 300 down sleep 60 up sleep 1500
set -- $(guest "grep 'ZWL HOME icon name=\"Log Out\"' /run/user/0/session.log | tail -1" | sed -n 's/.* x=\([0-9]*\) y=\([0-9]*\).*/\1 \2/p')
if [ -n "${1:-}" ]; then
	frames logout 16 &
	sleep 1
	pointer move "$1" "$2" sleep 400 down sleep 60 up
	wait
else
	echo "no Log Out icon"
	status=1
fi
expect_log /var/log/sessiond.log 'SESSION end user=root' 10
expect_log /var/log/sessiond.log 'SESSIOND HANDOFF greeter ready: waits' 5
expect_log /var/log/sessiond.log 'SESSIOND HANDOFF session released=1' 5
expect_log /var/log/sessiond.log 'SESSIOND HANDOFF greeter go written=3' 5
expect_log /var/log/sessiond.log 'SESSIOND GREETER adopt pid=' 5
no_text logout

# The greeter after the Log Out takes a login again (the adopted greeter answers AUTH).
sleep 2
keys '\n'
expect_log /var/log/sessiond.log 'AUTH ok user=root' 10
expect_log /var/log/sessiond.log 'HANDOFF session ready=1' 20
sleep 4
python3 plan/ws035/tests/zdesktop-check.py "$out/again.png" --runtime "$GUEST_RUNTIME" >/dev/null

guest "cat /var/log/sessiond.log" > "$out/sessiond.log"
echo "zdesktop-p101: status=$status"
exit $status
