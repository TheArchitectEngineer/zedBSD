#!/bin/sh
# Builds randread.c twice: OUT/randread-zedbsd (static, amd64 zedBSD, against the sysroot of build/amd64) and
# OUT/randread-linux (static, the build host's cc), for nvme-bench-5330.sh.
#   sh plan/ws299/tests/build-randread.sh OUT
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
OUT=${1:?OUT}
cd "$(dirname "$0")/../../.."
S=build/amd64/sysroot
[ -d "$S" ] || S=/home/awe/zedBSD-claude1/build/amd64/sysroot
C=build/llvm/bin/clang
mkdir -p "$OUT"
"$C" --target=x86_64-unknown-zedbsd --sysroot="$S" -nostdinc -isystem "$S/usr/include" -O2 -Wall -Wextra -Werror \
	-c plan/ws299/tests/randread.c -o "$OUT/randread.o"
"$C" --target=x86_64-unknown-zedbsd --sysroot="$S" -nostdlib -static "$S/usr/lib/crt0.o" "$OUT/randread.o" \
	"$S/usr/lib/libc.a" "$S/usr/lib/libzedbsd-compiler-rt.a" -o "$OUT/randread-zedbsd"
cc -O2 -Wall -Wextra -Werror -pthread -static plan/ws299/tests/randread.c -o "$OUT/randread-linux"
echo "build-randread: $OUT/randread-zedbsd $OUT/randread-linux"
