#!/bin/sh
# Builds the kernel stress programs of this directory twice: OUT/NAME-zedbsd (static, amd64 zedBSD, against the
# sysroot of build/amd64) and OUT/NAME-linux (static, the build host's cc), for iostress.c, credrace.c and sigexec.c.
# The tree's include/ comes before the sysroot's, so that its uapi headers (O_DIRECT) are the ones used.  directio
# and randread, which qemu-iostress.sh, qemu-nvme-errors.sh and qemu-nvme-timeout.sh also put in the guest, come from
# plan/ws299/tests/build-randread.sh.
#   sh plan/tools/kstress/build.sh OUT
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
OUT=${1:?OUT}
cd "$(dirname "$0")/../../.."
S=build/amd64/sysroot
[ -d "$S" ] || S=/home/awe/zedBSD-claude1/build/amd64/sysroot
C=build/llvm/bin/clang
mkdir -p "$OUT"
for name in iostress credrace sigexec; do
	"$C" --target=x86_64-unknown-zedbsd --sysroot="$S" -nostdinc -isystem include -isystem "$S/usr/include" -O2 -Wall -Wextra -Werror \
		-c plan/tools/kstress/$name.c -o "$OUT/$name.o"
	"$C" --target=x86_64-unknown-zedbsd --sysroot="$S" -nostdlib -static "$S/usr/lib/crt0.o" "$OUT/$name.o" \
		"$S/usr/lib/libc.a" "$S/usr/lib/libzedbsd-compiler-rt.a" -o "$OUT/$name-zedbsd"
	cc -O2 -Wall -Wextra -Werror -pthread -static plan/tools/kstress/$name.c -o "$OUT/$name-linux"
done
echo "kstress-build: $OUT"
