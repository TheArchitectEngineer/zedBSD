#!/bin/sh
# WS307 p005: diskpart batch, native UFS and mkswap on a partition, in a guest, on a disk of 512-byte blocks and one
# of 4096.  Boots IMAGE (an SSH guest image) with two blank 512 MiB NVMe disks and runs
# plan/ws307/tests/p005-batch-guest-steps.sh.  The summary is the steps' lines and "p005-batch: PASS|FAIL OUT".
# Output goes to a new build/ws307-test/p005-batch.*.
#   sh plan/ws307/tests/p005-batch.sh IMAGE
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname "$0")/../../.."
image=${1:?usage: p005-batch.sh IMAGE}
mkdir -p build/ws307-test
out=$(mktemp -d build/ws307-test/p005-batch.XXXXXX)
export GUEST_RUNTIME="$(pwd)/$out/guest"
truncate -s 512M "$out/d512.img" "$out/d4k.img"
cp --sparse=always "$image" "$out/boot.img"
g() { timeout 1200 python3 plan/tools/guest/guest.py "$@"; }
g start "$out/boot.img" --qemu-extra "-drive file=$out/d512.img,if=none,id=b512,format=raw -device nvme,serial=b512,drive=b512 -drive file=$out/d4k.img,if=none,id=b4k,format=raw -device nvme,serial=b4k,drive=b4k,logical_block_size=4096,physical_block_size=4096" > "$out/start.txt" 2>&1
status=0
if g wait --timeout 300 > "$out/wait.txt" 2>&1; then
	g put plan/ws307/tests/p005-batch-guest-steps.sh /tmp/p005-steps.sh > /dev/null 2>&1 || true
	g run sh /tmp/p005-steps.sh > "$out/steps.txt" 2>&1 || true
else
	echo "the guest did not answer" > "$out/steps.txt"
fi
g stop > /dev/null 2>&1 || true
grep -E '^(DISK|STEP|P005)' "$out/steps.txt" || true
grep -q '^P005 DONE' "$out/steps.txt" || status=1
grep -q '^STEP .* fail' "$out/steps.txt" && status=1
[ $status -eq 0 ] && echo "p005-batch: PASS $out" || echo "p005-batch: FAIL $out"
exit $status
