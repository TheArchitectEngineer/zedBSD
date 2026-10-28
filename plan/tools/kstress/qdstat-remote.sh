#!/bin/sh
# Runs on the 5330's Debian as root (debian-qdstat.sh copies it): one randread run of /dev/nvme0n1's data region
# with the block layer's deltas around it.
#   sh qdstat-remote.sh RANDREAD SECONDS DEPTH [randread words...]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
RR=$1
SECS=$2
DEPTH=$3
shift 3
STAT=/sys/block/nvme0n1/stat
irqs() {
	grep nvme /proc/interrupts | awk '{ s = 0; for (i = 2; i <= NF; i++) if ($i ~ /^[0-9]+$/) s += $i; print $NF, s }'
}
s0=$(cat $STAT)
irqs > /tmp/qdstat-irq0
t0=$(date +%s%N)
"$RR" /dev/nvme0n1 4096 "$DEPTH" "$SECS" "${R0_MIB:-25681}" "${R_MIB:-8064}" "$@"
t1=$(date +%s%N)
s1=$(cat $STAT)
irqs > /tmp/qdstat-irq1
echo $s0 $s1 $t0 $t1 | awk -v how="$DEPTH $*" '{
	n = (NF - 2) / 2
	reads = $(n + 1) - $1; ticks = $(n + 4) - $4; inq = $(n + 11) - $11; wall = ($(2 * n + 2) - $(2 * n + 1)) / 1e6
	printf "qdstat: %s reads=%d avg_block_us=%.1f inflight=%.2f\n", how, reads, reads ? ticks * 1000 / reads : 0, inq / wall
}'
paste /tmp/qdstat-irq0 /tmp/qdstat-irq1 | awk '{ d = $4 - $2; if (d > 0) printf "qdstat: irq %s %d\n", $1, d }'
