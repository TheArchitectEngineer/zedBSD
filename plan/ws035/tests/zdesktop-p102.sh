#!/bin/sh
# ws035-p102: the session's lock screen on the Venus guest of the graphical login image
# (plan/ws035/tests/build-login-image.sh BUILD graphical).  ws136-p002: since 2026-09-29 the image logs kei in at
# boot (/etc/keiland/autologin) and kei's password is "kei"; the run stops that session, empties the autologin file
# (restored at the end) and starts sessiond itself, so the greeter comes up with kei selected (the first person's
# account in passwd):
#  1. kei logs in; a terminal is started in the session.  Super+L locks (KWL LOCK locked reason=key): locked.png.
#     Keys typed while locked do not reach the terminal (unlocked.png: no "nope" in it).
#  2. A wrong password: sessiond says FAIL after its delay (SESSIOND UNLOCK fail): wrong.png.
#  3. The right password ("kei") unlocks (SESSIOND UNLOCK ok, KWL LOCK unlocked): unlocked.png.
#  4. App Home's Lock Screen locks (reason=home), the password unlocks.
#  5. Idle: the session script is given --lock-idle=20 for this run (restored after); without input the session
#     locks by itself (reason=idle): idle.png; the password unlocks.
#
#   plan/tools/files/files-guest.sh start build/amd64/hdd-graphical.img
#   plan/ws035/tests/zdesktop-p102.sh [OUTDIR]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
GUEST_RUNTIME="${GUEST_RUNTIME:-$(pwd)/build/ws071-run}"
export GUEST_RUNTIME
out=${1:-build/ws035-p102}
mkdir -p "$out"
guest() { timeout 120 python3 plan/tools/guest/guest.py run "$1" 2>&1 </dev/null; }
check() { python3 plan/ws035/tests/zdesktop-check.py "$@" --runtime "$GUEST_RUNTIME"; }
keys() { python3 plan/ws035/tests/qmp-keys.py "$GUEST_RUNTIME/qmp.sock" "$@"; sleep 0.8; }
pointer() { python3 plan/ws035/tests/qmp-pointer.py --width 1280 --height 800 "$GUEST_RUNTIME/qmp.sock" "$@"; }
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

# The boot's session (kei's autologin) stopped, the autologin emptied and clean logs.
stop_all='service stop greeter >/dev/null 2>&1; for p in $(ps -A -o pid,args | grep -E "[s]essiond|[w]ayland( |$)" | awk "{print \$1}"); do kill $p; done; sleep 2'
guest "$stop_all
[ -f /tmp/p102-autologin.saved ] || cp /etc/keiland/autologin /tmp/p102-autologin.saved; : > /etc/keiland/autologin
rm -f /var/log/sessiond.log /var/log/greeter.log /run/user/1000/session.log" >/dev/null

# The session script with --lock-idle=20 for this run.
guest "cp /etc/keiland/session /tmp/session.saved && sed 's/--session /--session --lock-idle=20 /' /tmp/session.saved > /etc/keiland/session; grep -c lock-idle=20 /etc/keiland/session" | tail -1 | sed 's/^/session script lock-idle lines: /'

# 1. Login, a terminal, and Super+L.
guest "/sbin/sessiond --graphical </dev/null >/dev/null 2>&1 & sleep 1; echo started" >/dev/null
expect_log /var/log/greeter.log 'KWL GREETER open .*selected=kei' 60
sleep 2
keys 'kei' '\n'
expect_log /var/log/sessiond.log 'AUTH ok user=kei uid=1000' 10
expect_log /run/user/1000/session.log 'KWL HANDOFF go=1' 20
guest 'export XDG_RUNTIME_DIR=/run/user/1000 WAYLAND_DISPLAY=wayland-0; /bin/terminal --token=t1 --timeout-s=300 > /tmp/t1.log 2>&1 </dev/null & sleep 5; echo started' >/dev/null
keys 'echo before-lock' '\n'
keys '<super-l>'
expect_log /run/user/1000/session.log 'KWL LOCK locked reason=key user=kei' 5
pointer move 1270 790 sleep 300
check "$out/locked.png" >/dev/null

# 2. A wrong password (typed while locked: the terminal hears nothing).
keys 'nope' '\n'
expect_log /var/log/sessiond.log 'SESSIOND UNLOCK fail user=kei wrong=1 delay=2' 6
sleep 3
check "$out/wrong.png" >/dev/null

# 3. The right one.
keys 'kei' '\n'
expect_log /var/log/sessiond.log 'SESSIOND UNLOCK ok user=kei' 6
expect_log /run/user/1000/session.log 'KWL LOCK unlocked' 6
sleep 1
check "$out/unlocked.png" >/dev/null

# 4. App Home's Lock Screen.
pointer move 23 17 sleep 300 down sleep 60 up sleep 1500
set -- $(guest "grep 'KWL HOME icon name=\"Lock Screen\"' /run/user/1000/session.log | tail -1" | sed -n 's/.* x=\([0-9]*\) y=\([0-9]*\).*/\1 \2/p')
if [ -n "${1:-}" ]; then
	pointer move "$1" "$2" sleep 400 down sleep 60 up sleep 800
	expect_log /run/user/1000/session.log 'KWL LOCK locked reason=home' 5
	keys 'kei' '\n'
	expect_log /var/log/sessiond.log 'SESSIOND UNLOCK ok user=kei' 6
else
	echo "no Lock Screen icon"
	status=1
fi

# 5. Idle: nothing for more than twenty seconds.
sleep 24
expect_log /run/user/1000/session.log 'KWL LOCK locked reason=idle' 5
check "$out/idle.png" >/dev/null
keys 'kei' '\n'
sleep 2
unlocks=$(guest "grep -c 'KWL LOCK unlocked' /run/user/1000/session.log" | tail -1)
[ "${unlocks:-0}" -ge 3 ] && echo "unlocks: $unlocks ok" || { echo "unlocks: $unlocks FAIL"; status=1; }

# The session script and the autologin as they were (kei's session keeps running, as the boot's did).
guest 'cp /tmp/session.saved /etc/keiland/session' >/dev/null
guest "[ -f /tmp/p102-autologin.saved ] && cat /tmp/p102-autologin.saved > /etc/keiland/autologin && rm -f /tmp/p102-autologin.saved" >/dev/null
guest "cat /var/log/sessiond.log" > "$out/sessiond.log"
guest "cat /run/user/1000/session.log" > "$out/session.log"
echo "zdesktop-p102: status=$status"
exit $status
