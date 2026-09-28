#!/bin/sh
# ws075-p012: builds and runs the host test of the resident display's output choice (host-output-test.c): src/kern/boot.c
# and src/drivers/gpu/i915/display/output.c with the host compiler, ASan and UBSan, linked with
# --gc-sections so only the tested functions and what they reach stay (no kernel service is reached: no stubs).
#
#   plan/ws075/tests/hdmi/host-output-test.sh [OUTDIR]      (default build/ws075-h2-host)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../../.."
out=${1:-build/ws075-h2-host}
mkdir -p "$out"
CC=${CC:-clang}
FLAGS="-std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=all -ffunction-sections -fdata-sections -Iinclude -Isrc -I. -DHAL_ARCH_AMD64 -DHAL_BOARD_PCAT -DKERN_USER_ABI_LP64"
for f in src/kern/boot.c src/drivers/gpu/i915/display/output.c plan/ws075/tests/hdmi/host-output-test.c ; do
	$CC $FLAGS -c "$f" -o "$out/$(basename "$f" .c).o"
done
$CC -fsanitize=address,undefined -Wl,--gc-sections "$out/boot.o" "$out/output.o" "$out/host-output-test.o" -o "$out/host-output-test"
"$out/host-output-test" plan/ws075/phase011/lcd-edid.hex
