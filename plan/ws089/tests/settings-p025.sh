#!/bin/sh
# ws089-p025: the Sharing page's Remote Login on the Venus guest of the graphical login image
# (plan/ws035/tests/build-login-image.sh BUILD graphical), in the session sessiond starts for kei at boot (kei is a
# member of wheel).  The guest harness itself speaks SSH, so Remote Login is turned off and on again by clicks
# through QMP, and what happened in between is read from the logs once sshd answers again.
#  1. Settings (as kei, in kei's session) on the Sharing page: the state comes from sessiond (SHARING state
#     available=1 enabled=1 running=1 port=22 allowed=1 fingerprint=SHA256:...) (sharing-on.png).
#  2. The switch (control 1) turns Remote Login off: SHARING result errno=0, the state enabled=0 running=0
#     (sharing-off.png, taken through QMP while SSH is down).
#  3. The switch again turns it on: SHARING result errno=0, enabled=1 running=1; SSH answers again; rc.conf
#     says sshd is enabled; the authentication log says "service sshd off by kei" and "service sshd on by kei".
# The refusal of a user outside wheel and of other services is sessiond's rule, tested on the host
# (run-host-service-rules.sh).
# The image is the graphical login image with su (plan/ws089/tests/config-amd64-sharing.mk; T1-159: the plain
# build-login-image.sh image has no su, so Settings never started and /tmp/s.log was never written).
# PASS: every "ok" line and the last line settings-p025: PASS.
#   SETTINGS_CONFIG=plan/ws089/tests/config-amd64-sharing.mk plan/ws089/tests/build-settings-image.sh BUILD
#   plan/tools/files/files-guest.sh start BUILD/hdd-image.img
#   plan/ws089/tests/settings-p025.sh [OUTDIR]            (default build/ws089-shots/p025)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
GUEST_RUNTIME="${GUEST_RUNTIME:-$(pwd)/build/ws071-run}"
export GUEST_RUNTIME
out=${1:-build/ws089-shots/p025}
mkdir -p "$out"
guest() { timeout 120 python3 plan/tools/guest/guest.py run "$1" 2>&1 </dev/null; }
check() { python3 plan/ws035/tests/zdesktop-check.py "$@" --runtime "$GUEST_RUNTIME"; }
pointer() { python3 plan/ws035/tests/qmp-pointer.py --width 1280 --height 800 "$GUEST_RUNTIME/qmp.sock" "$@"; }
session=/run/user/1000/session.log
status=0
. plan/ws089/tests/settings-wait.sh
pass() { echo "$1: ok"; }
fail() { echo "$1: FAILED"; status=1; }
expect_log() {
	tries=0
	found=0
	while [ $tries -lt "${3:-10}" ]; do
		found=$(guest "grep -caE '$2' $1" | tail -1)
		[ "${found:-0}" -gt 0 ] 2>/dev/null && break
		tries=$((tries + 1))
		sleep 1
	done
	if [ "${found:-0}" -gt 0 ] 2>/dev/null; then echo "log: $2 ok"; else echo "log: $2 MISSING"; status=1; fi
}
shot() {
	pointer move 1270 790 sleep 300
	check "$out/$1" >/dev/null
	echo "shot: $out/$1"
}

# kei's session, and Remote Login on to begin with (the harness needs it).
wait_guest
expect_log "$session" 'KWL HANDOFF go=1' 60
guest 'service enable sshd >/dev/null 2>&1; service start sshd >/dev/null 2>&1; echo on' >/dev/null

# 1. Settings as kei on the Sharing page; its window from the session's log.  Without su it cannot start.
has_su=$(guest 'command -v su >/dev/null 2>&1 && echo yes || echo no' | tail -1)
if [ "$has_su" != yes ]; then
	fail "the image has su (build it with config-amd64-sharing.mk)"
	echo "settings-p025: FAIL"
	exit 1
fi
guest "su kei -c 'env XDG_RUNTIME_DIR=/run/user/1000 WAYLAND_DISPLAY=wayland-0 HOME=/home/kei /bin/settings --timeout-s=600 sharing > /tmp/s.log 2>&1 </dev/null &'; sleep 6; echo started" >/dev/null
line=$(guest "grep 'KWL MAP client=' $session | tail -1")
set -- $(echo "$line" | sed -n 's/.* surface=\([0-9]*\) x=\([-0-9]*\) y=\([-0-9]*\).*/\1 \2 \3/p') 0 0 0
wx=$2; wy=$3
echo "settings: window at $wx,$wy"
started=$(guest 'test -f /tmp/s.log && echo yes || echo no' | tail -1)
[ "$started" = yes ] && pass "settings started as kei (/tmp/s.log)" || fail "settings started as kei (/tmp/s.log)"
expect_log /tmp/s.log 'SHARING state available=1 enabled=1 running=1 port=22 allowed=1 fingerprint=SHA256:' 15
shot sharing-on.png
set -- $(guest "grep -a 'ZSETTINGS CONTROL index=1 ' /tmp/s.log | tail -1" | sed -n 's/.* x=\([-0-9]*\) y=\([-0-9]*\) width=\([0-9]*\) height=\([0-9]*\).*/\1 \2 \3 \4/p') 0 0 0 0
cx=$((wx + $1 + $3 / 2)); cy=$((wy + $2 + $4 / 2))
echo "switch at $cx,$cy"

# 2. Off (SSH goes; the picture comes through QMP).
pointer move $((cx - 2)) "$cy" sleep 150 move "$cx" "$cy" sleep 300 down sleep 60 up sleep 6000
shot sharing-off.png

# 3. On again; SSH answers again, and the logs tell both.
pointer move $((cx - 2)) "$cy" sleep 150 move "$cx" "$cy" sleep 300 down sleep 60 up sleep 6000
wait_guest
expect_log /tmp/s.log 'SHARING state available=1 enabled=0 running=0 ' 5
expect_log /tmp/s.log 'SHARING state available=1 enabled=1 running=1 ' 5
results=$(guest "grep -ac 'SHARING result errno=0' /tmp/s.log" | tail -1)
[ "${results:-0}" -ge 2 ] 2>/dev/null && pass "both switches answered" || fail "both switches answered (${results:-0})"
expect_log /var/log/messages 'service sshd off by kei: errno=0' 5
expect_log /var/log/messages 'service sshd on by kei: errno=0' 5
guest "awk '/^  sshd:/{s=1} s && /enabled:/{print; exit}' /etc/rc.conf" > "$out/rcconf.txt"
grep -q 'true' "$out/rcconf.txt" && pass "rc.conf enables sshd" || fail "rc.conf enables sshd"
expect_log "$session" 'KWL SYSTEM sharing client=[0-9]+ action=2 error=0' 3
expect_log "$session" 'KWL SYSTEM sharing client=[0-9]+ action=1 error=0' 3

guest 'grep -a "ZSETTINGS SHARING" /tmp/s.log' > "$out/settings.log"
guest 'grep -a "service sshd" /var/log/messages | tail -5' > "$out/messages.txt"
[ $status = 0 ] && echo "settings-p025: PASS" || echo "settings-p025: FAIL"
exit $status
