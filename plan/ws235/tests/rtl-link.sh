#!/bin/sh
# ws235: the RTL8127 on the RDNA machine (10.0.10.3) under a zedBSD kernel with the RTL8125-family driver, in one run:
# the NVMe's zedBSD kernel is saved, the test kernel put in, zedBSD started once with UEFI BootNext, the driver's
# interface brought up and read over SSH (ifconfig: flags, media, address) with the link partner's side read on this
# host (ethtool of centris's X540 port), then zedBSD rebooted into Debian and the saved kernel put back.
#   sh plan/ws235/tests/rtl-link.sh VMUNIX OUT        (from the top of the tree; the caller holds the RDNA slot's lock)
# With IPTEST=1 the link also carries IPv4 (zedBSD ZIP, this host PIP on PARTNER, added for the run and removed after):
# ping both ways and an SSH command over the link.
# With LANTEST=1 the address the interface took from the LAN's DHCP is reached from this host instead: ping, an SSH
# command and 100 MB each way over SSH, with the interface counters of IFACE and the control interface before and after.
# LANTEST=2 first takes the control interface CONTROL down and deletes its connected route, so every reply leaves
# through IFACE, and goes on (the reboot too) over IFACE's address; if that fails, Debian does not come back without
# a power cycle, which POWERCYCLE=1 does once with the plug PLUG (the caller holds the slot's lock).
# With PERF=<tcp-perf built for zedBSD> as well (LANTEST=1 or 2), the program is copied to /tmp over IFACE's address and
# run send and recv for SECONDS_PERF seconds each against plan/ws221/tests/perf-peer.py on PEER_IP:PERF_PORT here.
# With PROFILE=1 as well (a kernel built with plan/ws235/tests/config-amd64-rtl8127-profile.mk), each direction is
# bracketed by hw.cputimes and the driver's last profile line, which plan/ws235/tests/profile-summary.py turns into
# the driver's share of the time and each CPU's load (OUT/profile.txt).
# OUT/summary.txt is the result; OUT also keeps the raw outputs.  Nothing is removed.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
kernel=${1:?the test kernel}
out=${2:?the output directory}
mkdir -p "$out"
DEBIAN=${DEBIAN:-awe@10.0.10.3}
ZEDBSD_MAC=${ZEDBSD_MAC:-6c:1f:f7:1a:08:b6}
ZEDBSD_SUBNET=${ZEDBSD_SUBNET:-10.0.30}
ZEDBSD_BOOT=${ZEDBSD_BOOT:-0004}
PARTNER=${PARTNER:-enp94s0f1}
IFACE=${IFACE:-rge0}
IPTEST=${IPTEST:-0}
LANTEST=${LANTEST:-0}
CONTROL=${CONTROL:-ue0}
POWERCYCLE=${POWERCYCLE:-0}
PERF=${PERF:-}
PEER_IP=${PEER_IP:-10.0.10.2}
PERF_PORT=${PERF_PORT:-5321}
SECONDS_PERF=${SECONDS_PERF:-10}
PROFILE=${PROFILE:-0}
PLUG=${PLUG:-AC276E42B252}
ZIP=${ZIP:-192.168.127.2}
PIP=${PIP:-192.168.127.1}
KEY=${KEY:-/home/awe/zedBSD-claude1/plan/tmp/guest/id_ed25519}
NV_BOOT=/dev/disk/by-partuuid/d335c6ec-77ea-486f-8a95-57f10a367f0a
SSHOPT="-o BatchMode=yes -o ConnectTimeout=8 -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR"
summary=$out/summary.txt
: > "$summary"

say() {
	echo "rtl-link: $*" | tee -a "$summary"
}
deb() {
	timeout "${T:-60}" ssh $SSHOPT "$DEBIAN" "$@"
}
zs() {
	timeout "${T:-30}" ssh $SSHOPT -i "$KEY" "root@$1" "$2" < /dev/null
}
linux_up() {
	[ "$(T=15 deb uname -s 2> /dev/null)" = Linux ]
}
zedbsd_ip() {
	i=2
	while [ $i -lt 255 ]; do
		ping -c 1 -W 1 "$ZEDBSD_SUBNET.$i" > /dev/null 2>&1 &
		i=$((i + 1))
	done
	wait
	zip=$(ip neigh | awk -v mac="$ZEDBSD_MAC" '$0 ~ mac && $0 !~ /FAILED/ && $1 ~ /^[0-9.]+$/ {print $1; exit}')
	[ -n "$zip" ] || return 1
	[ "$(T=15 zs "$zip" "uname -s" 2> /dev/null)" = zedBSD ] || return 1
	echo "$zip"
}
partner() {
	sudo -n ethtool "$PARTNER" 2> /dev/null | sed -n 's/^[[:space:]]*\(Speed\|Duplex\|Link detected\): */\1: /p' | tr '\n' ' '
}

linux_up || { say "FAIL: Debian does not answer"; exit 1; }
say "partner before: $(partner)"

# Saves the NVMe's kernel, then puts the test kernel in (the one before stays as vmunix.old too).
deb "sudo -n mkdir -p /mnt/ws235 && sudo -n mount -o ro $NV_BOOT /mnt/ws235 && cp /mnt/ws235/vmunix ws235-vmunix.saved; s=\$?; sudo -n umount /mnt/ws235; exit \$s" ||
	{ say "FAIL: could not save the NVMe kernel"; exit 1; }
T=120 timeout 120 scp $SSHOPT -q "$DEBIAN:ws235-vmunix.saved" "$out/vmunix.saved" || { say "FAIL: could not fetch the saved kernel"; exit 1; }
say "saved NVMe kernel $(sha256sum "$out/vmunix.saved" | cut -c1-16)"
sh plan/ws291/tests/rdna-nvme.sh kernel "$kernel" | tee -a "$summary" || { say "FAIL: kernel install"; exit 1; }

# Starts zedBSD once.
say "Debian: BootNext $ZEDBSD_BOOT, reboot"
deb "sudo -n efibootmgr -q -n $ZEDBSD_BOOT && (sleep 1; sudo -n systemctl reboot) > /dev/null 2>&1 &"
sleep 40
zip=
end=$(($(date +%s) + 300))
while [ "$(date +%s)" -lt $end ]; do
	zip=$(zedbsd_ip) && break
	zip=
	sleep 5
done

if [ -n "$zip" ]; then
	say "zedBSD up at $zip"
	zs "$zip" "uname -a" > "$out/uname.txt" 2>&1
	zs "$zip" "ifconfig -a" > "$out/ifconfig-all.txt" 2>&1
	say "interfaces: $(awk '/^[a-z]/ {printf "%s ", $1}' "$out/ifconfig-all.txt")"
	zs "$zip" "ifconfig $IFACE" > "$out/ifconfig-before.txt" 2>&1
	say "before up: $(tr '\n' ' ' < "$out/ifconfig-before.txt")"
	T=60 zs "$zip" "ifconfig $IFACE up" > "$out/ifconfig-up.txt" 2>&1
	say "ifconfig up: exit $? $(tr '\n' ' ' < "$out/ifconfig-up.txt")"
	for wait in 5 10 20 30; do
		sleep "$wait"
		zs "$zip" "ifconfig $IFACE" > "$out/ifconfig-$wait.txt" 2>&1
		say "after +$wait s: $(tr '\n' ' ' < "$out/ifconfig-$wait.txt")"
		say "partner after +$wait s: $(partner)"
	done
	if [ "$LANTEST" = 1 ] || [ "$LANTEST" = 2 ]; then
		rip=$(awk '$1 == "inet" && $2 !~ /^127\./ {print $2; exit}' "$out/ifconfig-30.txt")
		say "LAN address of $IFACE: ${rip:-none}"
		if [ -n "$rip" ] && [ "$LANTEST" = 2 ]; then
			# A closed device keeps its routes, and the stack sends by the route's device with that
			# device's address: CONTROL's connected route goes too, so the replies take IFACE's.
			zs "$zip" "route show" > "$out/route-before.txt" 2>&1
			say "routes before: $(tr '\n' ' ' < "$out/route-before.txt")"
			cnet=$(awk -v c="$CONTROL" '$NF == c && $1 != "default" && $3 ~ /C/ {print $1; exit}' "$out/route-before.txt")
			say "connected route of $CONTROL: ${cnet:-none}"
			del=
			[ -n "$cnet" ] && del="; route delete -net $cnet -interface $CONTROL"
			T=10 zs "$zip" "(sleep 2; ifconfig $CONTROL down$del) > /dev/null 2>&1 &"
			sleep 8
			zip=$rip
			T=30 zs "$zip" "ifconfig $CONTROL" > "$out/control-down.txt" 2>&1
			say "$CONTROL after down, read over $rip: exit $? $(tr '\n' ' ' < "$out/control-down.txt")"
			T=30 zs "$zip" "route show" > "$out/route-after.txt" 2>&1
			say "routes after: $(tr '\n' ' ' < "$out/route-after.txt")"
		fi
		if [ -n "$rip" ]; then
			zs "$zip" "ifconfig -a" > "$out/counters-before.txt" 2>&1
			say "counters before: $(grep -E '^[a-z]|packets' "$out/counters-before.txt" | tr '\n' ' ')"
			ping -c 200 -i 0.05 -W 1 "$rip" > "$out/ping-lan.txt" 2>&1
			say "ping $rip: exit $? $(tail -2 "$out/ping-lan.txt" | tr '\n' ' ')"
			T=30 zs "$rip" "uname -a" > "$out/ssh-lan.txt" 2>&1
			say "ssh $rip: exit $? $(tr '\n' ' ' < "$out/ssh-lan.txt")"
			t0=$(date +%s.%N)
			bytes=$(T=120 zs "$rip" "dd if=/dev/zero bs=65536 count=1600 2> /dev/null" | wc -c)
			t1=$(date +%s.%N)
			say "from zedBSD over SSH: $bytes bytes in $(echo "$t1 - $t0" | bc) s"
			t0=$(date +%s.%N)
			head -c 104857600 /dev/zero | timeout 120 ssh $SSHOPT -i "$KEY" "root@$rip" "cat > /dev/null"
			rc=$?
			t1=$(date +%s.%N)
			say "to zedBSD over SSH: exit $rc, 104857600 bytes in $(echo "$t1 - $t0" | bc) s"
			if [ -n "$PERF" ]; then
				# tcp-perf both ways against perf-peer.py on this host's LAN address, SECONDS each.
				timeout 60 ssh $SSHOPT -i "$KEY" "root@$rip" "cat > /tmp/tcp-perf && chmod 755 /tmp/tcp-perf" < "$PERF"
				say "tcp-perf copied: exit $?"
				: > "$out/peer.log"
				timeout $((4 * SECONDS_PERF + 120)) python3 -I plan/ws221/tests/perf-peer.py "$PEER_IP" "$PERF_PORT" "$out/peer.log" 2 > /dev/null 2>&1 &
				peer=$!
				sleep 2
				for dir in send recv; do
					[ "$PROFILE" = 1 ] && zs "$rip" "sysctl hw.cputimes; dmesg | grep 'rtl8125: $IFACE profile' | tail -1" > "$out/profile-$dir-before.txt" 2>&1
					# This host's TCP counters (RetransSegs among them) before and after: the peer's side of the run.
					[ "$PROFILE" = 1 ] && grep '^Tcp:' /proc/net/snmp > "$out/snmp-$dir-before.txt"
					# With PROFILE=1, top takes two samples 10 s apart in the middle of the run.
					top=
					if [ "$PROFILE" = 1 ]; then
						(sleep 3; T=40 zs "$rip" "top -b -n 2 -d 10" > "$out/top-$dir.txt" 2>&1) &
						top=$!
					fi
					T=$((SECONDS_PERF + 60)) zs "$rip" "/tmp/tcp-perf $dir $PEER_IP $PERF_PORT $SECONDS_PERF" > "$out/perf-$dir.txt" 2>&1
					say "tcp-perf $dir: exit $? $(tail -1 "$out/perf-$dir.txt")"
					[ -n "$top" ] && wait "$top"
					if [ "$PROFILE" = 1 ]; then
						# The driver logs once a second: the line after the run is at most a second late.
						grep '^Tcp:' /proc/net/snmp > "$out/snmp-$dir-after.txt"
						sleep 2
						zs "$rip" "sysctl hw.cputimes; dmesg | grep 'rtl8125: $IFACE profile' | tail -1" > "$out/profile-$dir-after.txt" 2>&1
						python3 -I plan/ws235/tests/profile-summary.py "$dir" "$SECONDS_PERF" "$out/profile-$dir-before.txt" "$out/profile-$dir-after.txt" > "$out/profile-$dir.txt" 2>&1
						say "profile $dir: $(tr '\n' ' ' < "$out/profile-$dir.txt")"
					fi
				done
				[ "$PROFILE" = 1 ] && zs "$rip" "dmesg | grep rtl8125" > "$out/dmesg-rtl.txt" 2>&1
				sleep 2
				kill "$peer" 2> /dev/null
				wait "$peer"
				say "peer: $(tr '\n' ' ' < "$out/peer.log")"
			fi
			zs "$zip" "ifconfig -a" > "$out/counters-after.txt" 2>&1
			say "counters after: $(grep -E '^[a-z]|packets' "$out/counters-after.txt" | tr '\n' ' ')"
		fi
	fi
	if [ "$IPTEST" = 1 ]; then
		# The partner's address lives only for the run; the trap takes it away on any exit.
		sudo -n ip addr add "$PIP/24" dev "$PARTNER" && trap 'sudo -n ip addr del "$PIP/24" dev "$PARTNER" 2> /dev/null' EXIT
		say "partner address: $(ip -br addr show "$PARTNER")"
		zs "$zip" "ifconfig $IFACE inet $ZIP/24" > "$out/ifconfig-inet.txt" 2>&1
		say "zedBSD inet: exit $? $(tr '\n' ' ' < "$out/ifconfig-inet.txt")"
		sleep 2
		ping -c 5 -W 1 -I "$PARTNER" "$ZIP" > "$out/ping-from-partner.txt" 2>&1
		say "ping from partner: exit $? $(tail -2 "$out/ping-from-partner.txt" | tr '\n' ' ')"
		T=40 zs "$zip" "ping -c 5 $PIP" > "$out/ping-from-zedbsd.txt" 2>&1
		say "ping from zedBSD: exit $? $(tail -2 "$out/ping-from-zedbsd.txt" | tr '\n' ' ')"
		T=30 zs "$ZIP" "uname -a" > "$out/ssh-over-link.txt" 2>&1
		say "ssh over the link: exit $? $(tr '\n' ' ' < "$out/ssh-over-link.txt")"
		zs "$zip" "ifconfig $IFACE" > "$out/ifconfig-after-ip.txt" 2>&1
		say "after IP: $(tr '\n' ' ' < "$out/ifconfig-after-ip.txt")"
		ip neigh show dev "$PARTNER" > "$out/neigh.txt" 2>&1
		say "partner neighbours: $(tr '\n' ' ' < "$out/neigh.txt")"
		sudo -n ip addr del "$PIP/24" dev "$PARTNER"
		trap - EXIT
		say "partner address after removal: $(ip -br addr show "$PARTNER")"
	fi
	say "zedBSD: reboot"
	T=10 zs "$zip" "(sleep 1; reboot) > /dev/null 2>&1 &"
else
	say "FAIL: zedBSD did not answer in 300 s"
fi

# Back in Debian, the saved kernel goes back.
sleep 30
end=$(($(date +%s) + 400))
cycled=0
until linux_up; do
	if [ "$(date +%s)" -ge $end ]; then
		# With POWERCYCLE=1 the caller, holding the slot's lock, lets the run cycle the machine's plug once.
		if [ "$POWERCYCLE" = 1 ] && [ $cycled = 0 ]; then
			say "Debian did not come back: power cycle of plug $PLUG"
			timeout 60 python3 -I plan/tools/switchbot.py cycle "$PLUG" 15 >> "$out/powercycle.txt" 2>&1
			say "power cycle: exit $?"
			cycled=1
			sleep 60
			end=$(($(date +%s) + 400))
			continue
		fi
		say "FAIL: Debian did not come back (power-cycle needed)"
		exit 1
	fi
	sleep 5
done
say "Debian is back"
sh plan/ws291/tests/rdna-nvme.sh kernel "$out/vmunix.saved" | tee -a "$summary" || { say "FAIL: saved kernel not put back"; exit 1; }
back=$(deb "sudo -n mount -o ro $NV_BOOT /mnt/ws235 && sha256sum /mnt/ws235/vmunix | cut -c1-64; sudo -n umount /mnt/ws235")
if [ "$back" = "$(sha256sum "$out/vmunix.saved" | cut -c1-64)" ]; then
	say "NVMe kernel is the saved one again ($(echo "$back" | cut -c1-16))"
else
	say "FAIL: NVMe kernel $back is not the saved one"
fi
say "done"
