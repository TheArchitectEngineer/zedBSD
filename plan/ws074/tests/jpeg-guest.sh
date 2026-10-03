#!/bin/sh
# ws074-p019: libjpeg-compat in the guest.  Builds jpeg-driver.c for zedBSD against /lib/libjpeg-compat.so (the image's,
# build-browser-image.sh), makes the JPEG files (run-jpeg-tests.py --make-only), decodes them all in the running guest
# (browser-guest.sh start|plain), one file at a time, and compares the pictures with djpeg's on the host (run-jpeg-tests.py --outputs).
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

# The files, into the guest one by one (the guest has no tar), under /root: /tmp is a 32 MiB tmpfs, too small.
python3 plan/ws074/tests/run-jpeg-tests.py --make-only
python3 plan/tools/guest/guest.py run 'rm -rf /root/ws074/jpeg /root/ws074/jpeg-out; mkdir -p /root/ws074 /root/ws074/jpeg /root/ws074/jpeg-out' >/dev/null
python3 plan/tools/guest/guest.py put "$out/host-jpeg" /root/ws074/host-jpeg >/dev/null
for file in build/ws074-jpeg/files/*.jpg; do
	python3 plan/tools/guest/guest.py put "$file" "/root/ws074/jpeg/$(basename "$file")" >/dev/null
done

# Every file decoded there, and the pictures and logs fetched back.
python3 plan/tools/guest/guest.py run 'chmod 755 /root/ws074/host-jpeg; cd /root/ws074/jpeg
for f in *.jpg; do n=${f%.jpg}; /root/ws074/host-jpeg "$f" /root/ws074/jpeg-out/$n.pnm > /root/ws074/jpeg-out/$n.log 2>&1; done; echo decoded' | tail -1
rm -rf "$out/jpeg-out"
mkdir -p "$out/jpeg-out"
for file in build/ws074-jpeg/files/*.jpg; do
	name=$(basename "$file" .jpg)
	python3 plan/tools/guest/guest.py get "/root/ws074/jpeg-out/$name.log" "$out/jpeg-out/$name.log" >/dev/null
	python3 plan/tools/guest/guest.py get "/root/ws074/jpeg-out/$name.pnm" "$out/jpeg-out/$name.pnm" >/dev/null 2>&1 || true
done
exec python3 plan/ws074/tests/run-jpeg-tests.py --outputs "$out/jpeg-out"
