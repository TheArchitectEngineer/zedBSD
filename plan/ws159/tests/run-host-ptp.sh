#!/bin/sh
# Builds and runs the host test of the Precision Touchpad path (ws159-p003)
# on the Latitude 5330's touchpad report descriptor.  The kernel HID files
# are compiled freestanding like the kernel; the test uses the host C library.
# Usage: plan/ws159/tests/run-host-ptp.sh [build-dir]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu

root=$(cd "$(dirname "$0")/../../.." && pwd)
out=${1:-$root/build/ws159-host-ptp}
mkdir -p "$out"

cc=${CC:-clang}
extra=${EXTRA_CFLAGS:-}
kflags="-std=gnu11 -O1 -g -Wall -Wextra -Werror -ffreestanding -nostdlibinc \
	-fno-builtin -ffunction-sections -fdata-sections -D__ZEDBSD__ \
	-DKERN_USER_ABI_LP64 -I $root/include -I $root/src"
$cc $kflags $extra -c "$root/src/drivers/generic/hid-report.c" -o "$out/hid-report.o"
$cc $kflags $extra -c "$root/src/drivers/generic/hid-digitizer.c" -o "$out/hid-digitizer.o"
$cc $kflags $extra -c "$root/src/drivers/generic/hid-touch.c" -o "$out/hid-touch.o"
$cc -std=gnu11 -O1 -g -Wall -Wextra -Werror -I "$root/include" $extra \
	-c "$root/plan/ws159/tests/host-ptp.c" -o "$out/host-ptp.o"
$cc -Wl,--gc-sections $extra "$out/host-ptp.o" "$out/hid-report.o" \
	"$out/hid-digitizer.o" "$out/hid-touch.o" -o "$out/host-ptp"
"$out/host-ptp" "$root/plan/ws159/tests/latitude5330-linux/synaptics-06cb-ce65-rdesc.bin"
