#!/bin/sh
# The host test of the kernel's word copy and Internet checksum (copy-sum-host-test.c): src/kern/kcrt.c (without the
# standard names) and src/kern/net/checksum.c built as the kernel builds them, for 64 and 32 bits.  OUT= chooses the
# build folder (default build/ws221-host).  The last line is "run-copy-sum-host-test: PASS|FAIL".
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
out=${OUT:-build/ws221-host}
mkdir -p "$out"
cc=${CC:-clang}
status=0
for arch in AMD64 I386; do
	bits=
	[ $arch = I386 ] && bits=-m32
	flags="-std=c11 -O2 -g -nostdlibinc -Iinclude -Isrc -I. -DKERN_UAPI_NATIVE -DKERN_KCRT_NATIVE -DKERN_KCRT_NO_STANDARD_NAMES
		-DHAL_ARCH_$arch -ffreestanding -fno-builtin -Wall -Wextra -Werror $bits"
	# shellcheck disable=SC2086
	$cc $flags -c src/kern/kcrt.c -o "$out/kcrt-$arch.o" &&
	    $cc $flags -c src/kern/net/checksum.c -o "$out/checksum-$arch.o" &&
	    $cc -std=c11 -O1 -g -Wall -Wextra -Werror $bits -c plan/ws221/tests/copy-sum-host-test.c \
		-o "$out/copy-sum-host-test-$arch.o" &&
	    $cc $bits -o "$out/copy-sum-host-test-$arch" "$out/copy-sum-host-test-$arch.o" "$out/kcrt-$arch.o" \
		"$out/checksum-$arch.o" -lc ||
	    { echo "run-copy-sum-host-test: build for $arch failed"; status=1; continue; }
	echo "== $arch"
	timeout 120 "$out/copy-sum-host-test-$arch" || status=1
done
[ $status = 0 ] && echo "run-copy-sum-host-test: PASS" || echo "run-copy-sum-host-test: FAIL"
exit $status
