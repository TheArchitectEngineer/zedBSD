#!/bin/sh
# Runs in the guest (as root, through plan/ws307/tests/p005-batch.sh): on each blank disk (one of 512-byte blocks,
# one of 4096), a GPT with only an ESP, then one diskpart batch (add a root of the unallocated type, add a swap, add
# a third, set the root to UFS with a new PARTUUID and name, delete the third) confirmed once, the reloaded
# partitions checked, native UFS on the root mounted, written and read back after a remount, and mkswap and swapon
# on the swap partition.  Each step prints "STEP SECTOR name ok|fail"; the last line is "P005 DONE".
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
step() { name=$1; shift; if "$@" > /tmp/p005-step.txt 2>&1; then echo "STEP $size $name ok"; else echo "STEP $size $name fail"; cat /tmp/p005-step.txt; fi; }
part_reg() { printf '%s\n' "$listing" | awk -F'\t' -v d="$1" '$1 == "device" && $2 == d { print $3; exit }'; }
part_geometry() { printf '%s\n' "$listing" | awk -F'\t' -v d="$1" '$1 == "device" && $2 == d { print $7 " " $8; exit }'; }
listing=$(diskpart --machine list)
for size in 512 4096; do
	disk=$(printf '%s\n' "$listing" | awk -F'\t' -v s=$size '$1 == "device" && $6 == s && $4 == 0 && $2 != "nvme0n1" { print $2; exit }')
	reg=$(part_reg "$disk")
	echo "DISK $size $disk $reg"
	case $disk in *[0-9]) prefix=${disk}p ;; *) prefix=$disk ;; esac
	mib=$((1048576 / size))
	init_table() {
		echo "ERASE $disk:$reg" | diskpart init "$disk" 1a2b3c4d-5e6f-4708-9a1b-2c3d4e5f6a7$((size / 512)) \
			$mib $((64 * mib)) C12A7328-F81F-11D2-BA4B-00A0C93EC93B 2b3c4d5e-6f70-4819-8a2b-3c4d5e6f7a8$((size / 512)) esp
	}
	step init init_table
	listing=$(diskpart --machine list)
	reg=$(part_reg "$disk")
	run_batch() {
		echo "WRITE $disk:$reg" | diskpart batch "$disk" \
			add 2 $((65 * mib)) $((256 * mib)) 7EF6A3E2-F243-445D-B77A-C37CA9605418 3c4d5e6f-7081-492a-9b3c-4d5e6f7a8b9$((size / 512)) new \
			add 3 $((321 * mib)) $((64 * mib)) 516E7CB5-6ECF-11D6-8FF8-00022D09712B 4d5e6f70-8192-4a3b-8c4d-5e6f7a8b9ca$((size / 512)) swap \
			add 4 $((385 * mib)) $((16 * mib)) EBD0A0A2-B9E5-4433-87C0-68B6B72699C7 5e6f7081-92a3-4b4c-9d5e-6f7a8b9cadb$((size / 512)) temp \
			set 2 516E7CB6-6ECF-11D6-8FF8-00022D09712B 6f708192-a3b4-4c5d-8e6f-7a8b9cadbec$((size / 512)) root 0 \
			delete 4
	}
	step batch run_batch
	listing=$(diskpart --machine list)
	diskpart show "$disk" | sed "s/^/SHOW $size /"
	step root-geometry test "$(part_geometry ${prefix}2)" = "$((256 * mib)) $((65 * mib))"
	step swap-geometry test "$(part_geometry ${prefix}3)" = "$((64 * mib)) $((321 * mib))"
	step third-gone test -z "$(part_geometry ${prefix}4)"
	step root-kind sh -c "diskpart show $disk | grep -i '6f708192-a3b4-4c5d-8e6f-7a8b9cadbec$((size / 512))' | grep -q 'root'"
	step mkfs-ufs sh -c "echo 'FORMAT ${prefix}2:$(part_reg ${prefix}2)' | mkfs -t ufs --profile=native /dev/${prefix}2"
	mkdir -p /mnt/p005-$size
	step mount mount -t ufs /dev/${prefix}2 /mnt/p005-$size
	step write dd if=/dev/urandom of=/mnt/p005-$size/r.bin bs=4096 count=300
	sum=$(cksum < /mnt/p005-$size/r.bin)
	step umount umount /mnt/p005-$size
	step remount mount -t ufs /dev/${prefix}2 /mnt/p005-$size
	step read-back test "$sum" = "$(cksum < /mnt/p005-$size/r.bin)"
	step umount-final umount /mnt/p005-$size
	step mkswap sh -c "echo 'FORMAT ${prefix}3:$(part_reg ${prefix}3)' | mkswap /dev/${prefix}3"
	step swapon swapon /dev/${prefix}3
	step swapoff swapoff /dev/${prefix}3
done
echo "P005 DONE"
