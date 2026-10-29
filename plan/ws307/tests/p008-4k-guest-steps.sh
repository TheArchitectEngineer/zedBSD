#!/bin/sh
# Runs in the guest (as root, through plan/ws307/tests/p008-4k-boot.sh): on the one blank disk of 4096-byte blocks,
# the installed system's form made by hand -- a GPT of an ESP (512 MiB, FAT32 of 4096-byte sectors) holding only the
# loader and zedbsd.cfg, and a root (native UFS of 4 KiB fragments, PARTUUID ROOT_UUID) holding a copy of this
# guest's root with the kernel and the logo in /boot.  /tmp/p008k holds BOOTX64.EFI, vmunix and logo.ppm.  Each
# step prints "STEP name ok|fail"; the last line is "P008K DONE".
#   sh p008-4k-guest-steps.sh ROOT_UUID
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
root_uuid=$1
step() { name=$1; shift; if "$@" > /tmp/p008k-step.txt 2>&1; then echo "STEP $name ok"; else echo "STEP $name fail"; cat /tmp/p008k-step.txt; fi; }
listing=$(diskpart --machine list)
disk=$(printf '%s\n' "$listing" | awk -F'\t' '$1 == "device" && $6 == 4096 && $4 == 0 { print $2; exit }')
reg=$(printf '%s\n' "$listing" | awk -F'\t' -v d="$disk" '$1 == "device" && $2 == d { print $3; exit }')
echo "DISK $disk $reg"
init_table() {
	echo "ERASE $disk:$reg" | diskpart init "$disk" 7d2e5f1a-4b3c-4d6e-9f8a-1b2c3d4e5f60 \
		256 131072 C12A7328-F81F-11D2-BA4B-00A0C93EC93B 2e3f4a5b-6c7d-4e8f-9a0b-1c2d3e4f5a6b esp \
		131328 390000 516E7CB6-6ECF-11D6-8FF8-00022D09712B "$root_uuid" root
}
step table init_table
listing=$(diskpart --machine list)
part_reg() { printf '%s\n' "$listing" | awk -F'\t' -v d="$1" '$1 == "device" && $2 == d { print $3; exit }'; }
# A disk named with a final digit (nvme1n1) takes "p" before the partition number (sda1 does not).
case $disk in *[0-9]) prefix=${disk}p ;; *) prefix=$disk ;; esac
esp=${prefix}1
root=${prefix}2
step mkfs-fat32 sh -c "echo 'FORMAT $esp:$(part_reg "$esp")' | mkfs -t fat32 /dev/$esp"
step mkfs-ufs sh -c "echo 'FORMAT $root:$(part_reg "$root")' | mkfs -t ufs --profile=native /dev/$root"
mkdir -p /mnt/p008e /mnt/p008r
step mount-esp mount -t fat /dev/$esp /mnt/p008e
step mount-root mount -t ufs /dev/$root /mnt/p008r
step loader sh -c "mkdir -p /mnt/p008e/EFI/BOOT && cp /tmp/p008k/BOOTX64.EFI /mnt/p008e/EFI/BOOT/BOOTX64.EFI"
write_cfg() {
	printf 'kernel=rootpart:/boot/vmunix\nrootpart=PARTUUID=%s\nlogo=rootpart:/boot/logo.ppm\n' "$root_uuid" \
		> /mnt/p008e/zedbsd.cfg
}
step cfg write_cfg
# What is mounted below the root is not copied: only its mount point is made.
mount > /tmp/p008k-mounts.txt
echo "MOUNTS $(awk '{ print $3 }' /tmp/p008k-mounts.txt | tr '\n' ' ')"
copy_root() {
	for entry in /*; do
		name=${entry#/}
		case "$name" in
		mnt | dev | proc | tmp | shm | run) mkdir -p "/mnt/p008r/$name"; continue ;;
		esac
		if awk -v p="$entry" '$3 == p { found = 1 } END { exit !found }' /tmp/p008k-mounts.txt; then
			mkdir -p "/mnt/p008r/$name"
			continue
		fi
		cp -Rpa "$entry" "/mnt/p008r/" || return 1
	done
	chmod 1777 /mnt/p008r/tmp /mnt/p008r/shm
	mkdir -p /mnt/p008r/boot
	for entry in /boot/*; do
		[ -e "$entry" ] || continue
		if awk -v p="$entry" '$3 == p { found = 1 } END { exit !found }' /tmp/p008k-mounts.txt; then
			mkdir -p "/mnt/p008r/boot/${entry#/boot/}"
		fi
	done
}
step copy-root copy_root
step kernel cp /tmp/p008k/vmunix /mnt/p008r/boot/vmunix
step logo cp /tmp/p008k/logo.ppm /mnt/p008r/boot/logo.ppm
echo "ROOTDF $(df /mnt/p008r | tail -1)"
step sync sync
step umount-root umount /mnt/p008r
step umount-esp umount /mnt/p008e
step final-sync sync
echo "P008K DONE"
