#!/bin/sh
# ws291: builds gap (plan/ws291/tests/gap/main.c) for zedBSD amd64 into BUILD/bin/gap, from the top of the tree, with
# the libc of BUILD/dynamic (the test image's build, as nggshape-build.sh).
#   usage: plan/ws291/tests/gap-build.sh BUILD
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
BUILD=${1:?usage: gap-build.sh BUILD}
TOP=$(pwd)
SYSROOT=$TOP/build/amd64/sysroot
CC=$TOP/build/llvm/bin/clang
OBJ=$BUILD/dynamic/obj/plan/ws291/tests/gap
mkdir -p "$OBJ" "$BUILD/bin"
"$CC" --target=x86_64-unknown-zedbsd --sysroot="$SYSROOT" -nostdinc -I. -Iinclude -isystem "$SYSROOT/usr/include" \
	-DHAL_ARCH_AMD64 -DKERN_USER_ABI_LP64 -DKERN_DYNAMIC_LIBC -m64 -march=x86-64 -mno-red-zone -O2 -ffreestanding -fPIC \
	-fno-builtin -fno-stack-protector -fasynchronous-unwind-tables -ftls-model=global-dynamic -Wall -Wextra -Werror \
	-c plan/ws291/tests/gap/main.c -o "$OBJ/main.o"
"$CC" --target=x86_64-unknown-zedbsd --sysroot="$SYSROOT" -m64 -nostdlib -pie -Wl,--no-relax \
	-Wl,--hash-style=gnu,-z,now,-z,relro,-z,separate-code -Wl,-z,stack-size=0x100000,--allow-shlib-undefined \
	-Wl,--dynamic-linker=/lib/ld.so "$SYSROOT/usr/lib/crt1.o" "$OBJ/main.o" \
	-L"$BUILD/dynamic" -Wl,-rpath-link,"$BUILD/dynamic" -l:libc.so -o "$BUILD/bin/gap"
