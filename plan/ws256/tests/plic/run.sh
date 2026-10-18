#!/bin/sh
# Host check of the riscv64 PLIC driver and IRQ dispatch on a fake controller.
#   plan/ws256/tests/plic/run.sh OUTDIR
# The sources are copied beside stand-ins for the headers that hold riscv
# instructions, so that the copies, not the originals, include them.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
root=$(cd -- "$(dirname -- "$0")/../../../.." && pwd)
here=$root/plan/ws256/tests/plic
hal=$root/src/hal/riscv64
out=$1
mkdir -p "$out/src"
cp "$hal/plic.c" "$hal/irq.c" "$hal/plic.h" "$hal/irq.h" "$hal/percpu.h" "$hal/defs.h" \
	"$hal/bootinfo.h" "$hal/fdt.h" "$hal/fdt.c" "$out/src/"
cp "$here/asm.h" "$here/spin.h" "$out/src/"
dtc -q -I dts -O dtb -o "$out/virt.dtb" "$root/plan/ws256/tests/fdt/virt-like.dts"
cc -std=c99 -O1 -Wall -Wextra -Wno-unused-function -I"$out/src" -I"$root/include" \
	"$out/src/plic.c" "$out/src/irq.c" "$out/src/fdt.c" "$here/plic-host-test.c" -o "$out/plic-host-test"
"$out/plic-host-test" "$out/virt.dtb"
