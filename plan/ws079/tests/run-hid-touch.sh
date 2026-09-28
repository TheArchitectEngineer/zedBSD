#!/bin/sh
# Builds and runs the host test of the USB HID touch screen (ws079-p012).
# The driver files are compiled freestanding like the kernel; the test itself
# uses the host C library.
# Usage: plan/ws079/tests/run-hid-touch.sh [build-dir]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu

root=$(cd "$(dirname "$0")/../../.." && pwd)
out=${1:-$root/build/ws079-p012-host}
mkdir -p "$out"

cc=${CC:-clang}
extra=${EXTRA_CFLAGS:-}
kflags="-std=gnu11 -O1 -g -Wall -Wextra -Werror -ffreestanding -nostdlibinc \
	-fno-builtin -ffunction-sections -fdata-sections -D__ZEDBSD__ \
	-DKERN_USER_ABI_LP64 -I $root/include -I $root/src"
# EXTRA_CFLAGS (for example the sanitizers) reach the state machines and the
# test; usb-hid.c keeps its USB half, which the linker drops only without them.
$cc $kflags -c "$root/src/drivers/usb/usb-hid.c" -o "$out/usb-hid.o"
$cc $kflags $extra -c "$root/src/drivers/usb/hid-digitizer.c" -o "$out/hid-digitizer.o"
$cc $kflags $extra -c "$root/src/drivers/usb/hid-touch.c" -o "$out/hid-touch.o"
$cc -std=gnu11 -O1 -g -Wall -Wextra -Werror -I "$root/include" $extra \
	-c "$root/plan/ws079/tests/host-hid-touch.c" -o "$out/host-hid-touch.o"
$cc -Wl,--gc-sections $extra "$out/host-hid-touch.o" "$out/usb-hid.o" \
	"$out/hid-digitizer.o" "$out/hid-touch.o" -o "$out/host-hid-touch"
"$out/host-hid-touch"
