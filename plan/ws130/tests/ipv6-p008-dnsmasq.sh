#!/bin/sh
# ws130-p008: stateful DHCPv6 and networkd's M flag on a zedBSD guest of plan/ws130/tests/config-amd64-ipv6.mk with a
# second USB network adapter on a host tap.  The tap is on a bridge with a veth whose other end is in the network
# namespace zb6, where dnsmasq (dnsmasq-base) sends Router Advertisements (M and O, the prefix fd00:6::/64 with A, no
# default router: the zedBSD kernel reaches a prefix on the link through an address in it, the SLAAC one) and leases
# fd00:6::100-1ff for two minutes (T1 one minute), with the DNS server fd00:6::53 and the search list zb6.test.  The
# host's root namespace has no address on that segment.  The first adapter stays on QEMU's user network for SSH.
#  1. The second interface (MAC 52:54:00:33:00:02) up; networkd restarted so that it solicits a router there.
#  2. ifconfig IF: an "inet6 fd00:6::1.. prefixlen 128 dhcp" address (networkd ran dhcpc -6 on the M flag), and a
#     SLAAC one "inet6 fd00:6::... prefixlen 64 autoconf".
#  3. /var/db/dhcpc/IF.dhcp6: "mode stateful", "renew 60"; networkd ran dhcpc -6 on the M flag: dnsmasq's log has a
#     DHCPSOLICIT and a DHCPREPLY before the test runs dhcpc itself (networkd's own lines go to the console, not read).
#  4. ping -c 1 fd00:6::1 (the namespace's address).
#  5. /etc/resolv.conf: "nameserver fd00:6::53" and "search zb6.test" (RDNSS or DHCPv6).
#  6. dhcpc -6 -v IF: "renew taken" and the same address (a Renew of the recorded lease).
#  7. After 70 seconds: networkd ran dhcpc -6 again at T1: dnsmasq's log has two DHCPRENEW (step 6's and networkd's).
#  8. IPv4 on the first adapter unchanged: ping -c 1 10.0.2.2.
# PASS: every "ok" line and the last line ipv6-p008-dnsmasq: PASS.  The host's part needs sudo (ip, dnsmasq); the
# guest runs under its own runtime directory (build/ws130-p008-run), and both are taken down at the end.
#
#   plan/tools/guest/test-image.sh plan/ws130/tests/config-amd64-ipv6.mk BUILD
#   plan/ws130/tests/ipv6-p008-dnsmasq.sh IMAGE [OUTDIR]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
image=${1:?usage: ipv6-p008-dnsmasq.sh IMAGE [OUTDIR]}
out=${2:-build/ws130-p008}
mkdir -p "$out"
GUEST_RUNTIME=$(pwd)/build/ws130-p008-run
export GUEST_RUNTIME
guest() { timeout 120 python3 plan/tools/guest/guest.py run "$1" 2>&1 | tr -d '\r'; }
status=0
ok() { echo "ok: $1"; }
bad() { echo "FAIL: $1"; status=1; }
dnsmasq_pid=

# The host's segment taken down: dnsmasq, the guest, the links and the namespace.
teardown() {
	python3 plan/tools/guest/guest.py stop >/dev/null 2>&1
	[ -n "$dnsmasq_pid" ] && sudo kill "$dnsmasq_pid" 2>/dev/null
	sudo ip link del zbtap0 2>/dev/null
	sudo ip link del zbbr0 2>/dev/null
	sudo ip netns del zb6 2>/dev/null
}
trap teardown EXIT

# The segment: a bridge with the tap and a veth into the namespace, fd00:6::1 there.
teardown
sudo ip netns add zb6 || exit 1
sudo ip link add zbbr0 type bridge
sudo sysctl -q -w net.ipv6.conf.zbbr0.disable_ipv6=1
sudo ip link set zbbr0 up
sudo ip tuntap add dev zbtap0 mode tap user "$(id -un)"
sudo sysctl -q -w net.ipv6.conf.zbtap0.disable_ipv6=1
sudo ip link set zbtap0 master zbbr0 up
sudo ip link add zbv0 type veth peer name zbv1
sudo sysctl -q -w net.ipv6.conf.zbv0.disable_ipv6=1
sudo ip link set zbv0 master zbbr0 up
sudo ip link set zbv1 netns zb6
sudo ip -n zb6 link set lo up
sudo ip -n zb6 link set zbv1 up
sudo ip -n zb6 addr add fd00:6::1/64 dev zbv1 nodad

# dnsmasq in the namespace: Router Advertisements every few seconds, stateful DHCPv6, no DNS service, no lease file.
sudo ip netns exec zb6 /usr/sbin/dnsmasq --no-daemon --conf-file=/dev/null --port=0 --interface=zbv1 \
	--bind-interfaces --enable-ra --ra-param=zbv1,10,0 --dhcp-range=fd00:6::100,fd00:6::1ff,slaac,64,2m \
	--dhcp-option=option6:dns-server,[fd00:6::53] --dhcp-option=option6:domain-search,zb6.test --leasefile-ro \
	--log-dhcp --log-facility=- > "$out/dnsmasq.log" 2>&1 &
dnsmasq_pid=$!

# The guest with the second adapter on the tap.
python3 plan/tools/guest/guest.py start "$image" --qemu-extra "-netdev tap,id=net1,ifname=zbtap0,script=no,downscript=no \
-device usb-net,bus=xhci.0,port=5,id=ecm1,netdev=net1,mac=52:54:00:33:00:02" || exit 1
python3 plan/tools/guest/guest.py wait --timeout 240 || { bad "the guest did not come up"; exit 1; }

# 1. The second interface, up, and networkd started again.
iface=$(guest "ifconfig -a | awk '/^[a-z]+[0-9]+:/ {name = substr(\$1, 1, length(\$1) - 1)} /ether 52:54:00:33:00:02/ {print name}'" |
    tail -1)
echo "interface: $iface"
[ -n "$iface" ] && ok "the second adapter: $iface" || { bad "no second adapter"; exit 1; }
guest "ifconfig $iface up; sleep 2; service restart networkd >/dev/null 2>&1; sleep 20; echo waited" >/dev/null

# 2. The leased address.
guest "ifconfig $iface" > "$out/ifconfig.txt"
cat "$out/ifconfig.txt"
grep -q 'inet6 fd00:6::1[0-9a-f][0-9a-f] prefixlen 128 dhcp$' "$out/ifconfig.txt" && ok "the DHCPv6 address" ||
    bad "DHCPv6 address"
grep -q 'inet6 fd00:6::.* prefixlen 64 autoconf$' "$out/ifconfig.txt" && ok "the SLAAC address" || bad "SLAAC address"
leased=$(sed -n 's/.*inet6 \(fd00:6::1[0-9a-f]*\) prefixlen 128 dhcp$/\1/p' "$out/ifconfig.txt" | head -1)

# 3. The record, and networkd's exchange as dnsmasq saw it.
guest "cat /var/db/dhcpc/$iface.dhcp6" > "$out/record.txt"
cat "$out/record.txt"
grep -q '^mode stateful$' "$out/record.txt" && grep -q '^renew 60$' "$out/record.txt" && ok "record: stateful, renew 60" ||
    bad "record"
grep -q 'DHCPSOLICIT(zbv1)' "$out/dnsmasq.log" && grep -q 'DHCPREPLY(zbv1) fd00:6::' "$out/dnsmasq.log" &&
    ok "networkd ran dhcpc -6 on the M flag" || bad "networkd on the M flag"

# 4. The namespace's address.
guest "ping -c 1 fd00:6::1" > "$out/ping.txt"
grep -q '1 packets received' "$out/ping.txt" && ok "ping fd00:6::1" || { bad "ping fd00:6::1"; cat "$out/ping.txt"; }

# 5. The resolver.
guest "cat /etc/resolv.conf" > "$out/resolv.txt"
cat "$out/resolv.txt"
grep -q '^nameserver fd00:6::53$' "$out/resolv.txt" && ok "resolv.conf: fd00:6::53" || bad "resolv.conf: server"
grep -q '^search .*zb6.test' "$out/resolv.txt" && ok "resolv.conf: search zb6.test" || bad "resolv.conf: search"

# 6. A Renew by hand: the same address.
guest "dhcpc -6 -v $iface; echo exit=\$?" > "$out/renew.txt"
cat "$out/renew.txt"
grep -q "^dhcpc: $iface: renew taken" "$out/renew.txt" && ok "renew taken" || bad "renew"
[ -n "$leased" ] && grep -q "^dhcpc: $iface: address $leased/128 " "$out/renew.txt" && ok "the same address: $leased" ||
    bad "the same address"

# 7. networkd's run at T1.
guest "sleep 70; echo waited" >/dev/null
grep 'DHCPRENEW(zbv1)' "$out/dnsmasq.log" > "$out/t1.txt"
cat "$out/t1.txt"
[ "$(grep -c 'DHCPRENEW(zbv1)' "$out/t1.txt")" -ge 2 ] && ok "networkd ran dhcpc -6 again at T1" || bad "T1"

# 8. IPv4 unchanged.
guest 'ping -c 1 10.0.2.2 >/dev/null 2>&1 && echo yes' | tail -1 | grep -q yes && ok "IPv4 still works" || bad "IPv4"

[ $status = 0 ] && echo "ipv6-p008-dnsmasq: PASS" || echo "ipv6-p008-dnsmasq: FAIL"
exit $status
