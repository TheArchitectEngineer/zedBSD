#!/bin/sh
# ws074-p051: libgif-compat in the guest.  Builds gif-driver.c for zedBSD against /lib/libgif-compat.so (the image's,
# build-browser-image.sh), makes the GIF files (run-gif-tests.py --make-only), reads them all in the running guest
# (browser-guest.sh start|plain), one file at a time, and compares the reports and rasters with giflib's on the host
# (run-gif-tests.py --outputs).
#
#   sh plan/ws074/tests/gif-guest.sh
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
    -fno-builtin -fno-stack-protector -Wall -Wextra -Werror -c plan/ws074/tests/gif-driver.c -o "$out/host-gif.o"
$cc -m64 -nostdlib -pie -Wl,--no-relax -Wl,--hash-style=sysv,-z,now,-z,relro,-z,separate-code \
    -Wl,--dynamic-linker=/lib/ld.so "$sysroot/usr/lib/crt1.o" "$out/host-gif.o" \
    -Lbuild/amd64/dynamic -l:libgif-compat.so -l:libc.so -o "$out/host-gif"

# The files, into the guest one by one (the guest has no tar), under /root: /tmp is a 32 MiB tmpfs, too small.
python3 plan/ws074/tests/run-gif-tests.py --make-only
python3 plan/tools/guest/guest.py run 'rm -rf /root/ws074/gif /root/ws074/gif-out; mkdir -p /root/ws074 /root/ws074/gif /root/ws074/gif-out' >/dev/null
python3 plan/tools/guest/guest.py put "$out/host-gif" /root/ws074/host-gif >/dev/null
for file in build/ws074-gif/files/*.gif; do
	python3 plan/tools/guest/guest.py put "$file" "/root/ws074/gif/$(basename "$file")" >/dev/null
done

# Every file read there, and the reports and rasters fetched back.
python3 plan/tools/guest/guest.py run 'chmod 755 /root/ws074/host-gif; cd /root/ws074/gif
for f in *.gif; do n=${f%.gif}; /root/ws074/host-gif "$f" /root/ws074/gif-out/$n.raw > /root/ws074/gif-out/$n.txt 2>&1; done; echo read' | tail -1
rm -rf "$out/gif-out"
mkdir -p "$out/gif-out"
for file in build/ws074-gif/files/*.gif; do
	name=$(basename "$file" .gif)
	python3 plan/tools/guest/guest.py get "/root/ws074/gif-out/$name.txt" "$out/gif-out/$name.txt" >/dev/null
	python3 plan/tools/guest/guest.py get "/root/ws074/gif-out/$name.raw" "$out/gif-out/$name.raw" >/dev/null 2>&1 || true
done
exec python3 plan/ws074/tests/run-gif-tests.py --outputs "$out/gif-out"
