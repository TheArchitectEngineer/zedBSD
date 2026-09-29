#!/bin/sh
# ws100-p002: builds the audiod test image (config-amd64-audiod.mk): the image once (for the sysroot), the test client
# with the build's clang against that sysroot, then the image again with the client in /usr/bin.
#   plan/ws100/tests/build-audiod-image.sh [BUILD]     (default build/amd64)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
build=${1:-build/amd64}
make -j"$(nproc)" ZEDBSD_CONFIG=plan/ws100/tests/config-amd64-audiod.mk BUILD="$build" disk-image
mkdir -p build/ws100-tests
# The client, compiled and linked the way the build makes a dynamic program (see the build's audiod lines).
sysroot=$(pwd)/$build/sysroot
build/llvm/bin/clang --target=x86_64-unknown-zedbsd --sysroot="$sysroot" -nostdinc -I. -Iinclude -isystem "$sysroot/usr/include" \
    -DHAL_ARCH_AMD64 -DKERN_USER_ABI_LP64 -DKERN_DYNAMIC_LIBC -m64 -march=x86-64 -mno-red-zone -O2 -ffreestanding -fPIC \
    -fno-builtin -fno-stack-protector -Wall -Wextra -Werror -c plan/ws100/tests/audiod-feedback.c -o build/ws100-tests/audiod-feedback.o
build/llvm/bin/clang --target=x86_64-unknown-zedbsd --sysroot="$sysroot" -m64 -nostdlib -pie -Wl,--no-relax \
    -Wl,--hash-style=sysv,-z,now,-z,relro -Wl,--allow-shlib-undefined -Wl,--dynamic-linker=/lib/ld.so \
    "$sysroot/usr/lib/crt1.o" build/ws100-tests/audiod-feedback.o -L"$build/dynamic" -Wl,-rpath-link,"$build/dynamic" \
    -l:libc.so -o build/ws100-tests/audiod-feedback
exec make -j"$(nproc)" ZEDBSD_CONFIG=plan/ws100/tests/config-amd64-audiod.mk BUILD="$build" disk-image
