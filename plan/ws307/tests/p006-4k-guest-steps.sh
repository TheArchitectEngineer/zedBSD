#!/bin/sh
# Runs in the guest (as root, through plan/ws307/tests/p006-4k.sh): on the one disk of 4096-byte blocks, a GPT of
# an ESP (512 MiB: FAT32 of 4096-byte sectors needs 65525 clusters) and a swap partition, FAT32 on the ESP, mounted, written (a file of random bytes and a directory of many
# files), unmounted and mounted again, read back; mkswap and swapon on the swap partition; a swap file made on the
# FAT volume with mkswap, turned on and off.  Each step prints "STEP name ok|fail"; the last line is "P006 DONE".
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
step() { name=$1; shift; if "$@" > /tmp/p006-step.txt 2>&1; then echo "STEP $name ok"; else echo "STEP $name fail"; cat /tmp/p006-step.txt; fi; }
listing=$(diskpart --machine list)
disk=$(printf '%s\n' "$listing" | awk -F'\t' '$1 == "device" && $6 == 4096 && $4 == 0 { print $2; exit }')
reg=$(printf '%s\n' "$listing" | awk -F'\t' -v d="$disk" '$1 == "device" && $2 == d { print $3; exit }')
echo "DISK $disk $reg"
init_table() {
	echo "ERASE $disk:$reg" | diskpart init "$disk" 6c1d4f0e-3a2b-4c5d-8e9f-0a1b2c3d4e5f \
		256 131072 C12A7328-F81F-11D2-BA4B-00A0C93EC93B 1f2e3d4c-5b6a-4798-8a7b-6c5d4e3f2a1b esp \
		131328 32768 516E7CB5-6ECF-11D6-8FF8-00022D09712B 2a3b4c5d-6e7f-4801-9a2b-3c4d5e6f7a8b swap
}
step table init_table
listing=$(diskpart --machine list)
printf '%s\n' "$listing" | awk -F'\t' -v d="$disk" '$1 == "device" && index($2, d) == 1'
part_reg() { printf '%s\n' "$listing" | awk -F'\t' -v d="$1" '$1 == "device" && $2 == d { print $3; exit }'; }
esp=${disk}p1
swp=${disk}p2
step mkfs-fat32 sh -c "echo 'FORMAT $esp:$(part_reg "$esp")' | mkfs -t fat32 /dev/$esp"
mkdir -p /mnt/p006
step mount-fat mount -t fat /dev/$esp /mnt/p006
step write-file dd if=/dev/urandom of=/mnt/p006/random.bin bs=4096 count=777
make_many() { mkdir -p /mnt/p006/many && i=0 && while [ $i -lt 300 ]; do echo "file $i" > /mnt/p006/many/f$i.txt; i=$((i + 1)); done; }
step write-many make_many
before=$(cksum < /mnt/p006/random.bin)
echo "CKSUM $before"
step sync sync
step umount-fat umount /mnt/p006
step remount-fat mount -t fat /dev/$esp /mnt/p006
after=$(cksum < /mnt/p006/random.bin)
step read-back test "$before" = "$after"
step read-many test "$(cat /mnt/p006/many/f299.txt)" = "file 299"
step swapfile-write dd if=/dev/zero of=/mnt/p006/swapfile bs=65536 count=1024
step swapfile-mkswap mkswap /mnt/p006/swapfile
step swapfile-on swapon /mnt/p006/swapfile
swapctl_out=$(swapon -s 2>&1 || true)
echo "SWAPS $swapctl_out"
step swapfile-off swapoff /mnt/p006/swapfile
step umount-final umount /mnt/p006
step mkswap-partition sh -c "echo 'FORMAT $swp:$(part_reg "$swp")' | mkswap /dev/$swp"
step swapon-partition swapon /dev/$swp
step swapoff-partition swapoff /dev/$swp
step final-sync sync
echo "P006 DONE"
