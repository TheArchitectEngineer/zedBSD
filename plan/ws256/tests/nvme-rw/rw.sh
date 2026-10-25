#!/bin/sh
# ws256-p024: destructive NVMe block read/write check on the VisionFive 2
# (the user allowed destroying the M.2 SSD's data, 2026-10-26).  Runs on the
# board as root:
#   rw.sh write DEV ROUNDS   write random patterns, read each back, cmp;
#                            prints "case N: BYTES bytes ok|FAIL cksum C"
#   rw.sh verify DEV         read the same regions again (after a power cycle)
#                            and print their cksums, to compare with the last
#                            write round on the host
# Uses only dd, cmp and cksum (the board's base tools); the work files live
# in /tmp (tmpfs) and are overwritten, not removed.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
mode=$1
dev=$2
rounds=${3:-1}
work=/tmp/nvme-rw
mkdir -p "$work"

# case: name block-size seek(in blocks) count
# 1 GiB = 262144 blocks of 4 KiB.  Case 4 is 200 sectors (102400 bytes),
# more than the controller's 128-block transfer, so the driver must split it.
# Case 7 is the last 1 MiB of a 1953525168-sector namespace.
cases='1 512 2097152 1
2 4096 262400 1
3 65536 16416 1
4 102400 10600 1
5 1048576 1040 1
6 1048576 2048 64
7 4096 244190390 256'

run_case() {
	n=$1 bs=$2 seek=$3 count=$4
	bytes=$((bs * count))
	if [ "$mode" = write ]; then
		dd if=/dev/urandom of="$work/p$n" bs="$bs" count="$count" 2>/dev/null
		start=$(date +%s)
		dd if="$work/p$n" of="$dev" bs="$bs" seek="$seek" count="$count" 2>/dev/null ||
			{ echo "case $n: write error FAIL"; return; }
		mid=$(date +%s)
		dd if="$dev" of="$work/r$n" bs="$bs" skip="$seek" count="$count" 2>/dev/null ||
			{ echo "case $n: read error FAIL"; return; }
		end=$(date +%s)
		if cmp -s "$work/p$n" "$work/r$n"; then result=ok; else result=FAIL; fi
		sum=$(cksum < "$work/p$n")
		echo "case $n: $bytes bytes at block $seek x $bs $result cksum $sum write $((mid - start))s read $((end - mid))s"
	else
		sum=$(dd if="$dev" bs="$bs" skip="$seek" count="$count" 2>/dev/null | cksum)
		echo "case $n: $bytes bytes at block $seek x $bs cksum $sum"
	fi
}

round=1
while [ "$round" -le "$rounds" ]; do
	[ "$mode" = write ] && echo "round $round"
	echo "$cases" | while read -r n bs seek count; do run_case "$n" "$bs" "$seek" "$count"; done
	round=$((round + 1))
done
if [ "$mode" = write ]; then
	# One block past the end of the namespace must fail, not wrap.
	if dd if=/dev/zero of="$dev" bs=4096 seek=244190646 count=1 2>/dev/null; then
		echo "past the end: write accepted (FAIL)"
	else
		echo "past the end: write refused (ok)"
	fi
fi
