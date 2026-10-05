#!/bin/sh
# Builds and runs the host test of the placement of an unassigned BAR
# (BUG-210) with the Latitude 5330's _CRS windows.  The allocator is
# compiled freestanding like the kernel; the test uses the host C library.
# Usage: plan/ws159/tests/run-host-pci-window.sh [build-dir]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu

root=$(cd "$(dirname "$0")/../../.." && pwd)
out=${1:-$root/build/ws159-host-pci-window}
mkdir -p "$out"

cc=${CC:-clang}
extra=${EXTRA_CFLAGS:-}
kflags="-std=gnu11 -O1 -g -Wall -Wextra -Werror -ffreestanding -nostdlibinc \
	-fno-builtin -D__ZEDBSD__ -DKERN_USER_ABI_LP64 -I $root/include -I $root/src"
$cc $kflags $extra -c "$root/src/drivers/pci/pci-window.c" -o "$out/pci-window.o"
$cc -std=gnu11 -O1 -g -Wall -Wextra -Werror -I "$root/include" $extra \
	-c "$root/plan/ws159/tests/host-pci-window.c" -o "$out/host-pci-window.o"
$cc $extra "$out/host-pci-window.o" "$out/pci-window.o" -o "$out/host-pci-window"
"$out/host-pci-window"
