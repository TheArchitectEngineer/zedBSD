#!/bin/sh
# ws174-p002: builds and runs the end-to-end host test of the boot keys' record
# rewrite (boot-override-kernel-host-test.c, O11): the UEFI loader's parser
# (bootloader/uefi/zedbsd-config.c), the rewrite (bootloader/common/boot-override.c)
# and the kernel's parser (src/kern/boot.c) with the host compiler, ASan and UBSan,
# linked with --gc-sections so only the parsers and what they reach stay (no stubs),
# as plan/ws118/tests/host-boot-i915-test.sh does.
#
#   plan/ws174/tests/run-boot-override-kernel-host-test.sh [OUTDIR]   (default build/ws174-host)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
out=${1:-build/ws174-host}
mkdir -p "$out"
CC=${CC:-clang}
FLAGS="-std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=all -ffunction-sections -fdata-sections -Iinclude -Isrc -I. -DHAL_ARCH_AMD64 -DHAL_BOARD_PCAT -DKERN_USER_ABI_LP64 -D_POSIX_C_SOURCE=200809L -include time.h"
objects=
for f in src/kern/boot.c bootloader/uefi/zedbsd-config.c bootloader/common/boot-override.c plan/ws174/tests/boot-override-kernel-host-test.c ; do
	o="$out/e2e-$(basename "$f" .c).o"
	$CC $FLAGS -c "$f" -o "$o"
	objects="$objects $o"
done
$CC -fsanitize=address,undefined -Wl,--gc-sections $objects -o "$out/boot-override-kernel-host-test"
"$out/boot-override-kernel-host-test"
