#!/bin/sh
# ws094-p002: builds desktop-probe.c for the amd64 guest with the build's clang and sysroot, linked as
# platform/amd64/vmunix.mk links extras-probe (libwayland-client and the C library, dynamic).
#   plan/ws094/tests/build-probe.sh [BUILD]      (default build/ws094-amd64, which has the sysroot and dynamic/)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
build=${1:-build/ws094-amd64}
root=$(pwd)
sysroot=$root/build/amd64/sysroot
cc=$root/build/llvm/bin/clang
mkdir -p "$build/ws094"
"$cc" --target=x86_64-unknown-zedbsd --sysroot="$sysroot" -nostdinc -I. -Iinclude -isystem "$sysroot/usr/include" \
    -DHAL_ARCH_AMD64 -DKERN_USER_ABI_LP64 -DKERN_DYNAMIC_LIBC -m64 -march=x86-64 -mno-red-zone -Os -ffreestanding -fPIC \
    -fno-builtin -fno-stack-protector -Wall -Wextra -Werror \
    -c plan/ws094/tests/desktop-probe.c -o "$build/ws094/desktop-probe.o"
"$cc" --target=x86_64-unknown-zedbsd --sysroot="$sysroot" -m64 -nostdlib -pie -Wl,--no-relax \
    -Wl,--hash-style=sysv,-z,now,-z,relro,-z,separate-code -Wl,-z,stack-size=0x100000,--allow-shlib-undefined \
    -Wl,--dynamic-linker=/lib/ld.so "$sysroot/usr/lib/crt1.o" "$build/ws094/desktop-probe.o" \
    -L"$build/dynamic" -Wl,-rpath-link,"$build/dynamic" -l:libwayland-client.so -l:libc.so -o "$build/ws094/desktop-probe"
echo "built $build/ws094/desktop-probe"
