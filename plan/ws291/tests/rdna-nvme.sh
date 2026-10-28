#!/bin/sh
# ws291: zedBSD on the RX 5600 XT machine's own NVMe (p4 zedBSD-nvme-boot, p5 zedBSD-nvme-root, p6 zedBSD-nvme-swap,
# 2026-10-28), written from the host at the top of the tree through the machine's Debian (rdna-nvme-write.sh, run there
# as root), as plan/tools/hw5330/5330-run.sh does for the Latitude 5330.
#   sh plan/ws291/tests/rdna-nvme.sh image IMG [LINE...]   IMG's root, swap, kernel and loader to the NVMe; each LINE
#                                                          is added to p4's zedbsd.cfg; the UEFI entry made when missing
#   sh plan/ws291/tests/rdna-nvme.sh kernel VMUNIX         p4's vmunix becomes VMUNIX (the one before is vmunix.old)
#   sh plan/ws291/tests/rdna-nvme.sh entry                 the UEFI entry "zedBSD NVMe" (its number), made when missing
# Debian must be running.  The boot order is never changed: measure.sh starts zedBSD once with BootNext (ZEDBSD_BOOT).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname "$0")/../../.."
HOST=${DEBIAN:-awe@10.0.10.3}

say() {
	echo "rdna-nvme: $*"
}
lin() {
	timeout "${T:-120}" ssh -o BatchMode=yes -o ConnectTimeout=10 "$HOST" "$1"
}
helper() {
	lin 'mkdir -p ws291' || return 1
	timeout 60 scp -q plan/ws291/tests/rdna-nvme-write.sh plan/tools/hw5330/ufs-grow.py "$HOST:ws291/" || return 1
	lin 'sudo -n sh $HOME/ws291/rdna-nvme-write.sh check' > /dev/null
}
entry() {
	number=$(lin 'sudo -n efibootmgr' | sed -n 's/^Boot\([0-9A-F]\{4\}\)\*\{0,1\} zedBSD NVMe\b.*/\1/p' | head -1)
	if [ -z "$number" ]; then
		lin 'sudo -n efibootmgr --create-only -d /dev/nvme0n1 -p 1 -L "zedBSD NVMe" -l "\EFI\zedbsd\BOOTX64.EFI"' > /dev/null || return 1
		number=$(lin 'sudo -n efibootmgr' | sed -n 's/^Boot\([0-9A-F]\{4\}\)\*\{0,1\} zedBSD NVMe\b.*/\1/p' | head -1)
	fi
	[ -n "$number" ] || return 1
	echo "$number"
}

case "${1:-}" in
image)
	img=${2:?the image}
	shift 2
	helper || { say "FAIL: the NVMe's zedBSD partitions are not as expected"; exit 1; }
	T=1200 timeout 1200 scp -q "$img" "$HOST:ws291/nvme-image.img" || { say "FAIL: copy of the image"; exit 1; }
	lines=
	for line in "$@"; do
		lines="$lines '$line'"
	done
	T=1500 lin "sudo -n sh \$HOME/ws291/rdna-nvme-write.sh image \$HOME/ws291/nvme-image.img $lines" || { say "FAIL: NVMe write"; exit 1; }
	number=$(entry) || { say "FAIL: no UEFI entry"; exit 1; }
	say "image $(sha256sum "$img" | cut -c1-16) on the NVMe, UEFI entry $number"
	;;
kernel)
	kernel=${2:?the kernel}
	helper || { say "FAIL: the NVMe's zedBSD partitions are not as expected"; exit 1; }
	timeout 120 scp -q "$kernel" "$HOST:ws291/vmunix-nvme" || { say "FAIL: copy of the kernel"; exit 1; }
	lin 'sudo -n sh $HOME/ws291/rdna-nvme-write.sh kernel $HOME/ws291/vmunix-nvme' > /dev/null || { say "FAIL: kernel install"; exit 1; }
	say "kernel $(sha256sum "$kernel" | cut -c1-16) on the NVMe"
	;;
entry)
	entry || { say "FAIL: no UEFI entry"; exit 1; }
	;;
*)
	echo "usage: rdna-nvme.sh image IMG [LINE...] | kernel VMUNIX | entry" >&2
	exit 1
	;;
esac
