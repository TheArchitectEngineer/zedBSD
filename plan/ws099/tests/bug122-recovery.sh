#!/bin/sh
# ws099-p010 (BUG-122): the graphical login comes back after the compositor dies, on the Venus guest of the criteria
# image (build-criteria-image.sh), kei's session at boot.
#  1. The session's compositor is killed (SIGKILL): sessiond starts the greeter, which becomes ready (greeter.png).
#  2. The greeter's compositor is killed five times in a row, each soon after it is ready (a compositor that keeps
#     dying, as on the 5330 after BUG-117): sessiond tries again after growing waits (SESSIOND GREETER retry) and
#     logs why each one failed (SESSIOND GREETER failed reason=), and never gives the console back (SESSIOND CONSOLE);
#     the greeter is ready at the end (greeter-after.png).  The sessiond before ws099-p010 gave up after three.
#  3. kei logs in on that greeter: the session starts (SESSIOND HANDOFF go) (session.png).
#   plan/ws099/tests/bug122-recovery.sh IMAGE [OUTDIR]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
image=${1:?usage: bug122-recovery.sh IMAGE [OUTDIR]}
out=${2:-build/ws099-shots/bug122}
mkdir -p "$out"
GUEST_RUNTIME=${GUEST_RUNTIME:-$(pwd)/build/ws099/run}
export GUEST_RUNTIME
log=/var/log/sessiond.log
guest() { timeout 90 python3 plan/tools/guest/guest.py run "$1" 2>&1 </dev/null; }
shot() { python3 plan/ws035/tests/zdesktop-shot.py "$out/$1" --runtime "$GUEST_RUNTIME" >/dev/null 2>&1; echo "shot: $out/$1"; }
status=0

# Records a verdict.
verdict() {
	if [ "$1" = ok ]; then
		echo "$2 ok"
	else
		echo "$2 FAIL"
		status=1
	fi
}

# The number of lines of sessiond's log matching a pattern.
count() {
	guest "grep -cE '$1' $log" | tail -1
}

# Waits until sessiond's log has more than N lines matching a pattern (within some seconds); 0 when it has.
expect_more() {
	tries=0
	found=0
	while [ $tries -lt "$3" ]; do
		found=$(count "$1")
		[ "${found:-0}" -gt "$2" ] 2>/dev/null && return 0
		tries=$((tries + 1))
		sleep 1
	done
	echo "log: $1 missing (${found:-0}, wanted more than $2)"
	return 1
}

# Kills the greeter's compositor (the _greeter account's /bin/wayland).
kill_greeter() {
	guest 'p=$(ps -A -o pid,user,args | awk "\$2==\"_greeter\" && /wayland/ {print \$1}"); [ -n "$p" ] && kill -9 $p; echo "killed $p"' | tail -1
}

# 0. A fresh guest, and kei's session.
sh plan/ws035/tests/zdesktop-guest.sh stop >/dev/null 2>&1
timeout 180 sh plan/ws035/tests/zdesktop-guest.sh start "$image" >/dev/null 2>&1
sleep 20
expect_more 'SESSIOND HANDOFF go' 0 60 || status=1

# 1. The session's compositor dies: the greeter comes.
ready=$(count 'HANDOFF greeter ready')
guest 'p=$(ps -A -o pid,user,args | awk "\$2==\"kei\" && /wayland/ {print \$1}"); kill -9 $p; echo "killed session compositor $p"' | tail -1
expect_more 'HANDOFF greeter ready' "$ready" 30 && verdict ok "session compositor killed: the greeter is ready" || verdict no "session compositor killed: the greeter is ready"
sleep 3
shot greeter.png

# 2. The greeter's compositor dies five times in a row, each soon after it is ready.
for round in 1 2 3 4 5; do
	ready=$(count 'HANDOFF greeter ready')
	kill_greeter
	expect_more 'HANDOFF greeter ready' "$ready" 45 && verdict ok "greeter killed ($round): a new greeter is ready" || verdict no "greeter killed ($round): a new greeter is ready"
done
consoles=$(count 'SESSIOND CONSOLE')
[ "${consoles:-1}" = 0 ] && verdict ok "the console was not given back" || verdict no "the console was given back"
retries=$(count 'SESSIOND GREETER retry')
[ "${retries:-0}" -ge 4 ] && verdict ok "the greeter was tried again after waits ($retries)" || verdict no "the greeter was tried again after waits (${retries:-0})"
reasons=$(count 'SESSIOND GREETER failed reason=')
[ "${reasons:-0}" -ge 4 ] && verdict ok "the reason of each failure is logged ($reasons)" || verdict no "the reason of each failure is logged (${reasons:-0})"
sleep 3
shot greeter-after.png

# 3. kei logs in on it.
gone=$(count 'SESSIOND HANDOFF go')
sleep 2
python3 plan/ws035/tests/qmp-keys.py "$GUEST_RUNTIME/qmp.sock" 'kei\n'
expect_more 'SESSIOND HANDOFF go' "$gone" 30 && verdict ok "kei logged in again" || verdict no "kei logged in again"
sleep 5
shot session.png

# The log, and the guest stopped.
guest "cat $log" > "$out/sessiond.log"
sh plan/ws035/tests/zdesktop-guest.sh stop >/dev/null 2>&1
[ $status -eq 0 ] && echo "bug122-recovery: PASS" || echo "bug122-recovery: FAIL"
exit $status
