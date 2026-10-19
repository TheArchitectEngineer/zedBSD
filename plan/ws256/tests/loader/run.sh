#!/bin/sh
# Host check of the riscv64 UEFI loader's kernel plan and placement, and of
# the loader image's PE header (no emulator).
#   plan/ws256/tests/loader/run.sh OUTDIR VMUNIX BOOTRISCV64.EFI LOADER.elf
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
root=$(cd -- "$(dirname -- "$0")/../../../.." && pwd)
here=$root/plan/ws256/tests/loader
out=$1
mkdir -p "$out"
cc -std=c11 -O2 -Wall -Wextra -Werror -D__riscv=1 -I"$root" \
	"$root/bootloader/uefi/elf64.c" "$here/elf64-host-test.c" -o "$out/elf64-host-test"
"$out/elf64-host-test" "$2"
PYTHONDONTWRITEBYTECODE=1 python3 -I "$root/platform/riscv64/tools/check-bootriscv64.py" --elf "$4" "$3"
