#!/bin/sh
# Host check of the riscv64 HAL's device tree reader (no emulator).
#   plan/ws256/tests/fdt/run.sh OUTDIR
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
root=$(cd -- "$(dirname -- "$0")/../../../.." && pwd)
here=$root/plan/ws256/tests/fdt
out=$1
mkdir -p "$out"
dtc -q -I dts -O dtb -o "$out/virt.dtb" "$here/virt-like.dts"
dtc -q -I dts -O dtb -o "$out/jh7110.dtb" "$here/jh7110-like.dts"
cc -std=c99 -O2 -Wall -Wextra -Werror -I"$root/include" -I"$root/src/hal/riscv64" \
	"$root/src/hal/riscv64/fdt.c" "$here/fdt-host-test.c" -o "$out/fdt-host-test"
"$out/fdt-host-test" "$out/virt.dtb" "$out/jh7110.dtb"
