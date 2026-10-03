#!/bin/sh
# Runs zlib's configure on the host as a cross build for
# x86_64-unknown-zedbsd with a given clang and reports whether it decides
# that shared libraries can be built (F-009: its check links a one-function
# object with zlib.map, which names symbols the object does not define).
# CHOST names Linux only so that configure takes its GNU-ld branch (the
# one with the version script), as it did on the guest (F-009); the
# compiler is still the zedbsd target.  Then builds the shared library and
# counts its versioned exports.
#   sh plan/tools/toolchain/zlib-shared-configure.sh CLANG SYSROOT [WORK_DIR]
# SYSROOT must have a shared usr/lib/libc.so (copy the sysroot and put an
# image's rootfs/lib/libc.so there), or the link takes the static libc.a.
# The tarball is build/distfiles/zlib-1.3.2.tar.xz (ws136-p003: it was another tree's, now gone)
# (read only).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
clang=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
sysroot=$(cd "$2" && pwd)
work=${3:-build/ws055/zlib}
rm -rf "$work"
mkdir -p "$work"
tar -C "$work" -xf "$(cd "$(dirname -- "$0")/../../.." && pwd)/build/distfiles/zlib-1.3.2.tar.xz"
cd "$work/zlib-1.3.2" || exit 1
CHOST=x86_64-pc-linux-gnu \
CC="$clang --target=x86_64-unknown-zedbsd --sysroot=$sysroot" \
AR="$(dirname "$clang")/llvm-ar" RANLIB="$(dirname "$clang")/llvm-ranlib" \
    ./configure > ../configure.out 2>&1
echo "configure status $?"
grep -i 'shared' ../configure.out | head -5
make libz.so.1.3.2 > ../make.out 2>&1
echo "make libz.so status $?"
if [ -f libz.so.1.3.2 ]; then
	"$(dirname "$clang")/llvm-readelf" --dyn-syms libz.so.1.3.2 | grep -c 'ZLIB_1'
fi
