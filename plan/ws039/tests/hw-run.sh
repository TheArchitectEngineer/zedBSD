#!/bin/sh
# One amdrdna run on the RX 5600 XT machine (10.0.10.3): Debian there writes the USB stick and starts zedBSD from it
# once (UEFI BootNext), zedBSD answers over SSH, and the run's evidence is collected.  Debian comes back by itself on
# the next boot, also after a power cycle, because BootNext lasts one boot.
#
#   plan/ws039/tests/hw-run.sh write IMAGE DEVICE    copy IMAGE to Debian and dd it to DEVICE (/dev/sdX; checked to
#                                                   be a USB disk that holds nothing mounted); a new image has a new
#                                                   ESP PARTUUID, so the UEFI entry that points at it is shown, or
#                                                   added (efibootmgr --create-only, BootOrder unchanged)
#   plan/ws039/tests/hw-run.sh boot ENTRY            set BootNext to the UEFI entry ENTRY (4 hex digits) and reboot;
#                                                   refused when ENTRY does not point at the USB stick's ESP (a
#                                                   stale entry leaves the machine with a dark screen)
#   plan/ws039/tests/hw-run.sh collect [OUT]         wait (5 minutes at most) for zedBSD's SSH, then save
#                                                   amdrdnactl status, the amdrdna kernel lines and the VBIOS image
#                                                   into OUT (a new directory below build/tmp by default)
#   plan/ws039/tests/hw-run.sh debian                reboot zedBSD back into Debian
#
# AMDRDNA_HOST (default awe@10.0.10.3) is Debian; AMDRDNA_ZED (default root@10.0.10.3) is zedBSD, reached with the
# guest harness key plan/tmp/guest/id_ed25519.  Nothing is removed.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
repo=$(cd "$(dirname "$0")/../../.." && pwd)
host=${AMDRDNA_HOST:-awe@10.0.10.3}
zed=${AMDRDNA_ZED:-root@10.0.10.3}
key=$repo/plan/tmp/guest/id_ed25519
debian="timeout 600 ssh -o BatchMode=yes -o ConnectTimeout=10 $host"
zedssh="ssh -i $key -o BatchMode=yes -o ConnectTimeout=10 -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null $zed"

case ${1:-} in
write)
	[ $# -eq 3 ] || { echo "usage: hw-run.sh write IMAGE DEVICE" >&2; exit 2; }
	image=$2
	device=$3
	sum=$(sha256sum "$image" | cut -d' ' -f1)
	timeout 900 scp -q "$image" "$host:ws039/hdd-image.img"
	$debian "set -eu
		[ \"\$(sha256sum ws039/hdd-image.img | cut -d' ' -f1)\" = $sum ] || { echo 'image copy differs'; exit 1; }
		[ \"\$(lsblk -dn -o TRAN $device)\" = usb ] || { echo '$device is not a USB disk'; exit 1; }
		if lsblk -n -o MOUNTPOINT $device | grep -q .; then echo '$device holds a mounted file system'; exit 1; fi
		sudo -n dd if=ws039/hdd-image.img of=$device bs=4M conv=fsync status=none
		sudo -n sync
		sudo -n partprobe $device 2>/dev/null || true
		echo written $sum to $device
		esp=\$(sudo -n blkid -s PARTUUID -o value ${device}1)
		if sudo -n efibootmgr | grep -qi \"GPT,\$esp\"; then
			echo the firmware has an entry for the new ESP \$esp:
			sudo -n efibootmgr | grep -i \"GPT,\$esp\"
		else
			sudo -n efibootmgr --create-only --disk $device --part 1 --label 'zedBSD USB' --loader '\\EFI\\Boot\\BootX64.efi' >/dev/null
			echo added an entry for the new ESP \$esp:
			sudo -n efibootmgr | grep -i \"GPT,\$esp\"
		fi"
	;;
boot)
	[ $# -eq 2 ] || { echo "usage: hw-run.sh boot ENTRY" >&2; exit 2; }
	$debian "set -e
		esp=\$(sudo -n blkid -s PARTUUID -o value /dev/disk/by-partlabel/zedBSD\\x20EFI\\x20System 2>/dev/null || true)
		if [ -n \"\$esp\" ] && ! sudo -n efibootmgr | grep -E '^Boot$2' | grep -qi \"GPT,\$esp\"; then
			echo 'Boot$2 does not point at the USB ESP '\$esp'; refusing (hw-run.sh write adds an entry)'
			sudo -n efibootmgr | grep -E '^Boot[0-9A-F]{4}' | grep -i usb || true
			exit 1
		fi
		sudo -n efibootmgr --bootnext $2 >/dev/null && sudo -n efibootmgr | grep -E '^BootNext|^Boot$2' && (sudo -n systemctl reboot &) "
	;;
collect)
	out=${2:-}
	if [ -z "$out" ]; then
		mkdir -p "$repo/build/tmp"
		out=$(mktemp -d "$repo/build/tmp/amdrdna-hw.XXXXXX")
	fi
	mkdir -p "$out"
	tries=0
	until timeout 20 $zedssh uname -a > "$out/uname.txt" 2>/dev/null; do
		tries=$((tries + 1))
		[ $tries -lt 15 ] || { echo "FAIL zedBSD did not answer over SSH in 5 minutes"; exit 1; }
		sleep 20
	done
	timeout 60 $zedssh amdrdnactl status > "$out/status.txt" 2>&1 || true
	timeout 60 $zedssh dmesg > "$out/dmesg.txt" 2>&1 || true
	grep 'amdrdna' "$out/dmesg.txt" > "$out/amdrdna.txt" || true
	timeout 60 $zedssh 'amdrdnactl vbios /tmp/vbios.rom >/dev/null && cat /tmp/vbios.rom' > "$out/vbios.rom" 2>/dev/null || true
	echo "collected in $out"
	cat "$out/status.txt"
	cat "$out/amdrdna.txt"
	;;
debian)
	timeout 60 $zedssh reboot || true
	;;
*)
	echo "usage: hw-run.sh write IMAGE DEVICE | boot ENTRY | collect [OUT] | debian" >&2
	exit 2
	;;
esac
