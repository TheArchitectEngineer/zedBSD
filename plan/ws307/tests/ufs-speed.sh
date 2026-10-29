#!/bin/sh
# The native UFS write speed on a 512-byte and a 4096-byte NVMe disk, in one guest: a raw write to each disk, a large
# file and many small files (plan/ws307/tests/ufs-speed-steps.sh).  Boots IMAGE (an image with the harness's SSH, the
# installer's image or a disk image, from USB) with two blank 2 GiB NVMe disks, std VGA.
# The summary is the "DISK" and "SPEED" lines and "ufs-speed: DONE|FAIL OUT".  Output goes to a new
# build/ws307-test/ufs-speed.*.
#   sh plan/ws307/tests/ufs-speed.sh IMAGE
# MIB (32) and FILES (300) size the loads.  CACHE (unsafe) is QEMU's cache mode of the two disks: unsafe drops the
# guest's flushes on the host, so the times show the guest's own work and not the host's fsync (which takes 0.5 to 2 s
# on a busy host); the IO lines count the flushes the guest asked for all the same.  CACHE=writeback keeps them.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname "$0")/../../.."
image=${1:?usage: ufs-speed.sh IMAGE}
cache=${CACHE:-unsafe}
steps=${STEPS:-plan/ws307/tests/ufs-speed-steps.sh}
mkdir -p build/ws307-test
out=$(mktemp -d build/ws307-test/ufs-speed.XXXXXX)
export GUEST_RUNTIME="$(pwd)/$out/guest"
truncate -s 2G "$out/d512.img" "$out/d4k.img"
cp --sparse=always "$image" "$out/boot.img"
g() { timeout 2400 python3 plan/tools/guest/guest.py "$@"; }
g start "$out/boot.img" --disk usb --qemu-extra "-drive file=$out/d512.img,if=none,id=s512,format=raw,cache=$cache -device nvme,serial=s512,drive=s512 -drive file=$out/d4k.img,if=none,id=s4k,format=raw,cache=$cache -device nvme,serial=s4k,drive=s4k,logical_block_size=4096,physical_block_size=4096" > "$out/start.txt" 2>&1
status=0
if g wait --timeout 420 > "$out/wait.txt" 2>&1; then
	g put "$steps" /tmp/ufs-speed-steps.sh > /dev/null 2>&1 || true
	g run "MIB=${MIB:-32} FILES=${FILES:-300} sh /tmp/ufs-speed-steps.sh" > "$out/steps.txt" 2>&1 || true
else
	echo "the guest did not answer" > "$out/steps.txt"
fi
g stop > /dev/null 2>&1 || true
# The 512-byte disk's volume checked on the host the way fsck would (the checker reads 1 KiB fragments only).
if timeout 600 python3 plan/tools/ufs/check-volume.py "$out/d512.img" > "$out/check512.txt" 2>&1; then
	echo "SPEED 512 host-check ok" >> "$out/steps.txt"
else
	echo "SPEED 512 host-check fail" >> "$out/steps.txt"
fi
grep -E '^(DISK|SPEED|IO)' "$out/steps.txt" || true
grep -q '^SPEED DONE' "$out/steps.txt" || status=1
grep -q 'fail' "$out/steps.txt" && status=1
[ $status -eq 0 ] && echo "ufs-speed: DONE $out" || echo "ufs-speed: FAIL $out"
exit $status
