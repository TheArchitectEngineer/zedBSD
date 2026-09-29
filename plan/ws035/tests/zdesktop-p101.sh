#!/bin/sh
# ws035-p101: the display's hand-over between the greeter and the session, on the Venus guest of the graphical
# login image (plan/ws035/tests/build-login-image.sh BUILD graphical), photographed several times a second
# (frames.py; each picture is black, text (the text console) or picture).  The image logs the user kei in by itself
# at boot (sessiond's autologin; kei's password is "kei", the demonstration's accounts), so the run starts in kei's
# session (ws035-p127: before, it started at the greeter and logged root in with Enter):
#  1. Log Out: the session says LOGOUT and keeps the screen, sessiond starts a greeter and, at its READY, tells the
#     session QUIT; the session gives the display back (SESSIOND HANDOFF session released=1) and the greeter gets GO
#     (SESSIOND HANDOFF greeter go written=3), then sessiond adopts the greeter.  No picture is the text console.
#  2. Login: kei's password and Enter at the adopted greeter; the greeter stays ("Starting session...") until the
#     session's zdesktop says READY, sessiond ends the greeter and answers GO (SESSIOND HANDOFF session ready=1,
#     ZWL HANDOFF go=1).  No picture is the text console (before p101 the console's text showed for about
#     3 seconds; ws035-p126 also took the black away, which plan/ws035/tests/zdesktop-p126.sh measures).
# The pictures are kept in OUTDIR/logout and OUTDIR/login, the lists in OUTDIR/logout.txt and OUTDIR/login.txt.
#
#   GUEST_RUNTIME=build/ws035-run plan/ws035/tests/zdesktop-guest.sh start build/amd64/hdd-image.img
#   GUEST_RUNTIME=build/ws035-run plan/ws035/tests/zdesktop-p101.sh [OUTDIR]
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

# Fails the run unless a guest file has at least COUNT lines matching a pattern (within some seconds).
expect_log() {
	tries=0
	found=0
	while [ $tries -lt "$4" ]; do
		found=$(guest "grep -cE '$2' $1" | tail -1)
		[ "${found:-0}" -ge "$3" ] 2>/dev/null && break
		tries=$((tries + 1))
		sleep 1
	done
	if [ "${found:-0}" -ge "$3" ] 2>/dev/null; then
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

# 0. kei's session, logged in at boot.
expect_log /var/log/sessiond.log 'SESSIOND HANDOFF go written=3' 1 90
expect_log /run/user/1000/session.log 'ZWL HANDOFF go=1' 1 10
sleep 4

# 1. Log Out photographed.
pointer move 23 17 sleep 300 down sleep 60 up sleep 1500
set -- $(guest "grep 'ZWL HOME icon name=\"Log Out\"' /run/user/1000/session.log | tail -1" | sed -n 's/.* x=\([0-9]*\) y=\([0-9]*\).*/\1 \2/p')
if [ -n "${1:-}" ]; then
	frames logout 16 &
	sleep 1
	pointer move "$1" "$2" sleep 400 down sleep 60 up
	wait
else
	echo "no Log Out icon"
	status=1
fi
expect_log /var/log/sessiond.log 'SESSION end user=kei' 1 10
expect_log /var/log/sessiond.log 'SESSIOND HANDOFF greeter ready: waits' 1 5
expect_log /var/log/sessiond.log 'SESSIOND HANDOFF session released=1' 1 5
expect_log /var/log/sessiond.log 'SESSIOND HANDOFF greeter go written=3' 1 5
expect_log /var/log/sessiond.log 'SESSIOND GREETER adopt pid=' 1 5
expect_log /var/log/greeter.log 'ZWL GREETER open' 1 5
no_text logout

# 2. The login at the adopted greeter photographed (it answers AUTH).
sleep 2
frames login 16 &
sleep 2
keys 'kei\n'
wait
expect_log /var/log/sessiond.log 'AUTH ok user=kei' 1 10
expect_log /var/log/sessiond.log 'SESSIOND GREETER stays pid=' 1 5
expect_log /var/log/sessiond.log 'SESSIOND HANDOFF session ready=1' 2 5
expect_log /var/log/sessiond.log 'SESSIOND HANDOFF greeter released=1' 1 5
expect_log /var/log/sessiond.log 'SESSIOND HANDOFF go written=3' 2 5
expect_log /run/user/1000/session.log 'ZWL HANDOFF go=1' 1 5
expect_log /var/log/greeter.log 'ZWL GREETER starting' 1 5
no_text login

# The desktop of the new session.
sleep 4
python3 plan/ws035/tests/zdesktop-check.py "$out/again.png" --runtime "$GUEST_RUNTIME" >/dev/null || status=1

guest "cat /var/log/sessiond.log" > "$out/sessiond.log"
echo "zdesktop-p101: status=$status"
exit $status
