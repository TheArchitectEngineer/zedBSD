#!/bin/sh
# BUG-075 on amd64 native (UEFI, NVMe, KVM), from the host.
#
#   sh plan/ws073/tests/bug075.sh VMUNIX [OUT]
#
# VMUNIX is this tree's guest kernel (the measurement guest's config).  A
# second NVMe disk carries two FAT32 partitions with 512 B and 4 KiB
# clusters.  The guest runs unlink-read.sh on each FAT (left mounted), on
# the UFS root (/var/tmp) and on /tmp, syncs, and is then stopped without
# an unmount, as a power loss would.  fsck.fat -n then checks each FAT: a
# chain the removal did not free shows as "Reclaimed N unused clusters".
# OUT (default build/ws073-bug075) keeps the images and the output.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
vmunix=${1:?vmunix}
out=${2:-build/ws073-bug075}
here=$(cd "$(dirname "$0")" && pwd)
g="sh $here/g.sh"
mkdir -p "$out"

# The FAT disk: a GPT with two FAT32 partitions of different cluster sizes.
fat=$out/fat-disk.img
rm -f "$fat"
truncate -s 400M "$fat"
printf '%s\n' 'label: gpt' \
	'start=2048, size=131072, type=EBD0A0A2-B9E5-4433-87C0-68B6B72699C7' \
	'start=133120, size=614400, type=EBD0A0A2-B9E5-4433-87C0-68B6B72699C7' |
	/sbin/sfdisk -q "$fat"
for spec in 2048:131072:1:C512 133120:614400:8:C4K; do
	start=${spec%%:*}; rest=${spec#*:}
	size=${rest%%:*}; rest=${rest#*:}
	cluster=${rest%%:*}; label=${rest#*:}
	part=$out/part.img
	rm -f "$part"
	truncate -s $((size * 512)) "$part"
	/sbin/mkfs.fat -F 32 -s "$cluster" -n "$label" "$part" >/dev/null
	dd if="$part" of="$fat" bs=512 seek="$start" conv=notrunc status=none
	rm -f "$part"
done

# The guest image: the full guest with this tree's kernel.
sh "$here/kernel-image.sh" "$vmunix" "$out/native.img" >/dev/null
$g stop >/dev/null 2>&1 || true
$g start "$out/native.img" --qemu-extra \
	"-drive if=none,id=fat,file=$(realpath "$fat"),format=raw -device nvme,serial=zedbsd-fat,drive=fat"
$g wait
$g put "$here/unlink-read.sh" /tmp/unlink-read.sh

status=0
$g run 'sh /tmp/unlink-read.sh /mnt-c512 c512 /dev/nvme1n1p1' || status=1
$g run 'sh /tmp/unlink-read.sh /mnt-c4k c4k /dev/nvme1n1p2' || status=1
$g run 'sh /tmp/unlink-read.sh /var/tmp ufs' || status=1
$g run 'sh /tmp/unlink-read.sh /tmp tmp' || status=1
$g run 'sync'
$g stop

# Each FAT is clean on the host, without the unmount.
for spec in 2048:131072:c512 133120:614400:c4k; do
	start=${spec%%:*}; rest=${spec#*:}
	part=$out/check.img
	dd if="$fat" of="$part" bs=512 skip="$start" count="${rest%%:*}" status=none
	if /sbin/fsck.fat -n "$part" >"$out/fsck-${rest#*:}.txt" 2>&1; then
		echo "PASS host: fsck.fat -n ${rest#*:} clean"
	else
		echo "FAIL host: fsck.fat -n ${rest#*:}"
		status=1
	fi
	rm -f "$part"
done
exit $status
