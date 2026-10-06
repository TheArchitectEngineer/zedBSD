#!/bin/sh
# ws130-p007: dhcpc -6 on a running zedBSD guest of plan/ws130/tests/config-amd64-ipv6.mk (with p006's networkd), its USB
# network adapter on QEMU's user network, whose DHCPv6 answers only Information-Request (its DNS server fec0::3).
#  1. dhcpc -6 -i -v IF: exit 0, "dhcpc: IF: dns fec0::3" and "DHCPv6 information renew 86400" (no refresh time given).
#     The user network's packets meanwhile are kept in OUTDIR/dhcp6.pcap (QMP filter-dump on net0).
#  2. /var/db/dhcpc/duid: 18 bytes beginning 00 04 (a DUID-UUID); /var/db/dhcpc/IF.dhcp6: "mode stateless".
#  3. /etc/resolv.conf: a "nameserver fec0::3" line (from the RDNSS or DHCPv6), recorded.
#  4. dhcpc -6 -i IF again: the same DUID.
#  5. dhcpc -6 -t 4 IF (stateful; QEMU answers no Solicit): exit 1 with "DHCPv6 lease:", within 10 seconds.
#  6. networkd's DHCPv6 lines (recorded only: it runs dhcpc -6 when an advertisement's M flag, or its O flag without
#     RDNSS, asks; QEMU's advertisements carry RDNSS).
#  7. IPv4 unchanged: ping -c 1 10.0.2.2.
# PASS: every "ok" line and the last line ipv6-p007: PASS.
#
#   plan/tools/guest/test-image.sh plan/ws130/tests/config-amd64-ipv6.mk BUILD; plan/tools/files/files-guest.sh start IMAGE
#   plan/ws130/tests/ipv6-p007.sh [OUTDIR]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
out=${1:-build/ws130-p007}
mkdir -p "$out"
guest() { timeout 120 python3 plan/tools/guest/guest.py run "$1" 2>&1 | tr -d '\r'; }
status=0
ok() { echo "ok: $1"; }
bad() { echo "FAIL: $1"; status=1; }

# The interface, and time for an advertisement and the link-local address's DAD.
iface=$(guest "ifconfig -a | sed -n 's/^\([a-z]*[0-9]*\): flags=.*UP.*/\1/p' | grep -v '^lo' | head -1" | tail -1)
echo "interface: $iface"
guest 'sleep 5; echo waited' >/dev/null

# 1. The information, with -v, the user network's traffic meanwhile in dhcp6.pcap (QEMU's filter-dump by QMP).
monitor=$(python3 -c 'import json, sys; print(json.load(open(sys.argv[1]))["monitor"])' \
    "${GUEST_RUNTIME:-build/guest}/session.json" 2>/dev/null)
pcap=$(realpath "$out")/dhcp6.pcap
[ -S "$monitor" ] && python3 plan/tools/qmp.py "$monitor" object-add \
    "{\"qom-type\":\"filter-dump\",\"id\":\"p007dump\",\"netdev\":\"net0\",\"file\":\"$pcap\"}" >/dev/null
guest "dhcpc -6 -i -v $iface; echo exit=\$?" > "$out/information.txt"
[ -S "$monitor" ] && python3 plan/tools/qmp.py "$monitor" object-del '{"id":"p007dump"}' >/dev/null
cat "$out/information.txt"
grep -q '^exit=0' "$out/information.txt" && ok "dhcpc -6 -i: exit 0" || bad "dhcpc -6 -i: exit"
grep -q "^dhcpc: $iface: dns fec0::3" "$out/information.txt" && ok "the DNS server fec0::3" || bad "DNS server"
grep -q "^dhcpc: $iface: DHCPv6 information renew 86400" "$out/information.txt" && ok "renew in a day" || bad "renew"

# 2. The DUID and the record.
guest "wc -c < /var/db/dhcpc/duid; od -An -tx1 /var/db/dhcpc/duid; cat /var/db/dhcpc/$iface.dhcp6" > "$out/state.txt"
cat "$out/state.txt"
grep -q '^ *18$' "$out/state.txt" && ok "DUID: 18 bytes" || bad "DUID size"
grep -q '^ *00 04 ' "$out/state.txt" && ok "DUID: a DUID-UUID" || bad "DUID type"
grep -q '^mode stateless$' "$out/state.txt" && ok "record: stateless" || bad "record"

# 3. The resolver.
guest "cat /etc/resolv.conf" > "$out/resolv.txt"
cat "$out/resolv.txt"
grep -q '^nameserver fec0::3$' "$out/resolv.txt" && ok "resolv.conf: fec0::3" || bad "resolv.conf"

# 4. The same DUID the second time.
first=$(grep '^ *00 04 ' "$out/state.txt")
guest "dhcpc -6 -i $iface >/dev/null; od -An -tx1 /var/db/dhcpc/duid" > "$out/again.txt"
second=$(grep '^ *00 04 ' "$out/again.txt")
[ -n "$first" ] && [ "$first" = "$second" ] && ok "the same DUID again" || bad "DUID changed"

# 5. No stateful server: a failure in time.
guest "start=\$(date +%s); dhcpc -6 -t 4 $iface; echo exit=\$?; echo took=\$((\$(date +%s) - start))" > "$out/stateful.txt"
cat "$out/stateful.txt"
grep -q '^exit=1' "$out/stateful.txt" && grep -q 'DHCPv6 lease:' "$out/stateful.txt" && ok "no lease: exit 1" ||
    bad "no lease"
took=$(sed -n 's/^took=//p' "$out/stateful.txt")
[ -n "$took" ] && [ "$took" -le 10 ] && ok "no lease: in $took seconds" || bad "no lease: time"

# 6. networkd's DHCPv6 (recorded).
guest "grep -h 'DHCPv6' /var/log/networkd.log /var/log/messages 2>/dev/null | tail -3" > "$out/networkd.txt"
cat "$out/networkd.txt"

# 7. IPv4 unchanged.
guest 'ping -c 1 10.0.2.2 >/dev/null 2>&1 && echo yes' | tail -1 | grep -q yes && ok "IPv4 still works" || bad "IPv4"

[ $status = 0 ] && echo "ipv6-p007: PASS" || echo "ipv6-p007: FAIL"
exit $status
