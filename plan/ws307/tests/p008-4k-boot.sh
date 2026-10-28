#!/bin/sh
# WS307 p008 (the 4096-byte step): the installed system's form on a 4Kn disk, booted through OVMF.  IMAGE (an SSH
# guest image, plan/tools/guest/build-ssh-image.sh) boots with a blank 2 GiB NVMe disk of 4096-byte blocks; in it
# plan/ws307/tests/p008-4k-guest-steps.sh makes the ESP (loader and zedbsd.cfg) and the native UFS root (a copy of
# the guest's root, the kernel and the logo in /boot) with the guest's own diskpart and mkfs.  The guest stops and
# that disk alone boots with BOOT_NVME_BLOCK_SIZE=4096 plan/tools/boot-test.sh (the login prompt).  LOADER and
# KERNEL default to BUILD/uefi/BOOTX64.EFI and BUILD/vmunix of the image's BUILD.  The last line is
# "p008-4k-boot: PASS|FAIL OUT".  Output goes to a new build/ws307-test/p008-4k.*.
#   sh plan/ws307/tests/p008-4k-boot.sh IMAGE [LOADER [KERNEL]]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname "$0")/../../.."
image=${1:?usage: p008-4k-boot.sh IMAGE [LOADER [KERNEL]]}
build=$(dirname "$image")
loader=${2:-$build/uefi/BOOTX64.EFI}
kernel=${3:-$build/vmunix}
mkdir -p build/ws307-test
out=$(mktemp -d build/ws307-test/p008-4k.XXXXXX)
export GUEST_RUNTIME="$(pwd)/$out/guest"
truncate -s 2G "$out/disk4k.img"
cp --sparse=always "$image" "$out/boot.img"
root_uuid=$(python3 -c 'import uuid; print(uuid.uuid4())')
g() { timeout 1800 python3 plan/tools/guest/guest.py "$@"; }
g start "$out/boot.img" --qemu-extra "-drive file=$out/disk4k.img,if=none,id=p008k,format=raw -device nvme,serial=p008k,drive=p008k,logical_block_size=4096,physical_block_size=4096" > "$out/start.txt" 2>&1
status=0
if g wait --timeout 300 > "$out/wait.txt" 2>&1; then
	g run mkdir -p /tmp/p008k > /dev/null 2>&1 || true
	g put "$loader" /tmp/p008k/BOOTX64.EFI > /dev/null 2>&1 || true
	g put "$kernel" /tmp/p008k/vmunix > /dev/null 2>&1 || true
	g put "$build/boot-logo.ppm" /tmp/p008k/logo.ppm > /dev/null 2>&1 || true
	g put plan/ws307/tests/p008-4k-guest-steps.sh /tmp/p008k/steps.sh > /dev/null 2>&1 || true
	g run sh /tmp/p008k/steps.sh "$root_uuid" > "$out/steps.txt" 2>&1 || true
else
	echo "p008-4k: the guest did not answer" > "$out/steps.txt"
fi
g stop > /dev/null 2>&1 || true
grep -E '^(DISK|STEP|MOUNTS|ROOTDF|P008K)' "$out/steps.txt" || true
grep -q '^P008K DONE' "$out/steps.txt" || status=1
grep -q '^STEP .* fail' "$out/steps.txt" && status=1
if [ $status -eq 0 ]; then
	BOOT_MODE=uefi-nvme BOOT_NVME_BLOCK_SIZE=4096 OUTPUT="$out/boot" BOOT_TIMEOUT=${BOOT_TIMEOUT:-240} \
		timeout 500 bash plan/tools/boot-test.sh "$out/disk4k.img" > "$out/boot-test.txt" 2>&1 || status=$?
	tail -2 "$out/boot-test.txt"
fi
[ $status -eq 0 ] && echo "p008-4k-boot: PASS $out" || echo "p008-4k-boot: FAIL $out"
exit $status
