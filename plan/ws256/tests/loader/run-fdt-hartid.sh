#!/bin/sh
# Host check of the riscv64 UEFI loader's /chosen boot-hartid reader on five
# trees built by dtc and on damaged copies of one (no emulator).
#   plan/ws256/tests/loader/run-fdt-hartid.sh OUTDIR
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
root=$(cd -- "$(dirname -- "$0")/../../../.." && pwd)
here=$root/plan/ws256/tests/loader
out=$1
mkdir -p "$out"
for name in u32 u64 no-property no-chosen bad-length; do
	dtc -q -I dts -O dtb -o "$out/hartid-$name.dtb" "$here/hartid-$name.dts"
done
cc -std=c11 -O2 -Wall -Wextra -Werror -I"$root" \
	"$root/bootloader/uefi/fdt-hartid.c" "$here/fdt-hartid-host-test.c" -o "$out/fdt-hartid-host-test"
"$out/fdt-hartid-host-test" "$out/hartid-u32.dtb" "$out/hartid-u64.dtb" \
	"$out/hartid-no-property.dtb" "$out/hartid-no-chosen.dtb" "$out/hartid-bad-length.dtb"
