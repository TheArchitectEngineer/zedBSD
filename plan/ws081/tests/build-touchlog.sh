#!/bin/sh
# ws081-p016: builds touchlog (touchlog.c) for Kei with the build's clang against the amd64 sysroot and BUILD's libc
# (as plan/ws100/tests/build-audiod-feedback.sh builds its client), into BUILD/tests/touchlog (ws136-p001: no longer the
# shared build/ws081-tests/).  The configs that carry it read $(BUILD)/tests/touchlog (config-amd64-touchlog.mk,
# config-amd64-demo-win.mk).  With "image", then builds the test image (config-amd64-touchlog.mk) with it in /usr/bin.
#   [TOUCHLOG_CONFIG=CONFIG] plan/ws081/tests/build-touchlog.sh [BUILD] [image]     (default build/amd64)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
. plan/tools/guest/jobs.sh  # ZEDBSD_JOBS, the parallel jobs (default 16)
build=${1:-build/amd64}
config=${TOUCHLOG_CONFIG:-plan/ws081/tests/config-amd64-touchlog.mk}
sysroot=$(pwd)/build/amd64/sysroot
[ -f "$build/dynamic/libc.so" ] || make -j"$ZEDBSD_JOBS" ZEDBSD_CONFIG="$config" BUILD="$build" "$build/dynamic/libc.so"
[ -f "$sysroot/usr/include/stdint.h" ] || { echo "build-touchlog: no sysroot headers in $sysroot"; exit 1; }
mkdir -p "$build/tests"
build/llvm/bin/clang --target=x86_64-unknown-zedbsd --sysroot="$sysroot" -nostdinc -I. -Iinclude -isystem "$sysroot/usr/include" \
    -DHAL_ARCH_AMD64 -DKERN_USER_ABI_LP64 -DKERN_DYNAMIC_LIBC -m64 -march=x86-64 -mno-red-zone -O2 -ffreestanding -fPIC \
    -fno-builtin -fno-stack-protector -Wall -Wextra -Werror -c plan/ws081/tests/touchlog.c -o "$build/tests/touchlog.o"
build/llvm/bin/clang --target=x86_64-unknown-zedbsd --sysroot="$sysroot" -m64 -nostdlib -pie -Wl,--no-relax \
    -Wl,--hash-style=sysv,-z,now,-z,relro -Wl,--allow-shlib-undefined -Wl,--dynamic-linker=/lib/ld.so \
    "$sysroot/usr/lib/crt1.o" "$build/tests/touchlog.o" -L"$build/dynamic" -Wl,-rpath-link,"$build/dynamic" \
    -l:libc.so -o "$build/tests/touchlog"
echo "built $build/tests/touchlog"
[ "${2:-}" = image ] || exit 0
exec plan/tools/guest/test-image.sh plan/ws081/tests/config-amd64-touchlog.mk "$build"
