#!/bin/sh
# ws235: the RTL8127 on the RDNA machine (10.0.10.3) under a zedBSD kernel with the RTL8125-family driver, in one run:
# the NVMe's zedBSD kernel is saved, the test kernel put in, zedBSD started once with UEFI BootNext, the driver's
# interface brought up and read over SSH (ifconfig: flags, media, address) with the link partner's side read on this
# host (ethtool of centris's X540 port), then zedBSD rebooted into Debian and the saved kernel put back.
#   sh plan/ws235/tests/rtl-link.sh VMUNIX OUT        (from the top of the tree; the caller holds the RDNA slot's lock)
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
	say "zedBSD: reboot"
	T=10 zs "$zip" "(sleep 1; reboot) > /dev/null 2>&1 &"
else
	say "FAIL: zedBSD did not answer in 300 s"
fi

# Back in Debian, the saved kernel goes back.
sleep 30
end=$(($(date +%s) + 400))
until linux_up; do
	[ "$(date +%s)" -lt $end ] || { say "FAIL: Debian did not come back (power-cycle needed)"; exit 1; }
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
