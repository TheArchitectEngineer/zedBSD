#!/bin/sh
# Host check of the generic ECAM PCI host (ws256-p016): the device tree's
# windows and interrupt map, BAR placement and the INTx routing, on a fake
# configuration space (no emulator).
#   plan/ws256/tests/ecam/run.sh OUTDIR
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
root=$(cd -- "$(dirname -- "$0")/../../../.." && pwd)
here=$root/plan/ws256/tests/ecam
out=$1
mkdir -p "$out"
dtc -q -I dts -O dtb -o "$out/virt-pcie.dtb" "$here/virt-pcie.dts"
cc -std=c99 -O2 -Wall -Wextra -Werror -I"$root/include" \
	"$root/src/drivers/pci/pci-ecam-fdt.c" "$root/src/drivers/generic/fdt.c" \
	"$here/ecam-host-test.c" -o "$out/ecam-host-test"
"$out/ecam-host-test" "$out/virt-pcie.dtb"
