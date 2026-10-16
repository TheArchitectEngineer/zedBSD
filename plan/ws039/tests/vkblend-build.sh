#!/bin/sh
# Builds plan/ws039/tests/vkblend.c into BUILD/bin/vkblend with the same compile and link lines as the image's
# vkquad (the image's libvulkan.so and libc.so in BUILD/dynamic; the image is built first).
#   usage: plan/ws039/tests/vkblend-build.sh BUILD   (for example build/p3/usb)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
BUILD=${1:?usage: vkblend-build.sh BUILD}
TOP=$(pwd)
SYSROOT=$TOP/build/amd64/sysroot
CC=$TOP/build/llvm/bin/clang
mkdir -p "$BUILD/dynamic/obj/plan/ws039/tests" "$BUILD/bin"
"$CC" --target=x86_64-unknown-zedbsd --sysroot="$SYSROOT" -nostdinc -I. -Iinclude -isystem "$SYSROOT/usr/include" \
	-DHAL_ARCH_AMD64 -DKERN_USER_ABI_LP64 -DKERN_DYNAMIC_LIBC -m64 -march=x86-64 -mno-red-zone -Os -ffreestanding -fPIC \
	-fno-builtin -fno-stack-protector -fasynchronous-unwind-tables -ftls-model=global-dynamic -Wall -Wextra -Werror \
	-c plan/ws039/tests/vkblend.c -o "$BUILD/dynamic/obj/plan/ws039/tests/vkblend.o"
"$CC" --target=x86_64-unknown-zedbsd --sysroot="$SYSROOT" -m64 -nostdlib -pie -Wl,--no-relax \
	-Wl,--hash-style=gnu,-z,now,-z,relro,-z,separate-code -Wl,-z,stack-size=0x100000,--allow-shlib-undefined \
	-Wl,--dynamic-linker=/lib/ld.so "$SYSROOT/usr/lib/crt1.o" "$BUILD/dynamic/obj/plan/ws039/tests/vkblend.o" \
	-L"$BUILD/dynamic" -Wl,-rpath-link,"$BUILD/dynamic" -l:libvulkan.so -l:libc.so -o "$BUILD/bin/vkblend"
python3 tools/build/check-dynamic-elf.py --machine amd64 --role application --needed libvulkan.so --needed libc.so "$BUILD/bin/vkblend"
