#!/bin/sh
# ws130-p002: the kernel's IPv6 core on a running zedBSD guest of plan/ws130/tests/config-amd64-ipv6.mk, its USB network
# adapter on QEMU's user network (which answers router solicitations with fec0::/64).
#  1. ipv6-probe as root: ::1 on lo0; a link-local address passes duplicate address detection; the router's advertisement
#     comes (the kernel solicited it) with its prefix; an address in it has its lifetimes counted; a default route through
#     the router reads back with the connected routes; both removed; IPv6 off and on ("IPV6 PASS").
#  2. The probe as a user who is not root (kei, else nobody) is refused the change (step=add-link-local, EPERM).
#  3. IPv4 still works: the guest still reaches its IPv4 gateway (ping 10.0.2.2) after the probe.
# PASS: every "ok" line and the last line ipv6-p002: PASS.
#
#   plan/tools/guest/test-image.sh plan/ws130/tests/config-amd64-ipv6.mk BUILD; plan/tools/files/files-guest.sh start IMAGE
#   plan/ws130/tests/ipv6-p002.sh
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
guest() { timeout 90 python3 plan/tools/guest/guest.py run "$1" 2>&1 | tr -d '\r'; }
status=0

# 1. The probe.
guest '/bin/ipv6-probe' | tee /dev/stderr | grep -q '^IPV6 PASS$' && echo "ok: ipv6-probe" || { echo "FAIL: ipv6-probe"; status=1; }

# 2. Not for another user.
user=$(guest 'grep -q "^kei:" /etc/passwd && echo kei || echo nobody' | tail -1)
guest "runas $user /bin/ipv6-probe 2>&1; true" | grep -q 'FAIL step=add-link-local' && echo "ok: $user is refused" || { echo "FAIL: $user was not refused"; status=1; }

# 3. IPv4 unchanged.
guest 'ping -c 1 10.0.2.2 >/dev/null 2>&1 && echo yes' | tail -1 | grep -q yes && echo "ok: IPv4 still works" || { echo "FAIL: IPv4"; status=1; }

[ $status = 0 ] && echo "ipv6-p002: PASS" || echo "ipv6-p002: FAIL"
exit $status
