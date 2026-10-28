#!/bin/sh
# WS307 p006: the kernel on a disk of 4096-byte blocks.  Boots IMAGE (an SSH guest image,
# plan/tools/guest/build-ssh-image.sh) with a second, empty NVMe disk of 4096-byte logical and physical blocks,
# runs plan/ws307/tests/p006-4k-guest-steps.sh in it (GPT, FAT32 mounted, written and read back, swap partition and
# swap file), stops the guest and reads the FAT volume back on the host (fsck.fat when present, mtools).  The
# summary is the steps' lines and "p006-4k: PASS|FAIL OUT".  Output goes to a new build/ws307-test/p006-4k.*.
#   sh plan/ws307/tests/p006-4k.sh IMAGE
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname "$0")/../../.."
image=${1:?usage: p006-4k.sh IMAGE}
mkdir -p build/ws307-test
out=$(mktemp -d build/ws307-test/p006-4k.XXXXXX)
export GUEST_RUNTIME="$(pwd)/$out/guest"
truncate -s 1G "$out/disk4k.img"
cp --sparse=always "$image" "$out/boot.img"
g() { timeout 900 python3 plan/tools/guest/guest.py "$@"; }
g start "$out/boot.img" --qemu-extra "-drive file=$out/disk4k.img,if=none,id=p006k,format=raw -device nvme,serial=p006k,drive=p006k,logical_block_size=4096,physical_block_size=4096" > "$out/start.txt" 2>&1
status=0
if g wait --timeout 300 > "$out/wait.txt" 2>&1; then
	g put plan/ws307/tests/p006-4k-guest-steps.sh /tmp/p006-steps.sh > /dev/null 2>&1 || true
	g run sh /tmp/p006-steps.sh > "$out/steps.txt" 2>&1 || true
else
	echo "p006-4k: the guest did not answer" > "$out/steps.txt"
fi
g stop > /dev/null 2>&1 || true
grep -E '^(DISK|STEP|CKSUM|P006)' "$out/steps.txt" || true
grep -q '^P006 DONE' "$out/steps.txt" || status=1
grep -q '^STEP .* fail' "$out/steps.txt" && status=1
# The FAT volume read on the host: it starts 1 MiB in.
python3 - "$out/disk4k.img" > "$out/esp.offset" <<'PY'
import sys
print(256 * 4096)
PY
offset=$(cat "$out/esp.offset")
if mdir -i "$out/disk4k.img@@$offset" ::/ > "$out/mdir.txt" 2>&1 && grep -q RANDOM "$out/mdir.txt"; then
	echo "HOST mdir ok"
else
	echo "HOST mdir fail"; status=1
fi
if mcopy -n -i "$out/disk4k.img@@$offset" ::/random.bin "$out/random.bin" 2>/dev/null; then
	echo "HOST CKSUM $(cksum < "$out/random.bin")"
fi
if command -v fsck.fat > /dev/null 2>&1; then
	dd if="$out/disk4k.img" of="$out/esp.img" bs=4096 skip=256 count=131072 status=none
	fsck.fat -n "$out/esp.img" > "$out/fsck.txt" 2>&1 && echo "HOST fsck.fat ok" || { echo "HOST fsck.fat fail"; status=1; }
fi
[ $status -eq 0 ] && echo "p006-4k: PASS $out" || echo "p006-4k: FAIL $out"
exit $status
