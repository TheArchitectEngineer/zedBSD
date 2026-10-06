#!/bin/sh
# ws130-p004: the C library's IPv6 on a running zedBSD guest of plan/ws130/tests/config-amd64-ipv6.mk.
#  1. ipv6-probe -l: inet_pton and inet_ntop (RFC 5952's form, a v4-mapped address, a bad one), if_nametoindex and
#     if_indextoname of lo0, getaddrinfo of ::1 with a port, of fe80::1%lo0 (its scope), ::1 refused for AF_INET,
#     127.0.0.1 for AF_INET6 with AI_V4MAPPED, no node (0.0.0.0 then ::), getnameinfo of fe80::1%lo0 (by name and by
#     number with NI_NUMERICSCOPE) ("IPV6 libc ... ok" each, "IPV6 PASS").
#  2. With LOOKUP_NAME (a name with A or AAAA records the guest's DNS, QEMU's 10.0.2.3, can answer): ipv6-probe -l NAME
#     prints its addresses in getaddrinfo's order; IPv6 ones only after the IPv4 ones while the guest has no global IPv6
#     route out (QEMU's user network gives none).  Recorded, judged only by "IPV6 libc lookup ok".
#  3. Earlier steps unchanged: plan/ws130/tests/ipv6-p003.sh (the transports and IPv4).
# PASS: every "ok" line and the last line ipv6-p004: PASS.
#
#   plan/tools/guest/test-image.sh plan/ws130/tests/config-amd64-ipv6.mk BUILD; plan/tools/files/files-guest.sh start IMAGE
#   [LOOKUP_NAME=example.com] plan/ws130/tests/ipv6-p004.sh [OUTDIR]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
out=${1:-build/ws130-p004}
mkdir -p "$out"
guest() { timeout 120 python3 plan/tools/guest/guest.py run "$1" 2>&1 | tr -d '\r'; }
status=0

# 1. The libc steps.
guest '/bin/ipv6-probe -l' > "$out/libc.txt"
cat "$out/libc.txt"
for step in text interface numeric mapped unnamed names; do
	grep -q "^IPV6 libc $step ok$" "$out/libc.txt" && echo "ok: $step" || { echo "FAIL: $step"; status=1; }
done
grep -q '^IPV6 PASS$' "$out/libc.txt" && echo "ok: ipv6-probe -l" || { echo "FAIL: ipv6-probe -l"; status=1; }

# 2. A name in the DNS, when one is given.
if [ -n "${LOOKUP_NAME:-}" ]; then
	guest "/bin/ipv6-probe -l $LOOKUP_NAME" > "$out/lookup.txt"
	grep 'IPV6 libc lookup ' "$out/lookup.txt"
	grep -q '^IPV6 libc lookup ok$' "$out/lookup.txt" && echo "ok: $LOOKUP_NAME looked up" || { echo "FAIL: $LOOKUP_NAME"; status=1; }
fi

# 3. The transports and IPv4, unchanged.
sh plan/ws130/tests/ipv6-p003.sh "$out/p003" > "$out/p003.txt" 2>&1
tail -1 "$out/p003.txt"
grep -q '^ipv6-p003: PASS$' "$out/p003.txt" && echo "ok: ipv6-p003" || { echo "FAIL: ipv6-p003"; status=1; }

[ $status = 0 ] && echo "ipv6-p004: PASS" || echo "ipv6-p004: FAIL"
exit $status
