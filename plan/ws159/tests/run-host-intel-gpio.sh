#!/bin/sh
# Builds and runs the host test of the Intel GPIO pad lookup (ws159-p006)
# with the Latitude 5330's pad group table.  The driver is compiled
# freestanding like the kernel; the test supplies the ACPI and mapping
# stand-ins with the host C library.
# Usage: plan/ws159/tests/run-host-intel-gpio.sh [build-dir]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu

root=$(cd "$(dirname "$0")/../../.." && pwd)
out=${1:-$root/build/ws159-host-intel-gpio}
mkdir -p "$out"

cc=${CC:-clang}
extra=${EXTRA_CFLAGS:-}
kflags="-std=gnu11 -O1 -g -Wall -Wextra -Werror -ffreestanding -nostdlibinc \
	-fno-builtin -D__ZEDBSD__ -DKERN_USER_ABI_LP64 -I $root/include -I $root/src"
$cc $kflags $extra -c "$root/src/drivers/gpio/intel-gpio.c" -o "$out/intel-gpio.o"
$cc -std=gnu11 -O1 -g -Wall -Wextra -Werror -I "$root/include" $extra \
	-c "$root/plan/ws159/tests/host-intel-gpio.c" -o "$out/host-intel-gpio.o"
$cc $extra "$out/host-intel-gpio.o" "$out/intel-gpio.o" -o "$out/host-intel-gpio"
"$out/host-intel-gpio"
"$out/host-intel-gpio" nomode
