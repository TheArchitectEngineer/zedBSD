#!/bin/sh
# ws130-p003: the IPv6 transports on a running zedBSD guest of plan/ws130/tests/config-amd64-ipv6.mk, its USB network
# adapter on QEMU's user network (which answers router solicitations with fec0::/64 and echoes at fe80::2 and fec0::2).
#  1. ipv6-probe -t as root: UDP on ::1 and a connect's source; an AF_INET6 socket on [::] takes an IPv4 datagram from
#     ::ffff:127.0.0.1, answers it, and keeps its port from AF_INET; IPV6_V6ONLY leaves IPv4 and the port; TCP on ::1
#     (the accept's peer, data both ways), an IPv4 connection to an AF_INET6 listener (an IPv4-mapped peer), a refused
#     connect; an ICMPv6 echo to ::1 on a raw socket ("IPV6 PASS").
#  2. ipv6-probe -T as root: the core's steps of ws130-p002, and echoes to the router's link-local address (by the
#     interface's scope) and its global one ("IPV6 echo router-link-local ok", "... router-global ok", "IPV6 PASS").
#  3. A user who is not root gets no raw ICMPv6 socket, but UDP and TCP (the probe's -t fails only at step=echo-socket).
#  4. IPv4 still works: the guest still reaches its IPv4 gateway (ping 10.0.2.2) after the probes.
# PASS: every "ok" line and the last line ipv6-p003: PASS.
#
#   plan/tools/guest/test-image.sh plan/ws130/tests/config-amd64-ipv6.mk BUILD; plan/tools/files/files-guest.sh start IMAGE
#   plan/ws130/tests/ipv6-p003.sh [OUTDIR]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
out=${1:-build/ws130-p003}
mkdir -p "$out"
guest() { timeout 120 python3 plan/tools/guest/guest.py run "$1" 2>&1 | tr -d '\r'; }
status=0

# 1. The transports on the host itself.
guest '/bin/ipv6-probe -t' > "$out/local.txt"
cat "$out/local.txt"
grep -q '^IPV6 PASS$' "$out/local.txt" && echo "ok: ipv6-probe -t" || { echo "FAIL: ipv6-probe -t"; status=1; }

# 2. The core and the transports through the router.
guest '/bin/ipv6-probe -T' > "$out/router.txt"
cat "$out/router.txt"
grep -q '^IPV6 echo router-link-local ok$' "$out/router.txt" && echo "ok: echo to the router's link-local address" || { echo "FAIL: router link-local echo"; status=1; }
grep -q '^IPV6 echo router-global ok$' "$out/router.txt" && echo "ok: echo to the router's global address" || { echo "FAIL: router global echo"; status=1; }
grep -q '^IPV6 PASS$' "$out/router.txt" && echo "ok: ipv6-probe -T" || { echo "FAIL: ipv6-probe -T"; status=1; }

# 3. Another user: UDP and TCP, but no raw socket.
user=$(guest 'grep -q "^kei:" /etc/passwd && echo kei || echo nobody' | tail -1)
guest "runas $user /bin/ipv6-probe -t 2>&1; true" > "$out/user.txt"
grep -q 'IPV6 tcp refused ok' "$out/user.txt" && grep -q 'FAIL step=echo-socket' "$out/user.txt" && echo "ok: $user has UDP and TCP, not a raw socket" || { echo "FAIL: $user"; cat "$out/user.txt"; status=1; }

# 4. IPv4 unchanged.
guest 'ping -c 1 10.0.2.2 >/dev/null 2>&1 && echo yes' | tail -1 | grep -q yes && echo "ok: IPv4 still works" || { echo "FAIL: IPv4"; status=1; }

[ $status = 0 ] && echo "ipv6-p003: PASS" || echo "ipv6-p003: FAIL"
exit $status
