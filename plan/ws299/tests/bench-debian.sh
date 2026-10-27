#!/bin/sh
# ws299-p001: the NVMe measurement on the Latitude 5330's Debian, the reference for zedBSD's (bench-zedbsd.sh).
# nvme-bench-5330.sh copies it to the 5330 and runs it as root (sudo -n sh).
#   sh bench-debian.sh read            dd and fio reads of /dev/nvme0n1 (only reads), with the HMB on and off
#   sh bench-debian.sh write           fio and dd writes onto p6 (zedBSD's swap, saved first and put back after), and a
#                                      1 GiB file on the ext4 (one fixed file, $DIR/w.bin, written over each time)
#   sh bench-debian.sh p6save | p6restore   keeps zedBSD's p6 in $DIR/p6-save.img, or writes it back and compares
# The partition written is found by PARTUUID and its label and number are checked; Debian's p1 to p3 are only read.
# Each step prints "step NAME" and its result line.  Nothing is removed.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
MODE=${1:?read|write|p6save|p6restore}
DEV=/dev/nvme0n1
P6=/dev/disk/by-partuuid/a26f52a1-fed3-4073-b577-1bc61322882f
HMB=/sys/class/nvme/nvme0/hmb
DIR=${DIR:-/home/awe/ws299}
OFF_GIB=${OFF_GIB:-100}
CHUNK_MIB=${CHUNK_MIB:-256}
CHUNKS=${CHUNKS:-16}

fail() {
	echo "bench-debian: $*" >&2
	exit 1
}

# p6 must be the NVMe's zedBSD swap and nothing Debian uses.
p6_check() {
	dev=$(readlink -f "$P6")
	[ "$dev" = /dev/nvme0n1p6 ] || fail "p6 is $dev"
	[ "$(lsblk -no PARTLABEL "$dev")" = zedBSD-nvme-swap ] || fail "p6 is not labelled zedBSD-nvme-swap"
	grep -q "^$dev " /proc/swaps && fail "p6 is a Debian swap"
	findmnt -S "$dev" > /dev/null && fail "p6 is mounted"
	return 0
}

ddstep() {
	echo "step $1"
	shift
	dd "$@" 2>&1 | tail -1
}

fiostep() {
	echo "step $1"
	shift
	fio --output-format=json "$@" | python3 -I -c '
import json, sys
job = json.load(sys.stdin)["jobs"][0]
for side in ("read", "write"):
    d = job[side]
    if d["io_bytes"]:
        print("%s bw_kib=%d iops=%d lat_us=%.1f" % (side, d["bw"], d["iops"], d["lat_ns"]["mean"] / 1000.0))
'
}

mkdir -p "$DIR"
echo "bench mode=$MODE"
case "$MODE" in
p6save)
	p6_check
	dd if="$P6" of="$DIR/p6-save.img" bs=1M status=none conv=fsync || fail "save"
	echo "p6 saved $(sha256sum "$DIR/p6-save.img" | cut -c1-16)"
	exit 0
	;;
p6restore)
	p6_check
	[ -s "$DIR/p6-save.img" ] || fail "no saved p6"
	if cmp -s "$DIR/p6-save.img" "$P6"; then
		echo "p6 unchanged"
		exit 0
	fi
	dd if="$DIR/p6-save.img" of="$P6" bs=1M status=none conv=fsync || fail "restore"
	cmp -s "$DIR/p6-save.img" "$P6" || fail "p6 differs after the restore"
	echo "p6 restored"
	exit 0
	;;
esac

uname -a
nproc
echo "hmb $(cat $HMB)"
cat /sys/block/nvme0n1/queue/max_sectors_kb /sys/block/nvme0n1/queue/read_ahead_kb | tr '\n' ' '
echo

case "$MODE" in
read)
	# Block sizes, direct, cold, at offsets the curve does not touch.
	base=$(((OFF_GIB - 3) * 1024))
	ddstep d-bs64k if=$DEV of=/dev/null iflag=direct bs=64k skip=$((base * 16)) count=2048
	ddstep d-bs1m if=$DEV of=/dev/null iflag=direct bs=1M skip=$((base + 1024)) count=128
	ddstep d-bs4m if=$DEV of=/dev/null iflag=direct bs=4M skip=$(((base + 2048) / 4)) count=32

	# The curve, direct, then through the page cache (zedBSD's dd goes through its buffer cache).
	i=0
	while [ "$i" -lt "$CHUNKS" ]; do
		ddstep "d-chunk$i" if=$DEV of=/dev/null iflag=direct bs=1M skip=$((OFF_GIB * 1024 + i * CHUNK_MIB)) count=$CHUNK_MIB
		i=$((i + 1))
	done
	sync
	echo 3 > /proc/sys/vm/drop_caches
	i=0
	while [ "$i" -lt "$CHUNKS" ]; do
		ddstep "b-chunk$i" if=$DEV of=/dev/null bs=1M skip=$(((OFF_GIB + 8) * 1024 + i * CHUNK_MIB)) count=$CHUNK_MIB
		i=$((i + 1))
	done

	# fio with the HMB on and off: sequential 128k at depth 1 and 32, random 4k at depth 1 and 32.
	for hmb in 1 0; do
		echo "$hmb" > $HMB
		echo "hmb $(cat $HMB)"
		common="--filename=$DEV --readonly --direct=1 --ioengine=libaio --runtime=15 --time_based --group_reporting"
		fiostep "f-seq128k-qd1-hmb$hmb" --name=s1 $common --rw=read --bs=128k --iodepth=1 --offset=$((OFF_GIB + 20))g
		fiostep "f-seq128k-qd32-hmb$hmb" --name=s32 $common --rw=read --bs=128k --iodepth=32 --offset=$((OFF_GIB + 40))g
		fiostep "f-rand4k-qd1-hmb$hmb" --name=r1 $common --rw=randread --bs=4k --iodepth=1
		fiostep "f-rand4k-qd32-hmb$hmb" --name=r32 $common --rw=randread --bs=4k --iodepth=32
	done
	echo 1 > $HMB
	echo "hmb $(cat $HMB)"
	;;
write)
	p6_check
	[ -s "$DIR/p6-save.img" ] || fail "p6 is not saved (p6save first)"
	ddstep d-w1m if=/dev/zero of="$P6" oflag=direct bs=1M count=1024
	ddstep b-w1m if=/dev/zero of="$P6" bs=1M count=1024 conv=fsync
	common="--filename=$P6 --direct=1 --ioengine=libaio --runtime=15 --time_based --group_reporting"
	fiostep f-seqw128k-qd1 --name=w1 $common --rw=write --bs=128k --iodepth=1
	fiostep f-seqw128k-qd32 --name=w32 $common --rw=write --bs=128k --iodepth=32
	fiostep f-randw4k-qd1 --name=rw1 $common --rw=randwrite --bs=4k --iodepth=1
	fiostep f-randw4k-qd32 --name=rw32 $common --rw=randwrite --bs=4k --iodepth=32
	ddstep ext4-w1m if=/dev/zero of="$DIR/w.bin" bs=1M count=1024 conv=fsync
	;;
esac
echo "bench done"
