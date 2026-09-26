#!/bin/sh
# Makes a guest image that runs this tree's kernel, libc, make
# and sh with the packages (clang, sshd) of an existing guest image, so
# that kernel, libc, make and sh changes can be tried with clang (expat)
# without building the packages here.  From ws064-p003.  The existing image is copied (read only); the ESP of
# the copy gets this tree's vmunix and BOOTX64.EFI (mtools), and a boot of
# it gets /lib/libc.so, /usr/bin/make and /bin/sh, then syncs.
#
#   sh plan/tools/guest/hybrid-image.sh BUILD OUTPUT_IMAGE [BASE_IMAGE]
#
# BUILD is this tree's image build (e.g. build/ws065/image); BASE_IMAGE
# defaults to /home/awe/zedBSD-rpi4/build/ws053-full-hal-guest/hdd-image.img.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
build=$1
out=$2
base=${3:-/home/awe/zedBSD-rpi4/build/ws053-full-hal-guest/hdd-image.img}
guest="python3 plan/tools/guest/guest.py"
export GUEST_RUNTIME="$(pwd)/build/hybrid-image-run"

# The ESP starts at sector 2048 in these images.
cp --reflink=auto "$base" "$out.tmp"
mcopy -o -i "$build/hdd-image.img@@1048576" ::/EFI/BOOT/BOOTX64.EFI "$out.efi"
mcopy -o -i "$out.tmp@@1048576" "$build/vmunix" ::/vmunix
mcopy -o -i "$out.tmp@@1048576" "$out.efi" ::/EFI/BOOT/BOOTX64.EFI
rm -f "$out.efi"

# The userland pieces, into a booted copy, which is kept as the image.
$guest stop >/dev/null 2>&1 || true
$guest start --disk nvme "$out.tmp" >/dev/null
$guest wait >/dev/null
$guest put "$build/rootfs/lib/libc.so" /lib/libc.so.new
$guest put "$build/rootfs/usr/bin/make" /usr/bin/make.new
$guest put "$build/rootfs/bin/sh" /bin/sh.new
$guest run 'chmod 755 /lib/libc.so.new /usr/bin/make.new /bin/sh.new && mv /lib/libc.so.new /lib/libc.so && mv /usr/bin/make.new /usr/bin/make && mv /bin/sh.new /bin/sh && cp /lib/libc.so /usr/lib/libc.so && sync && uname -a'
$guest run 'sync'
$guest stop >/dev/null
mv "$GUEST_RUNTIME/disk.img" "$out"
rm -f "$out.tmp"
echo "$out"
