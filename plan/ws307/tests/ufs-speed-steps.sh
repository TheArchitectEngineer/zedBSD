#!/bin/sh
# Runs in the guest (as root, through plan/ws307/tests/ufs-speed.sh).  For each blank disk (one of 512-byte blocks,
# one of 4096): a raw write to the disk, then native UFS on the whole disk, mounted, and three timed loads, each ended
# by a sync:
#   raw     dd of MIB MiB of zeros straight to the disk (bs 1 MiB) -- the driver's speed, without the file system
#   big     dd of MIB MiB of zeros to one file (bs 1 MiB) and a sync
#   small   cp -R of a tree of FILES files of 4 KiB in 20 directories (made in /tmp first) and a sync
# then the volume is mounted again and both are read back and compared ("SPEED SECTOR big-readback|small-readback").
# Prints "SPEED SECTOR load ms=N kib_s=N" lines and, for each load, "IO SECTOR load event=N calls=N bytes=N" for each
# counter of vfs.io.stats that moved (the event numbers are include/uapi/io-stats.h's); the last line is "SPEED DONE".
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
MIB=${MIB:-32}
FILES=${FILES:-300}
now() { date +%s%N; }
report() {
	ms=$(( ($3 - $2) / 1000000 ))
	[ $ms -gt 0 ] || ms=1
	echo "SPEED $1 ms=$ms kib_s=$(( $4 * 1000 / ms ))"
	sysctl vfs.io.stats > /tmp/speed-io1
	awk -v label="$1" '
		{ split($2, e, "="); split($3, c, "="); split($4, b, "=") }
		FNR == NR { calls[e[2]] = c[2]; bytes[e[2]] = b[2]; next }
		c[2] != calls[e[2]] { printf "IO %s event=%s calls=%d bytes=%d\n", label, e[2], c[2] - calls[e[2]], b[2] - bytes[e[2]] }
	' /tmp/speed-io0 /tmp/speed-io1
}
mark() { sysctl vfs.io.stats > /tmp/speed-io0; now; }

# The small files' tree, in memory.
rm -rf /tmp/speed-src
mkdir -p /tmp/speed-src
dd if=/dev/urandom of=/tmp/speed-4k bs=4096 count=1 2> /dev/null
d=0
while [ $d -lt 20 ]; do
	mkdir /tmp/speed-src/d$d
	d=$((d + 1))
done
i=0
while [ $i -lt "$FILES" ]; do
	cp /tmp/speed-4k /tmp/speed-src/d$((i % 20))/f$i
	i=$((i + 1))
done

zero_sum=$(dd if=/dev/zero bs=1048576 count=$MIB 2> /dev/null | cksum)
listing=$(diskpart --machine list)
for size in 512 4096; do
	disk=$(printf '%s\n' "$listing" | awk -F'\t' -v s=$size '$1 == "device" && $6 == s && $4 == 0 && $2 ~ /^nvme/ { print $2; exit }')
	reg=$(printf '%s\n' "$listing" | awk -F'\t' -v d="$disk" '$1 == "device" && $2 == d { print $3; exit }')
	echo "DISK $size $disk $reg"
	[ -n "$disk" ] || { echo "SPEED $size nodisk fail"; continue; }

	t0=$(mark)
	dd if=/dev/zero of=/dev/$disk bs=1048576 count=$MIB 2> /dev/null || echo "SPEED $size raw fail"
	sync
	t1=$(now)
	report "$size raw" $t0 $t1 $((MIB * 1024))

	echo "FORMAT $disk:$reg" | mkfs -t ufs --profile=native /dev/$disk > /tmp/speed-mkfs.txt 2>&1 || { echo "SPEED $size mkfs fail"; cat /tmp/speed-mkfs.txt; continue; }
	mkdir -p /mnt/s$size
	mount -t ufs /dev/$disk /mnt/s$size || { echo "SPEED $size mount fail"; continue; }

	t0=$(mark)
	dd if=/dev/zero of=/mnt/s$size/zero.bin bs=1048576 count=$MIB 2> /dev/null || echo "SPEED $size big fail"
	sync
	t1=$(now)
	report "$size big" $t0 $t1 $((MIB * 1024))

	t0=$(mark)
	cp -R /tmp/speed-src /mnt/s$size/src || echo "SPEED $size small fail"
	sync
	t1=$(now)
	report "$size small" $t0 $t1 $((FILES * 4))
	n=$(find /mnt/s$size/src -type f | wc -l)
	echo "SPEED $size small-files $n"

	# What was written reads back the same after the volume is mounted again.
	umount /mnt/s$size
	mount -t ufs /dev/$disk /mnt/s$size || { echo "SPEED $size remount fail"; continue; }
	if [ "$(cksum < /mnt/s$size/zero.bin)" = "$zero_sum" ]; then echo "SPEED $size big-readback ok"; else echo "SPEED $size big-readback fail"; fi
	if diff -r /tmp/speed-src /mnt/s$size/src > /dev/null; then echo "SPEED $size small-readback ok"; else echo "SPEED $size small-readback fail"; fi
	umount /mnt/s$size
done
echo "SPEED DONE"
