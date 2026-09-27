#!/bin/sh
# ws074-p019: libjpeg-compat in the guest.  Builds jpeg-driver.c for zedBSD against /lib/libjpeg-compat.so (the image's,
# build-browser-image.sh), makes the JPEG files (run-jpeg-tests.py --make-only), decodes them all in the running guest
# (browser-guest.sh start|plain) and compares the pictures with djpeg's on the host (run-jpeg-tests.py --outputs).
#
#   sh plan/ws074/tests/jpeg-guest.sh
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
root=$(pwd)
GUEST_RUNTIME="${GUEST_RUNTIME:-$root/build/ws074-run}"
export GUEST_RUNTIME
sysroot=$root/build/amd64/sysroot
out=build/ws074-guest
cc="$root/build/llvm/bin/clang --target=x86_64-unknown-zedbsd --sysroot=$sysroot"
mkdir -p "$out"

# The driver, linked with the library the image has.
$cc -nostdinc -Iinclude/libc -isystem "$sysroot/usr/include" -m64 -march=x86-64 -mno-red-zone -Os -ffreestanding -fPIC \
    -fno-builtin -fno-stack-protector -Wall -Wextra -Werror -c plan/ws074/tests/jpeg-driver.c -o "$out/host-jpeg.o"
$cc -m64 -nostdlib -pie -Wl,--no-relax -Wl,--hash-style=sysv,-z,now,-z,relro,-z,separate-code \
    -Wl,--dynamic-linker=/lib/ld.so "$sysroot/usr/lib/crt1.o" "$out/host-jpeg.o" \
    -Lbuild/amd64/dynamic -l:libjpeg-compat.so -l:libc.so -o "$out/host-jpeg"

# The files, into the guest in one archive.
python3 plan/ws074/tests/run-jpeg-tests.py --make-only
tar -C build/ws074-jpeg/files -cf "$out/jpeg-files.tar" .
python3 plan/tools/guest/guest.py put "$out/jpeg-files.tar" /tmp/jpeg-files.tar >/dev/null
python3 plan/tools/guest/guest.py put "$out/host-jpeg" /tmp/host-jpeg >/dev/null

# Every file decoded there; the pictures (and the refusals' lines) come back in one archive.
python3 plan/tools/guest/guest.py run 'rm -rf /tmp/jpeg /tmp/jpeg-out; mkdir -p /tmp/jpeg /tmp/jpeg-out; cd /tmp/jpeg; tar -xf /tmp/jpeg-files.tar; chmod 755 /tmp/host-jpeg
for f in *.jpg; do n=${f%.jpg}; /tmp/host-jpeg "$f" /tmp/jpeg-out/$n.pnm > /tmp/jpeg-out/$n.log 2>&1; done
cd /tmp/jpeg-out; tar -cf /tmp/jpeg-out.tar .; echo decoded' | tail -1
python3 plan/tools/guest/guest.py get /tmp/jpeg-out.tar "$out/jpeg-out.tar" >/dev/null
rm -rf "$out/jpeg-out"
mkdir -p "$out/jpeg-out"
tar -C "$out/jpeg-out" -xf "$out/jpeg-out.tar"
exec python3 plan/ws074/tests/run-jpeg-tests.py --outputs "$out/jpeg-out"
