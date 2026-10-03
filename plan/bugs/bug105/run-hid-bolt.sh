#!/bin/sh
# BUG-105: builds and runs the host test of the Logi Bolt receiver's HID descriptors.
# Usage: plan/bugs/bug105/run-hid-bolt.sh [build-dir]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
root=$(cd "$(dirname "$0")/../../.." && pwd)
out=${1:-$root/build/bug105-host}
mkdir -p "$out"
cc=${CC:-clang}
kflags="-std=gnu11 -O1 -g -Wall -Wextra -Werror -ffreestanding -nostdlibinc \
	-fno-builtin -ffunction-sections -fdata-sections -D__ZEDBSD__ \
	-DKERN_USER_ABI_LP64 -I $root/include -I $root/src"
$cc $kflags -c "$root/src/drivers/usb/usb-hid.c" -o "$out/usb-hid.o"
$cc $kflags -c "$root/src/drivers/usb/hid-digitizer.c" -o "$out/hid-digitizer.o"
$cc -std=gnu11 -O1 -g -Wall -Wextra -Werror -I "$root/include" \
	-c "$root/plan/bugs/bug105/host-hid-bolt.c" -o "$out/host-hid-bolt.o"
$cc -Wl,--gc-sections "$out/host-hid-bolt.o" "$out/usb-hid.o" \
	"$out/hid-digitizer.o" -o "$out/host-hid-bolt"
"$out/host-hid-bolt" "$root/plan/bugs/bug105"
