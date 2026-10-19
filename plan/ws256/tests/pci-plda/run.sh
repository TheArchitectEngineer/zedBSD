#!/bin/sh
# Host check of the PLDA PCIe root complex driver (ws256-p020): the real
# pci-plda.c, pci.c and fdt.c on fake registers for the VisionFive 2's two
# root complexes, once plainly and once with ASan/UBSan.  No emulator.
#   plan/ws256/tests/pci-plda/run.sh OUTDIR
# OUTDIR is the caller's (for example one made by plan/tools/fresh-out.sh
# under build/tmp/); the script removes nothing.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
root=$(cd -- "$(dirname -- "$0")/../../../.." && pwd)
here=$root/plan/ws256/tests/pci-plda
out=$1
mkdir -p "$out"
cc=${CC:-cc}
dtc -q -I dts -O dtb -o "$out/vf2-pcie.dtb" "$here/vf2-pcie.dts"
flags="-std=c11 -D_POSIX_C_SOURCE=200809L -I$root/include -I$root -I$root/src -Wall -Wextra -Werror"
sources="$root/src/drivers/pci/pci.c $root/src/drivers/pci/pci-plda.c $root/src/drivers/generic/fdt.c $here/pci-plda-host-test.c"

# shellcheck disable=SC2086
$cc $flags -O2 $sources -o "$out/pci-plda-host-test"
"$out/pci-plda-host-test" "$out/vf2-pcie.dtb"

# shellcheck disable=SC2086
$cc $flags -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all \
	$sources -o "$out/pci-plda-host-test-sanitize"
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 "$out/pci-plda-host-test-sanitize" "$out/vf2-pcie.dtb"

echo "pci plda: ordinary, ASan/UBSan PASS"
