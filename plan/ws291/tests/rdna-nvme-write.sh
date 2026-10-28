#!/bin/sh
# ws291: puts zedBSD on the RX 5600 XT machine's own NVMe partitions (p4 to p6, 2026-10-28), the same steps as
# plan/tools/hw5330/5330-nvme-write.sh.  rdna-nvme.sh copies this to the machine's Debian and runs it there as root
# (sudo -n sh); it is not run on the build host.
#   sh rdna-nvme-write.sh check                 the three zedBSD partitions are the NVMe's, with the expected labels
#   sh rdna-nvme-write.sh kernel VMUNIX         p4's vmunix becomes VMUNIX, the one before is kept as vmunix.old
#   sh rdna-nvme-write.sh image IMG [LINE...]   the image's root UFS to p5, grown to fill p5 (ufs-grow.py, which
#                                               rdna-nvme.sh copies beside this script), its swap to p6, its vmunix
#                                               and logo to p4 with a zedbsd.cfg naming the NVMe labels, its loader
#                                               to the Debian ESP's \EFI\zedbsd\BOOTX64.EFI; each LINE is added to
#                                               zedbsd.cfg
# The partitions are found by their fixed PARTUUIDs (plan/ws291/phase006/phase.md, the NVMe's zedBSD partitions); p4
# keeps its own FAT volume serial.  Debian's p1 (only EFI\zedbsd),
# p2 and p3 are not written.  Nothing is removed.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
NV_BOOT=/dev/disk/by-partuuid/d335c6ec-77ea-486f-8a95-57f10a367f0a
NV_ROOT=/dev/disk/by-partuuid/b98d1f62-b4da-4a95-b71d-151e14565490
NV_SWAP=/dev/disk/by-partuuid/feff9017-d794-4405-afbe-15f33796e92f
ESP=/boot/efi
WORK=/mnt/zb
IMAGE_MOUNT=/mnt/zbimg

fail() {
	echo "rdna-nvme-write: $*" >&2
	exit 1
}

# Each partition must be on the NVMe (p4 to p6) and carry the label zedBSD's configuration names.
check() {
	for pair in "$NV_BOOT=zedBSD-nvme-boot" "$NV_ROOT=zedBSD-nvme-root" "$NV_SWAP=zedBSD-nvme-swap"; do
		dev=$(readlink -f "${pair%%=*}")
		case "$dev" in
		/dev/nvme0n1p4 | /dev/nvme0n1p5 | /dev/nvme0n1p6) ;;
		*) fail "not an NVMe zedBSD partition: ${pair%%=*} -> $dev" ;;
		esac
		label=$(lsblk -no PARTLABEL "$dev")
		[ "$label" = "${pair#*=}" ] || fail "$dev is labelled $label, not ${pair#*=}"
	done
	mountpoint -q "$ESP" || fail "the Debian ESP is not mounted at $ESP"
	[ "$(findmnt -no SOURCE "$ESP")" = /dev/nvme0n1p1 ] || fail "$ESP is not /dev/nvme0n1p1"
}

mode=${1:-}
case "$mode" in
check)
	check
	;;
kernel)
	kernel=${2:?the kernel}
	check
	mkdir -p "$WORK"
	mount "$NV_BOOT" "$WORK"
	[ -f "$WORK/vmunix" ] && cp "$WORK/vmunix" "$WORK/vmunix.old"
	cp "$kernel" "$WORK/vmunix"
	sync
	umount "$WORK"
	;;
image)
	image=${2:?the image}
	check
	mkdir -p "$WORK" "$IMAGE_MOUNT"
	loop=$(losetup -r -P --show -f "$image")
	trap 'umount "$IMAGE_MOUNT" 2> /dev/null || true; umount "$WORK" 2> /dev/null || true; losetup -d "$loop"' EXIT
	udevadm settle

	# The image's partitions: 1 the ESP, 2 the boot FAT, 3 the root UFS, 4 the swap.
	[ "$(lsblk -no PARTLABEL "${loop}p3")" = zedBSD-root ] || fail "the image's p3 is not zedBSD-root"
	[ "$(lsblk -no PARTLABEL "${loop}p4")" = zedBSD-swap ] || fail "the image's p4 is not zedBSD-swap"
	[ "$(blockdev --getsize64 "${loop}p3")" -le "$(blockdev --getsize64 "$NV_ROOT")" ] || fail "the root does not fit"
	[ "$(blockdev --getsize64 "${loop}p4")" -le "$(blockdev --getsize64 "$NV_SWAP")" ] || fail "the swap does not fit"

	# The root and the swap, whole; then the root's UFS grows to fill p5 (ufs-grow.py beside this script).
	dd if="${loop}p3" of="$NV_ROOT" bs=4M conv=fsync status=none
	dd if="${loop}p4" of="$NV_SWAP" bs=4M conv=fsync status=none
	python3 -I "$(dirname "$0")/ufs-grow.py" "$NV_ROOT"

	# The kernel, the logo and the configuration, on p4's own FAT.
	mount -o ro "${loop}p2" "$IMAGE_MOUNT"
	mount "$NV_BOOT" "$WORK"
	cp "$IMAGE_MOUNT/vmunix" "$WORK/vmunix"
	cp "$IMAGE_MOUNT/logo.ppm" "$WORK/logo.ppm"
	sed -e 's/^rootpart=PARTLABEL=zedBSD-root$/rootpart=PARTLABEL=zedBSD-nvme-root/' \
	    -e 's/^swap0=PARTLABEL=zedBSD-swap$/swap0=PARTLABEL=zedBSD-nvme-swap/' \
	    "$IMAGE_MOUNT/zedbsd.cfg" > "$WORK/zedbsd.cfg"
	grep -qx 'rootpart=PARTLABEL=zedBSD-nvme-root' "$WORK/zedbsd.cfg" || fail "zedbsd.cfg names no NVMe root"
	grep -qx 'swap0=PARTLABEL=zedBSD-nvme-swap' "$WORK/zedbsd.cfg" || fail "zedbsd.cfg names no NVMe swap"
	shift 2
	for line in "$@"; do
		echo "$line" >> "$WORK/zedbsd.cfg"
	done
	umount "$IMAGE_MOUNT"
	umount "$WORK"

	# The loader, on the Debian ESP beside Debian's own.
	mount -o ro "${loop}p1" "$IMAGE_MOUNT"
	mkdir -p "$ESP/EFI/zedbsd"
	cp "$IMAGE_MOUNT/EFI/BOOT/BOOTX64.EFI" "$ESP/EFI/zedbsd/BOOTX64.EFI"
	umount "$IMAGE_MOUNT"
	sync
	;;
*)
	fail "usage: rdna-nvme-write.sh check | kernel VMUNIX | image IMG [LINE...]"
	;;
esac
echo "rdna-nvme-write: $mode done"
