#!/bin/sh
# WS307 p006: the native UFS write speed on a disk of 512-byte and one of 4096-byte blocks, in one guest.  Boots
# IMAGE (an SSH guest image) with two blank 512 MiB NVMe disks and runs plan/ws307/tests/p006-ufs-speed-steps.sh.
# The summary is the "SPEED" lines and "p006-ufs-speed: DONE|FAIL OUT".  Output goes to a new
# build/ws307-test/p006-speed.*.
#   sh plan/ws307/tests/p006-ufs-speed.sh IMAGE
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname "$0")/../../.."
image=${1:?usage: p006-ufs-speed.sh IMAGE}
mkdir -p build/ws307-test
out=$(mktemp -d build/ws307-test/p006-speed.XXXXXX)
export GUEST_RUNTIME="$(pwd)/$out/guest"
truncate -s 512M "$out/d512.img" "$out/d4k.img"
cp --sparse=always "$image" "$out/boot.img"
g() { timeout 1200 python3 plan/tools/guest/guest.py "$@"; }
g start "$out/boot.img" --qemu-extra "-drive file=$out/d512.img,if=none,id=s512,format=raw -device nvme,serial=s512,drive=s512 -drive file=$out/d4k.img,if=none,id=s4k,format=raw -device nvme,serial=s4k,drive=s4k,logical_block_size=4096,physical_block_size=4096" > "$out/start.txt" 2>&1
status=0
if g wait --timeout 300 > "$out/wait.txt" 2>&1; then
	g put plan/ws307/tests/p006-ufs-speed-steps.sh /tmp/speed-steps.sh > /dev/null 2>&1 || true
	g run sh /tmp/speed-steps.sh > "$out/steps.txt" 2>&1 || true
else
	echo "the guest did not answer" > "$out/steps.txt"
fi
g stop > /dev/null 2>&1 || true
grep -E '^(DISK|SPEED)' "$out/steps.txt" || true
grep -q '^SPEED DONE' "$out/steps.txt" || status=1
[ $status -eq 0 ] && echo "p006-ufs-speed: DONE $out" || echo "p006-ufs-speed: FAIL $out"
exit $status
