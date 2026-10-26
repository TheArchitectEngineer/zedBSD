#!/bin/sh
# q1612: a SystemRescue live USB for the Latitude 5330, to change the NVMe's partitions while the Debian on it is not
# running.  The stick gets one FAT32 partition labelled RESCUE1302 holding the ISO's files (the "copy the files" way of
# SystemRescue, so sysrescue.d stays writable) and sysrescue.d/500-zedbsd-ssh.yaml with this host's public keys for
# root and no firewall.  The partition GUIDs are fixed, so the UEFI entry made once is reused after every rewrite.
#   sh plan/tools/hw5330/5330-live-usb.sh build ISO     check ISO against ISO.sha256 and make LIVE/disk.img
#   sh plan/tools/hw5330/5330-live-usb.sh write         copy LIVE/disk.img to the 5330's Linux and dd it to /dev/sda
#   sh plan/tools/hw5330/5330-live-usb.sh boot          BootNext to the stick once, reboot, wait for root's SSH
#   sh plan/tools/hw5330/5330-live-usb.sh sh 'COMMAND'  run COMMAND as root on the live system
#   sh plan/tools/hw5330/5330-live-usb.sh back          reboot the live system (the boot order starts Debian)
# LIVE is build/hw5330/live (a fresh directory each build; plan/tools/q1-clean.sh removes old ones).  /dev/sda must be
# the removable KIOXIA TransMemory USB disk; anything else is refused.  The boot order is never changed.  Nothing is
# removed on either machine.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname "$0")/../../.."
. plan/tools/fresh-out.sh
HOST=solaris10-man
ZB=10.0.30.3
LIVE=build/hw5330/live
LABEL=RESCUE1302
DISK_GUID=212ab4eb-420a-4bd1-83bb-15869a281ccc
PART_GUID=41826d39-b373-4765-b18a-5ef3b61fee62
SO='-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o ConnectTimeout=5 -o LogLevel=ERROR -o BatchMode=yes'

say() {
	echo "5330-live-usb: $*"
}
lin() {
	timeout "${T:-120}" ssh -o BatchMode=yes -o ConnectTimeout=10 "$HOST" "$1"
}
live() {
	timeout "${T:-120}" ssh $SO "root@$ZB" "$1"
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
	T=1200 timeout 1200 scp -q "$img" "$HOST:ws245/live-sysrescue.img" || { say "FAIL: copy"; exit 1; }
	T=1500 lin 'sudo -n dd if=$HOME/ws245/live-sysrescue.img of=/dev/sda bs=4M conv=fsync status=none && sync && sudo -n partprobe /dev/sda' 2> /dev/null ||
	    { say "FAIL: dd"; exit 1; }
	lin 'sudo -n blkid /dev/sda1'
	say "written ($(sha256sum "$img" | cut -c1-16))"
	;;
boot)
	stick_ok || { say "FAIL: /dev/sda is not the removable TransMemory USB disk"; exit 1; }
	lin "lsblk -no PARTUUID /dev/sda1" | grep -qi "$PART_GUID" || { say "FAIL: the stick does not hold the live image"; exit 1; }
	entry=$(lin "sudo -n efibootmgr" | grep -i "$PART_GUID" | sed -n 's/^Boot\([0-9A-F]\{4\}\).*/\1/p' | head -1)
	if [ -z "$entry" ]; then
		lin 'sudo -n efibootmgr --create-only -d /dev/sda -p 1 -L "SystemRescue USB q1612" -l "\EFI\boot\bootx64.efi"' > /dev/null
		entry=$(lin "sudo -n efibootmgr" | grep -i "$PART_GUID" | sed -n 's/^Boot\([0-9A-F]\{4\}\).*/\1/p' | head -1)
	fi
	[ -n "$entry" ] || { say "FAIL: no UEFI entry for the stick"; exit 1; }
	lin "sudo -n efibootmgr -n $entry > /dev/null && sudo -n systemctl reboot" > /dev/null 2>&1
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
	say "live up after $(($(date +%s) - start + 30)) s: $(T=10 live 'cat /run/archiso/bootmnt/sysresccd/VERSION 2> /dev/null; uname -r' | tr '\n' ' ')"
	;;
sh)
	live "${2:?COMMAND}"
	;;
back)
	T=10 live 'systemctl reboot' > /dev/null 2>&1
	start=$(date +%s)
	until T=8 lin hostname 2> /dev/null | grep -q chaos; do
		if [ $(($(date +%s) - start)) -gt 420 ]; then
			say "FAIL: Linux did not come back in 7 minutes"
			exit 2
		fi
		sleep 5
	done
	say "Debian up after $(($(date +%s) - start)) s"
	;;
*)
	echo "usage: 5330-live-usb.sh build ISO | write | boot | sh COMMAND | back" >&2
	exit 1
	;;
esac
