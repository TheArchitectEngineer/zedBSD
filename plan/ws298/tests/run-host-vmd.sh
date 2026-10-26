#!/bin/sh
# Builds and runs the host test of the Intel VMD driver's planning half
# (WS298): the bus numbering, window and BAR placement and the message
# table, against a model of the configuration space behind a VMD.  The
# planning half and the window allocator are compiled freestanding like the
# kernel; the test uses the host C library.
# Usage: plan/ws298/tests/run-host-vmd.sh [build-dir]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu

root=$(cd "$(dirname "$0")/../../.." && pwd)
out=${1:-$root/build/ws298-host-vmd}
mkdir -p "$out"

cc=${CC:-clang}
extra=${EXTRA_CFLAGS:-}
kflags="-std=gnu11 -O1 -g -Wall -Wextra -Werror -ffreestanding -nostdlibinc \
	-fno-builtin -D__ZEDBSD__ -DKERN_USER_ABI_LP64 -I $root/include -I $root/src"
$cc $kflags $extra -c "$root/src/drivers/pci/pci-vmd-plan.c" -o "$out/pci-vmd-plan.o"
$cc $kflags $extra -c "$root/src/drivers/pci/pci-window.c" -o "$out/pci-window.o"
$cc -std=gnu11 -O1 -g -Wall -Wextra -Werror -I "$root/include" $extra \
	-c "$root/plan/ws298/tests/host-vmd-plan.c" -o "$out/host-vmd-plan.o"
$cc $extra "$out/host-vmd-plan.o" "$out/pci-vmd-plan.o" "$out/pci-window.o" -o "$out/host-vmd-plan"
"$out/host-vmd-plan"
