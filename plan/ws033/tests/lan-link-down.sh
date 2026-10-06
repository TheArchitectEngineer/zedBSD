#!/bin/sh
# BUG-212 / BUG-213 (q780): a wired cable that goes takes its address and its subnet's route with it.
# On the SSH guest (plan/tools/guest/guest.py; image: plan/tools/guest/test-image.sh plan/tools/guest/config-amd64-ssh.mk
# BUILD, built from the commit under test), QEMU's usb-net (CDC ECM) is plugged in on its own user network 10.0.5.0/24
# through QMP, as plan/ws033/tests/lan-hotplug.sh does, and its link is then taken down and up with QMP set_link.
# Judged over SSH only (ifconfig, route show, net show), never the console.
#  1. Plugged in: networkd gives the adapter a 10.0.5.x address by DHCP within 40 s.
#  2. Link down: the interface loses RUNNING within 30 s (the carrier event).  Then, within 20 s more, it holds no
#     10.0.5.x address and the table has no 10.0.5.0 route (before the fix both stayed, BUG-213 and BUG-212).
#     If QEMU's usb-net never tells the guest of the link (RUNNING stays), step 2 is reported "skipped: no carrier
#     event", not failed.
#  3. Link up: a 10.0.5.x address again within 40 s.
#  4. The guest still answers over SSH and networkd is running.
# Each poll's output goes to OUTDIR for the analysis when a step fails.
#   plan/tools/guest/guest.py start IMAGE; plan/tools/guest/guest.py wait
#   plan/ws033/tests/lan-link-down.sh [OUTDIR]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
runtime=${GUEST_RUNTIME:-$PWD/build/guest}
qmp="$runtime/qmp.sock"
out=${1:-build/ws033-lan-link-down}
mkdir -p "$out"
guest() { timeout 60 python3 plan/tools/guest/guest.py run "$1" 2>&1; }
send() { timeout 40 python3 plan/ws049/tests/qmp-send.py "$qmp" "$@" >> "$out/qmp.txt" 2>&1; }
mac=52:54:00:33:00:06
status=0
pass() { echo "$1: ok"; }
fail() { echo "$1: FAILED"; status=1; }
: > "$out/qmp.txt"

# The interface with the adapter's MAC, or nothing.
hot_name() { guest "ifconfig -a" | awk -v mac="$mac" '/^[a-z]+[0-9]+:/ { name = $1; sub(":", "", name) } tolower($0) ~ mac { print name; exit }'; }

# Polls up to 40 s for a 10.0.5.x address on the adapter; each poll is kept.  Prints the interface's name.
wait_address() {
	tag=$1
	i=0
	while [ $i -lt 20 ]; do
		name=$(hot_name)
		if [ -n "$name" ]; then
			guest "ifconfig $name; net show; route show" > "$out/$tag-poll$i.txt"
			if grep -q 'inet 10\.0\.5\.' "$out/$tag-poll$i.txt"; then
				echo "$name"
				return 0
			fi
		fi
		sleep 2
		i=$((i + 1))
	done
	echo "${name:-}"
	return 1
}

# 1. Plugged in.
guest "ifconfig -a; net show; route show" > "$out/before.txt"
send netdev_add '{"type":"user","id":"hotnet","net":"10.0.5.0/24","host":"10.0.5.2","dhcpstart":"10.0.5.15"}'
send device_add "{\"driver\":\"usb-net\",\"bus\":\"xhci.0\",\"netdev\":\"hotnet\",\"id\":\"hotnic\",\"mac\":\"$mac\"}"
name=$(wait_address plug)
[ $? -eq 0 ] && pass plugged-dhcp-address || fail plugged-dhcp-address
echo "interface: ${name:-none}"

# 2. Link down: first the carrier, then the address and the route.
send set_link '{"name":"hotnic","up":false}'
carrier=1
i=0
while [ -n "$name" ] && [ $i -lt 15 ]; do
	guest "ifconfig $name" > "$out/down-carrier$i.txt"
	if ! grep -q 'RUNNING' "$out/down-carrier$i.txt"; then
		carrier=0
		break
	fi
	sleep 2
	i=$((i + 1))
done
if [ -z "$name" ]; then
	fail link-down
elif [ $carrier -eq 1 ]; then
	echo "link-down: skipped: no carrier event (RUNNING stayed for 30 s)"
else
	cleared=0
	i=0
	while [ $i -lt 10 ]; do
		guest "ifconfig $name; route show; net show" > "$out/down-poll$i.txt"
		if ! grep -q 'inet 10\.0\.5\.' "$out/down-poll$i.txt" && ! grep -q '^10\.0\.5\.0' "$out/down-poll$i.txt"; then
			cleared=1
			break
		fi
		sleep 2
		i=$((i + 1))
	done
	[ $cleared -eq 1 ] && pass down-address-and-route-cleared || fail down-address-and-route-cleared
fi

# 3. Link up: an address again.
send set_link '{"name":"hotnic","up":true}'
name=$(wait_address relink)
[ $? -eq 0 ] && pass relinked-dhcp-address || fail relinked-dhcp-address

# 4. The guest answers, and networkd runs.
reply=$(guest 'echo alive; ps -A -o args | grep -c "[n]etworkd"')
printf '%s\n' "$reply" > "$out/after.txt"
printf '%s\n' "$reply" | grep -q '^alive$' && pass ssh-alive || fail ssh-alive
[ "$(printf '%s\n' "$reply" | tail -1)" = "1" ] && pass networkd-alive || fail networkd-alive
send device_del '{"id":"hotnic"}'
send netdev_del '{"id":"hotnet"}'

echo "lan-link-down: status $status (outputs in $out)"
exit $status
