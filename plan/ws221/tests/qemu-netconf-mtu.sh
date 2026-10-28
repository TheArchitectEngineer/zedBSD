#!/bin/sh
# The MTU kept in net.conf, on a QEMU guest with two usb-net (CDC ECM, frame limit 1514) adapters: ue0 carries SSH,
# ue1 is on a second user network (10.0.3.0/24, DHCP).  Builds the image (config-amd64-jumbo.mk) unless IMAGE is given
# and checks over SSH, with the net console fed on its standard input:
#   1. interface ue1, dhcp, mtu 1400, commit: net.conf names "mtu: 1400", ue1 is at 1400, SSH keeps answering;
#   2. no mtu, commit: net.conf names no mtu, ue1 is back at 1500;
#   3. mtu 9000, commit: refused by the device, net.conf still names none, ue1 stays at 1500, SSH keeps answering;
#   4. mtu 1300, commit confirmed 1, the console left: ue1 at 1300 and net.conf unchanged; after the minute networkd
#      rolls back to 1500 (the rollback program's MTU line);
#   5. mtu 1400 committed, the guest restarted from its own disk: ue1 at 1400 again (the startup record's MTU);
#   6. ue1's adapter pulled out and put back (QMP device_del, device_add): the new interface gets its DHCP address
#      and the policy gives it 1400 before it comes up.
#   plan/ws221/tests/qemu-netconf-mtu.sh [IMAGE]
# The guest runs in build/ws221-qemu-netconf/run (GUEST_RUNTIME), the image is built in build/ws221-jumbo; the
# restarted disk and the outputs go to build/ws221-qemu-netconf/out (plan/tools/fresh-out.sh).  The last line is
# "qemu-netconf-mtu: PASS" when every check held.  Judged over SSH and QMP, never the console.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
image=${1:-}
if [ -z "$image" ]; then
	flock /tmp/zedbsd-image-build.lock timeout 3600 sh plan/tools/guest/test-image.sh plan/ws221/tests/config-amd64-jumbo.mk \
	    build/ws221-jumbo > build/ws221-jumbo.log 2>&1 || { echo "qemu-netconf-mtu: FAIL (image build, build/ws221-jumbo.log)"; exit 1; }
	image=build/ws221-jumbo/hdd-image.img
fi
. plan/tools/fresh-out.sh
fresh_out build/ws221-qemu-netconf/out
out=build/ws221-qemu-netconf/out
GUEST_RUNTIME=$(pwd)/build/ws221-qemu-netconf/run
export GUEST_RUNTIME
mkdir -p "$GUEST_RUNTIME"
qmp="$GUEST_RUNTIME/qmp.sock"
guest() { timeout "${T:-120}" python3 plan/tools/guest/guest.py "$@"; }
run() { guest run "$1" 2>&1 | tr -d '\r'; }
send() { timeout 40 python3 plan/ws049/tests/qmp-send.py "$qmp" "$@" >> "$out/qmp.txt" 2>&1; }
trap 'guest stop > /dev/null 2>&1' EXIT
mac=52:54:00:33:00:02
extra="-netdev user,id=net1,net=10.0.3.0/24,host=10.0.3.2,dhcpstart=10.0.3.15"
extra="$extra -device usb-net,bus=xhci.0,port=4,id=ecm1,netdev=net1,mac=$mac"
status=0
check() {
	# check NAME CONDITION-RESULT: prints the line and keeps the failure.
	if [ "$2" = 0 ]; then echo "ok: $1"; else echo "FAIL: $1"; status=1; fi
}
# The interface with ue1's MAC, and an interface's MTU.
ue1_name() { run "ifconfig -a" | awk -v mac="$mac" '/^[a-z]+[0-9]+:/ { name = $1; sub(":", "", name) } tolower($0) ~ mac { print name; exit }'; }
mtu_of() { run "ifconfig $1" | sed -n 's/.*mtu \([0-9]*\).*/\1/p' | head -1; }
# ue1's lines of net.conf (its key block, up to the next interface).
conf_ue1() { run 'cat /etc/net.conf' | awk '/^  [a-z]+[0-9]*:/ { on = ($1 == "ue1:") } on'; }
# Feeds the net console the lines given (\n between them); its output goes to the step's file.
console() {
	tag=$1
	shift
	run "printf '$1' | net; echo rc=\$?" > "$out/console-$tag.txt"
	cat "$out/console-$tag.txt"
}
boot() {
	guest start --qemu-extra "$extra" "$1" || { echo "qemu-netconf-mtu: FAIL (start)"; exit 1; }
	T=300 guest wait --timeout 240 || { echo "qemu-netconf-mtu: FAIL (no SSH)"; exit 1; }
}
# Polls up to 40 s for ue1 to hold a 10.0.3.x address; prints its name.
wait_address() {
	i=0
	name=
	while [ $i -lt 20 ]; do
		name=$(ue1_name)
		if [ -n "$name" ] && run "ifconfig $name" | grep -q 'inet 10\.0\.3\.'; then
			echo "$name"
			return 0
		fi
		sleep 2
		i=$((i + 1))
	done
	echo "$name"
	return 1
}
: > "$out/qmp.txt"

boot "$image"
run 'ifconfig -a; cat /etc/net.conf' > "$out/before.txt"

echo "--- 1. mtu 1400 committed"
console mtu1400 'configure\ninterface ue1\ndhcp\nmtu 1400\nexit\ncommit\nend\nexit\n'
conf_ue1 > "$out/conf-1.txt"
grep -q '^    mtu: 1400$' "$out/conf-1.txt"; check "net.conf names mtu 1400 for ue1" $?
[ "$(mtu_of ue1)" = 1400 ]; check "ue1 at 1400" $?
run 'uname -s' | grep -q zedBSD; check "SSH answers after the change" $?

echo "--- 2. no mtu"
console nomtu 'configure\ninterface ue1\nno mtu\nexit\ncommit\nend\nexit\n'
conf_ue1 > "$out/conf-2.txt"
grep -q 'mtu' "$out/conf-2.txt"; check "net.conf names no mtu for ue1" $((1 - $?))
grep -q 'dhcp: true' "$out/conf-2.txt"; check "ue1 still DHCP" $?
[ "$(mtu_of ue1)" = 1500 ]; check "ue1 back at 1500" $?

echo "--- 3. mtu 9000 refused"
console mtu9000 'configure\ninterface ue1\nmtu 9000\nexit\ncommit\nend\ny\nexit\ny\n'
conf_ue1 > "$out/conf-3.txt"
grep -q 'mtu' "$out/conf-3.txt"; check "net.conf still names no mtu" $((1 - $?))
[ "$(mtu_of ue1)" = 1500 ]; check "ue1 stays at 1500" $?
run 'uname -s' | grep -q zedBSD; check "SSH answers after the refusal" $?

echo "--- 4. confirmed commit rolled back by the timer"
console confirmed 'configure\ninterface ue1\nmtu 1300\nexit\ncommit confirmed 1\nend\ny\nexit\ny\n'
[ "$(mtu_of ue1)" = 1300 ]; check "ue1 at 1300 while confirmed" $?
conf_ue1 > "$out/conf-4.txt"
grep -q 'mtu' "$out/conf-4.txt"; check "net.conf not written by the confirmed commit" $((1 - $?))
i=0
while [ $i -lt 30 ] && [ "$(mtu_of ue1)" != 1500 ]; do
	sleep 5
	i=$((i + 1))
done
echo "waited $((i * 5)) s"
[ "$(mtu_of ue1)" = 1500 ]; check "ue1 back at 1500 after the timer's rollback" $?
run 'tail -30 /var/log/messages 2>/dev/null' > "$out/messages-4.txt"

echo "--- 5. restart with mtu 1400"
console restart 'configure\ninterface ue1\nmtu 1400\nexit\ncommit\nend\nexit\n'
[ "$(mtu_of ue1)" = 1400 ]; check "ue1 at 1400 before the restart" $?
run 'sync; sync; poweroff' > /dev/null 2>&1
i=0
while [ $i -lt 30 ] && [ -n "$(python3 -c 'import json,os,sys; s=json.load(open(sys.argv[1])); os.kill(s["pid"],0); print(1)' "$GUEST_RUNTIME/session.json" 2>/dev/null)" ]; do
	sleep 2
	i=$((i + 1))
done
guest stop > /dev/null 2>&1
cp --reflink=auto "$GUEST_RUNTIME/disk.img" "$out/disk-after.img"
boot "$out/disk-after.img"
name=$(wait_address)
got=$?
run 'ifconfig -a; cat /etc/net.conf' > "$out/after-restart.txt"
[ $got = 0 ]; check "ue1 (${name:-none}) has its DHCP address after the restart" $?
[ -n "$name" ] && [ "$(mtu_of "$name")" = 1400 ]; check "ue1 at 1400 after the restart" $?

echo "--- 6. the adapter pulled out and put back"
send device_del '{"id":"ecm1"}'
i=0
while [ $i -lt 15 ] && [ -n "$(ue1_name)" ]; do
	sleep 2
	i=$((i + 1))
done
[ -z "$(ue1_name)" ]; check "ue1 gone" $?
send device_add "{\"driver\":\"usb-net\",\"bus\":\"xhci.0\",\"port\":\"4\",\"netdev\":\"net1\",\"id\":\"ecm1\",\"mac\":\"$mac\"}"
name=$(wait_address)
got=$?
run 'ifconfig -a' > "$out/after-replug.txt"
[ $got = 0 ]; check "the adapter (${name:-none}) has its DHCP address again" $?
[ -n "$name" ] && [ "$(mtu_of "$name")" = 1400 ]; check "the adapter at 1400 again" $?

[ $status = 0 ] && echo "qemu-netconf-mtu: PASS" || echo "qemu-netconf-mtu: FAIL"
exit $status
