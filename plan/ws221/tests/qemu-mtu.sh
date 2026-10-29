#!/bin/sh
# The MTU interface on a QEMU guest with two usb-net (CDC ECM, frame limit 1514) adapters: ue0 carries SSH, ue1 is on a
# second user network (10.0.3.0/24, its host 10.0.3.2 answers ping).  Builds the image (config-amd64-jumbo.mk) unless
# IMAGE is given and checks, over SSH:
#   1. ifconfig ue0 mtu 9000 and mtu 1279 are refused and ue0 stays at 1500;
#   2. ifconfig ue1 mtu 1400 while up succeeds and SSH (ue0) keeps answering; mtu default gives 1500 back;
#   3. at 1400, ping -s 1372 to 10.0.3.2 comes back at full length and ping -s 1373 fails with Message too long
#      (IPv6: ping -6 -s 1352 / 1353 to fec0::2, reported but not judged when the guest has no address there);
#   4. a non-root ifconfig ue1 mtu 1400 is refused, and net.tcpstats shows the pool's jumbo counts.
#   plan/ws221/tests/qemu-mtu.sh [IMAGE]
# The guest runs in build/ws221-qemu-mtu/run (GUEST_RUNTIME), the image is built in build/ws221-jumbo.  The last line is
# "qemu-mtu: PASS" when every judged check held.  Judged over SSH, never the console.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
image=${1:-}
if [ -z "$image" ]; then
	flock /tmp/zedbsd-image-build.lock timeout 3600 sh plan/tools/guest/test-image.sh plan/ws221/tests/config-amd64-jumbo.mk \
	    build/ws221-jumbo > build/ws221-jumbo.log 2>&1 || { echo "qemu-mtu: FAIL (image build, build/ws221-jumbo.log)"; exit 1; }
	image=build/ws221-jumbo/hdd-image.img
fi
GUEST_RUNTIME=$(pwd)/build/ws221-qemu-mtu/run
export GUEST_RUNTIME
mkdir -p "$GUEST_RUNTIME"
guest() { timeout "${T:-120}" python3 plan/tools/guest/guest.py "$@"; }
run() { guest run "$1" 2>&1 | tr -d '\r'; }
trap 'guest stop > /dev/null 2>&1' EXIT
extra="-netdev user,id=net1,net=10.0.3.0/24,host=10.0.3.2,dhcpstart=10.0.3.15,ipv6-net=fec0:3::/64,ipv6-host=fec0:3::2"
extra="$extra -device usb-net,bus=xhci.0,port=4,id=ecm1,netdev=net1,mac=52:54:00:33:00:02"
guest start --qemu-extra "$extra" "$image" || { echo "qemu-mtu: FAIL (start)"; exit 1; }
T=300 guest wait --timeout 240 || { echo "qemu-mtu: FAIL (no SSH)"; exit 1; }
status=0
check() {
	# check NAME CONDITION-RESULT: prints the line and keeps the failure.
	if [ "$2" = 0 ]; then echo "ok: $1"; else echo "FAIL: $1"; status=1; fi
}
mtu_of() { run "ifconfig $1" | sed -n 's/.*mtu \([0-9]*\).*/\1/p' | head -1; }

echo "--- interfaces"
run 'ifconfig ue0; ifconfig ue1'
# ue1 gets its address from the second network's DHCP; set one when it did not.
if ! run 'ifconfig ue1' | grep -q 'inet 10\.0\.3\.'; then
	run 'ifconfig ue1 inet 10.0.3.15/24; ifconfig ue1 up'
fi

echo "--- 1. limits"
out=$(run 'ifconfig ue0 mtu 9000; echo rc=$?')
echo "$out"
echo "$out" | grep -q 'rc=0' ; check "ue0 mtu 9000 refused" $((1 - $?))
out=$(run 'ifconfig ue0 mtu 1279; echo rc=$?')
echo "$out"
echo "$out" | grep -q 'rc=0' ; check "ue0 mtu 1279 refused" $((1 - $?))
[ "$(mtu_of ue0)" = 1500 ]; check "ue0 stays at 1500" $?

echo "--- 2. change while up"
out=$(run 'ifconfig ue1 mtu 1400; echo rc=$?')
echo "$out"
echo "$out" | grep -q 'rc=0'; check "ue1 mtu 1400" $?
[ "$(mtu_of ue1)" = 1400 ]; check "ue1 shows 1400" $?
run 'uname -s' | grep -q zedBSD; check "SSH answers after the change" $?

echo "--- 3. datagram length at 1400"
out=$(run 'ping -c 2 -W 3 -s 1372 10.0.3.2; echo rc=$?')
echo "$out"
echo "$out" | grep -q '1380 bytes from' && echo "$out" | grep -q 'rc=0'; check "ping -s 1372 full length" $?
out=$(run 'ping -c 1 -W 3 -s 1373 10.0.3.2; echo rc=$?')
echo "$out"
echo "$out" | grep -q 'Message too long'; check "ping -s 1373 Message too long" $?
out=$(run 'ping -6 -c 2 -W 3 -s 1352 fec0:3::2; echo rc=$?; ping -6 -c 1 -W 3 -s 1353 fec0:3::2; echo rc=$?')
echo "$out" | sed 's/^/ipv6 (not judged): /'

echo "--- default"
out=$(run 'ifconfig ue1 mtu default; echo rc=$?')
echo "$out"
[ "$(mtu_of ue1)" = 1500 ]; check "ue1 mtu default gives 1500" $?

echo "--- 4. privilege and tcpstats"
out=$(run "su user -c 'id -u; ifconfig ue1 mtu 1400; echo rc=\$?'")
echo "$out"
echo "$out" | grep -q '^1000$' && echo "$out" | grep -q 'rc=1'; check "non-root mtu refused" $?
[ "$(mtu_of ue1)" = 1500 ]; check "ue1 still 1500 after the refusal" $?
out=$(run 'sysctl net.tcpstats | head -1')
echo "$out"
echo "$out" | grep -q 'jumbo_total=32 .*jumbo_reserve=8'; check "tcpstats jumbo total 32 reserve 8" $?

[ $status = 0 ] && echo "qemu-mtu: PASS" || echo "qemu-mtu: FAIL"
exit $status
