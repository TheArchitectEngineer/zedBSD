#!/bin/sh
# BUG-107: N parallel loops of short SSH sessions (each holds three or four sockets in the guest) against the running
# WS073 guest of GUEST_RUNTIME, then the guest's fault and ENFILE lines.  Prints SSH-PARALLEL:PASS when no session failed.
#   sh plan/ws073/tests/ssh-parallel.sh [N] [SESSIONS_PER_LOOP]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
cd "$(dirname -- "$0")/../../.."
n=${1:-10}
per=${2:-20}
runtime=${GUEST_RUNTIME:-$PWD/build/ws073-run}
port=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["ssh_port"])' "$runtime/session.json")
S="ssh -i plan/tmp/guest/id_ed25519 -p $port -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR -o BatchMode=yes -o ConnectTimeout=20 root@127.0.0.1"
tmp=$(mktemp -d)
w=1
while [ "$w" -le "$n" ]; do
	( f=0; i=0
	  while [ "$i" -lt "$per" ]; do
		timeout 120 $S 'sleep 1; uname -n' </dev/null >/dev/null 2>&1 || f=$((f+1))
		i=$((i+1))
	  done
	  echo "$f" > "$tmp/$w" ) &
	w=$((w+1))
done
wait
failed=$(cat "$tmp"/* | awk '{s += $1} END {print s}')
rm -rf "$tmp"
echo "parallel $n x $per sessions: failed $failed"
timeout 60 $S 'grep -cE "Too many open files|penalty" /var/log/messages; dmesg | grep -c "killed by signal"' </dev/null
[ "$failed" = 0 ] && echo "SSH-PARALLEL:PASS" || echo "SSH-PARALLEL:FAIL"
