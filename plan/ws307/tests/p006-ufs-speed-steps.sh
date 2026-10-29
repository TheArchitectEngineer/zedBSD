#!/bin/sh
# Runs in the guest (as root, through plan/ws307/tests/p006-ufs-speed.sh): native UFS on the whole of each blank
# disk (one of 512-byte blocks, one of 4096), mounted, then a 16 MiB write and a sync timed on each, and mkswap of
# a 16 MiB file on each.  Prints "SPEED SECTOR step seconds|ok|fail" lines; the last line is "SPEED DONE".
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
listing=$(diskpart --machine list)
for size in 512 4096; do
	disk=$(printf '%s\n' "$listing" | awk -F'\t' -v s=$size '$1 == "device" && $6 == s && $4 == 0 && $2 != "nvme0n1" { print $2; exit }')
	reg=$(printf '%s\n' "$listing" | awk -F'\t' -v d="$disk" '$1 == "device" && $2 == d { print $3; exit }')
	echo "DISK $size $disk $reg"
	echo "FORMAT $disk:$reg" | mkfs -t ufs --profile=native /dev/$disk > /tmp/speed-mkfs.txt 2>&1 || { echo "SPEED $size mkfs fail"; cat /tmp/speed-mkfs.txt; continue; }
	mkdir -p /mnt/s$size
	mount -t ufs /dev/$disk /mnt/s$size || { echo "SPEED $size mount fail"; continue; }
	t0=$(date +%s)
	dd if=/dev/zero of=/mnt/s$size/zero.bin bs=65536 count=256 2> /dev/null
	t1=$(date +%s)
	sync
	t2=$(date +%s)
	dd if=/dev/urandom of=/mnt/s$size/small.bin bs=4096 count=256 2> /dev/null
	sync
	t3=$(date +%s)
	echo "SPEED $size write16m $((t1 - t0))"
	echo "SPEED $size sync $((t2 - t1))"
	echo "SPEED $size write1m+sync $((t3 - t2))"
	dd if=/dev/zero of=/mnt/s$size/swapfile bs=65536 count=256 2> /dev/null
	if mkswap /mnt/s$size/swapfile > /tmp/speed-mkswap.txt 2>&1; then echo "SPEED $size mkswap ok"; else echo "SPEED $size mkswap fail: $(cat /tmp/speed-mkswap.txt)"; fi
	umount /mnt/s$size
done
echo "SPEED DONE"
