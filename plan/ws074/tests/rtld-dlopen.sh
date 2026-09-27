#!/bin/sh
# ws074-p017 (BUG-083): builds the rtld dlopen check and runs it in the running guest (browser-guest.sh start|plain).
# rtld-probe.c becomes /usr/lib/libws074probe.so (a package-like library) and rtld-dlopen.c a program that needs it;
# the guest's image must have the OpenSSL package (build-browser-image.sh's has it).
#
#   sh plan/ws074/tests/rtld-dlopen.sh
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
root=$(pwd)
GUEST_RUNTIME="${GUEST_RUNTIME:-$root/build/ws074-run}"
export GUEST_RUNTIME
sysroot=$root/build/amd64/sysroot
out=build/ws074-guest
cc="$root/build/llvm/bin/clang --target=x86_64-unknown-zedbsd --sysroot=$sysroot"
cflags="-nostdinc -Iinclude -isystem $sysroot/usr/include -m64 -march=x86-64 -mno-red-zone -Os -ffreestanding -fPIC
	-fno-builtin -fno-stack-protector -Wall -Wextra -Werror"
links="-Wl,--no-relax -Wl,--hash-style=sysv,-z,now,-z,relro,-z,separate-code"
mkdir -p "$out"

# The library, then the program that needs it.
$cc $cflags -c plan/ws074/tests/rtld-probe.c -o "$out/rtld-probe.o"
$cc -m64 -nostdlib -shared $links -Wl,-soname,libws074probe.so "$out/rtld-probe.o" \
    -Lbuild/amd64/dynamic -l:libc.so -o "$out/libws074probe.so"
$cc $cflags -c plan/ws074/tests/rtld-dlopen.c -o "$out/rtld-dlopen.o"
$cc -m64 -nostdlib -pie $links -Wl,--dynamic-linker=/lib/ld.so "$sysroot/usr/lib/crt1.o" "$out/rtld-dlopen.o" \
    -L"$out" -Lbuild/amd64/dynamic -l:libws074probe.so -l:libc.so -o "$out/rtld-dlopen"

# Into the guest, and run.
python3 plan/tools/guest/guest.py put "$out/libws074probe.so" /usr/lib/libws074probe.so >/dev/null
python3 plan/tools/guest/guest.py put "$out/rtld-dlopen" /tmp/rtld-dlopen >/dev/null
python3 plan/tools/guest/guest.py run 'chmod 755 /tmp/rtld-dlopen /usr/lib/libws074probe.so; /tmp/rtld-dlopen; echo "status $?"; rm -f /usr/lib/libws074probe.so'
