#!/bin/sh
# BUG-090: builds plan/ws073/tests/qsort-guest.c for zedBSD (amd64) against the image's libc.so, copies it into
# the running WS073 guest (tests/g.sh start IMAGE) and runs it with the image's libc.so; with OLD_LIBC (a libc.so
# from a build before the fix) it also runs a short count against that library through LD_LIBRARY_PATH.
#   sh plan/ws073/tests/qsort-guest.sh [COUNT]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
root=$(pwd)
sysroot=$root/build/amd64/sysroot
out=build/ws073-p026/guest
count=${1:-1000000}
cc="$root/build/llvm/bin/clang --target=x86_64-unknown-zedbsd --sysroot=$sysroot"
mkdir -p "$out"
$cc -nostdinc -I. -Iinclude -isystem "$sysroot/usr/include" -DHAL_ARCH_AMD64 -DKERN_USER_ABI_LP64 -DKERN_DYNAMIC_LIBC \
	-m64 -march=x86-64 -mno-red-zone -O2 -ffreestanding -fPIC -fno-builtin -fno-stack-protector -Wall -Wextra -Werror \
	-c plan/ws073/tests/qsort-guest.c -o "$out/qsort-guest.o"
$cc -m64 -nostdlib -pie -Wl,--no-relax -Wl,--hash-style=sysv,-z,now,-z,relro,-z,separate-code \
	-Wl,-z,stack-size=0x100000 -Wl,--dynamic-linker=/lib/ld.so \
	"$sysroot/usr/lib/crt1.o" "$out/qsort-guest.o" -Lbuild/amd64/dynamic -l:libc.so -o "$out/qsort-guest"
g() { timeout 600 sh plan/ws073/tests/g.sh "$@" </dev/null; }
g put "$out/qsort-guest" /tmp/qsort-guest
g run "chmod 755 /tmp/qsort-guest && /tmp/qsort-guest $count" | tee "$out/new.txt"
if [ -n "${OLD_LIBC:-}" ]; then
	g run "mkdir -p /tmp/oldlibc"
	g put "$OLD_LIBC" /tmp/oldlibc/libc.so
	g run "LD_LIBRARY_PATH=/tmp/oldlibc /tmp/qsort-guest 20000" | tee "$out/old-20000.txt"
	g run "/tmp/qsort-guest 20000" | tee "$out/new-20000.txt"
fi
grep -q "QSORT-GUEST:PASS" "$out/new.txt"
