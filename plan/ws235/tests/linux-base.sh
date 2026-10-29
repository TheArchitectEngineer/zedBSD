#!/bin/sh
# ws235: the Linux reference for the RTL8127 on the RDNA machine (10.0.10.3): Debian with the trixie-backports kernel
# and firmware-realtek (r8169), measured with the same tcp-perf (userland/tests/tcp-perf, built for Linux here)
# against plan/ws221/tests/perf-peer.py on this host.  The caller holds the RDNA slot's lock.
#   sh plan/ws235/tests/linux-base.sh survey  OUT    reads only: release, kernels, apt sources, GRUB, network, the NIC
#   sh plan/ws235/tests/linux-base.sh install OUT    the backports kernel and firmware-realtek; GRUB's default stays on
#                                                    the running kernel (GRUB_DEFAULT=saved and grub-set-default)
#   sh plan/ws235/tests/linux-base.sh measure OUT    grub-reboot into the newest kernel in /boot once, the RTL8127's address
#                                                    (NIC_ADDR for the run when it has none: no DHCP client there), a host route to PEER_IP over
#                                                    it, ethtool and the PCIe link, tcp-perf send and recv SECONDS each,
#                                                    then a reboot back to the default kernel, read back with uname -r
# OUT/summary.txt is the result; OUT also keeps the raw outputs.  Nothing is removed.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
stage=${1:?survey, install or measure}
out=${2:?the output directory}
mkdir -p "$out"
DEBIAN=${DEBIAN:-awe@10.0.10.3}
NIC_PCI=${NIC_PCI:-0000:03:00.0}
PEER_IP=${PEER_IP:-10.0.10.2}
PERF_PORT=${PERF_PORT:-5322}
SECONDS_PERF=${SECONDS_PERF:-10}
NIC_ADDR=${NIC_ADDR:-10.0.30.6/16}
PERF_SRC=userland/tests/tcp-perf/main.c
SSHOPT="-o BatchMode=yes -o ConnectTimeout=8 -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR"
summary=$out/summary.txt
touch "$summary"

say() {
	echo "linux-base: $*" | tee -a "$summary"
}
deb() {
	timeout "${T:-60}" ssh $SSHOPT "$DEBIAN" "$@" < /dev/null
}
linux_up() {
	[ "$(T=15 deb uname -s 2> /dev/null)" = Linux ]
}
wait_linux() {
	sleep 30
	end=$(($(date +%s) + 400))
	until linux_up; do
		[ "$(date +%s)" -lt $end ] || return 1
		sleep 5
	done
}
nic_name() {
	deb "ls /sys/bus/pci/devices/$NIC_PCI/net 2> /dev/null | head -1"
}

linux_up || { say "FAIL: Debian does not answer"; exit 1; }
say "stage $stage, running $(deb uname -r)"

case $stage in
survey)
	deb "cat /etc/os-release; echo; ls /boot; echo; cat /etc/apt/sources.list /etc/apt/sources.list.d/* 2> /dev/null; echo;
		grep -E '^GRUB_DEFAULT|^GRUB_SAVEDEFAULT' /etc/default/grub; sudo -n grub-editenv list; echo;
		sudo -n grep -E \"^[[:space:]]*(menuentry|submenu) \" /boot/grub/grub.cfg | cut -c1-160; echo;
		systemctl is-active NetworkManager systemd-networkd networking 2> /dev/null; ls /etc/network/interfaces.d 2> /dev/null;
		ip -br addr; ip route; echo; sudo -n lspci -s $NIC_PCI -nnk -vv | grep -E 'Ethernet|Kernel|LnkCap:|LnkSta:'" \
		> "$out/survey.txt" 2>&1
	say "survey: exit $? (survey.txt)"
	say "backports in sources: $(grep -c 'trixie-backports' "$out/survey.txt")"
	say "GRUB: $(grep -E '^GRUB_DEFAULT|saved_entry|next_entry' "$out/survey.txt" | tr '\n' ' ')"
	say "NIC: $(grep -E 'Ethernet|Kernel|LnkSta:' "$out/survey.txt" | tr -s ' \t' ' ' | tr '\n' ' ')"
	;;
install)
	running=$(deb uname -r)
	# The running kernel's menu entry, so that it stays the default after a newer kernel is added.
	entry=$(deb "sudo -n grep -E \"^[[:space:]]*menuentry \" /boot/grub/grub.cfg" |
		awk -v k="$running" 'index($0, k) && $0 !~ /recovery/ {match($0, /\$menuentry_id_option '"'"'[^'"'"']*'"'"'/); print substr($0, RSTART + 22, RLENGTH - 23); exit}')
	say "running $running, its entry id: ${entry:-none}"
	[ -n "$entry" ] || { say "FAIL: no menu entry for the running kernel"; exit 1; }
	sub=$(deb "sudo -n grep -E \"^submenu \" /boot/grub/grub.cfg" |
		sed -n "s/.*\$menuentry_id_option '\([^']*\)'.*/\1/p" | head -1)
	default="$entry"
	[ -n "$sub" ] && default="$sub>$entry"
	T=900 deb "grep -q trixie-backports /etc/apt/sources.list /etc/apt/sources.list.d/* 2> /dev/null ||
			echo 'deb http://deb.debian.org/debian trixie-backports main contrib non-free non-free-firmware' |
			sudo -n tee /etc/apt/sources.list.d/trixie-backports.list > /dev/null
		sudo -n apt-get update -q &&
		sudo -n DEBIAN_FRONTEND=noninteractive apt-get install -y -q -t trixie-backports linux-image-amd64 firmware-realtek" \
		> "$out/install.txt" 2>&1
	say "apt install: exit $? (install.txt; $(grep -E '^Setting up (linux-image|firmware-realtek)' "$out/install.txt" | tr '\n' ' '))"
	deb "grep -q '^GRUB_DEFAULT=saved' /etc/default/grub || sudo -n sed -i 's/^GRUB_DEFAULT=.*/GRUB_DEFAULT=saved/' /etc/default/grub
		sudo -n update-grub > /dev/null 2>&1 && sudo -n grub-set-default '$default' && sudo -n grub-editenv list;
		grep '^GRUB_DEFAULT' /etc/default/grub; ls /boot | grep vmlinuz" > "$out/grub.txt" 2>&1
	say "GRUB after: exit $? $(tr '\n' ' ' < "$out/grub.txt")"
	;;
measure)
	cc -O2 -Wall -o "$out/tcp-perf" "$PERF_SRC" || { say "FAIL: tcp-perf for Linux"; exit 1; }
	timeout 60 scp $SSHOPT -q "$out/tcp-perf" "$DEBIAN:ws235-tcp-perf" || { say "FAIL: copy of tcp-perf"; exit 1; }
	bpo=$(deb "ls /boot" | sed -n 's/^vmlinuz-\(.*\)$/\1/p' | sort -V | tail -1)
	[ -n "$bpo" ] || { say "FAIL: no backports kernel in /boot"; exit 1; }
	entry=$(deb "sudo -n grep -E \"^[[:space:]]*menuentry \" /boot/grub/grub.cfg" |
		awk -v k="$bpo" 'index($0, k) && $0 !~ /recovery/ {match($0, /\$menuentry_id_option '"'"'[^'"'"']*'"'"'/); print substr($0, RSTART + 22, RLENGTH - 23); exit}')
	sub=$(deb "sudo -n grep -E \"^submenu \" /boot/grub/grub.cfg" | sed -n "s/.*\$menuentry_id_option '\([^']*\)'.*/\1/p" | head -1)
	once="$entry"
	[ -n "$sub" ] && once="$sub>$entry"
	say "backports kernel $bpo, once: $once"
	deb "sudo -n grub-reboot '$once' && (sleep 1; sudo -n systemctl reboot) > /dev/null 2>&1 &"
	wait_linux || { say "FAIL: Debian did not come back (power-cycle needed)"; exit 1; }
	say "booted $(deb uname -r)"
	nic=$(nic_name)
	say "RTL8127 interface: ${nic:-none}"
	if [ -n "$nic" ]; then
		# No ethtool on the machine: the speed and the driver from sysfs.
		deb "sudo -n ip link set $nic up; sleep 8; echo speed \$(cat /sys/class/net/$nic/speed) carrier \$(cat /sys/class/net/$nic/carrier);
			echo driver \$(basename \$(readlink /sys/class/net/$nic/device/driver)); sudo -n dmesg | grep -i -E 'r8169|$nic' | tail -5;
			sudo -n lspci -s $NIC_PCI -vv | grep -E 'LnkCap:|LnkSta:'" > "$out/nic.txt" 2>&1
		say "NIC: $(grep -E '^speed|^driver|LnkSta:|firmware|r8169' "$out/nic.txt" | tr -s ' \t' ' ' | tr '\n' ' ')"
		addr=$(deb "ip -4 -br addr show dev $nic" | awk '{print $3}' | cut -d/ -f1)
		if [ -z "$addr" ]; then
			# No DHCP client on the machine: the address the LAN's DHCP leases to this MAC (zedBSD's rge0 took it),
			# for the run only; it goes with the reboot back to the default kernel.
			deb "sudo -n ip addr add $NIC_ADDR dev $nic" > "$out/addr.txt" 2>&1
			addr=$(deb "ip -4 -br addr show dev $nic" | awk '{print $3}' | cut -d/ -f1)
		fi
		say "RTL8127 address: ${addr:-none}"
		if [ -n "$addr" ]; then
			# Two ports on one subnet: ARP answered only by the port with the address (for the run; a reboot resets it).
			deb "sudo -n sysctl -q -w net.ipv4.conf.all.arp_ignore=1 net.ipv4.conf.all.arp_announce=2;
				sudo -n ip route replace $PEER_IP/32 dev $nic src $addr; ip route get $PEER_IP" > "$out/route.txt" 2>&1
			say "route: $(tr '\n' ' ' < "$out/route.txt")"
			deb "ip -s link" > "$out/counters-before.txt" 2>&1
			: > "$out/peer.log"
			timeout $((4 * SECONDS_PERF + 120)) python3 -I plan/ws221/tests/perf-peer.py "$PEER_IP" "$PERF_PORT" "$out/peer.log" 2 > /dev/null 2>&1 &
			peer=$!
			sleep 2
			for dir in send recv; do
				T=$((SECONDS_PERF + 60)) deb "./ws235-tcp-perf $dir $PEER_IP $PERF_PORT $SECONDS_PERF" > "$out/perf-$dir.txt" 2>&1
				say "tcp-perf $dir: exit $? $(tail -1 "$out/perf-$dir.txt")"
			done
			sleep 2
			kill "$peer" 2> /dev/null
			wait "$peer"
			say "peer: $(tr '\n' ' ' < "$out/peer.log")"
			deb "ip -s link" > "$out/counters-after.txt" 2>&1
			say "counters before: $(grep -A5 -E '^[0-9]+: (enp|enx)' "$out/counters-before.txt" | grep -E '^[0-9]|^ +[0-9]' | tr -s ' ' | tr '\n' ' ')"
			say "counters after: $(grep -A5 -E '^[0-9]+: (enp|enx)' "$out/counters-after.txt" | grep -E '^[0-9]|^ +[0-9]' | tr -s ' ' | tr '\n' ' ')"
		fi
	fi
	say "reboot to the default kernel"
	deb "(sleep 1; sudo -n systemctl reboot) > /dev/null 2>&1 &"
	wait_linux || { say "FAIL: Debian did not come back (power-cycle needed)"; exit 1; }
	say "back on $(deb uname -r)"
	;;
*)
	say "FAIL: unknown stage $stage"
	exit 2
	;;
esac
say "done"
