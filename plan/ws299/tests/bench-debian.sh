#!/bin/sh
# ws299: the NVMe measurement on the Latitude 5330's Debian, the reference for zedBSD's (bench-zedbsd.sh).
# nvme-bench-5330.sh copies it (and randread-linux) to the 5330 and runs it as root (sudo -n sh).
#   sh bench-debian.sh read            reads of /dev/nvme0n1 inside the data region (below), direct and through the
#                                      page cache, random reads (fio and randread), and the ext4 file w.bin read cold
#   sh bench-debian.sh write           writes onto p6 (zedBSD's swap, discarded before each step and after), and
#                                      the 1 GiB ext4 file $DIR/w.bin written again
#   sh bench-debian.sh p6fresh         discards p6 and writes back only its first 4 KiB (the swap header, kept
#                                      in $DIR/p6-save.img), so a write starts on erased flash and zedBSD's swap
#                                      still configures
#   sh bench-debian.sh p6save          keeps zedBSD's p6 in $DIR/p6-save.img (once; only its header is used)
#   sh bench-debian.sh p7fresh         discards p7 (zedBSD-nvme-bench, the 32 GiB partition for measurements)
#   sh bench-debian.sh p7raw           p7 discarded, P7_MIB MiB of patwrite's data written to it (page cache and
#                                      fsync), then read back cold, through the page cache and direct
#   sh bench-debian.sh p7fs            p7 discarded, an ext4 made on it, a file of P7_MIB MiB written (fsync) and
#                                      read back cold; p7 is left unmounted
#   sh bench-debian.sh p7direct        p7 discarded, P7_MIB MiB written to it with O_DIRECT (directio, 1 MiB requests,
#                                      fsync), read back direct in 1 MiB and 4 MiB requests, and P7_MIB MiB of the
#                                      data region read direct, as bench-zedbsd.sh p7direct does
#   sh bench-debian.sh p7ufs           p7 discarded, then the root UFS of the zedBSD image P7_IMAGE (as 5330-nvme-write.sh
#                                      puts it on p5) written to it and grown to fill it (ufs-grow.py), for zedBSD's
#                                      file measurement on a volume of the kind zedBSD's root is
# The data region: R0_MIB..R0_MIB+R_MIB of the disk lies inside the extents of $DIR/data.bin (12 GiB of random
# bytes on Debian's ext4, never removed), so every read there reads mapped flash, not trimmed blocks that the
# drive answers without reading.  The partition written is found by PARTUUID and its label and number are
# checked; Debian's p1 to p3 are only read (and w.bin written through ext4).  Each step prints "step NAME" and
# its result line.  REST seconds pass before every write step.  Nothing is removed.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
MODE=${1:?read|write|p6fresh|p6save|p7fresh|p7raw|p7fs|p7ufs|p7direct}
DEV=/dev/nvme0n1
P6=/dev/disk/by-partuuid/a26f52a1-fed3-4073-b577-1bc61322882f
HMB=/sys/class/nvme/nvme0/hmb
DIR=${DIR:-/home/awe/ws299}
R0_MIB=${R0_MIB:-25681}
R_MIB=${R_MIB:-8064}
CHUNK_MIB=${CHUNK_MIB:-256}
CHUNKS=${CHUNKS:-16}
REST=${REST:-30}
RAND_SECONDS=${RAND_SECONDS:-15}
RANDREAD=${RANDREAD:-$(dirname "$0")/randread-linux}
PATWRITE=${PATWRITE:-$(dirname "$0")/patwrite-linux}
DIRECTIO=${DIRECTIO:-$(dirname "$0")/directio-linux}
P7=/dev/disk/by-partuuid/1beb91c6-dedf-4af0-b132-4770ccf2954d
P7_MIB=${P7_MIB:-4096}
P7_MNT=/mnt/bench7

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

# p7 must be the NVMe's measurement partition, unmounted and no swap.
p7_check() {
	dev=$(readlink -f "$P7")
	[ "$dev" = /dev/nvme0n1p7 ] || fail "p7 is $dev"
	[ "$(lsblk -no PARTLABEL "$dev")" = zedBSD-nvme-bench ] || fail "p7 is not labelled zedBSD-nvme-bench"
	grep -q "^$dev " /proc/swaps && fail "p7 is a swap"
	findmnt -S "$dev" > /dev/null && fail "p7 is mounted"
	return 0
}

# Erases the whole of p7.
p7_fresh() {
	p7_check
	blkdiscard -f "$P7" || fail "blkdiscard p7"
}

patstep() {
	echo "step $1"
	shift
	"$PATWRITE" "$@"
}

# Erases p6 and puts the swap header back.
p6_fresh() {
	p6_check
	[ -s "$DIR/p6-save.img" ] || fail "no saved p6 (p6save first)"
	blkdiscard "$P6" || fail "blkdiscard"
	dd if="$DIR/p6-save.img" of="$P6" bs=4096 count=1 conv=fsync status=none || fail "header"
	cmp -s -n 4096 "$DIR/p6-save.img" "$P6" || fail "p6 header differs"
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

randstep() {
	echo "step $1"
	shift
	"$RANDREAD" "$@"
}

drop() {
	sync
	echo 3 > /proc/sys/vm/drop_caches
}

mkdir -p "$DIR"
echo "bench mode=$MODE"
case "$MODE" in
p6save)
	p6_check
	if [ -s "$DIR/p6-save.img" ]; then
		echo "p6 already saved"
		exit 0
	fi
	dd if="$P6" of="$DIR/p6-save.img" bs=1M count=1 status=none conv=fsync || fail "save"
	echo "p6 saved"
	exit 0
	;;
p6fresh)
	p6_fresh
	echo "p6 fresh"
	exit 0
	;;
p7fresh)
	p7_fresh
	echo "p7 fresh"
	exit 0
	;;
p7ufs)
	[ -s "${P7_IMAGE:-}" ] || fail "no P7_IMAGE"
	p7_fresh
	loop=$(losetup -f --show -P -r "$P7_IMAGE") || fail "losetup"
	dd if="${loop}p3" of="$P7" bs=4M conv=fsync status=none
	rc=$?
	losetup -d "$loop"
	[ "$rc" = 0 ] || fail "dd of the root UFS"
	python3 -I "$(dirname "$0")/ufs-grow.py" "$P7" || fail "ufs-grow"
	echo "p7 ufs"
	exit 0
	;;
esac

uname -a
nproc
echo "hmb $(cat $HMB)"
cat /sys/block/nvme0n1/queue/max_sectors_kb /sys/block/nvme0n1/queue/read_ahead_kb | tr '\n' ' '
echo
echo "region ${R0_MIB}+${R_MIB} MiB"

case "$MODE" in
read)
	[ -s "$DIR/data.bin" ] || fail "no data.bin: the data region is not there"
	# Random reads first, while nothing of the region is cached; fio and randread, the HMB on and off.
	for hmb in 1 0; do
		echo "$hmb" > $HMB
		echo "hmb $(cat $HMB)"
		common="--filename=$DEV --readonly --direct=1 --ioengine=libaio --runtime=$RAND_SECONDS --time_based"
		common="$common --offset=${R0_MIB}m --size=${R_MIB}m --group_reporting"
		fiostep "f-rand4k-qd1-hmb$hmb" --name=r1 $common --rw=randread --bs=4k --iodepth=1
		fiostep "f-rand4k-qd32-hmb$hmb" --name=r32 $common --rw=randread --bs=4k --iodepth=32
		randstep "r-rand4k-qd1-hmb$hmb" $DEV 4096 1 "$RAND_SECONDS" "$R0_MIB" "$R_MIB" direct
		randstep "r-rand4k-qd32-hmb$hmb" $DEV 4096 32 "$RAND_SECONDS" "$R0_MIB" "$R_MIB" direct
	done
	echo 1 > $HMB
	echo "hmb $(cat $HMB)"
	# The same reads through the page cache, as zedBSD's go through its buffer cache.
	drop
	randstep r-rand4k-qd1-cached $DEV 4096 1 "$RAND_SECONDS" "$R0_MIB" "$R_MIB"
	randstep r-rand4k-qd32-cached $DEV 4096 32 "$RAND_SECONDS" "$R0_MIB" "$R_MIB"

	# The ext4 file the write mode wrote, cold.
	drop
	[ -s "$DIR/w.bin" ] && ddstep ext4-r1m if="$DIR/w.bin" of=/dev/null bs=1M

	# Block sizes, 128 MiB each, direct and then through the page cache.
	ddstep d-bs64k if=$DEV of=/dev/null iflag=direct bs=64k skip=$((R0_MIB * 16)) count=2048
	ddstep d-bs1m if=$DEV of=/dev/null iflag=direct bs=1M skip=$((R0_MIB + 128)) count=128
	ddstep d-bs4m if=$DEV of=/dev/null iflag=direct bs=4M skip=$(((R0_MIB + 256) / 4)) count=32
	drop
	ddstep b-bs64k if=$DEV of=/dev/null bs=64k skip=$((R0_MIB * 16)) count=2048
	ddstep b-bs1m if=$DEV of=/dev/null bs=1M skip=$((R0_MIB + 128)) count=128
	ddstep b-bs4m if=$DEV of=/dev/null bs=4M skip=$(((R0_MIB + 256) / 4)) count=32

	# The curve, direct, then the same blocks through the page cache (zedBSD's dd goes through its buffer cache).
	i=0
	while [ "$i" -lt "$CHUNKS" ]; do
		ddstep "d-chunk$i" if=$DEV of=/dev/null iflag=direct bs=1M skip=$((R0_MIB + 512 + i * CHUNK_MIB)) count=$CHUNK_MIB
		i=$((i + 1))
	done
	drop
	i=0
	while [ "$i" -lt "$CHUNKS" ]; do
		ddstep "b-chunk$i" if=$DEV of=/dev/null bs=1M skip=$((R0_MIB + 512 + i * CHUNK_MIB)) count=$CHUNK_MIB
		i=$((i + 1))
	done

	# fio sequential reads at depth 1 and 32, inside the last 3 GiB of the region.
	common="--filename=$DEV --readonly --direct=1 --ioengine=libaio --runtime=15 --time_based --group_reporting"
	common="$common --offset=$((R0_MIB + 4608))m --size=3072m"
	fiostep f-seq128k-qd1 --name=s1 $common --rw=read --bs=128k --iodepth=1
	fiostep f-seq128k-qd32 --name=s32 $common --rw=read --bs=128k --iodepth=32
	;;
write)
	p6_check
	# p6 in four direct pieces of 256 MiB (the shape of zedBSD's raw pieces), then whole through the page cache.
	p6_fresh
	sleep "$REST"
	i=0
	while [ "$i" -lt 4 ]; do
		ddstep "d-w$i" if=/dev/zero of="$P6" oflag=direct bs=1M seek=$((i * 256)) count=256
		i=$((i + 1))
	done
	p6_fresh
	sleep "$REST"
	ddstep b-w1m if=/dev/zero of="$P6" bs=1M count=1024 conv=fsync
	p6_fresh
	sleep "$REST"
	common="--filename=$P6 --direct=1 --ioengine=libaio --size=1g --group_reporting"
	fiostep f-seqw128k-qd1 --name=w1 $common --rw=write --bs=128k --iodepth=1
	p6_fresh
	sleep "$REST"
	fiostep f-seqw128k-qd32 --name=w32 $common --rw=write --bs=128k --iodepth=32
	p6_fresh

	# The ext4 file, written again (dd truncates it first), as zedBSD's UFS file.
	sleep "$REST"
	ddstep ext4-w1m if=/dev/zero of="$DIR/w.bin" bs=1M count=1024 conv=fsync
	;;
p7raw)
	p7_fresh
	sleep "$REST"
	patstep p7-rawwrite "$P7" "$P7_MIB"
	sleep "$REST"
	drop
	ddstep p7-rawread if="$P7" of=/dev/null bs=1M count="$P7_MIB"
	ddstep p7-rawread-direct if="$P7" of=/dev/null bs=1M count="$P7_MIB" iflag=direct
	;;
p7direct)
	p7_fresh
	sleep "$REST"
	echo "step p7-directwrite"
	"$DIRECTIO" write "$P7" "$P7_MIB" 1024
	echo "step p7-directread"
	"$DIRECTIO" read "$P7" "$P7_MIB" 1024
	echo "step p7-directread4m"
	"$DIRECTIO" read "$P7" "$P7_MIB" 4096
	echo "step data-directread"
	"$DIRECTIO" read $DEV "$P7_MIB" 1024 "$R0_MIB"
	;;
p7fs)
	p7_fresh
	sleep "$REST"
	mkfs.ext4 -q -F -E nodiscard,lazy_itable_init=0,lazy_journal_init=0 "$P7" || fail "mkfs.ext4"
	mkdir -p "$P7_MNT"
	mount "$P7" "$P7_MNT" || fail "mount p7"
	df -k "$P7_MNT" | tail -1
	sleep "$REST"
	patstep p7-fswrite "$P7_MNT/f.bin" "$P7_MIB"
	sleep "$REST"
	drop
	ddstep p7-fsread if="$P7_MNT/f.bin" of=/dev/null bs=1M
	umount "$P7_MNT" || fail "umount p7"
	;;
esac
echo "bench done"
