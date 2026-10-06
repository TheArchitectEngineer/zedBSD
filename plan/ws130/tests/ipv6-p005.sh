#!/bin/sh
# ws130-p005: the IPv6 tools on a running zedBSD guest of plan/ws130/tests/config-amd64-ipv6.mk, its USB network adapter
# on QEMU's user network (a router at fe80::2 that answers solicitations with fec0::/64, and echoes at fec0::2).
#  1. ifconfig IF: an "inet6 fe80::... prefixlen 64 scopeid 0x..." line (the link-local address).
#  2. ifconfig IF inet6 2001:db8:5::5/64, then the address shown; ifconfig IF -inet6 2001:db8:5::5/64, then not shown.
#  3. route -6 add 2001:db8:6::/48 fe80::2 -ifp IF, then route -6 show lists it; route -6 delete 2001:db8:6::/48.
#  4. ping -6 -c 1 ::1, ping -c 1 fec0::2 (by the router's global address), ping -c 1 fe80::2%IF: "1 packets received".
#  5. ifconfig IF ipv6 off: no inet6 line; ifconfig IF ipv6 on: the link-local address again (duplicate address
#     detection takes a second or two).
#  6. With LOOKUP_NAME: host NAME and nslookup NAME print their addresses (A and AAAA), recorded.
#  7. IPv4 unchanged: ping -c 1 10.0.2.2.
# PASS: every "ok" line and the last line ipv6-p005: PASS.
#
#   plan/tools/guest/test-image.sh plan/ws130/tests/config-amd64-ipv6.mk BUILD; plan/tools/files/files-guest.sh start IMAGE
#   [LOOKUP_NAME=example.com] plan/ws130/tests/ipv6-p005.sh [OUTDIR]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
out=${1:-build/ws130-p005}
mkdir -p "$out"
guest() { timeout 120 python3 plan/tools/guest/guest.py run "$1" 2>&1 | tr -d '\r'; }
status=0
ok() { echo "ok: $1"; }
bad() { echo "FAIL: $1"; status=1; }

# The interface: the first that is up and not the loopback.
iface=$(guest "ifconfig -a | sed -n 's/^\([a-z]*[0-9]*\): flags=.*UP.*/\1/p' | grep -v '^lo' | head -1" | tail -1)
echo "interface: $iface"

# 1. The link-local address.
guest "ifconfig $iface" > "$out/ifconfig.txt"
grep -q 'inet6 fe80::.* prefixlen 64 scopeid 0x' "$out/ifconfig.txt" && ok "link-local address shown" || bad "link-local address"

# 2. A static address added and removed.
guest "ifconfig $iface inet6 2001:db8:5::5/64 && ifconfig $iface" > "$out/added.txt"
grep -q 'inet6 2001:db8:5::5 prefixlen 64' "$out/added.txt" && ok "address added" || bad "address added"
guest "ifconfig $iface -inet6 2001:db8:5::5/64 && ifconfig $iface" > "$out/removed.txt"
grep -q '2001:db8:5::5' "$out/removed.txt" && bad "address removed" || ok "address removed"

# 3. A route added, shown and removed.
guest "route -6 add 2001:db8:6::/48 fe80::2 -ifp $iface && route -6 show" > "$out/route.txt"
grep -q '^2001:db8:6::/48 *fe80::2 .*UG.* '"$iface" "$out/route.txt" && ok "route added and shown" || bad "route added"
guest "route -6 delete 2001:db8:6::/48 && route -6 show" > "$out/route-gone.txt"
grep -q '2001:db8:6::/48' "$out/route-gone.txt" && bad "route removed" || ok "route removed"

# 4. Echoes: the loopback, the router's global address, its link-local one.
for target in "-6 ::1" "fec0::2" "fe80::2%$iface"; do
	guest "ping -c 1 $target" > "$out/ping.txt"
	grep -q '1 packets received' "$out/ping.txt" && ok "ping $target" || { bad "ping $target"; cat "$out/ping.txt"; }
done

# 5. IPv6 off and on again.
guest "ifconfig $iface ipv6 off && ifconfig $iface" > "$out/off.txt"
grep -q 'inet6' "$out/off.txt" && bad "IPv6 off" || ok "IPv6 off: no address"
guest "ifconfig $iface ipv6 on && sleep 3 && ifconfig $iface" > "$out/on.txt"
grep -q 'inet6 fe80::' "$out/on.txt" && ok "IPv6 on: the link-local address again" || bad "IPv6 on"

# 6. A name's addresses, when one is given.
if [ -n "${LOOKUP_NAME:-}" ]; then
	guest "host $LOOKUP_NAME; nslookup $LOOKUP_NAME" > "$out/lookup.txt"
	cat "$out/lookup.txt"
	grep -q 'has address\|has IPv6 address' "$out/lookup.txt" && ok "host $LOOKUP_NAME" || bad "host $LOOKUP_NAME"
fi

# 7. IPv4 unchanged.
guest 'ping -c 1 10.0.2.2 >/dev/null 2>&1 && echo yes' | tail -1 | grep -q yes && ok "IPv4 still works" || bad "IPv4"

[ $status = 0 ] && echo "ipv6-p005: PASS" || echo "ipv6-p005: FAIL"
exit $status
