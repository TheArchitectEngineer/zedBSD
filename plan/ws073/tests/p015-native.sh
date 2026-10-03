#!/bin/sh
# ws073-p015 / BUG-073 on amd64 native (UEFI, NVMe, KVM), from the host.
#
#   sh plan/ws073/tests/p015-native.sh BUILD MOUNT [OUT]
#
# BUILD is where this tree's full guest image (with sshd) is built (kernel-image.sh; ws136-p003: it was a VMUNIX
# put into another tree's image); MOUNT is this
# tree's /sbin/mount (it knows the msdosfs spelling).  A second NVMe disk
# carries three FAT32 partitions with 512 B, 2 KiB and 4 KiB clusters.  The
# guest runs boot-slots.sh native (nothing shown at boot; a runtime
# swapon boot0:swapfile shows /boot/boot0; the file in use is protected),
# fat-claim.sh on each FAT, and a short UFS directory workload on the root
# (the buffer cache is shared).  After the guest stops, fsck.fat -n checks
# each FAT.  OUT (default build/ws073-p015) keeps the images and the output.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
build=${1:?build}
mount_binary=${2:?mount}
out=${3:-build/ws073-p015}
here=$(cd "$(dirname "$0")" && pwd)
g="sh $here/g.sh"
mkdir -p "$out"

# The FAT disk: a GPT with three FAT32 partitions of different cluster sizes.
fat=$out/fat-disk.img
rm -f "$fat"
truncate -s 560M "$fat"
printf '%s\n' 'label: gpt' \
	'start=2048, size=131072, type=EBD0A0A2-B9E5-4433-87C0-68B6B72699C7' \
	'start=133120, size=327680, type=EBD0A0A2-B9E5-4433-87C0-68B6B72699C7' \
	'start=460800, size=614400, type=EBD0A0A2-B9E5-4433-87C0-68B6B72699C7' |
	/sbin/sfdisk -q "$fat"
for spec in 2048:131072:1:C512 133120:327680:4:C2K 460800:614400:8:C4K; do
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
sh "$here/kernel-image.sh" "$build" "$out/native.img" >/dev/null
$g stop >/dev/null 2>&1 || true
$g start "$out/native.img" --qemu-extra \
	"-drive if=none,id=fat,file=$(realpath "$fat"),format=raw -device nvme,serial=zedbsd-fat,drive=fat"
$g wait
$g put "$here/boot-slots.sh" /tmp/boot-slots.sh
$g put "$here/fat-claim.sh" /tmp/fat-claim.sh
$g put "$here/../../tools/ufs/dir-grow.sh" /tmp/dir-grow.sh
$g put "$mount_binary" /tmp/mount
$g run 'chmod 755 /tmp/mount'

status=0
$g run 'MOUNT=/tmp/mount sh /tmp/boot-slots.sh native' || status=1
for spec in 1:c512 2:c2k 3:c4k; do
	$g run "sh /tmp/fat-claim.sh /dev/nvme1n1p${spec%%:*} ${spec#*:}" || status=1
done
$g run 'mkdir -p /var/tmp/dg && export LONG=300 SHORT=600 MOVE=100 GONE=300 && sh /tmp/dir-grow.sh /var/tmp/dg make && sh /tmp/dir-grow.sh /var/tmp/dg verify && rm -r /var/tmp/dg && sync && echo "PASS ufs: dir-grow make, verify, remove"' || status=1
$g run 'sync'
$g stop

# Each FAT is clean on the host.
for spec in 2048:131072:c512 133120:327680:c2k 460800:614400:c4k; do
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
