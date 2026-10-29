#!/bin/sh
# IPv6 fragments reassembled on a QEMU guest of plan/tools/ipv6-frag/config-amd64-frag.mk.  The guest's second usb-net
# adapter (MAC 52:54:00:33:00:02) is on the host tap p6tap0, in the new bridge p6br0 with the veth p6v0, whose other end
# p6v1 is in the network namespace p6ws312 with fd00:312::1/64; the guest has fd00:312::2/64.  The host's Linux in the
# namespace is the peer (plan/tools/ipv6-frag/frag6.py); the first adapter stays on QEMU's user network for SSH.  The
# namespace's host route to the guest has mtu 1280, so Linux cuts what it sends to the guest at 1280.
#  1. ping -6 -s 1400 from the namespace: three replies of 1408 bytes (an echo request in two fragments, the reply
#     1448 bytes in one frame).
#  2. UDP datagrams of 1400, 4000 and 8000 bytes from the namespace, one at a time: udp6-recv on the guest prints each
#     one's length and FNV-1a hash, equal to what was sent; 12000 bytes (past the 9152-byte bound) does not arrive.
#  3. Hand-made fragments (AF_PACKET on p6v1): reversed order arrives; overlapping pieces do not; an atomic fragment
#     arrives; of two datagrams with the middle piece missing, the one with its offset-zero piece is answered with one
#     Time Exceeded code 1 after about 60 s, the other not at all.
#  4. Behind the reassembly: an atomic fragment of an unknown next header gets a Parameter Problem code 1 pointing at
#     40 (the fragment header); hop-by-hop options after a fragment header get one too; two fragment headers are
#     dropped; a Neighbor Solicitation in an atomic fragment is not answered, the same one plain is.
#  5. ping -6 -s 3000 (recorded, not judged: the 3048-byte reply needs fragments sent, which the guest does not make).
#  6. The guest still answers SSH.
# Judged by the guest's output over SSH and the namespace's tools; never the console.  The host's part needs sudo
# (ip, AF_PACKET and raw ICMPv6 sockets); it touches no physical NIC, existing bridge, route, firewall or global
# sysctl, and takes down and reads back what it made.  The last line is "qemu-frag: PASS" when every judged check held.
#   plan/tools/ipv6-frag/qemu-frag.sh [IMAGE [OUTDIR]]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
image=${1:-}
out=${2:-build/ws312-qemu}
mkdir -p "$out"
if [ -z "$image" ]; then
	flock /tmp/zedbsd-image-build.lock timeout 5400 sh plan/tools/guest/test-image.sh plan/tools/ipv6-frag/config-amd64-frag.mk \
	    build/ws312-frag > build/ws312-frag.log 2>&1 || { echo "qemu-frag: FAIL (image build, build/ws312-frag.log)"; exit 1; }
	image=build/ws312-frag/hdd-image.img
fi
GUEST_RUNTIME=$(pwd)/build/ws312-qemu-run
export GUEST_RUNTIME
mkdir -p "$GUEST_RUNTIME"
ns=p6ws312
port=3120
guest_mac=52:54:00:33:00:02
here=fd00:312::1
there=fd00:312::2
helper=$(pwd)/plan/tools/ipv6-frag/frag6.py
status=0
ok() { echo "ok: $1"; }
bad() { echo "FAIL: $1"; status=1; }
guest() { timeout "${T:-120}" python3 plan/tools/guest/guest.py run "$1" 2>&1 | tr -d '\r'; }
in_ns() { sudo timeout "${T:-120}" ip netns exec "$ns" "$@"; }

# The links and the namespace this test makes, taken down; then read back.
teardown() {
	python3 plan/tools/guest/guest.py stop >/dev/null 2>&1
	sudo ip link del p6tap0 2>/dev/null
	sudo ip link del p6v0 2>/dev/null
	sudo ip link del p6br0 2>/dev/null
	sudo ip netns del "$ns" 2>/dev/null
}
readback() {
	left=
	for link in p6tap0 p6v0 p6br0; do
		ip link show "$link" >/dev/null 2>&1 && left="$left $link"
	done
	ip netns list 2>/dev/null | grep -q "^$ns\b" && left="$left netns:$ns"
	if [ -z "$left" ]; then
		echo "teardown: p6tap0 p6v0 p6br0 netns:$ns all gone"
	else
		echo "teardown: still there:$left"
	fi
}
finish() {
	teardown
	readback
}
trap finish EXIT

# The segment: a new bridge with the tap and a veth into the namespace.
teardown
sudo ip netns add "$ns" || { echo "qemu-frag: FAIL (netns)"; exit 1; }
sudo ip link add p6br0 type bridge
sudo ip link set p6br0 up
sudo ip tuntap add dev p6tap0 mode tap user "$(id -un)"
sudo ip link set p6tap0 master p6br0 up
sudo ip link add p6v0 type veth peer name p6v1
sudo ip link set p6v0 master p6br0 up
sudo ip link set p6v1 netns "$ns"
sudo ip -n "$ns" link set lo up
sudo ip -n "$ns" link set p6v1 up
sudo ip -n "$ns" addr add "$here/64" dev p6v1 nodad
here_mac=$(sudo ip -n "$ns" link show p6v1 | awk '/link\/ether/ {print $2}')
echo "namespace: $ns p6v1 $here_mac $here"

# The guest with the second adapter on the tap.
python3 plan/tools/guest/guest.py start "$image" --qemu-extra "-netdev tap,id=net1,ifname=p6tap0,script=no,downscript=no \
-device usb-net,bus=xhci.0,port=5,id=ecm1,netdev=net1,mac=$guest_mac" || { echo "qemu-frag: FAIL (start)"; exit 1; }
python3 plan/tools/guest/guest.py wait --timeout 240 || { echo "qemu-frag: FAIL (no SSH)"; exit 1; }

# The guest's interface on the segment, with its address.
iface=$(guest "ifconfig -a | awk '/^[a-z]+[0-9]+:/ {name = substr(\$1, 1, length(\$1) - 1)} /ether $guest_mac/ {print name}'" | tail -1)
echo "interface: $iface"
[ -n "$iface" ] || { bad "no second adapter"; exit 1; }
guest "ifconfig $iface up; ifconfig $iface inet6 $there/64; sleep 4; ifconfig $iface" > "$out/ifconfig.txt"
cat "$out/ifconfig.txt"
grep -q "inet6 $there prefixlen 64" "$out/ifconfig.txt" && ok "guest address $there" || bad "guest address"

# The receiver on the guest, in the background.
guest "nohup udp6-recv $port 1500 100 > /tmp/udp6.log 2>&1 < /dev/null & sleep 1; cat /tmp/udp6.log" > "$out/recv-start.txt"
grep -q "UDP6-RECV ready port=$port" "$out/recv-start.txt" && ok "udp6-recv ready" || bad "udp6-recv ready"

# The namespace reaches the guest, then cuts what it sends at 1280.
in_ns ping -6 -c 2 -W 3 "$there" > "$out/ping-small.txt" 2>&1
grep -q ' 0% packet loss' "$out/ping-small.txt" && ok "ping the guest" || { bad "ping the guest"; cat "$out/ping-small.txt"; }
sudo ip -n "$ns" -6 route replace "$there/128" dev p6v1 mtu lock 1280
sudo ip -n "$ns" -6 route show > "$out/routes.txt"
cat "$out/routes.txt"

echo "--- 1. ping -s 1400"
in_ns ping -6 -c 3 -W 3 -s 1400 "$there" > "$out/ping-1400.txt" 2>&1
cat "$out/ping-1400.txt"
[ "$(grep -c '^1408 bytes from' "$out/ping-1400.txt")" = 3 ] && ok "ping -s 1400: three full replies" || bad "ping -s 1400"

# How many datagrams the receiver has printed, and whether one of a length and hash is among them.
received() { guest "cat /tmp/udp6.log" | grep -c '^UDP6 len='; }
arrived() { guest "cat /tmp/udp6.log" | grep -q "^UDP6 len=$1 fnv=$2\$"; }

echo "--- 2. UDP datagrams"
for length in 1400 4000 8000; do
	hash=$(python3 -I "$helper" fnv "$length")
	in_ns python3 -I "$helper" udp "$there" "$port" "$length"
	sleep 2
	arrived "$length" "$hash" && ok "UDP $length bytes arrived whole (fnv $hash)" || bad "UDP $length bytes"
done
before=$(received)
in_ns python3 -I "$helper" udp "$there" "$port" 12000
sleep 3
[ "$(received)" = "$before" ] && ok "UDP 12000 bytes (past the bound) not delivered" || bad "UDP 12000 bytes delivered"

# One hand-made case sent from the namespace.
craft() { in_ns python3 -I "$helper" craft p6v1 "$here_mac" "$guest_mac" "$here" "$there" "$1" "$port"; }

echo "--- 3. hand-made fragments"
craft reverse
sleep 2
arrived 3000 "$(python3 -I "$helper" fnv 3000)" && ok "reversed fragments arrived" || bad "reversed fragments"
before=$(received)
craft overlap
sleep 2
[ "$(received)" = "$before" ] && ok "overlapping fragments not delivered" || bad "overlapping fragments delivered"
craft atomic
sleep 2
arrived 500 "$(python3 -I "$helper" fnv 500)" && ok "atomic fragment arrived" || bad "atomic fragment"

# The Time Exceeded: listened for while the two datagrams wait out their 60 seconds.
(T=100 in_ns python3 -I "$helper" listen 80 3 1 > "$out/time-exceeded.txt" 2>&1) &
listener=$!
sleep 2
craft timeout
wait "$listener"
cat "$out/time-exceeded.txt"
[ "$(grep -c '^frag6: got type=3 code=1' "$out/time-exceeded.txt")" = 1 ] && ok "one Time Exceeded code 1" || bad "Time Exceeded count"
grep -q '^frag6: got type=3 code=1 .* id=0x31200003' "$out/time-exceeded.txt" &&
    ok "it quotes the datagram with its offset-zero piece" || bad "Time Exceeded quote"

echo "--- 4. behind the reassembly"
# One case and the Parameter Problem it should draw (or none: EXPECT is the number of messages).
problem() {
	(in_ns python3 -I "$helper" listen 4 4 1 > "$out/problem-$1.txt" 2>&1) &
	listener=$!
	sleep 1
	craft "$1"
	wait "$listener"
	cat "$out/problem-$1.txt"
}
problem unknown
grep -q '^frag6: got type=4 code=1 data=40 ' "$out/problem-unknown.txt" && ok "unknown next header: Parameter Problem at 40" ||
    bad "unknown next header"
sleep 1
problem hbh
grep -q '^frag6: got type=4 code=1 data=40 ' "$out/problem-hbh.txt" && ok "hop-by-hop after a fragment: Parameter Problem at 40" ||
    bad "hop-by-hop after a fragment"
before=$(received)
craft nested
sleep 2
[ "$(received)" = "$before" ] && ok "two fragment headers dropped" || bad "two fragment headers delivered"

# Neighbor Solicitations, plain then in an atomic fragment: only the plain one is answered.
for kind in plain fragment; do
	(in_ns python3 -I "$helper" listen 3 136 0 > "$out/ns-$kind.txt" 2>&1) &
	listener=$!
	sleep 1
	craft "ns-$kind"
	wait "$listener"
	cat "$out/ns-$kind.txt"
done
grep -q "^frag6: got type=136 code=0 .* target=$there" "$out/ns-plain.txt" && ok "plain NS answered" || bad "plain NS"
grep -q "target=$there" "$out/ns-fragment.txt" && bad "NS in a fragment answered" || ok "NS in a fragment not answered"

echo "--- 5. ping -s 3000 (not judged)"
in_ns ping -6 -c 2 -W 3 -s 3000 "$there" > "$out/ping-3000.txt" 2>&1
sed 's/^/not judged: /' "$out/ping-3000.txt"

echo "--- 6. the guest"
guest 'uname -s' | grep -q zedBSD && ok "guest answers SSH" || bad "guest answers SSH"
guest "cat /tmp/udp6.log" > "$out/udp6.log"
cat "$out/udp6.log"

[ $status = 0 ] && echo "qemu-frag: PASS" || echo "qemu-frag: FAIL"
exit $status
