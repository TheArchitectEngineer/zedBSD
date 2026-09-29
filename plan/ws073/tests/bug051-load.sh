#!/bin/sh
# BUG-051 reproduction (2026-09-29): with a running WS073 guest that has /tmp/regcheck (tests/regcheck.c), starts the
# register-check load on every CPU and runs two parallel loops of short SSH sessions (chacha20-poly1305, 256 KB
# read) for SECONDS.  Reports failed sessions and the kernel's fault-signal lines.
#   sh plan/ws073/tests/bug051-load.sh [SECONDS]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
cd "$(dirname -- "$0")/../../.."
seconds=${1:-200}
runtime=${GUEST_RUNTIME:-$PWD/build/ws073-run}
port=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["ssh_port"])' "$runtime/session.json")
S="ssh -c chacha20-poly1305@openssh.com -i plan/tmp/guest/id_ed25519 -p $port -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR -o BatchMode=yes -o ConnectTimeout=10 root@127.0.0.1"
timeout 30 $S "/tmp/regcheck $seconds 6 > /tmp/regcheck.log 2>&1 &" </dev/null
start=$(date +%s)
for w in 1 2; do
	( f=0; i=0
	  while [ $(( $(date +%s) - start )) -lt "$seconds" ]; do
		timeout 60 $S 'uname -n; dd if=/dev/zero bs=4096 count=64 2>/dev/null | wc -c' </dev/null >/dev/null 2>&1 || f=$((f+1))
		i=$((i+1))
	  done
	  echo "worker $w sessions $i failed $f" ) &
done
wait
timeout 30 $S 'dmesg | grep "killed by signal"; true' </dev/null
