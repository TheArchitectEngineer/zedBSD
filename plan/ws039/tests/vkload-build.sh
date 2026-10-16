#!/bin/sh
# Builds plan/ws039/tests/vkload/ into BUILD/bin/vkload with the image's compile and link lines (as vkhang-build.sh):
# glslc makes the pixel program's SPIR-V into a header first.
#   usage: plan/ws039/tests/vkload-build.sh BUILD   (for example build/p3/usb)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
BUILD=${1:?usage: vkload-build.sh BUILD}
TOP=$(pwd)
SYSROOT=$TOP/build/amd64/sysroot
CC=$TOP/build/llvm/bin/clang
OBJ=$BUILD/dynamic/obj/plan/ws039/tests/vkload
mkdir -p "$OBJ" "$BUILD/bin"
{
	echo "/* Made by plan/ws039/tests/vkload-build.sh from vkload/spin.frag; do not edit. */"
	glslc -fshader-stage=frag -mfmt=c "plan/ws039/tests/vkload/spin.frag" -o "$OBJ/spin.inc"
	echo "static const uint32_t vkload_spin_frag[] ="
	cat "$OBJ/spin.inc"
	echo ";"
} > "$OBJ/vkload-programs.h"
"$CC" --target=x86_64-unknown-zedbsd --sysroot="$SYSROOT" -nostdinc -I. -Iinclude -I"$OBJ" -isystem "$SYSROOT/usr/include" \
	-DHAL_ARCH_AMD64 -DKERN_USER_ABI_LP64 -DKERN_DYNAMIC_LIBC -m64 -march=x86-64 -mno-red-zone -Os -ffreestanding -fPIC \
	-fno-builtin -fno-stack-protector -fasynchronous-unwind-tables -ftls-model=global-dynamic -Wall -Wextra -Werror \
	-c plan/ws039/tests/vkload/main.c -o "$OBJ/main.o"
"$CC" --target=x86_64-unknown-zedbsd --sysroot="$SYSROOT" -m64 -nostdlib -pie -Wl,--no-relax \
	-Wl,--hash-style=gnu,-z,now,-z,relro,-z,separate-code -Wl,-z,stack-size=0x100000,--allow-shlib-undefined \
	-Wl,--dynamic-linker=/lib/ld.so "$SYSROOT/usr/lib/crt1.o" "$OBJ/main.o" \
	-L"$BUILD/dynamic" -Wl,-rpath-link,"$BUILD/dynamic" -l:libvulkan.so -l:libc.so -o "$BUILD/bin/vkload"
python3 tools/build/check-dynamic-elf.py --machine amd64 --role application --needed libvulkan.so --needed libc.so "$BUILD/bin/vkload"
