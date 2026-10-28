#!/bin/sh
# ws291: a SystemRescue live USB for the RX 5600 XT machine (10.0.10.3), to change the NVMe's partitions while the
# Debian on it is not running; the same steps as plan/tools/hw5330/5330-live-usb.sh (the Latitude 5330's).  The stick
# gets one FAT32 partition labelled RESCUE1302 holding the ISO's files (so sysrescue.d stays writable) and
# sysrescue.d/500-zedbsd-ssh.yaml with this host's public keys for root and no firewall.  The partition GUIDs are fixed,
# so the UEFI entry made once is reused after every rewrite.
#   sh plan/ws291/tests/rdna-live-usb.sh build ISO     check ISO against ISO.sha256 and make LIVE/disk.img
#   sh plan/ws291/tests/rdna-live-usb.sh write         copy LIVE/disk.img to the machine's Debian and dd it to /dev/sda
#   sh plan/ws291/tests/rdna-live-usb.sh boot          BootNext to the stick once, reboot, wait for root's SSH
#   sh plan/ws291/tests/rdna-live-usb.sh sh 'COMMAND'  run COMMAND as root on the live system
#   sh plan/ws291/tests/rdna-live-usb.sh back          reboot the live system (the boot order starts Debian)
# The live system takes its address by DHCP on the USB LAN adapter Debian and zedBSD use too (found by its MAC on
# 10.0.10.0/24 and 10.0.30.0/24).  /dev/sda must be the removable KIOXIA TransMemory USB disk; anything else is
# refused.  The boot order is never changed.  Nothing is removed on either machine.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname "$0")/../../.."
. plan/tools/fresh-out.sh
HOST=${DEBIAN:-awe@10.0.10.3}
MAC=${ZEDBSD_MAC:-6c:1f:f7:1a:08:b6}
LIVE=build/ws291/live
LABEL=RESCUE1302
DISK_GUID=b9807865-7e2f-4b47-84f2-feeb17fdc98e
PART_GUID=19743359-c966-4b5c-93a6-7a6a8a260c8f
SO='-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o ConnectTimeout=5 -o LogLevel=ERROR -o BatchMode=yes'

say() {
	echo "rdna-live-usb: $*"
}
lin() {
	timeout "${T:-120}" ssh -o BatchMode=yes -o ConnectTimeout=10 "$HOST" "$1"
}
# The live system's address: the adapter's MAC in the neighbour table, after a sweep of both subnets when missing.
live_ip() {
	ip=$(ip neigh | awk -v mac="$MAC" '$0 ~ mac && $0 !~ /FAILED/ && $1 ~ /^[0-9.]+$/ {print $1; exit}')
	if [ -z "$ip" ]; then
		for net in 10.0.10 10.0.30; do
			i=2
			while [ $i -lt 255 ]; do
				ping -c 1 -W 1 "$net.$i" > /dev/null 2>&1 &
				i=$((i + 1))
			done
		done
		wait
		ip=$(ip neigh | awk -v mac="$MAC" '$0 ~ mac && $0 !~ /FAILED/ && $1 ~ /^[0-9.]+$/ {print $1; exit}')
	fi
	[ -n "$ip" ] || return 1
	echo "$ip"
}
live() {
	ip=$(live_ip) || return 1
	timeout "${T:-120}" ssh $SO "root@$ip" "$1"
}
stick_ok() {
	lin 'test "$(lsblk -dn -o TRAN,RM,MODEL /dev/sda | tr -s " ")" = "usb 1 TransMemory"'
}

case "${1:-}" in
build)
	iso=${2:?the SystemRescue ISO}
	(cd "$(dirname "$iso")" && sha256sum -c --quiet "$(basename "$iso").sha256") || { say "FAIL: sha256 of $iso"; exit 1; }
	fresh_out "$LIVE"
	w=$LIVE
	xorriso -osirrox on -indev "$iso" -extract / "$w/tree" > /dev/null 2>&1 || { say "FAIL: extract"; exit 1; }
	chmod -R u+w "$w/tree"
	grep -q "archisolabel=$LABEL" "$w/tree/boot/grub/grubsrcd.cfg" || { say "FAIL: the ISO does not boot by label $LABEL"; exit 1; }
	sed -i 's/^\(\s*set timeout=\)30$/\15/' "$w/tree/boot/grub/grubsrcd.cfg"
	{
		echo '---'
		echo 'global:'
		echo '    nofirewall: true'
		echo 'sysconfig:'
		echo '    authorized_keys:'
		for k in "$HOME"/.ssh/id_ed25519.pub "$HOME"/.ssh/id_rsa.pub; do
			[ -s "$k" ] && awk -v h="$(hostname)" '{ printf "        \"%s-%s\": \"%s %s\"\n", h, $1, $1, $2 }' "$k"
		done
	} > "$w/tree/sysrescue.d/500-zedbsd-ssh.yaml"
	mib=$(($(du -sm "$w/tree" | cut -f1) + 96))
	truncate -s "${mib}M" "$w/part.img"
	/usr/sbin/mkfs.vfat -F 32 -n "$LABEL" "$w/part.img" > /dev/null || { say "FAIL: mkfs"; exit 1; }
	for e in "$w"/tree/*; do
		mcopy -s -i "$w/part.img" "$e" ::/ || { say "FAIL: mcopy $e"; exit 1; }
	done
	truncate -s "$((mib + 2))M" "$w/disk.img"
	printf 'label: gpt\nlabel-id: %s\nstart=2048, size=%d, type=C12A7328-F81F-11D2-BA4B-00A0C93EC93B, uuid=%s, name="%s"\n' \
	    "$DISK_GUID" $((mib * 2048)) "$PART_GUID" "$LABEL" | /usr/sbin/sfdisk -q "$w/disk.img" || { say "FAIL: sfdisk"; exit 1; }
	dd if="$w/part.img" of="$w/disk.img" bs=1M seek=1 conv=notrunc status=none
	mtype -i "$w/part.img" ::/sysrescue.d/500-zedbsd-ssh.yaml | grep -c ssh- | sed 's/^/keys: /'
	say "built $(readlink -f "$w/disk.img") (${mib} MiB partition, $(sha256sum "$w/disk.img" | cut -c1-16))"
	;;
write)
	img=$LIVE/disk.img
	[ -s "$img" ] || { say "FAIL: no $img (run build)"; exit 1; }
	stick_ok || { say "FAIL: /dev/sda is not the removable TransMemory USB disk"; exit 1; }
	lin 'mkdir -p ws291' || { say "FAIL: mkdir ws291"; exit 1; }
	T=1200 timeout 1200 scp -q "$img" "$HOST:ws291/live-sysrescue.img" || { say "FAIL: copy"; exit 1; }
	T=1500 lin 'sudo -n dd if=$HOME/ws291/live-sysrescue.img of=/dev/sda bs=4M conv=fsync status=none && sync && sudo -n blockdev --rereadpt /dev/sda; sudo -n cmp -n $(stat -c %s $HOME/ws291/live-sysrescue.img) $HOME/ws291/live-sysrescue.img /dev/sda' ||
	    { say "FAIL: dd"; exit 1; }
	lin 'sudo -n blkid /dev/sda1'
	say "written and compared ($(sha256sum "$img" | cut -c1-16))"
	;;
boot)
	stick_ok || { say "FAIL: /dev/sda is not the removable TransMemory USB disk"; exit 1; }
	lin "lsblk -no PARTUUID /dev/sda1" | grep -qi "$PART_GUID" || { say "FAIL: the stick does not hold the live image"; exit 1; }
	entry=$(lin "sudo -n efibootmgr" | grep -i "$PART_GUID" | sed -n 's/^Boot\([0-9A-F]\{4\}\).*/\1/p' | head -1)
	if [ -z "$entry" ]; then
		lin 'sudo -n efibootmgr --create-only -d /dev/sda -p 1 -L "SystemRescue USB ws291" -l "\EFI\boot\bootx64.efi"' > /dev/null
		entry=$(lin "sudo -n efibootmgr" | grep -i "$PART_GUID" | sed -n 's/^Boot\([0-9A-F]\{4\}\).*/\1/p' | head -1)
	fi
	[ -n "$entry" ] || { say "FAIL: no UEFI entry for the stick"; exit 1; }
	lin "sudo -n efibootmgr -n $entry > /dev/null && (sleep 1; sudo -n systemctl reboot) > /dev/null 2>&1 &" > /dev/null 2>&1
	say "BootNext $entry, rebooting"
	sleep 30
	start=$(date +%s)
	until T=10 live 'test -d /run/archiso' 2> /dev/null; do
		if [ $(($(date +%s) - start)) -gt 420 ]; then
			say "FAIL: the live system did not answer in 7 minutes"
			exit 2
		fi
		sleep 5
	done
	say "live up at $(live_ip) after $(($(date +%s) - start + 30)) s: $(T=10 live 'cat /run/archiso/bootmnt/sysresccd/VERSION 2> /dev/null; uname -r' | tr '\n' ' ')"
	;;
sh)
	live "${2:?COMMAND}"
	;;
back)
	T=10 live 'systemctl reboot' > /dev/null 2>&1
	sleep 20
	start=$(date +%s)
	until T=8 lin 'uname -s' 2> /dev/null | grep -q Linux && T=8 lin 'test ! -d /run/archiso' 2> /dev/null; do
		if [ $(($(date +%s) - start)) -gt 420 ]; then
			say "FAIL: Debian did not come back in 7 minutes"
			exit 2
		fi
		sleep 5
	done
	say "Debian up after $(($(date +%s) - start + 20)) s"
	;;
*)
	echo "usage: rdna-live-usb.sh build ISO | write | boot | sh COMMAND | back" >&2
	exit 1
	;;
esac
