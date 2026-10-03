#!/bin/sh
# BUG-106: runs `ssh -tt ... 'cd /bin && ls'` N times against the running WS073 guest (GUEST_RUNTIME) and compares
# each output with a reference taken with '; true' after ls.  Records every run's exit status and byte count, and after
# every run that differs (and every 50 runs) the guest's 'killed by signal' lines.  With LOAD=1 the register-check
# load (/tmp/regcheck, tests/regcheck.c) runs in the guest meanwhile.  Prints BUG106:PASS when every run matched.
#   sh plan/ws073/tests/bug106-ls.sh [N] [OUT]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
cd "$(dirname -- "$0")/../../.."
n=${1:-300}
out=${2:-build/ws073-bug106}
mkdir -p "$out"
runtime=${GUEST_RUNTIME:-$PWD/build/ws073-run}
port=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["ssh_port"])' "$runtime/session.json")
S="ssh -i plan/tmp/guest/id_ed25519 -p $port -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR -o BatchMode=yes -o ConnectTimeout=20"
timeout 60 $S -tt root@127.0.0.1 'cd /bin && ls; true' </dev/null > "$out/reference.txt" 2>&1
[ "${LOAD:-0}" = 1 ] && timeout 30 $S root@127.0.0.1 "/tmp/regcheck 100000 6 > /tmp/regcheck.log 2>&1 &" </dev/null
bad=0
i=1
: > "$out/runs.txt"
while [ "$i" -le "$n" ]; do
	timeout 60 $S -tt root@127.0.0.1 'cd /bin && ls' </dev/null > "$out/run.txt" 2>&1
	rc=$?
	bytes=$(wc -c < "$out/run.txt")
	if cmp -s "$out/run.txt" "$out/reference.txt"; then
		same=same
	else
		same=DIFF
		bad=$((bad + 1))
		cp "$out/run.txt" "$out/diff-$i.txt"
	fi
	killed=
	if [ "$same" = DIFF ] || [ $((i % 50)) = 0 ]; then
		killed=$(timeout 30 $S root@127.0.0.1 'dmesg | grep -c "killed by signal"' </dev/null 2>&1)
	fi
	echo "$i rc=$rc bytes=$bytes $same ${killed:+killed=$killed}" >> "$out/runs.txt"
	[ "$same" = DIFF ] && echo "run $i: rc=$rc bytes=$bytes (reference $(wc -c < "$out/reference.txt")) killed=$killed"
	i=$((i + 1))
done
[ "${LOAD:-0}" = 1 ] && timeout 30 $S root@127.0.0.1 'ps -A -o pid,comm | awk "\$2 == \"/tmp/regcheck\" {print \$1}" | while read p; do kill $p; done; true' </dev/null
timeout 30 $S root@127.0.0.1 'dmesg | grep "killed by signal"; true' </dev/null > "$out/dmesg-killed.txt"
echo "runs $n, differing $bad, reference $(wc -c < "$out/reference.txt") bytes, guest faults $(grep -c . "$out/dmesg-killed.txt")"
[ "$bad" = 0 ] && echo "BUG106:PASS" || echo "BUG106:FAIL"
