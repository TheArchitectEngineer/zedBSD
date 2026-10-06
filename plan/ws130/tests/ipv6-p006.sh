#!/bin/sh
# ws130-p006: networkd's SLAAC on a running zedBSD guest of plan/ws130/tests/config-amd64-ipv6.mk, its USB network adapter
# on QEMU's user network (a router at fe80::2 whose advertisements give fec0::/64, the default route and, when QEMU sends
# it, the DNS server fec0::3).  Nothing is configured by hand: networkd does it when the interface comes up.
#  1. ifconfig IF: the link-local address networkd gave (inet6 fe80::... prefixlen 64), a stable SLAAC address
#     (inet6 fec0::... prefixlen 64 autoconf) and a temporary one (... autoconf temporary).
#  2. route -6 show: the default route through fe80::2 on IF (UGD).
#  3. ping -c 1 fec0::2 from the SLAAC address (the router's global address).
#  4. /var/db/networkd/ipv6-secret: 32 bytes, mode 0600.
#  5. service restart networkd: the same stable addresses again (RFC 7217: the same secret, prefix and interface).
#  6. /etc/resolv.conf: recorded; with QEMU's RDNSS a "nameserver fec0::3" after the IPv4 one (judged only when
#     QEMU sent the option: the "dns=" count of networkd's advertisement line in its log).
#  7. IPv4 unchanged: ping -c 1 10.0.2.2.
# PASS: every "ok" line and the last line ipv6-p006: PASS.
#
#   plan/tools/guest/test-image.sh plan/ws130/tests/config-amd64-ipv6.mk BUILD; plan/tools/files/files-guest.sh start IMAGE
#   plan/ws130/tests/ipv6-p006.sh [OUTDIR]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
out=${1:-build/ws130-p006}
mkdir -p "$out"
guest() { timeout 120 python3 plan/tools/guest/guest.py run "$1" 2>&1 | tr -d '\r'; }
status=0
ok() { echo "ok: $1"; }
bad() { echo "FAIL: $1"; status=1; }

# The interface, and time for an advertisement (QEMU answers the solicitation at once).
iface=$(guest "ifconfig -a | sed -n 's/^\([a-z]*[0-9]*\): flags=.*UP.*/\1/p' | grep -v '^lo' | head -1" | tail -1)
echo "interface: $iface"
guest 'sleep 5; echo waited' >/dev/null

# 1. The addresses.
guest "ifconfig $iface" > "$out/ifconfig.txt"
cat "$out/ifconfig.txt"
grep -q 'inet6 fe80::.* prefixlen 64' "$out/ifconfig.txt" && ok "link-local address" || bad "link-local address"
grep -q 'inet6 fec0::.* prefixlen 64 autoconf$' "$out/ifconfig.txt" && ok "stable SLAAC address" || bad "stable SLAAC address"
grep -q 'inet6 fec0::.* prefixlen 64 temporary autoconf$' "$out/ifconfig.txt" && ok "temporary address" || bad "temporary address"

# 2. The default route.
guest "route -6 show" > "$out/route.txt"
cat "$out/route.txt"
grep -q "^default *fe80::2 .*UGD $iface" "$out/route.txt" && ok "default route through fe80::2" || bad "default route"

# 3. The router's global address.
guest "ping -c 1 fec0::2" > "$out/ping.txt"
grep -q '1 packets received' "$out/ping.txt" && ok "ping fec0::2" || bad "ping fec0::2"

# 4. The secret.
guest "ls -l /var/db/networkd/ipv6-secret" > "$out/secret.txt"
grep -q '^-rw------- .* 32 ' "$out/secret.txt" && ok "the secret: 32 bytes, 0600" || { bad "the secret"; cat "$out/secret.txt"; }

# 5. The same stable addresses after networkd starts again.
stable_before=$(grep -E 'inet6 (fe80|fec0)::' "$out/ifconfig.txt" | grep -v temporary | awk '{print $2}' | sort | tr '\n' ' ')
guest "service restart networkd >/dev/null 2>&1; sleep 6; ifconfig $iface" > "$out/again.txt"
stable_after=$(grep -E 'inet6 (fe80|fec0)::' "$out/again.txt" | grep -v temporary | awk '{print $2}' | sort | tr '\n' ' ')
echo "stable before: $stable_before"
echo "stable after:  $stable_after"
[ -n "$stable_before" ] && [ "$stable_before" = "$stable_after" ] && ok "the same stable addresses after a restart" || bad "stable after a restart"

# 6. The resolver.
guest "cat /etc/resolv.conf" > "$out/resolv.txt"
cat "$out/resolv.txt"
guest "grep -h 'router advertisement' /var/log/networkd.log /var/log/messages 2>/dev/null | tail -1" > "$out/ra-line.txt"
cat "$out/ra-line.txt"
if grep -q 'dns=[1-9]' "$out/ra-line.txt"; then
	grep -q '^nameserver fec0::3' "$out/resolv.txt" && ok "RDNSS in resolv.conf" || bad "RDNSS in resolv.conf"
else
	echo "note: no RDNSS line seen (QEMU may not send RDNSS); resolv.conf recorded"
fi

# 7. IPv4 unchanged.
guest 'ping -c 1 10.0.2.2 >/dev/null 2>&1 && echo yes' | tail -1 | grep -q yes && ok "IPv4 still works" || bad "IPv4"

[ $status = 0 ] && echo "ipv6-p006: PASS" || echo "ipv6-p006: FAIL"
exit $status
