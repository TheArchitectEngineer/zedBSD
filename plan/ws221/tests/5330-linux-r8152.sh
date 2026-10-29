#!/bin/sh
# The Latitude 5330's Debian (the RTL8156 under Linux's r8152) with the same tool as zedBSD: builds
# userland/tests/tcp-perf for Linux on this host, copies it to the 5330 (ssh alias solaris10-man), starts
# plan/ws221/tests/perf-peer.py on this host for two connections, and runs tcp-perf send and then recv against it,
# SECONDS each (default 30).  The baseline zedBSD's CDC NCM is compared with, direction by direction.
#   plan/ws221/tests/5330-linux-r8152.sh NAME [SECONDS]
# Output: build/ws221-5330/NAME (fresh-out.sh): summary.txt (the driver, the link speed, both TCP-PERF lines, the
# peer's lines), client.txt (the 5330's output), peer.log.  The 5330 must be running Debian; nothing is changed there
# but the copied program (~/ws221/tcp-perf).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
name=${1:?usage: 5330-linux-r8152.sh NAME [SECONDS]}
seconds=${2:-30}
server_ip=$(ip route get 10.0.30.3 | sed -n 's/.* src \([0-9.]*\).*/\1/p' | head -1)
port=5312
. plan/tools/fresh-out.sh
fresh_out "build/ws221-5330/$name"
out=build/ws221-5330/$name
say() { echo "5330-linux: $*" | tee -a "$out/summary.txt"; }
lin() { timeout "${T:-60}" ssh -o ConnectTimeout=8 -o BatchMode=yes solaris10-man "$1"; }

# The program, built for Linux here and copied over.
cc -O2 -Wall -o "$out/tcp-perf" userland/tests/tcp-perf/main.c || { say "FAIL: build of tcp-perf"; exit 1; }
lin 'mkdir -p ~/ws221' && timeout 60 scp -q -o ConnectTimeout=8 -o BatchMode=yes "$out/tcp-perf" solaris10-man:ws221/tcp-perf ||
	{ say "FAIL: copy to the 5330"; exit 1; }

# The interface toward this host, its driver and speed.
dev=$(lin "ip route get $server_ip" | sed -n 's/.* dev \([^ ]*\).*/\1/p' | head -1)
say "interface $dev driver $(lin "basename \$(readlink -f /sys/class/net/$dev/device/driver)") speed $(lin "cat /sys/class/net/$dev/speed") kernel $(lin 'uname -r')"

# The peer here, then send and recv from the 5330.
: > "$out/peer.log"
timeout $((seconds * 2 + 120)) python3 -I plan/ws221/tests/perf-peer.py "$server_ip" "$port" "$out/peer.log" 2 > /dev/null 2>&1 &
peer=$!
sleep 1
T=$((seconds + 60)) lin "~/ws221/tcp-perf send $server_ip $port $seconds | tail -1; ~/ws221/tcp-perf recv $server_ip $port $seconds | tail -1" > "$out/client.txt" 2>&1
wait "$peer" 2> /dev/null
grep '^TCP-PERF' "$out/client.txt" | sed 's/^/5330-linux: /' | tee -a "$out/summary.txt"
sed 's/^/5330-linux: peer: /' "$out/peer.log" | tee -a "$out/summary.txt"
[ "$(grep -c '^TCP-PERF' "$out/client.txt")" = 2 ] || { say "FAIL"; exit 1; }
say "PASS"
