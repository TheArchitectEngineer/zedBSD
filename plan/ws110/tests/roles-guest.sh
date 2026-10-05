#!/bin/sh
# ws110-p002: the compositor's roles on the Venus guest (the files image, plan/tools/files/build-files-image.sh, built
# after WS110: its /bin/wayland knows --testing).  Each run reads the compositor's READY line:
#  1. No role: role=normal with no deadline (timeout_ms=18446744073709551615).  The compositor is stopped with
#     SIGTERM after the check.
#  2. --session: the same (role=normal), the name sessiond and the Linux launchers use.
#  3. --testing: role=testing timeout_ms=150000; --testing --timeout=20: timeout_ms=20000, and it ends by itself
#     within 30 s.
#  4. Refused, each with exit 2 and its reason on stderr: --timeout=5 alone, --max-frames=3 alone,
#     --testing --session, --testing --control-fd=3.
# PASS: every "ok" line and the last line roles-guest: PASS.
#   plan/tools/files/files-guest.sh start BUILD/hdd-image.img
#   plan/ws110/tests/roles-guest.sh [OUTDIR]          (default build/ws110-roles)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
GUEST_RUNTIME="${GUEST_RUNTIME:-$(pwd)/build/ws071-run}"
export GUEST_RUNTIME
out=${1:-build/ws110-roles}
mkdir -p "$out"
guest() { timeout 120 python3 plan/tools/guest/guest.py run "$1" 2>&1 </dev/null; }
stop_all='service stop greeter >/dev/null 2>&1; for p in $(ps -A -o pid,args | grep -E "[w]ayland( |$)|[f]iles" | awk "{print \$1}"); do kill $p; done; i=0; while ps -A -o args | grep -qE "[w]ayland( |$)" && [ $i -lt 50 ]; do sleep 0.2; i=$((i+1)); done'
status=0
pass() { echo "$1: ok"; }
fail() { echo "$1: FAILED"; status=1; }

# start NAME ARGUMENTS: starts the compositor in the background and keeps its READY line in OUT/NAME.txt.
start() {
	guest "$stop_all" >/dev/null
	guest "export XDG_RUNTIME_DIR=/tmp; rm -f /tmp/wayland-0; /bin/wayland $2 --width=1280 --height=800 > /tmp/roles.log 2>&1 </dev/null &
for w in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do grep -q ZWL.READY /tmp/roles.log && break; sleep 0.5; done; grep 'ZWL READY' /tmp/roles.log" > "$out/$1.txt"
}

# 1. No role.
start none ""
grep -q 'role=normal' "$out/none.txt" && grep -q 'timeout_ms=18446744073709551615' "$out/none.txt" &&
	pass "no role: a desktop without a deadline" || fail "no role: a desktop without a deadline ($(cat "$out/none.txt"))"

# 2. --session.
start session "--session"
grep -q 'role=normal' "$out/session.txt" && grep -q 'timeout_ms=18446744073709551615' "$out/session.txt" &&
	pass "--session: the same" || fail "--session: the same ($(cat "$out/session.txt"))"

# 3. --testing, its default and a deadline of its own, which it keeps.
start testing "--testing"
grep -q 'role=testing' "$out/testing.txt" && grep -q 'timeout_ms=150000 ' "$out/testing.txt" &&
	pass "--testing: 150 s" || fail "--testing: 150 s ($(cat "$out/testing.txt"))"
start testing-20 "--testing --timeout=20"
grep -q 'role=testing' "$out/testing-20.txt" && grep -q 'timeout_ms=20000 ' "$out/testing-20.txt" &&
	pass "--testing --timeout=20" || fail "--testing --timeout=20 ($(cat "$out/testing-20.txt"))"
sleep 30
left=$(guest "ps -A -o args | grep -cE '^/bin/wayland '" | tail -1)
[ "${left:-1}" = 0 ] && pass "--testing --timeout=20 ended by itself" || fail "--testing --timeout=20 ended by itself (${left})"

# 4. Refusals.
guest "$stop_all" >/dev/null
for words in "--timeout=5" "--max-frames=3" "--testing --session" "--testing --control-fd=3"; do
	result=$(guest "/bin/wayland $words >/dev/null 2>/tmp/roles.err; echo exit=\$?; head -1 /tmp/roles.err" | tr '\n' ' ')
	case "$result" in
	*exit=2*wayland:*) pass "refuses $words ($result)" ;;
	*) fail "refuses $words ($result)" ;;
	esac
done

guest "$stop_all" >/dev/null
[ $status = 0 ] && echo "roles-guest: PASS" || echo "roles-guest: FAIL"
exit $status
