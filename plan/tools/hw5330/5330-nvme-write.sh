#!/bin/sh
# q1612 / WS298: puts zedBSD on the Latitude 5330's own NVMe partitions.  5330-run.sh copies this to the 5330's Debian
# and runs it there as root (sudo -n sh); it is not run on the build host.
#   sh 5330-nvme-write.sh check                 the three zedBSD partitions are the NVMe's, with the expected labels
#   sh 5330-nvme-write.sh kernel VMUNIX         p4's vmunix becomes VMUNIX, the one before is kept as vmunix.old
#   sh 5330-nvme-write.sh image IMG             the image's root UFS to p5, its swap to p6, its vmunix and logo to p4
#                                               with a zedbsd.cfg naming the NVMe labels, its loader to the Debian ESP's
#                                               \EFI\zedbsd\BOOTX64.EFI
# The partitions are found by their fixed PARTUUIDs (plan/tools/hw5330/README.md, "NVMe の zedBSD の区画"); p4 keeps
# its own FAT volume serial, so the loader's boot0= never matches the stick's FAT.  Debian's p1 (only EFI\zedbsd),
# p2 and p3 are not written.  Nothing is removed.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
NV_BOOT=/dev/disk/by-partuuid/d4dbe962-cc37-4f27-8529-bb7446730178
NV_ROOT=/dev/disk/by-partuuid/24c59452-7bc3-42e7-b532-5653d935c325
NV_SWAP=/dev/disk/by-partuuid/a26f52a1-fed3-4073-b577-1bc61322882f
ESP=/boot/efi
WORK=/mnt/zb
IMAGE_MOUNT=/mnt/zbimg

fail() {
	echo "5330-nvme-write: $*" >&2
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

case "${1:-}" in
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

	# The root and the swap, whole.
	dd if="${loop}p3" of="$NV_ROOT" bs=4M conv=fsync status=none
	dd if="${loop}p4" of="$NV_SWAP" bs=4M conv=fsync status=none

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
	fail "usage: 5330-nvme-write.sh check | kernel VMUNIX | image IMG"
	;;
esac
echo "5330-nvme-write: ${1} done"
