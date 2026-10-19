#!/bin/sh
# Host check of the memory disk on the riscv64 root image (no emulator).
#   plan/ws256/tests/ramdisk/run.sh OUTDIR
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
root=$(cd -- "$(dirname -- "$0")/../../../.." && pwd)
here=$root/plan/ws256/tests/ramdisk
out=$1
mkdir -p "$out"
PYTHONDONTWRITEBYTECODE=1 python3 "$root/platform/riscv64/tools/make-riscv64-root-image.py" \
	--force --bare-root "$out/root-bare.img"
cc -std=c99 -O2 -Wall -Wextra -Werror -I"$root/include" \
	"$root/src/drivers/generic/ramdisk.c" "$root/src/drivers/disklabel/mbr.c" \
	"$here/ramdisk-host-test.c" \
	-o "$out/ramdisk-host-test"
"$out/ramdisk-host-test" "$out/root-bare.img"
