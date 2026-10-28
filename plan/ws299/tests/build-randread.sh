#!/bin/sh
# Builds randread.c, patwrite.c and directio.c twice: OUT/NAME-zedbsd (static, amd64 zedBSD, against the sysroot of build/amd64)
# and OUT/NAME-linux (static, the build host's cc), for nvme-bench-5330.sh.  The tree's include/ comes before the
# sysroot's so that the tree's uapi headers (O_DIRECT) are the ones used.
#   sh plan/ws299/tests/build-randread.sh OUT
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
OUT=${1:?OUT}
cd "$(dirname "$0")/../../.."
S=build/amd64/sysroot
[ -d "$S" ] || S=/home/awe/zedBSD-claude1/build/amd64/sysroot
C=build/llvm/bin/clang
mkdir -p "$OUT"
for name in randread patwrite directio cpuloop; do
	"$C" --target=x86_64-unknown-zedbsd --sysroot="$S" -nostdinc -isystem include -isystem "$S/usr/include" -O2 -Wall -Wextra -Werror \
		-c plan/ws299/tests/$name.c -o "$OUT/$name.o"
	"$C" --target=x86_64-unknown-zedbsd --sysroot="$S" -nostdlib -static "$S/usr/lib/crt0.o" "$OUT/$name.o" \
		"$S/usr/lib/libc.a" "$S/usr/lib/libzedbsd-compiler-rt.a" -o "$OUT/$name-zedbsd"
	cc -O2 -Wall -Wextra -Werror -pthread -static plan/ws299/tests/$name.c -o "$OUT/$name-linux"
done
echo "build-randread: $OUT"
