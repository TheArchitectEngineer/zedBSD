#!/bin/sh
# ws299-p001: the NVMe read/write measurement on the Latitude 5330's zedBSD, run as root by nvme-bench-5330.sh.
#   sh bench-zedbsd.sh read | write
# read:  /dev/zero and cold reads of /dev/nvme0n1 at three block sizes, then the curve: CHUNKS reads of CHUNK_MIB MiB each,
#        one after another from OFF_GIB GiB, with the cache and I/O counters after every chunk ("snap" blocks), and
#        a re-read of the last chunk (a cache hit).  Only reads; the NVMe is not written.
# write: a file of WRITE_MIB MiB on the root UFS (p5, zedBSD's own) in 256 MiB pieces and a sync, then, when the
#        swap on p6 (zedBSD's own) can be turned off, WRITE_MIB MiB of zeros onto p6 (the caller puts p6 back from
#        Debian).  Debian's partitions are never written.
# Every timed step prints "step NAME bytes=N" and the /bin/time lines (real/user/sys); the summary is made on the
# build host (summary.py).  Nothing is removed.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
MODE=${1:?read|write}
DEV=/dev/nvme0n1
SWAP=/dev/nvme0n1p6
OFF_GIB=${OFF_GIB:-100}
CHUNK_MIB=${CHUNK_MIB:-256}
CHUNKS=${CHUNKS:-16}
BUDGET=${BUDGET:-900}
WRITE_MIB=${WRITE_MIB:-1024}
WFILE=${WFILE:-/home/kei/ws299-w.bin}

snap() {
	echo "snap $1"
	for name in vfs.bufcache.max_bytes vfs.bufcache.current_bytes vfs.bufcache.stats vfs.cache_memory.stats vfs.io.stats hw.cputimes; do
		sysctl "$name"
	done
	echo "endsnap $1"
}

# step NAME BYTES dd-arguments...: one timed dd.
step() {
	name=$1
	bytes=$2
	shift 2
	echo "step $name bytes=$bytes"
	/bin/time dd "$@" 2>&1 | grep -E '^(real|user|sys) |^dd:|records|time:'
}

echo "bench mode=$MODE"
uname -a
sysctl hw.ncpu
sysctl hw.memory.stats
snap start

case "$MODE" in
read)
	# The read system call alone (no disk): /dev/zero at the same three block sizes, 512 MiB each.
	step zero64k $((512 * 1048576)) if=/dev/zero of=/dev/null bs=64k count=8192
	step zero1m $((512 * 1048576)) if=/dev/zero of=/dev/null bs=1024k count=512
	step zero4m $((512 * 1048576)) if=/dev/zero of=/dev/null bs=4096k count=128
	snap zero

	# Cold reads at three block sizes, 128 MiB each, at offsets the curve does not touch.
	base=$(((OFF_GIB - 3) * 1024))
	step bs64k $((128 * 1048576)) if=$DEV of=/dev/null bs=64k skip=$((base * 16)) count=2048
	step bs1m $((128 * 1048576)) if=$DEV of=/dev/null bs=1024k skip=$((base + 1024)) count=128
	step bs4m $((128 * 1048576)) if=$DEV of=/dev/null bs=4096k skip=$(((base + 2048) / 4)) count=32
	snap bs

	# The curve.
	started=$(date +%s)
	i=0
	while [ "$i" -lt "$CHUNKS" ]; do
		step "chunk$i" $((CHUNK_MIB * 1048576)) if=$DEV of=/dev/null bs=1024k \
		    skip=$((OFF_GIB * 1024 + i * CHUNK_MIB)) count=$CHUNK_MIB
		snap "chunk$i"
		i=$((i + 1))
		if [ $(($(date +%s) - started)) -gt "$BUDGET" ]; then
			echo "budget spent after $i chunks"
			break
		fi
	done

	# The last chunk again, which the cache should hold.
	last=$((i - 1))
	step reread $((CHUNK_MIB * 1048576)) if=$DEV of=/dev/null bs=1024k \
	    skip=$((OFF_GIB * 1024 + last * CHUNK_MIB)) count=$CHUNK_MIB
	snap reread
	;;
write)
	# A file on the root UFS, in 256 MiB pieces, then the sync that puts it on the NVMe.
	i=0
	while [ $((i * 256)) -lt "$WRITE_MIB" ]; do
		step "ufs$i" $((256 * 1048576)) if=/dev/zero of=$WFILE bs=1024k seek=$((i * 256)) count=256 conv=notrunc
		snap "ufs$i"
		i=$((i + 1))
	done
	echo "step ufssync bytes=0"
	/bin/time sync 2>&1 | grep -E '^(real|user|sys) '
	snap ufssync

	# The raw swap partition, when zedBSD lets go of it.
	if swapoff $SWAP; then
		echo "swapoff ok"
		i=0
		while [ $((i * 256)) -lt "$WRITE_MIB" ]; do
			step "raw$i" $((256 * 1048576)) if=/dev/zero of=$SWAP bs=1024k seek=$((i * 256)) count=256
			snap "raw$i"
			i=$((i + 1))
		done
		echo "step rawsync bytes=0"
		/bin/time sync 2>&1 | grep -E '^(real|user|sys) '
		snap rawsync
	else
		echo "swapoff failed: raw write skipped"
	fi
	;;
esac
echo "bench done"
